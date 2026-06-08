// TODO: Use the router to then return the desired response
#include <iostream>
#include <map>
#include <string>

#include "HTTPTypes.hpp"

#ifndef ROUTER_HPP
#define ROUTER_HPP
using Handler = std::function<HTTPResponse(HTTPRequest)>;

struct RouteKey
{
    std::string method;
    std::string path;

    bool operator<(const RouteKey& other) const
    {
        return std::tie(method, path)
             < std::tie(other.method, other.path);
    }
};


class Router
{
    private:
        // routes[i] returns function that handles the given route
        std::map<RouteKey, Handler> routes_; 
    public:
        void addPath(std::string method, std::string path, Handler function);
        HTTPResponse handleRequest(HTTPRequest req);
};

#endif
