// THIS IS AN AI GENERATED FILE AND NOT MY WORK

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

struct Detection
{
    cv::Rect box;     // bounding box in original image coordinates
    float confidence; // objectness * class score
    int class_id;     // index of the predicted class
};

class YOLODetector
{
public:
    // model_path: path to the .onnx file (exported YOLOv8/YOLOv5 style model)
    // conf_threshold: minimum confidence to keep a detection
    // nms_threshold: IoU threshold used during non-max suppression
    explicit YOLODetector(const std::string &model_path,
                          float conf_threshold = 0.25f,
                          float nms_threshold = 0.45f);

    std::vector<Detection> detect(const cv::Mat &image);
    std::string parseResults(const std::vector<Detection> &detections);

private:
    // Preprocessing: letterbox-resize the image to the model's input size,
    // convert BGR->RGB, normalize to [0,1] and produce a CHW blob.
    cv::Mat preprocess(const cv::Mat &image, float &scale, int &pad_x, int &pad_y);

    // Postprocessing: parse raw network output tensor into Detection objects,
    // undoing the letterbox transform and running NMS.
    std::vector<Detection> postprocess(const float *output_data,
                                       const std::vector<int64_t> &output_shape,
                                       float scale, int pad_x, int pad_y,
                                       const cv::Size &original_size);

    void initClassNames();

    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

    // Cached I/O metadata
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::vector<const char *> input_names_ptr_;
    std::vector<const char *> output_names_ptr_;

    int input_width_ = 640;
    int input_height_ = 640;
    int num_classes_ = 80;

    float conf_threshold_;
    float nms_threshold_;

    std::vector<std::string> class_names_;
};