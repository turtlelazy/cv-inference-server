// Using the following commands to implement the TCPServer
// socket()
// bind()
// listen()
// accept()
// recv()
// send()

#include <iostream>
#include <cstring>
#include <sstream>

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>

#include <arpa/inet.h>
#include <unistd.h>

#include "HTTPTypes.hpp"
#include "TCPServer.hpp"

bool DEBUG = true;

TCPServer::TCPServer(int port, Router router, int thread_count)
    : thread_pool_(thread_count),
      router_(router),
      port_(port)
{
    start();
}

TCPServer::~TCPServer(){
    if (sock_fd_ >= 0)
    {
        close(sock_fd_);
    }
}

std::string TCPServer::parseResponseHTTP(HTTPResponse response){

    std::string response_str = std::format(
        "HTTP/1.1 {} {}\r\nContent-Type: text/plain\r\n",
        response.code, response.status
    );
    response_str += std::format("Content-Length: {}\r\n\r\n{}", response.message.size(), response.message);
    return response_str;

}

HTTPRequest TCPServer::parseHTTPRequest(int client_fd)
{
    HTTPRequest request;
    std::string header;

    char buffer[BUFFER_SIZE];
    int bytes_received = recv(
        client_fd,
        buffer,
        sizeof(buffer),
        0
    );

    if (bytes_received < 0)
    {
        perror("recv");
        throw std::runtime_error("Failed to receive request header");
    }

    // Data chunk processing for in-memory processing

    // Extracts the header including \r\n\r\n
    size_t i = 0;
    while (!check_last_four(header))
    {
        // Loop in case header is larger than buffer size
        if (i >= bytes_received)
        {
            bytes_received = recv(
                client_fd,
                buffer,
                sizeof(buffer),
                0);

            if (bytes_received <= 0)
            {
                throw std::runtime_error("Failed to receive request header");
            }

            i = 0;
        }

        header.append(1, buffer[i]);
        i++;
    }
    // Reached end of header; parsing header

    size_t pos = header.find("Content-Length:");
    request.headers["Content-Length"] = "0"; // Default to 0 if not found
    std::istringstream stream(header);
    std::string line;
    std::getline(stream, line);

    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }

    std::istringstream request_line(line);

    request_line >> request.method >> request.path;

    // Parse headers
    while (std::getline(stream, line))
    {
        if (line == "\r" || line.empty())
        {
            break;
        }

        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        size_t colon = line.find(':');

        if (colon == std::string::npos)
        {
            continue;
        }

        std::string key =
            line.substr(0, colon);

        std::string value =
            line.substr(colon + 1);

        while (!value.empty() && value.front() == ' ')
        {
            value.erase(value.begin());
        }

        request.headers[key] = value;
    }

    int content_length = std::stoi(request.headers["Content-Length"]);
    bool has_chunked_transfer = header.find("Transfer-Encoding: chunked") != std::string::npos;
    if (has_chunked_transfer)
    {
        std::cout << "Chunked transfer encoding not supported" << std::endl;
        // Throw error
        throw std::runtime_error("Chunked transfer encoding not supported");
    }

    // Receive body chunks if necessary
    int body_bytes_read = 0;
    while (body_bytes_read < content_length)
    {
        if (i >= bytes_received)
        {
            bytes_received = recv(
                client_fd,
                buffer,
                sizeof(buffer),
                0);

            if (bytes_received <= 0)
            {
                throw std::runtime_error("Failed to receive request body");
            }

            i = 0;
        }

        if (bytes_received <= 0)
        {
            perror("recv");
            break;
        }

        request.body.push_back(buffer[i]);
        i++;
        body_bytes_read++;
    }

    std::cout << "Parsed Request: " << request.method << " " << request.path << std::endl;
    std::cout << "Headers: " << std::endl;
    for (const auto& [key, value] : request.headers)
    {
        std::cout << key << ": " << value << std::endl;
    }
    // std::cout << "Body: " << std::string(request.body.begin(), request.body.end()) << std::endl;

    return request;
}

void TCPServer::acceptRequest(int client_fd){
    std::cout << "Started Request;" << std::endl;
    // std::this_thread::sleep_for(std::chrono::seconds(5)); // Test line

    // Request parsing variables
    HTTPRequest request = parseHTTPRequest(client_fd);

    HTTPResponse response = router_.handleRequest(request);

    std::string response_str = parseResponseHTTP(response);
    const char *response_cstr = response_str.c_str();

    printf("Response: %s\n", response_cstr);
    send(
        client_fd,
        response_cstr,
        response_str.size(),
        0
    );
    std::cout << "Finished Request;" << std::endl;
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

        std::cout << "Client connected;" << std::endl;
        // Lambda expression queueing the response operation
        thread_pool_.enqueue(
            ([this, client_fd] () {
                this->acceptRequest(client_fd);
                close(client_fd);
                std::cout << "Closed client;" << std::endl;
            })
        );

        std::cout << "Client queued;" << std::endl;

    }
    return -1;
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

bool TCPServer::check_last_four(const std::string &head)
{
    return head.size() >= 4 && (head[head.size() - 1] == '\n' && head[head.size() - 3] == '\n' && head[head.size() - 2] == '\r' && head[head.size() - 4] == '\r');
};