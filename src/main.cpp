#include <iostream>
#include "TCPServer.hpp"
int main()
{
    std::cout << "Server starting...";
    TCPServer server(9000);
    server.start();

    return -1;
}