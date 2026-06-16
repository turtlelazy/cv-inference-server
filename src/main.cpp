#include <iostream>
#include "TCPServer.hpp"
#include "Router.hpp"
#include "HTTPTypes.hpp"


HTTPResponse get_main(HTTPRequest req){
    HTTPResponse response = {"200","OK",""};
    return response;
}

int main()
{
    std::cout << "Server starting...";
    Router router;
    router.addPath("GET","/",get_main);
    router.addPath("GET", "/test", get_main);
    TCPServer server(9000, router);
    server.start();

    return -1;
}