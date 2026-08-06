#include "traffic_perception/inference/yolov8_backend.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <opencv2/imgproc.hpp>

#include "score/mw/log/logging.h"

namespace traffic_perception {

static std::string resolveCocoClassName(int classId) {
  switch (classId) {
    case 0:
      return "Person";
    case 1:
      return "Bicycle";
    case 2:
      return "Car";
    case 3:
      return "Motorcycle";
    case 5:
      return "Bus";
    case 7:
      return "Truck";
    default:
      return "Unknown_" + std::to_string(classId);
  }
}

YOLOv8Backend::YOLOv8Backend(const std::string& modelPath,
                             const std::string& emergencyClass)
    : emergencyClass_(emergencyClass),
      env_(ORT_LOGGING_LEVEL_WARNING, "YOLOv8Backend"),
      memory_info_(
          Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
  try {
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(1);
    sessionOptions.SetInterOpNumThreads(1);
    session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(),
                                              sessionOptions);

    // Setup input/output names
    Ort::AllocatorWithDefaultOptions allocator;
    auto input_name = session_->GetInputNameAllocated(0, allocator);
    input_node_names_.push_back(strdup(input_name.get()));

    auto output_name = session_->GetOutputNameAllocated(0, allocator);
    output_node_names_.push_back(strdup(output_name.get()));

  } catch (const Ort::Exception& e) {
    throw std::runtime_error("ONNX Runtime exception loading model: " +
                             std::string(e.what()));
  }
}

InferenceResult YOLOv8Backend::infer(const Frame& frame) {
  InferenceResult result;
  result.FrameId = frame.FrameId;

  try {
    std::vector<float> input_tensor_values;
    preprocess(frame.Image, input_tensor_values);

    std::array<int64_t, 4> input_shape = {1, 3, 640, 640};
    auto input_tensor = Ort::Value::CreateTensor<float>(
        memory_info_, input_tensor_values.data(), input_tensor_values.size(),
        input_shape.data(), input_shape.size());

    auto output_tensors = session_->Run(
        Ort::RunOptions{nullptr}, (const char* const*)input_node_names_.data(),
        &input_tensor, 1, (const char* const*)output_node_names_.data(), 1);

    postprocess(frame.Image, output_tensors, result);
  } catch (const std::exception& e) {
    score::mw::log::LogDebug() << "Inference failed: " << e.what() << "\n";
    return InferenceResult();
  }

  return result;
}

std::string YOLOv8Backend::getModelName() const {
  return "YOLOv8Backend (ONNX Runtime)";
}

void YOLOv8Backend::draw(cv::Mat& image, const InferenceResult& inference) {
  draw(image, inference.Detections);
}

void YOLOv8Backend::draw(cv::Mat& image,
                         const std::vector<Detection>& detections) {
  for (const auto& det : detections) {
    cv::rectangle(image, det.Box, cv::Scalar(0, 255, 0), 2);
    std::string label = det.ClassName + ": " + std::to_string(det.Confidence);
    cv::putText(image, label, det.Box.tl() - cv::Point(0, 5),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
  }
}

void YOLOv8Backend::preprocess(const cv::Mat& frame,
                               std::vector<float>& input_tensor_values) {
  // 1. Calculate Letterbox scaling parameters
  float r = std::min(640.0f / frame.rows, 640.0f / frame.cols);

  int new_unpad_w = static_cast<int>(std::round(frame.cols * r));
  int new_unpad_h = static_cast<int>(std::round(frame.rows * r));

  float dw = (640.0f - new_unpad_w) / 2.0f;
  float dh = (640.0f - new_unpad_h) / 2.0f;

  // Pad image to 640x640 with border color 114
  int top = static_cast<int>(std::round(dh - 0.1f));
  int bottom = static_cast<int>(std::round(dh + 0.1f));
  int left = static_cast<int>(std::round(dw - 0.1f));
  int right = static_cast<int>(std::round(dw + 0.1f));

  ratio_ = r;
  dw_ = static_cast<float>(left);
  dh_ = static_cast<float>(top);

  // Resize image conserving aspect ratio
  cv::Mat resized;
  if (frame.cols != new_unpad_w || frame.rows != new_unpad_h) {
    cv::resize(frame, resized, cv::Size(new_unpad_w, new_unpad_h), 0, 0,
               cv::INTER_LINEAR);
  } else {
    resized = frame;
  }

  cv::Mat letterboxed;
  cv::copyMakeBorder(resized, letterboxed, top, bottom, left, right,
                     cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

  // Convert from BGR to RGB
  cv::Mat rgb;
  cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);

  // Normalize
  rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);

  // HWC to CHW
  input_tensor_values.resize(3 * 640 * 640);
  std::vector<cv::Mat> channels(3);
  cv::split(rgb, channels);
  int offset = 0;
  for (int i = 0; i < 3; ++i) {
    memcpy(input_tensor_values.data() + offset, channels[i].data,
           640 * 640 * sizeof(float));
    offset += 640 * 640;
  }
}

