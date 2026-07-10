#include <iostream>
#include "TCPServer.hpp"
#include "Router.hpp"
#include "HTTPTypes.hpp"


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

    HTTPResponse response = {"200", "OK", "Image saved as " + filename};
    return response;
}

int main()
{
    std::cout << "Server starting...";
    std::filesystem::create_directories("uploads");

    Router router;
    router.addPath("GET","/",get_main);
    router.addPath("GET", "/test", get_main);
    router.addPath("POST", "/detect", get_main);
    router.addPath("POST", "/save_image", save_image);
    TCPServer server(9000, router);
    server.start();

    return -1;
}