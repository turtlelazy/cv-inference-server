#include "Router.hpp"
#include "HTTPTypes.hpp"

#ifndef TCPSERVER_HPP
#define TCPSERVER_HPP


class TCPServer{

    private:
        int port_;
        int sock_fd_;
        int setupSocket();
        int acceptClient();
        int BUFFER_SIZE = 1024;
        Router router_;
        HTTPRequest parseHTTPRequest(const char* buffer);
        std::string parseResponseHTTP(HTTPResponse response);

    public:
        TCPServer(int port, Router router);
        ~TCPServer();
        void start();
};


#endif
