#ifndef TCPSERVER_HPP
#define TCPSERVER_HPP

class TCPServer{
private:
    int port_;
    int sock_fd_;
    int setupSocket();
    int acceptClient();

public:
    TCPServer(int port);
    ~TCPServer();
    void start();
};

#endif