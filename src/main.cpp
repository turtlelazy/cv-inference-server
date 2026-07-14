#include <iostream>
#include "TCPServer.hpp"
#include "Router.hpp"
#include "HTTPTypes.hpp"
#include "YOLODetector.hpp"

#include <opencv2/opencv.hpp>

HTTPResponse get_main(HTTPRequest req){
    HTTPResponse response = {"200","OK",""};
    return response;
}

#include <fstream>
#include <chrono>
#include <stdexcept>
#include <filesystem>

// at the top of save_image, before opening the file

// Claude generated to quickly test body parsing and local file saving. Remove when done testing.
HTTPResponse save_image(HTTPRequest req)
{
    if (req.body.empty())
    {
        return HTTPResponse{"400", "Bad Request", "No image data in request body"};
    }

    // Determine file extension from Content-Type header, default to .jpg
    std::string extension = ".jpg";
    auto ct_it = req.headers.find("Content-Type");
    if (ct_it != req.headers.end())
    {
        const std::string &content_type = ct_it->second;
        if (content_type == "image/png")
        {
            extension = ".png";
        }
        else if (content_type == "image/jpeg" || content_type == "image/jpg")
        {
            extension = ".jpg";
        }
        else if (content_type == "image/gif")
        {
            extension = ".gif";
        }
        else if (content_type == "image/webp")
        {
            extension = ".webp";
        }
        else
        {
            return HTTPResponse{"415", "Unsupported Media Type", "Unsupported image content type: " + content_type};
        }
    }

    // Generate a unique filename using current time (avoid collisions)
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::string filename = "uploads/image_" + std::to_string(now_ms) + extension;

    std::ofstream out_file(filename, std::ios::binary);
    if (!out_file.is_open())
    {
        return HTTPResponse{"500", "Internal Server Error", "Failed to open file for writing: " + filename};
    }

    out_file.write(
        reinterpret_cast<const char *>(req.body.data()),
        static_cast<std::streamsize>(req.body.size()));

    if (!out_file)
    {
        return HTTPResponse{"500", "Internal Server Error", "Failed to write image to disk"};
    }

    out_file.close();
    // Test if OpenCV can read the saved image
    // cv::Mat image = cv::imread(filename);
    cv::Mat image =
        cv::imdecode(req.body, cv::IMREAD_COLOR);

    if (image.empty())
    {
        std::cerr << "Failed to load image\n";
    }
    else
    {
        std::cout
            << image.cols
            << " x "
            << image.rows
            << std::endl;
    }

    HTTPResponse response = {"200", "OK", "Image saved as " + filename};
    return response;
}

HTTPResponse detect(YOLODetector& detector, HTTPRequest req)
{
    // Placeholder for image detection logic
    HTTPResponse response = {"200", "OK", "Detection not implemented yet"};
    // Logic check to make sure 
        // the body is not empty
        // the content is an image format
    if (
        req.body.empty() || 
        req.headers.find("Content-Type") == req.headers.end() || 
        req.headers.at("Content-Type").find("image/") == std::string::npos
    )

    {
        response.code = "400";
        response.status = "Bad Request";
        response.message = "Request body is empty";
        return response;
    }


    // Directly read the image from the request body using OpenCV
    cv::Mat image = cv::imdecode(req.body, cv::IMREAD_COLOR);
    std::vector<Detection> result = detector.detect(image);

    std::cout << "Detection result size: " << result.size() << std::endl;
    response.message = detector.parseResults(result);
    return response;
}

int main()
{
    std::cout << "Server starting..." << std::endl;
    std::filesystem::create_directories("uploads");

    std::cout << "Initializing YOLO Detector..." << std::endl;
    YOLODetector detector("../models/yolov8n.onnx");

    Router router;
    router.addPath("GET","/",get_main);
    router.addPath("GET", "/test", get_main);

    // Lambda function to pass in the initialized YOLODetector instance to the detect function
    router.addPath("POST", "/detect", [&detector](HTTPRequest req) {
        return detect(detector, req);
    });

    router.addPath("POST", "/save_image", save_image);
    TCPServer server(9000, router, 8);
    server.start();

    return -1;
}