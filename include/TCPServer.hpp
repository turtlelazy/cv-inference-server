#include "Router.hpp"
#include "HTTPTypes.hpp"
#include "ThreadPool.hpp"

#ifndef TCPSERVER_HPP
#define TCPSERVER_HPP


class TCPServer{

    private:
        int port_;
        int sock_fd_;
        int setupSocket();
        int acceptClient();
        void acceptRequest(int client_fd);
        int BUFFER_SIZE = 1024;

        Router router_;
        ThreadPool thread_pool_;

        HTTPRequest parseHTTPRequest(const char* buffer);
        std::string parseResponseHTTP(HTTPResponse response);

    public:
        TCPServer(int port, Router router, int thread_count = 20);
        ~TCPServer();
        void start();
};


#endif
