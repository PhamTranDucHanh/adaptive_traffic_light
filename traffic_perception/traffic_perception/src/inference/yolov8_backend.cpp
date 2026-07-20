#include "traffic_perception/inference/yolov8_backend.h"
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstring>

namespace traffic_perception {

YOLOv8Backend::YOLOv8Backend(const std::string& modelPath)
    : env_(ORT_LOGGING_LEVEL_WARNING, "YOLOv8Backend"),
      memory_info_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
    try {
        session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(), Ort::SessionOptions{nullptr});
        
        // Setup input/output names
        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name = session_->GetInputNameAllocated(0, allocator);
        input_node_names_.push_back(strdup(input_name.get()));
        
        auto output_name = session_->GetOutputNameAllocated(0, allocator);
        output_node_names_.push_back(strdup(output_name.get()));
        
    } catch (const Ort::Exception& e) {
        throw std::runtime_error("ONNX Runtime exception loading model: " + std::string(e.what()));
    }
}

InferenceResult YOLOv8Backend::infer(const Frame& frame) {
    InferenceResult result;
    result.FrameId = frame.FrameId;

    try {
        std::vector<float> input_tensor_values;
        preprocess(frame.Image, input_tensor_values);

        std::array<int64_t, 4> input_shape = {1, 3, 640, 640};
        auto input_tensor = Ort::Value::CreateTensor<float>(memory_info_, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

        auto output_tensors = session_->Run(Ort::RunOptions{nullptr}, (const char* const*)input_node_names_.data(), &input_tensor, 1, (const char* const*)output_node_names_.data(), 1);

        postprocess(frame.Image, output_tensors, result);
    } catch (const std::exception& e) {
        std::cerr << "Inference failed: " << e.what() << std::endl;
        return InferenceResult();
    }

    return result;
}

std::string YOLOv8Backend::getModelName() const {
    return "YOLOv8Backend (ONNX Runtime)";
}

void YOLOv8Backend::draw(cv::Mat& image, const InferenceResult& inference) {
    for (const auto& det : inference.Detections) {
        cv::rectangle(image, det.Box, cv::Scalar(0, 255, 0), 2);
        std::string label = std::to_string(det.ClassId) + " (" + std::to_string(det.Confidence) + ")";
        cv::putText(image, label, det.Box.tl() - cv::Point(0, 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
    }
}

void YOLOv8Backend::preprocess(const cv::Mat& frame, std::vector<float>& input_tensor_values) {
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(640, 640));
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);

    resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);

    // HWC to CHW
    input_tensor_values.resize(3 * 640 * 640);
    std::vector<cv::Mat> channels(3);
    cv::split(resized, channels);
    int offset = 0;
    for (int i = 0; i < 3; ++i) {
        memcpy(input_tensor_values.data() + offset, channels[i].data, 640 * 640 * sizeof(float));
        offset += 640 * 640;
    }
}

void YOLOv8Backend::postprocess(const cv::Mat& frame, const std::vector<Ort::Value>& output_tensors, InferenceResult& result) {
    auto tensor_info = output_tensors[0].GetTensorTypeAndShapeInfo();
    auto shape = tensor_info.GetShape();
    const float* data = output_tensors[0].GetTensorData<float>();

    // Shape is [1, NumFeatures, NumAnchors]
    // NumFeatures = NumClasses + 4
    int num_classes = shape[1] - 4;
    int num_anchors = shape[2];

    // Transpose/Reshape logic: [1, Features, Anchors] -> [Anchors, Features] (row-major)
    cv::Mat output(shape[1], shape[2], CV_32F, const_cast<float*>(data));
    cv::Mat transposed = output.t();

    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    float x_scale = static_cast<float>(frame.cols) / 640.0f;
    float y_scale = static_cast<float>(frame.rows) / 640.0f;

    for (int i = 0; i < transposed.rows; ++i) {
        float* row = transposed.ptr<float>(i);
        float cx = row[0];
        float cy = row[1];
        float w = row[2];
        float h = row[3];
        float obj_score = row[4];

        // Remaining data are class scores
        float* class_scores = row + 5;
        int class_id = -1;
        float best_class_score = -1.0f;
        for (int j = 0; j < num_classes; ++j) {
            if (class_scores[j] > best_class_score) {
                best_class_score = class_scores[j];
                class_id = j;
            }
        }

        float confidence = obj_score * best_class_score;

        if (confidence > confThreshold_) {
            int left = static_cast<int>((cx - w / 2) * x_scale);
            int top = static_cast<int>((cy - h / 2) * y_scale);
            int width = static_cast<int>(w * x_scale);
            int height = static_cast<int>(h * y_scale);

            boxes.emplace_back(left, top, width, height);
            confidences.push_back(confidence);
            classIds.push_back(class_id);
        }
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, confThreshold_, nmsThreshold_, indices);

    for (int idx : indices) {
        Detection d;
        d.Box = boxes[idx];
        d.ClassId = classIds[idx];
        d.ClassName = "";
        d.Confidence = confidences[idx];
        result.Detections.push_back(d);
    }
}

}  // namespace traffic_perception
