

#ifndef TCPSERVER_HPP
#define TCPSERVER_HPP
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

class TCPServer{

    private:
        int port_;
        int sock_fd_;
        int setupSocket();
        int acceptClient();
        int BUFFER_SIZE = 1024;
        HTTPRequest parseHTTPRequest(const char* buffer);
        std::string parseResponseHTTP(HTTPResponse response);

    public:
        TCPServer(int port);
        ~TCPServer();
        void start();
};


#endif
