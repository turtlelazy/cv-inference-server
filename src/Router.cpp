#include <iostream>

#include "Router.hpp"

void Router::addPath(std::string method, std::string path, Handler function)
{
    RouteKey route = {method, path};
    routes_[route] = function;
    // printf("Route Request Added: %s\n", route.path.c_str());
    // printf("Registering route:\n");
    // printf("  Method: '%s'\n", method.c_str());
    // printf("  Path: '%s'\n", path.c_str());
};

HTTPResponse Router::handleRequest(HTTPRequest req){
    RouteKey route = {req.method, req.path};
    // printf("Incoming method: '%s'\n", req.method.c_str());
    // printf("Incoming path: '%s'\n", req.path.c_str());
    // printf("Route Request: %s\n", route.path.c_str());
    if (!routes_.contains(route)){
        return {"404", "Not Found", ""};
    }
    return routes_[route](req);
}
