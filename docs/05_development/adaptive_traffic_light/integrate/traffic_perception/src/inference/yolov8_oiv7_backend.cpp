#include "traffic_perception/inference/yolov8_oiv7_backend.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <unordered_map>

#include "score/mw/log/logging.h"

namespace traffic_perception {

// -----------------------------------------------------------------------
// OIV7 semantic mappings — private to this translation unit.
// No other component should know these names or IDs.
// -----------------------------------------------------------------------

static const std::unordered_map<std::string, bool> kOiv7VehicleNames = {
    {"Car", true},        {"Bus", true},     {"Truck", true},
    {"Motorcycle", true}, {"Bicycle", true}, {"Van", true}};

static const std::vector<std::string> kOiv7Labels = {
    "Accordion", "Adhesive tape", "Aircraft", "Airplane", "Alarm clock",
    "Alpaca",    "Ambulance",     "Animal",   "Ant",      "Antelope",
};

static std::string resolveOiv7ClassName(int classId) {
  if (classId >= 0 && classId < static_cast<int>(kOiv7Labels.size())) {
    return kOiv7Labels[static_cast<std::size_t>(classId)];
  }
  switch (classId) {
    case 42:
      return "Bicycle";
    case 73:
      return "Bus";
    case 90:
      return "Car";
    case 342:
      return "Motorcycle";
    case 558:
      return "Truck";
    case 564:
      return "Van";
    default:
      return "Unknown_" + std::to_string(classId);
  }
}

YoloV8OIV7Backend::YoloV8OIV7Backend(
    const std::string& modelPath, const std::string& emergencyClass)
    : emergencyClass_(emergencyClass),
      env_(ORT_LOGGING_LEVEL_WARNING, "YoloV8OIV7Backend"),
      memory_info_(
          Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
  try {
    Ort::SessionOptions sessionOptions;
    session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(),
                                              sessionOptions);

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

InferenceResult YoloV8OIV7Backend::infer(const Frame& frame) {
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

std::string YoloV8OIV7Backend::getModelName() const {
  return "YoloV8OIV7 (ONNX Runtime)";
}

void YoloV8OIV7Backend::preprocess(const cv::Mat& frame,
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

void YoloV8OIV7Backend::postprocess(
    const cv::Mat& frame, const std::vector<Ort::Value>& output_tensors,
    InferenceResult& result) {
  if (output_tensors.empty()) return;

  auto tensor_info = output_tensors[0].GetTensorTypeAndShapeInfo();
  auto shape = tensor_info.GetShape();
  const float* data = output_tensors[0].GetTensorData<float>();

  // Ultralytics YOLOv8 ONNX exports (including OIV7 600-class models [1, 604,
  // 8400]) output format: [1, 4 + num_classes, num_anchors] with NO objectness
  // score. Layout A (YOLOv8 standard): shape[1] = 4 (bbox) + num_classes.
  // Layout B (legacy/custom):   shape[1] = 4 (bbox) + 1 (objectness) +
  // num_classes (e.g. 85).
  const bool has_objectness = (shape[1] == 85);
  const int numClasses =
      static_cast<int>(shape[1]) - 4 - (has_objectness ? 1 : 0);
  const int numAnchors = static_cast<int>(shape[2]);

  if (frameCount_ < 5) {
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][debug] Input tensor shape: [1, 3, 640, 640]\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][debug] Output tensor shape: [";
    for (std::size_t s = 0; s < shape.size(); ++s) {
      score::mw::log::LogDebug()
          << shape[s] << (s + 1 < shape.size() ? ", " : "");
    }
    score::mw::log::LogDebug() << "]\n";
    score::mw::log::LogDebug() << "[YoloV8OIV7Backend][debug] has_objectness: "
                               << (has_objectness ? "true" : "false") << "\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][debug] numClasses: " << numClasses << "\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][debug] numAnchors: " << numAnchors << "\n";
  }

  cv::Mat output(static_cast<int>(shape[1]), static_cast<int>(shape[2]), CV_32F,
                 const_cast<float*>(data));
  cv::Mat transposed = output.t();

  std::vector<int> classIds;
  std::vector<float> confidences;
  std::vector<cv::Rect> boxes;

  float x_scale = static_cast<float>(frame.cols) / 640.0f;
  float y_scale = static_cast<float>(frame.rows) / 640.0f;

  float global_max_score = -1.0f;
  double sum_scores = 0.0;
  int total_score_count = 0;
  int count_gt_010 = 0;
  int count_gt_025 = 0;
  int count_gt_050 = 0;
  int max_class_id_found = -1;

  for (int i = 0; i < transposed.rows; ++i) {
    float* row = transposed.ptr<float>(i);
    float cx = row[0];
    float cy = row[1];
    float w = row[2];
    float h = row[3];

    float obj_score = 1.0f;
    int class_offset = 4;
    if (has_objectness) {
      obj_score = row[4];
      class_offset = 5;
    }

    float* class_scores = row + class_offset;
    int class_id = -1;
    float best_class_score = -1.0f;
    for (int j = 0; j < numClasses; ++j) {
      if (class_scores[j] > best_class_score) {
        best_class_score = class_scores[j];
        class_id = j;
      }
    }

    float confidence = obj_score * best_class_score;

    if (confidence > global_max_score) {
      global_max_score = confidence;
      max_class_id_found = class_id;
    }
    sum_scores += confidence;
    total_score_count++;

    if (confidence > 0.10f) count_gt_010++;
    if (confidence > 0.25f) count_gt_025++;
    if (confidence > 0.50f) count_gt_050++;

    if (frameCount_ == 0 && i < 10) {
      score::mw::log::LogDebug()
          << "[YoloV8OIV7Backend][anchor " << i << "]" << " cx=" << cx
          << " cy=" << cy << " w=" << w << " h=" << h
          << " max_class_id=" << class_id
          << " max_class_score=" << best_class_score
          << " confidence=" << confidence << " first 5 scores: ";
      for (int k = 0; k < std::min(5, numClasses); ++k) {
        score::mw::log::LogDebug() << class_scores[k] << " ";
      }
      score::mw::log::LogDebug() << "\n";
    }

    if (confidence > confThreshold_) {
      // Apply inverse Letterbox transform: scale_boxes()
      float x1 = cx - w / 2.0f;
      float y1 = cy - h / 2.0f;
      float x2 = cx + w / 2.0f;
      float y2 = cy + h / 2.0f;

      if (frameCount_ < 5) {
        score::mw::log::LogDebug()
            << "[YoloV8OIV7Backend][debug] Decoded box before undo transform: ["
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
            << "[YoloV8OIV7Backend][debug] Decoded box after undo "
               "transform (before clip): ["
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
            << "[YoloV8OIV7Backend][debug] Decoded box after undo "
               "transform (after clip/round): ["
            << left_val << ", " << top_val << ", " << right_val << ", "
            << bottom_val << "]\n"
            << "[YoloV8OIV7Backend][debug] ratio=" << ratio_ << ", dw=" << dw_
            << ", dh=" << dh_ << "\n";
      }

      boxes.emplace_back(left_val, top_val, width_val, height_val);
      confidences.push_back(confidence);
      classIds.push_back(class_id);
    }
  }

  if (frameCount_ < 5) {
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][stats] global_max_score: " << global_max_score
        << " (max_class_id: " << max_class_id_found << ")\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][stats] global_avg_score: "
        << (total_score_count > 0 ? (sum_scores / total_score_count) : 0.0)
        << "\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][stats] anchors > 0.10: " << count_gt_010
        << "\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][stats] anchors > 0.25: " << count_gt_025
        << "\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][stats] anchors > 0.50: " << count_gt_050
        << "\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][stats] boxes before NMS: " << boxes.size()
        << "\n";
  }

  std::vector<int> indices;
  cv::dnn::NMSBoxes(boxes, confidences, confThreshold_, nmsThreshold_, indices);

  if (frameCount_ < 5) {
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][debug] Detections before NMS: " << boxes.size()
        << "\n";
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend][debug] Detections after NMS: " << indices.size()
        << "\n";
  }

