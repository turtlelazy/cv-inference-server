// THIS IS AN AI GENERATED FILE AND NOT MY WORK

#include "YOLODetector.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

YOLODetector::YOLODetector(const std::string &model_path,
                           float conf_threshold,
                           float nms_threshold)
    : env_(ORT_LOGGING_LEVEL_WARNING, "YOLODetector"),
      conf_threshold_(conf_threshold),
      nms_threshold_(nms_threshold)
{
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(4);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // Uncomment to enable CUDA execution provider if built with GPU support:
    // OrtCUDAProviderOptions cuda_options;
    // session_options.AppendExecutionProvider_CUDA(cuda_options);

#ifdef _WIN32
    std::wstring w_model_path(model_path.begin(), model_path.end());
    session_ = std::make_unique<Ort::Session>(env_, w_model_path.c_str(), session_options);
#else
    session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), session_options);
#endif

    // --- Cache input/output names ---
    Ort::AllocatorWithDefaultOptions allocator;

    size_t num_inputs = session_->GetInputCount();
    for (size_t i = 0; i < num_inputs; ++i)
    {
        auto name = session_->GetInputNameAllocated(i, allocator);
        input_names_.emplace_back(name.get());
    }

    size_t num_outputs = session_->GetOutputCount();
    for (size_t i = 0; i < num_outputs; ++i)
    {
        auto name = session_->GetOutputNameAllocated(i, allocator);
        output_names_.emplace_back(name.get());
    }

    for (auto &n : input_names_)
        input_names_ptr_.push_back(n.c_str());
    for (auto &n : output_names_)
        output_names_ptr_.push_back(n.c_str());

    // --- Inspect input shape to determine network input size ---
    Ort::TypeInfo input_type_info = session_->GetInputTypeInfo(0);
    auto input_tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
    std::vector<int64_t> input_shape = input_tensor_info.GetShape();

    // Expected shape: [N, C, H, W]. Dynamic dims may show up as -1.
    if (input_shape.size() == 4)
    {
        if (input_shape[2] > 0)
            input_height_ = static_cast<int>(input_shape[2]);
        if (input_shape[3] > 0)
            input_width_ = static_cast<int>(input_shape[3]);
    }

    initClassNames();
}

void YOLODetector::initClassNames()
{
    // Default: COCO 80-class names. Replace with your own labels if the
    // model was trained on a different dataset.
    class_names_ = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train",
        "truck", "boat", "traffic light", "fire hydrant", "stop sign",
        "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep",
        "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
        "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard",
        "sports ball", "kite", "baseball bat", "baseball glove", "skateboard",
        "surfboard", "tennis racket", "bottle", "wine glass", "cup", "fork",
        "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
        "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair",
        "couch", "potted plant", "bed", "dining table", "toilet", "tv",
        "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave",
        "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase",
        "scissors", "teddy bear", "hair drier", "toothbrush"};
    num_classes_ = static_cast<int>(class_names_.size());
}

// ---------------------------------------------------------------------------
// Preprocessing (letterbox resize + BGR->RGB + normalize + HWC->CHW)
// ---------------------------------------------------------------------------

cv::Mat YOLODetector::preprocess(const cv::Mat &image, float &scale, int &pad_x, int &pad_y)
{
    int orig_w = image.cols;
    int orig_h = image.rows;

    scale = std::min(static_cast<float>(input_width_) / orig_w,
                     static_cast<float>(input_height_) / orig_h);

    int new_w = static_cast<int>(std::round(orig_w * scale));
    int new_h = static_cast<int>(std::round(orig_h * scale));

    pad_x = (input_width_ - new_w) / 2;
    pad_y = (input_height_ - new_h) / 2;

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    cv::Mat canvas(input_height_, input_width_, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(pad_x, pad_y, new_w, new_h)));

    cv::Mat rgb;
    cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);

    cv::Mat float_img;
    rgb.convertTo(float_img, CV_32FC3, 1.0 / 255.0);

    return float_img; // HWC, float32, normalized [0,1]
}

// ---------------------------------------------------------------------------
// Postprocessing: parse output, undo letterbox, run NMS
// ---------------------------------------------------------------------------