void YOLOv8Backend::postprocess(const cv::Mat& frame,
                                const std::vector<Ort::Value>& output_tensors,
                                InferenceResult& result) {
  auto tensor_info = output_tensors[0].GetTensorTypeAndShapeInfo();
  auto shape = tensor_info.GetShape();
  const float* data = output_tensors[0].GetTensorData<float>();

  // ---------------------------------------------------------------
  // Step 1: One-shot tensor investigation probe (frame 0 only).
  // Prints shape, first 10 raw values, and first row after transpose.
  // DO NOT remove until parser is verified correct.
  // ---------------------------------------------------------------
  if (frameCount_ == 0) {
    score::mw::log::LogDebug()
        << "[YOLOv8Backend][probe] Output tensor rank=" << shape.size() << "\n";
    for (std::size_t s = 0; s < shape.size(); ++s) {
      score::mw::log::LogDebug()
          << "[YOLOv8Backend][probe]   shape[" << s << "]=" << shape[s] << "\n";
    }
    score::mw::log::LogDebug() << "[YOLOv8Backend][probe] First 10 raw values:";
    for (int v = 0; v < 10; ++v) {
      score::mw::log::LogDebug() << " " << data[v];
    }
    score::mw::log::LogDebug() << "\n";

    // Show first row after applying the existing transpose logic
    if (shape.size() >= 3) {
      cv::Mat probe_out(static_cast<int>(shape[1]), static_cast<int>(shape[2]),
                        CV_32F, const_cast<float*>(data));
      cv::Mat probe_t = probe_out.t();
      float* first_row = probe_t.ptr<float>(0);
      score::mw::log::LogDebug()
          << "[YOLOv8Backend][probe] First transposed row (cols="
          << probe_t.cols << "):";
      for (int v = 0; v < std::min(probe_t.cols, 10); ++v) {
        score::mw::log::LogDebug() << " " << first_row[v];
      }
      score::mw::log::LogDebug() << "\n";
    }
  }
  // ---------------------------------------------------------------

  // Layout is determined at runtime from shape[1] (see layout detection block
  // below). Transpose/Reshape logic: [1, Features, Anchors] -> [Anchors,
  // Features] (row-major)
  cv::Mat output(static_cast<int>(shape[1]), static_cast<int>(shape[2]), CV_32F,
                 const_cast<float*>(data));
  cv::Mat transposed = output.t();

  std::vector<int> classIds;
  std::vector<float> confidences;
  std::vector<cv::Rect> boxes;

  float x_scale = static_cast<float>(frame.cols) / 640.0f;
  float y_scale = static_cast<float>(frame.rows) / 640.0f;

  // ---------------------------------------------------------------
  // Layout determination:
  //   shape[1] == 84  => Layout A: [1, 84, 8400]
  //                      4 bbox + 80 class scores, NO objectness.
  //   shape[1] == 85  => Layout B: [1, 85, 8400]
  //                      4 bbox + 1 objectness + 80 class scores.
  // Evidence: ClassId always 79 and Confidence > 1 when parsed as
  // Layout B proves the model is Layout A.
  // ---------------------------------------------------------------
  const bool has_objectness = (shape[1] == 85);
  const int num_classes_actual =
      static_cast<int>(shape[1]) - 4 - (has_objectness ? 1 : 0);

  for (int i = 0; i < transposed.rows; ++i) {
    float* row = transposed.ptr<float>(i);
    float cx = row[0];
    float cy = row[1];
    float w = row[2];
    float h = row[3];

    float obj_score = 1.0f;  // not present in Layout A
    int class_offset = 4;    // class scores start right after bbox
    if (has_objectness) {
      obj_score = row[4];
      class_offset = 5;
    }

    float* class_scores = row + class_offset;
    int class_id = -1;
    float best_class_score = -1.0f;
    for (int j = 0; j < num_classes_actual; ++j) {
      if (class_scores[j] > best_class_score) {
        best_class_score = class_scores[j];
        class_id = j;
      }
    }

    // Layout A: confidence = best class score (already a probability)
    // Layout B: confidence = objectness * best class score
    float confidence = obj_score * best_class_score;

    // Step 5: per-anchor debug log, first 5 frames, first 20 anchors
    if (frameCount_ < 5 && i < 20) {
      score::mw::log::LogDebug() << "[YOLOv8Backend][anchor" << i << "]";
      if (has_objectness) {
        score::mw::log::LogDebug() << " obj=" << obj_score;
      } else {
        score::mw::log::LogDebug() << " obj=N/A(LayoutA)";
      }
      score::mw::log::LogDebug()
          << " classId=" << class_id << " bestScore=" << best_class_score
          << " confidence=" << confidence
          << " isVehicle=" << isVehicleClass(class_id) << "\n";
    }

    if (confidence > confThreshold_) {
      // Apply inverse Letterbox transform: scale_boxes()
      float x1 = cx - w / 2.0f;
      float y1 = cy - h / 2.0f;
      float x2 = cx + w / 2.0f;
      float y2 = cy + h / 2.0f;

      if (frameCount_ < 5) {
        score::mw::log::LogDebug()
            << "[YOLOv8Backend][debug] Decoded box before undo transform: ["
            << x1 << ", " << y1 << ", " << x2 << ", " << y2 << "]\n";
      }

      // 1. Subtract dw, dh
      x1 -= dw_;
      y1 -= dh_;
      x2 -= dw_;
      y2 -= dh_;

      // 2. Divide by ratio
      x1 /= ratio_;
      y1 /= ratio_;
      x2 /= ratio_;
      y2 /= ratio_;

      if (frameCount_ < 5) {
        score::mw::log::LogDebug()
            << "[YOLOv8Backend][debug] Decoded box after undo transform "
               "(before clip): ["
            << x1 << ", " << y1 << ", " << x2 << ", " << y2 << "]\n";
      }

      // 3. Clip to original image size
      x1 = std::clamp(x1, 0.0f, static_cast<float>(frame.cols));
      y1 = std::clamp(y1, 0.0f, static_cast<float>(frame.rows));
      x2 = std::clamp(x2, 0.0f, static_cast<float>(frame.cols));
      y2 = std::clamp(y2, 0.0f, static_cast<float>(frame.rows));

      // Convert to cv::Rect
      int left_val = static_cast<int>(std::round(x1));
      int top_val = static_cast<int>(std::round(y1));
      int right_val = static_cast<int>(std::round(x2));
      int bottom_val = static_cast<int>(std::round(y2));

      int width_val = right_val - left_val;
      int height_val = bottom_val - top_val;

      if (frameCount_ < 5) {
        score::mw::log::LogDebug()
            << "[YOLOv8Backend][debug] Decoded box after undo transform (after "
               "clip/round): ["
            << left_val << ", " << top_val << ", " << right_val << ", "
            << bottom_val << "]\n"
            << "[YOLOv8Backend][debug] ratio=" << ratio_ << ", dw=" << dw_
            << ", dh=" << dh_ << "\n";
      }

      boxes.emplace_back(left_val, top_val, width_val, height_val);
      confidences.push_back(confidence);
      classIds.push_back(class_id);
    }
  }

  std::vector<int> indices;
  cv::dnn::NMSBoxes(boxes, confidences, confThreshold_, nmsThreshold_, indices);

  std::vector<Detection> persons;
  std::vector<Detection> final_detections;
  int motorcycleCount = 0;
  int personCount = 0;

  for (int idx : indices) {
    const bool isVehicle = isVehicleClass(classIds[idx]);
    const bool isEmergency = isEmergencyClass(classIds[idx]);
    Detection d = populateDetection(boxes[idx], classIds[idx], confidences[idx],
                                    isVehicle, isEmergency);

    if (frameCount_ < 5) {
      score::mw::log::LogDebug()
          << "[YOLOv8Backend][debug] ClassId=" << d.ClassId
          << " ClassName=" << d.ClassName << " Confidence=" << d.Confidence
          << " Box=[" << d.Box.x << ", " << d.Box.y << ", "
          << d.Box.x + d.Box.width << ", " << d.Box.y + d.Box.height << "]"
          << " IsVehicle=" << d.IsVehicle << " IsEmergency=" << d.IsEmergency
          << "\n";
    }

    if (d.ClassId == 0) {  // Person
      persons.push_back(d);
      personCount++;
    } else {
      if (d.ClassId == 3) {  // Motorcycle
        motorcycleCount++;
      }
      if (isVehicle) {
        final_detections.push_back(d);
      }
    }
  }

  int estimatedMotorcycles = std::max(motorcycleCount, personCount);
  int syntheticAdded = 0;
  if (personCount > motorcycleCount) {
    syntheticAdded = personCount - motorcycleCount;
    for (int i = 0; i < syntheticAdded; ++i) {
      Detection synth;
      synth.Box = persons[i].Box;
      synth.Confidence = persons[i].Confidence;
      synth.ClassId = 3;
      synth.ClassName = "Motorcycle";
      synth.IsVehicle = true;
      synth.IsEmergency = false;
      final_detections.push_back(synth);
    }
  }

  if (frameCount_ < 5) {
    score::mw::log::LogDebug()
        << "[YOLOv8Backend] Motorcycles detected: " << motorcycleCount << "\n"
        << "[YOLOv8Backend] Persons detected: " << personCount << "\n"
        << "[YOLOv8Backend] Estimated motorcycles: " << estimatedMotorcycles
        << "\n"
        << "[YOLOv8Backend] Synthetic motorcycles added: " << syntheticAdded
        << "\n"
        << "[YOLOv8Backend] Motorcycle=" << motorcycleCount
        << " Person=" << personCount << " Estimated=" << estimatedMotorcycles
        << " SyntheticAdded=" << syntheticAdded << "\n";
  }

  result.Detections = std::move(final_detections);
  ++frameCount_;
}

bool YOLOv8Backend::isVehicleClass(int classId) const {
  if (isEmergencyClass(classId)) {
    return true;
  }

  // Use CocoClass enum — no magic numbers.
  switch (static_cast<CocoClass>(classId)) {
    case CocoClass::Bicycle:
    case CocoClass::Car:
    case CocoClass::Motorcycle:
    case CocoClass::Bus:
    case CocoClass::Truck:
      return true;
    default:
      return false;
  }
}

bool YOLOv8Backend::isEmergencyClass(int classId) const {
  return resolveCocoClassName(classId) == emergencyClass_;
}

Detection YOLOv8Backend::populateDetection(const cv::Rect& box, int classId,
                                           float confidence, bool isVehicle,
                                           bool isEmergency) {
  Detection d;
  d.Box = box;
  d.ClassId = classId;
  d.ClassName = resolveCocoClassName(classId);
  d.Confidence = confidence;
  d.IsVehicle = isVehicle;
  d.IsEmergency = isEmergency;
  return d;
}

}  // namespace traffic_perception
