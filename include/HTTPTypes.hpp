#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct HTTPRequest
{
    std::string method;
    std::string path;
    std::vector<std::uint8_t> body;
    std::unordered_map<std::string, std::string> headers;
};

struct HTTPResponse
{
    std::string code;
    std::string status;
    std::string message;
};