std::vector<Detection> YOLODetector::postprocess(const float *output_data,
                                                 const std::vector<int64_t> &output_shape,
                                                 float scale, int pad_x, int pad_y,
                                                 const cv::Size &original_size)
{
    if (output_shape.size() != 3)
        throw std::runtime_error("Unexpected output tensor rank; expected 3D output");

    int64_t dim1 = output_shape[1];
    int64_t dim2 = output_shape[2];

    bool channels_first = (dim1 < dim2);
    int64_t num_attrs = channels_first ? dim1 : dim2;
    int64_t num_anchors = channels_first ? dim2 : dim1;
    int64_t num_classes = num_attrs - 4;

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> class_ids;

    auto get_val = [&](int64_t anchor, int64_t attr) -> float
    {
        if (channels_first)
            return output_data[attr * num_anchors + anchor];
        else
            return output_data[anchor * num_attrs + attr];
    };

    for (int64_t a = 0; a < num_anchors; ++a)
    {
        float cx = get_val(a, 0);
        float cy = get_val(a, 1);
        float w = get_val(a, 2);
        float h = get_val(a, 3);

        // Step 1: normalized [0,1] -> pixel coords in the model's 640x640 input space
        if (cx <= 1.0f && cy <= 1.0f && w <= 1.0f && h <= 1.0f)
        {
            cx *= input_width_;
            w *= input_width_;
            cy *= input_height_;
            h *= input_height_;
        }

        // Find best class
        float best_score = -1.0f;
        int best_class = -1;
        for (int64_t c = 0; c < num_classes; ++c)
        {
            float s = get_val(a, 4 + c);
            if (s > best_score)
            {
                best_score = s;
                best_class = static_cast<int>(c);
            }
        }

        if (best_score < conf_threshold_)
            continue;

        // Step 2: undo the letterbox (remove padding, undo the resize scale)
        // to map from the 640x640 model space back to the ORIGINAL image's pixels
        float x1 = (cx - w / 2.0f - pad_x) / scale;
        float y1 = (cy - h / 2.0f - pad_y) / scale;
        float box_w = w / scale;
        float box_h = h / scale;

        // Step 3: clip to the original image's bounds
        x1 = std::clamp(x1, 0.0f, static_cast<float>(original_size.width - 1));
        y1 = std::clamp(y1, 0.0f, static_cast<float>(original_size.height - 1));
        box_w = std::min(box_w, original_size.width - x1);
        box_h = std::min(box_h, original_size.height - y1);

        boxes.emplace_back(cv::Rect(static_cast<int>(x1), static_cast<int>(y1),
                                    static_cast<int>(box_w), static_cast<int>(box_h)));
        scores.push_back(best_score);
        class_ids.push_back(best_class);
    }

    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, scores, conf_threshold_, nms_threshold_, nms_indices);

    std::vector<Detection> detections;
    detections.reserve(nms_indices.size());
    for (int idx : nms_indices)
    {
        Detection det;
        det.box = boxes[idx];
        det.confidence = scores[idx];
        det.class_id = class_ids[idx];
        detections.push_back(det);
    }

    return detections;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<Detection> YOLODetector::detect(const cv::Mat &image)
{
    if (image.empty())
        throw std::invalid_argument("YOLODetector::detect received an empty image");

    float scale = 1.0f;
    int pad_x = 0, pad_y = 0;

    cv::Mat processed = preprocess(image, scale, pad_x, pad_y);

    // HWC -> CHW
    std::vector<float> input_tensor_values(1 * 3 * input_height_ * input_width_);
    std::vector<cv::Mat> channels(3);
    for (int c = 0; c < 3; ++c)
    {
        channels[c] = cv::Mat(input_height_, input_width_, CV_32FC1,
                              input_tensor_values.data() + c * input_height_ * input_width_);
    }
    cv::split(processed, channels);

    std::vector<int64_t> input_shape = {1, 3, input_height_, input_width_};

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info_, input_tensor_values.data(), input_tensor_values.size(),
        input_shape.data(), input_shape.size());

    auto output_tensors = session_->Run(
        Ort::RunOptions{nullptr},
        input_names_ptr_.data(), &input_tensor, 1,
        output_names_ptr_.data(), output_names_ptr_.size());

    const float *output_data = output_tensors.front().GetTensorData<float>();
    std::vector<int64_t> output_shape =
        output_tensors.front().GetTensorTypeAndShapeInfo().GetShape();

    return postprocess(output_data, output_shape, scale, pad_x, pad_y, image.size());
}

// Parse results for sending in body of HTTP response
std::string YOLODetector::parseResults(const std::vector<Detection> &detections){
    std::string result = "{ \"detections\": [";
    for (size_t i = 0; i < detections.size(); ++i)
    {
        const auto &det = detections[i];
        result += "{";
        result += "\"class_id\": " + std::to_string(det.class_id) + ",";
        result += "\"class_name\": \"" + class_names_[det.class_id] + "\",";
        result += "\"confidence\": " + std::to_string(det.confidence) + ",";
        result += "\"box\": {";
        result += "\"x\": " + std::to_string(det.box.x) + ",";
        result += "\"y\": " + std::to_string(det.box.y) + ",";
        result += "\"width\": " + std::to_string(det.box.width) + ",";
        result += "\"height\": " + std::to_string(det.box.height);
        result += "}}";
        if (i != detections.size() - 1)
            result += ",";
    }
    result += "] }";
    return result;
}