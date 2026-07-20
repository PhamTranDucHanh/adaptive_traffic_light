#include "traffic_perception/inference/yolov8_oiv7_backend.h"
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <set>

namespace traffic_perception {

// Open Images V7 vehicle classes to filter
static const std::set<std::string> VEHICLE_CLASSES = {
    "Car", "Bus", "Truck", "Motorcycle", "Bicycle", "Van", "Ambulance", "Fire truck", "Police car"
};

YoloV8OIV7Backend::YoloV8OIV7Backend(const std::string& modelPath) {
    try {
        net_ = cv::dnn::readNetFromONNX(modelPath);
        if (net_.empty()) {
            throw std::runtime_error("Failed to load model: " + modelPath);
        }
        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_DEFAULT);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    } catch (const cv::Exception& e) {
        throw std::runtime_error("OpenCV exception loading model: " + std::string(e.what()));
    }
}

InferenceResult YoloV8OIV7Backend::infer(const Frame& frame) {
    InferenceResult result;
    result.FrameId = frame.FrameId;

    try {
        std::cout << "[YoloV8OIV7Backend] Before preprocess" << std::endl;
        cv::Mat blob;
        preprocess(frame.Image, blob);

        std::cout << "[YoloV8OIV7Backend] Before setInput" << std::endl;
        net_.setInput(blob);
        
        std::cout << "[YoloV8OIV7Backend] Before forward" << std::endl;
        std::vector<cv::Mat> outs;
        net_.forward(outs, net_.getUnconnectedOutLayersNames());

        std::cout << "[YoloV8OIV7Backend] After forward, dims: " << outs[0].dims << std::endl;
        for (int i = 0; i < outs[0].dims; ++i) {
            std::cout << "[YoloV8OIV7Backend] Dim " << i << ": " << outs[0].size[i] << std::endl;
        }

        std::cout << "[YoloV8OIV7Backend] Before postprocess" << std::endl;
        postprocess(frame.Image, outs, result);
    } catch (const std::exception& e) {
        std::cerr << "Inference failed: " << e.what() << std::endl;
        // Return empty result on failure as per requirements
        return InferenceResult();
    }

    return result;
}

std::string YoloV8OIV7Backend::getModelName() const {
    return "YoloV8OIV7";
}

void YoloV8OIV7Backend::preprocess(const cv::Mat& frame, cv::Mat& blob) {
    // YOLOv8 preprocessing: 640x640, resize, normalize [0,1]
    cv::dnn::blobFromImage(frame, blob, 1.0 / 255.0, cv::Size(640, 640), cv::Scalar(), true, false);
}

void YoloV8OIV7Backend::postprocess(const cv::Mat& frame, const std::vector<cv::Mat>& outs, InferenceResult& result) {
    // Tensor shape is [1, 605, 8400]
    // 4 bbox, 1 objectness, 600 classes
    
    cv::Mat output = outs[0].reshape(1, 605);
    // Transpose to [8400, 605] for easier iteration
    cv::Mat transposed = output.t();

    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    float x_scale = static_cast<float>(frame.cols) / 640.0f;
    float y_scale = static_cast<float>(frame.rows) / 640.0f;

    for (int i = 0; i < transposed.rows; ++i) {
        cv::Mat row = transposed.row(i);
        float* data = row.ptr<float>();

        // 4 bbox, 1 objectness
        float cx = data[0];
        float cy = data[1];
        float w = data[2];
        float h = data[3];
        float obj_score = data[4];

        // Find best class score among 600 classes
        float* class_scores = data + 5;
        int class_id = -1;
        float best_class_score = -1.0f;
        for (int j = 0; j < 600; ++j) {
            if (class_scores[j] > best_class_score) {
                best_class_score = class_scores[j];
                class_id = j;
            }
        }

        float confidence = obj_score * best_class_score;

        if (confidence > confThreshold_) {
            // Need a way to map class_id to string, for now assume name is ID
            std::string class_name = "Class_" + std::to_string(class_id);
            if (VEHICLE_CLASSES.find(class_name) == VEHICLE_CLASSES.end()) continue;

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
        d.ClassName = "Class_" + std::to_string(d.ClassId);
        d.Confidence = confidences[idx];
        result.Detections.push_back(d);
    }
}

void YoloV8OIV7Backend::draw(cv::Mat& image,
                            const InferenceResult& inference) {
    // Render detections
    for (const auto& det : inference.Detections) {
        cv::rectangle(image, det.Box, cv::Scalar(0, 255, 0), 2);
        std::string label = det.ClassName + ": " + std::to_string(det.Confidence);
        cv::putText(image, label, det.Box.tl() - cv::Point(0, 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
    }
}

}  // namespace traffic_perception
