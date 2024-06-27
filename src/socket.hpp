#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <atomic>
#include <cstddef>
#include <exception>
#include <vector>
#include <netinet/in.h>
#include <mutex>
#include <memory>


void initSockets();
void cleanupSockets();

class ReadSocketException : public std::exception {
public:
    int code;
    std::string message;

    ReadSocketException(int code, const std::string& message)
        : code(code), message(message) {}

    const char* what() const noexcept override {
        return message.c_str();
    }
};

class WriteSocketException : public std::exception {
public:
    int code;
    std::string message;

    WriteSocketException(int code, const std::string& message)
        : code(code), message(message) {}

    const char* what() const noexcept override {
        return message.c_str();
    }
};

struct SocketIo {
    unsigned int socketIndex;
    const void* data;
    size_t size;
    sockaddr_in* addr;
};

class SocketPool {
private:
    std::unique_ptr<int[]> sockets;
    std::unique_ptr<sockaddr_in[]> addrs; // Store addresses of the sockets
    std::atomic_uint sentBytes;
    std::atomic_uint recvBytes;
    std::mutex mtx;  // 保护共享资源

public:
    static SocketPool* connect(unsigned int nSockets, char** hosts, int* ports);
    // static SocketPool* connect(unsigned int nSockets, char** hosts, int* ports);

    unsigned int nSockets;

    SocketPool(unsigned int nSockets, std::unique_ptr<int[]> sockets, std::unique_ptr<sockaddr_in[]> addrs);
    ~SocketPool();

    void setTurbo();
    void write(unsigned int socketIndex, const void* data, size_t size);
    void read(unsigned int socketIndex, void* data, size_t size);
    void writeMany(unsigned int n, SocketIo* ios);
    void readMany(unsigned int n, SocketIo* ios);
    void getStats(size_t* sentBytes, size_t* recvBytes);
};

class Socket {
private:
    int socket;

public:
    Socket(int socket);
    ~Socket();

    void setTurbo(bool enabled);
    void write(const void* data, size_t size, sockaddr_in addr);
    void read(void* data, size_t size);
    bool tryRead(void* data, size_t size, unsigned long maxAttempts);
    // std::vector<char> readHttpRequest(){};
};

class SocketServer {
private:
    int socket;
    struct sockaddr_in addr;
public:
    SocketServer(int port);
    ~SocketServer();
    Socket accept();
};

#endif
