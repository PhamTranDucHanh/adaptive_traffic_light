#include "traffic_perception/inference/rt_detrv2_backend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <opencv2/imgproc.hpp>

#include "score/mw/log/logging.h"

namespace traffic_perception {
namespace {

std::string ResolveCocoClassName(const std::int64_t classId) {
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

std::size_t FindName(const std::vector<std::string>& names,
                     const std::string& expected) {
  const auto position = std::find(names.begin(), names.end(), expected);
  if (position == names.end()) {
    throw std::runtime_error("RT-DETRv2 ONNX model is missing tensor: " +
                             expected);
  }
  return static_cast<std::size_t>(std::distance(names.begin(), position));
}

std::size_t ElementCount(const std::vector<std::int64_t>& shape) {
  std::size_t count{1U};
  for (const auto dimension : shape) {
    if (dimension <= 0) {
      throw std::runtime_error("RT-DETRv2 produced a dynamic output shape");
    }
    count *= static_cast<std::size_t>(dimension);
  }
  return count;
}

}  // namespace

RtDetrv2Backend::RtDetrv2Backend(const std::string& modelPath,
                                 const std::string& emergencyClass,
                                 const int inferenceCallerCpu,
                                 const int inferenceCallerPriority)
    : emergencyClass_(emergencyClass),
      env_(ORT_LOGGING_LEVEL_WARNING, "RtDetrv2Backend"),
      ortThreadPoolConfig_(inferenceCallerCpu, inferenceCallerPriority),
      memoryInfo_(
          Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
  try {
    Ort::SessionOptions sessionOptions;
    ConfigureOrtThreadPool(sessionOptions, ortThreadPoolConfig_);
    session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(),
                                              sessionOptions);
    inspectModelContract();
  } catch (const Ort::Exception& error) {
    throw std::runtime_error("ONNX Runtime exception loading RT-DETRv2 model: " +
                             std::string{error.what()});
  }
}

void RtDetrv2Backend::inspectModelContract() {
  Ort::AllocatorWithDefaultOptions allocator;

  const std::size_t inputCount = session_->GetInputCount();
  const std::size_t outputCount = session_->GetOutputCount();
  inputNamesStorage_.reserve(inputCount);
  outputNamesStorage_.reserve(outputCount);

  for (std::size_t index = 0; index < inputCount; ++index) {
    auto name = session_->GetInputNameAllocated(index, allocator);
    inputNamesStorage_.emplace_back(name.get());
  }
  for (std::size_t index = 0; index < outputCount; ++index) {
    auto name = session_->GetOutputNameAllocated(index, allocator);
    outputNamesStorage_.emplace_back(name.get());
  }

  const std::size_t imagesIndex = FindName(inputNamesStorage_, "images");
  static_cast<void>(FindName(inputNamesStorage_, "orig_target_sizes"));
  labelsOutputIndex_ = FindName(outputNamesStorage_, "labels");
  boxesOutputIndex_ = FindName(outputNamesStorage_, "boxes");
  scoresOutputIndex_ = FindName(outputNamesStorage_, "scores");

  // TensorTypeAndShapeInfo borrows storage from TypeInfo in this ORT API.
  // Keep TypeInfo alive until all tensor metadata has been consumed.
  const auto inputTypeInfo = session_->GetInputTypeInfo(imagesIndex);
  const auto inputInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
  const auto inputShape = inputInfo.GetShape();
  if (inputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
      inputShape.size() != 4U ||
      (inputShape[1] > 0 && inputShape[1] != 3)) {
    throw std::runtime_error(
        "RT-DETRv2 images input must be a float NCHW tensor with 3 channels");
  }
  if (inputShape[2] > 0) inputHeight_ = inputShape[2];
  if (inputShape[3] > 0) inputWidth_ = inputShape[3];

  // Run inputs in the model's declared order, regardless of their positions.
  inputNames_.reserve(inputNamesStorage_.size());
  for (const auto& name : inputNamesStorage_) inputNames_.push_back(name.c_str());
  outputNames_.reserve(outputNamesStorage_.size());
  for (const auto& name : outputNamesStorage_)
    outputNames_.push_back(name.c_str());

  score::mw::log::LogInfo()
      << "[RT_DETRV2][INIT] contract=images+orig_target_sizes->labels+boxes+scores"
      << "; input_width=" << inputWidth_ << "; input_height=" << inputHeight_
      << "; outputs=" << outputCount;
}

InferenceResult RtDetrv2Backend::infer(const Frame& frame) {
  InferenceResult result{};
  result.FrameId = frame.FrameId;

  try {
    std::vector<float> imageData;
    preprocess(frame.Image, imageData);

    const std::array<std::int64_t, 4> imageShape{1, 3, inputHeight_,
                                                 inputWidth_};
    std::array<std::int64_t, 2> originalSize{
        static_cast<std::int64_t>(frame.Image.cols),
        static_cast<std::int64_t>(frame.Image.rows)};
    const std::array<std::int64_t, 2> originalSizeShape{1, 2};

    auto imageTensor = Ort::Value::CreateTensor<float>(
        memoryInfo_, imageData.data(), imageData.size(), imageShape.data(),
        imageShape.size());
    auto originalSizeTensor = Ort::Value::CreateTensor<std::int64_t>(
        memoryInfo_, originalSize.data(), originalSize.size(),
        originalSizeShape.data(), originalSizeShape.size());

    std::vector<Ort::Value> inputTensors;
    inputTensors.reserve(inputNamesStorage_.size());
    for (const auto& name : inputNamesStorage_) {
      if (name == "images") {
        inputTensors.emplace_back(std::move(imageTensor));
      } else if (name == "orig_target_sizes") {
        inputTensors.emplace_back(std::move(originalSizeTensor));
      } else {
        throw std::runtime_error("Unsupported RT-DETRv2 input: " + name);
      }
    }

    auto outputs = session_->Run(
        Ort::RunOptions{nullptr}, inputNames_.data(), inputTensors.data(),
        inputTensors.size(), outputNames_.data(), outputNames_.size());
    postprocess(frame.Image, outputs, result);
  } catch (const std::exception& error) {
    score::mw::log::LogError()
        << "[RT_DETRV2][INFERENCE][ERROR] " << error.what();
    return InferenceResult{};
  }

  return result;
}

void RtDetrv2Backend::preprocess(const cv::Mat& frame,
                                 std::vector<float>& tensor) const {
  if (frame.empty()) {
    throw std::runtime_error("RT-DETRv2 received an empty frame");
  }

  cv::Mat resized;
  cv::resize(frame, resized,
             cv::Size(static_cast<int>(inputWidth_),
                      static_cast<int>(inputHeight_)),
             0.0, 0.0, cv::INTER_LINEAR);
  cv::Mat rgb;
  cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
  rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);

