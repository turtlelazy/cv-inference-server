#pragma once

#include <string>

struct HTTPRequest
{
    std::string method;
    std::string path;
};

struct HTTPResponse
{
    std::string code;
    std::string status;
    std::string message;
};