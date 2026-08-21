#include "traffic_perception/inference/yolov8_oiv7_backend.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <unordered_map>

#include "score/mw/log/logging.h"
#include "traffic_perception/inference/inference_trace.h"

namespace traffic_perception {

// -----------------------------------------------------------------------
// OIV7 semantic mappings — private to this translation unit.
// No other component should know these names or IDs.
// -----------------------------------------------------------------------

static const std::unordered_map<std::string, bool> kOiv7VehicleNames = {
    {"Car", true},
    {"Bus", true},
    {"Truck", true},
    {"Bicycle", true},
    {"Van", true}};

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

YoloV8OIV7Backend::YoloV8OIV7Backend(const std::string& modelPath,
                                     const std::string& emergencyClass,
                                     int inferenceCallerCpu,
                                     int inferenceCallerPriority)
    : emergencyClass_(emergencyClass),
      env_(ORT_LOGGING_LEVEL_WARNING, "YoloV8OIV7Backend"),
      ortThreadPoolConfig_(inferenceCallerCpu, inferenceCallerPriority),
      memory_info_(
          Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
  try {
    Ort::SessionOptions sessionOptions;
    ConfigureOrtThreadPool(sessionOptions, ortThreadPoolConfig_);
    session_ =
        std::make_unique<Ort::Session>(env_, modelPath.c_str(), sessionOptions);

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
  InferenceResult result{};
  result.FrameId = frame.FrameId;

  const std::uint64_t callIndex = inferenceCallCount_++;
  const auto traceBegin = InferenceTraceClock::now();
  auto preprocessEnd = traceBegin;
  auto runtimeEnd = traceBegin;
  const char* currentStage = "preprocess";

  try {
    preprocess(frame.Image, inputTensorValues_);
    preprocessEnd = InferenceTraceClock::now();

    std::array<int64_t, 4> input_shape = {1, 3, 640, 640};
    auto input_tensor = Ort::Value::CreateTensor<float>(
        memory_info_, inputTensorValues_.data(), inputTensorValues_.size(),
        input_shape.data(), input_shape.size());

    currentStage = "runtime";
    auto output_tensors = session_->Run(
        Ort::RunOptions{nullptr}, (const char* const*)input_node_names_.data(),
        &input_tensor, 1, (const char* const*)output_node_names_.data(), 1);
    runtimeEnd = InferenceTraceClock::now();

    currentStage = "postprocess";
    postprocess(frame.Image, output_tensors, result);
  } catch (const std::exception& e) {
    const auto traceEnd = InferenceTraceClock::now();
    LogInferenceTraceFailure("yolov8_oiv7", callIndex, frame.FrameId,
                             currentStage, traceBegin, preprocessEnd,
                             runtimeEnd, traceEnd);
    score::mw::log::LogDebug() << "Inference failed: " << e.what() << "\n";
    return InferenceResult{};
  }

  const auto traceEnd = InferenceTraceClock::now();
  result.InferenceLatencyMs =
      InferenceTraceElapsedUs(traceBegin, traceEnd) / 1000;
  LogInferenceTrace("yolov8_oiv7", callIndex, frame.FrameId, traceBegin,
                    preprocessEnd, runtimeEnd, traceEnd,
                    result.Detections.size());

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
  const cv::Mat* resized = &frame;
  if (frame.cols != new_unpad_w || frame.rows != new_unpad_h) {
    cv::resize(frame, resizedBuffer_, cv::Size(new_unpad_w, new_unpad_h), 0, 0,
               cv::INTER_LINEAR);
    resized = &resizedBuffer_;
  }

  cv::copyMakeBorder(*resized, letterboxBuffer_, top, bottom, left, right,
                     cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

  // Convert from BGR to RGB
  cv::cvtColor(letterboxBuffer_, rgbBuffer_, cv::COLOR_BGR2RGB);

  // Normalize
  rgbBuffer_.convertTo(normalizedBuffer_, CV_32FC3, 1.0 / 255.0);

  // HWC to CHW
  constexpr std::size_t kPlaneSize = 640U * 640U;
  if (input_tensor_values.size() != 3U * kPlaneSize) {
    input_tensor_values.resize(3U * kPlaneSize);
  }
  std::array<cv::Mat, 3> channels{
      cv::Mat(640, 640, CV_32F, input_tensor_values.data()),
      cv::Mat(640, 640, CV_32F, input_tensor_values.data() + kPlaneSize),
      cv::Mat(640, 640, CV_32F, input_tensor_values.data() + 2U * kPlaneSize)};
  cv::split(normalizedBuffer_, channels.data());
}

void YoloV8OIV7Backend::postprocess(
    const cv::Mat& frame, const std::vector<Ort::Value>& output_tensors,
    InferenceResult& result) {
  if (output_tensors.empty()) return;

  auto tensor_info = output_tensors[0].GetTensorTypeAndShapeInfo();
  auto shape = tensor_info.GetShape();
  const float* data = output_tensors[0].GetTensorData<float>();
  if (shape.size() != 3U || shape[0] != 1 || shape[1] <= 4 || shape[2] <= 0) {
    throw std::runtime_error("YOLOv8-OIV7 returned an invalid output shape");
  }

  // Ultralytics YOLOv8 ONNX exports (including OIV7 600-class models [1, 604,
  // 8400]) output format: [1, 4 + num_classes, num_anchors] with NO objectness
  // score. Layout A (YOLOv8 standard): shape[1] = 4 (bbox) + num_classes.
  // Layout B (legacy/custom):   shape[1] = 4 (bbox) + 1 (objectness) +
  // num_classes (e.g. 85).
  const bool has_objectness = (shape[1] == 85);
  const int numClasses =
      static_cast<int>(shape[1]) - 4 - (has_objectness ? 1 : 0);
  const auto numAnchors = static_cast<std::size_t>(shape[2]);
  const cv::Mat output(static_cast<int>(shape[1]), static_cast<int>(numAnchors),
                       CV_32F, const_cast<float*>(data));
  cv::transpose(output, transposedOutputBuffer_);

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

  std::vector<int> classIds;
  std::vector<float> confidences;
  std::vector<cv::Rect> boxes;

  float global_max_score = -1.0f;
  double sum_scores = 0.0;
  int total_score_count = 0;
  int count_gt_010 = 0;
  int count_gt_025 = 0;
  int count_gt_050 = 0;
  int max_class_id_found = -1;

  for (std::size_t i = 0; i < numAnchors; ++i) {
    const float* row = transposedOutputBuffer_.ptr<float>(static_cast<int>(i));
    const float cx = row[0];
    const float cy = row[1];
    const float w = row[2];
    const float h = row[3];

    float obj_score = 1.0f;
    std::size_t class_offset = 4U;
    if (has_objectness) {
      obj_score = row[4];
      class_offset = 5U;
    }

    int class_id = -1;
    float best_class_score = -1.0f;
    for (int j = 0; j < numClasses; ++j) {
      const float score = row[class_offset + static_cast<std::size_t>(j)];
      if (score > best_class_score) {
        best_class_score = score;
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

    if (frameCount_ == 0 && i < 10U) {
      score::mw::log::LogDebug()
          << "[YoloV8OIV7Backend][anchor " << i << "]" << " cx=" << cx
          << " cy=" << cy << " w=" << w << " h=" << h
          << " max_class_id=" << class_id
          << " max_class_score=" << best_class_score
          << " confidence=" << confidence << " first 5 scores: ";
      for (int k = 0; k < std::min(5, numClasses); ++k) {
        score::mw::log::LogDebug()
            << row[class_offset + static_cast<std::size_t>(k)] << " ";
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

  std::vector<Detection> final_detections;

  int loggedDetections = 0;
  for (int idx : indices) {
    const std::string class_name = resolveOiv7ClassName(classIds[idx]);

    // OIV7 motorcycle detections are not reliable enough for this application.
    // Reject the native class and do not synthesize motorcycles from Person.
    if (class_name == "Motorcycle") {
      continue;
    }

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

    if (isVehicle) {
      final_detections.push_back(d);
    }
  }

  result.Detections = std::move(final_detections);
  ++frameCount_;
}

bool YoloV8OIV7Backend::isVehicleClass(const std::string& className) const {
  return kOiv7VehicleNames.count(className) > 0 || isEmergencyClass(className);
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
    const cv::Scalar color =
        det.IsEmergency ? cv::Scalar{0, 0, 255} : cv::Scalar{0, 255, 0};
    cv::rectangle(image, det.Box, color, 2);
    std::string label = det.ClassName + ": " + std::to_string(det.Confidence);
    cv::putText(image, label, det.Box.tl() - cv::Point(0, 5),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
  }
}

}  // namespace traffic_perception