  const std::size_t planeSize =
      static_cast<std::size_t>(inputHeight_ * inputWidth_);
  tensor.resize(3U * planeSize);
  std::array<cv::Mat, 3> channels{
      cv::Mat(static_cast<int>(inputHeight_), static_cast<int>(inputWidth_),
              CV_32F, tensor.data()),
      cv::Mat(static_cast<int>(inputHeight_), static_cast<int>(inputWidth_),
              CV_32F, tensor.data() + planeSize),
      cv::Mat(static_cast<int>(inputHeight_), static_cast<int>(inputWidth_),
              CV_32F, tensor.data() + 2U * planeSize)};
  cv::split(rgb, channels.data());
}

void RtDetrv2Backend::postprocess(const cv::Mat& frame,
                                  const std::vector<Ort::Value>& outputs,
                                  InferenceResult& result) const {
  if (outputs.size() != outputNamesStorage_.size()) {
    throw std::runtime_error("RT-DETRv2 returned an unexpected output count");
  }

  const auto labelsInfo =
      outputs[labelsOutputIndex_].GetTensorTypeAndShapeInfo();
  const auto boxesInfo = outputs[boxesOutputIndex_].GetTensorTypeAndShapeInfo();
  const auto scoresInfo =
      outputs[scoresOutputIndex_].GetTensorTypeAndShapeInfo();
  const auto labelsShape = labelsInfo.GetShape();
  const auto boxesShape = boxesInfo.GetShape();
  const auto scoresShape = scoresInfo.GetShape();

  if (labelsInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
      boxesInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
      scoresInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
      boxesShape.empty() || boxesShape.back() != 4) {
    throw std::runtime_error("RT-DETRv2 output tensor types/shapes are invalid");
  }

  const std::size_t detectionCount = ElementCount(labelsShape);
  if (ElementCount(scoresShape) != detectionCount ||
      ElementCount(boxesShape) != detectionCount * 4U) {
    throw std::runtime_error("RT-DETRv2 output tensor sizes do not match");
  }

  const auto* labels =
      outputs[labelsOutputIndex_].GetTensorData<std::int64_t>();
  const auto* boxes = outputs[boxesOutputIndex_].GetTensorData<float>();
  const auto* scores = outputs[scoresOutputIndex_].GetTensorData<float>();
  result.Detections.reserve(detectionCount);

  for (std::size_t index = 0; index < detectionCount; ++index) {
    if (scores[index] < confidenceThreshold_) continue;

    const float* box = boxes + index * 4U;
    const float x1 =
        std::clamp(box[0], 0.0F, static_cast<float>(frame.cols));
    const float y1 =
        std::clamp(box[1], 0.0F, static_cast<float>(frame.rows));
    const float x2 =
        std::clamp(box[2], 0.0F, static_cast<float>(frame.cols));
    const float y2 =
        std::clamp(box[3], 0.0F, static_cast<float>(frame.rows));
    if (x2 <= x1 || y2 <= y1) continue;

    const std::string className = ResolveCocoClassName(labels[index]);
    const bool isEmergency = isEmergencyClass(labels[index]);
    const bool isVehicle = isVehicleClass(labels[index]);
    if (!isVehicle) continue;

    Detection detection{};
    detection.Box = cv::Rect{
        static_cast<int>(x1), static_cast<int>(y1),
        static_cast<int>(x2 - x1), static_cast<int>(y2 - y1)};
    detection.ClassId = static_cast<std::int32_t>(labels[index]);
    detection.Confidence = scores[index];
    detection.ClassName = className;
    detection.IsVehicle = true;
    detection.IsEmergency = isEmergency;
    result.Detections.emplace_back(std::move(detection));
  }
}

bool RtDetrv2Backend::isVehicleClass(const std::int64_t classId) const {
  return isEmergencyClass(classId) || classId == 1 || classId == 2 ||
         classId == 3 || classId == 5 || classId == 7;
}

bool RtDetrv2Backend::isEmergencyClass(const std::int64_t classId) const {
  return ResolveCocoClassName(classId) == emergencyClass_;
}

std::string RtDetrv2Backend::getModelName() const {
  return "RT-DETRv2 (ONNX Runtime)";
}

void RtDetrv2Backend::draw(cv::Mat& image,
                           const InferenceResult& inference) {
  for (const auto& detection : inference.Detections) {
    const cv::Scalar color = detection.IsEmergency
                                 ? cv::Scalar{0, 0, 255}
                                 : cv::Scalar{0, 255, 0};
    cv::rectangle(image, detection.Box, color, 2);
    const std::string label = detection.ClassName + ": " +
                              std::to_string(detection.Confidence);
    cv::putText(image, label, detection.Box.tl() - cv::Point{0, 5},
                cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
  }
}

}  // namespace traffic_perception