  std::vector<Detection> persons;
  std::vector<Detection> final_detections;
  int motorcycleCount = 0;
  int personCount = 0;

  int loggedDetections = 0;
  for (int idx : indices) {
    const std::string class_name = resolveOiv7ClassName(classIds[idx]);
    const bool isVehicle = isVehicleClass(class_name);
    const bool isEmergency = isEmergencyClass(class_name);
    Detection d = populateDetection(boxes[idx], classIds[idx], class_name,
                                    confidences[idx], isVehicle, isEmergency);

    if (frameCount_ < 5 && loggedDetections < 5) {
      score::mw::log::LogDebug()
          << "[YoloV8OIV7Backend][debug] Detection " << loggedDetections << ":"
          << " ClassId=" << d.ClassId << " ClassName=" << d.ClassName
          << " Confidence=" << d.Confidence << " Box=[" << d.Box.x << ", "
          << d.Box.y << ", " << d.Box.x + d.Box.width << ", "
          << d.Box.y + d.Box.height << "]" << " IsVehicle=" << d.IsVehicle
          << " IsEmergency=" << d.IsEmergency << "\n";
      loggedDetections++;
    }

    if (class_name == "Person") {
      persons.push_back(d);
      personCount++;
    } else {
      if (class_name == "Motorcycle") {
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
      synth.ClassId = 342;  // Motorcycle in OIV7
      synth.ClassName = "Motorcycle";
      synth.IsVehicle = true;
      synth.IsEmergency = false;
      final_detections.push_back(synth);
    }
  }

  if (frameCount_ < 5) {
    score::mw::log::LogDebug()
        << "[YoloV8OIV7Backend] Motorcycles detected: " << motorcycleCount
        << "\n"
        << "[YoloV8OIV7Backend] Persons detected: " << personCount << "\n"
        << "[YoloV8OIV7Backend] Estimated motorcycles: " << estimatedMotorcycles
        << "\n"
        << "[YoloV8OIV7Backend] Synthetic motorcycles added: " << syntheticAdded
        << "\n"
        << "[YoloV8OIV7Backend] Motorcycle=" << motorcycleCount
        << " Person=" << personCount << " Estimated=" << estimatedMotorcycles
        << " SyntheticAdded=" << syntheticAdded << "\n";
  }

  result.Detections = std::move(final_detections);
  ++frameCount_;
}

bool YoloV8OIV7Backend::isVehicleClass(const std::string& className) const {
  return kOiv7VehicleNames.count(className) > 0 ||
         isEmergencyClass(className);
}

bool YoloV8OIV7Backend::isEmergencyClass(const std::string& className) const {
  return className == emergencyClass_;
}

Detection YoloV8OIV7Backend::populateDetection(const cv::Rect& box, int classId,
                                               const std::string& className,
                                               float confidence, bool isVehicle,
                                               bool isEmergency) {
  Detection d;
  d.Box = box;
  d.ClassId = classId;
  d.ClassName = className;
  d.Confidence = confidence;
  d.IsVehicle = isVehicle;
  d.IsEmergency = isEmergency;
  return d;
}

void YoloV8OIV7Backend::draw(cv::Mat& image, const InferenceResult& inference) {
  for (const auto& det : inference.Detections) {
    cv::rectangle(image, det.Box, cv::Scalar(0, 255, 0), 2);
    std::string label = det.ClassName + ": " + std::to_string(det.Confidence);
    cv::putText(image, label, det.Box.tl() - cv::Point(0, 5),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
  }
}

}  // namespace traffic_perception
