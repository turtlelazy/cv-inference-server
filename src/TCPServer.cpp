// Using the following commands to implement the TCPServer
// socket()
// bind()
// listen()
// accept()
// recv()
// send()

#include <iostream>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>

#include <arpa/inet.h>
#include <unistd.h>

#include "TCPServer.hpp"

TCPServer::TCPServer(int port){

    port_ = port;
    start();
};

TCPServer::~TCPServer(){

}

int TCPServer::setupSocket(){
    

};

HTTPRequest TCPServer::parseHTTPRequest(const char* buffer){
    // printf("Parsing HTTP Request");
    HTTPRequest request;
    int method_index = -1;
    int route_index = -1;
    for(int i = 0; i < BUFFER_SIZE; i++){
        if (buffer[i] == ' '){
            if (method_index == -1){
                method_index = i;
            }
            else {
                route_index = i;
                break;
            }
        }
        
    }
    std::string method;
    std::string route;

    for(int i = 0; i < route_index;i++){
        if(i < method_index){
            method += buffer[i];
        }
        else{
            route += buffer[i];
        }
    }

    printf("Method: %s\n", method.c_str());
    printf("Route: %s\n", route.c_str());

    request.method = method;
    request.path = route;
    return request;
}


int TCPServer::acceptClient(){
    while (true)
    {
        sockaddr_storage client_addr{};
        socklen_t addr_size = sizeof(client_addr);

        int client_fd = accept(
            sock_fd_,
            (sockaddr *)&client_addr,
            &addr_size);

        if (client_fd == -1)
        {
            perror("accept");
            continue;
        }

        std::cout << "Client connected" << std::endl;

        char buffer[BUFFER_SIZE];

        int bytes_received =
            recv(
                client_fd,
                buffer,
                sizeof(buffer),
                0);

        if (bytes_received > 0)
        {
            printf("Received: %s\n", buffer);
            printf("Echoing back to client...\n");
            parseHTTPRequest(buffer);
            send(
                client_fd,
                buffer,
                bytes_received,
                0);
        }

        close(client_fd);
    }
};

void TCPServer::start(){
    // Load up address structs with getaddrinfo():
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo; // will point to the results

    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

    if ((status = getaddrinfo(NULL, std::to_string(port_).c_str(), &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "gai error: %s\n", gai_strerror(status));
        exit(1);
    }

    sock_fd_ = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

    bind(sock_fd_, servinfo->ai_addr, servinfo->ai_addrlen);
    listen(sock_fd_,1000);
    acceptClient();

}
