#ifndef SOCKET_UDP_HPP
#define SOCKET_UDP_HPP

#include <atomic>
#include <cstddef>
#include <exception>
#include <vector>
#include <netinet/in.h>
#include <mutex>
#include <memory>

namespace udp {

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
    static std::unique_ptr<SocketPool> connect(unsigned int nSockets, char** hosts, int* ports);
    unsigned int nSockets;

    SocketPool(unsigned int nSockets, std::unique_ptr<int[]> sockets, std::unique_ptr<sockaddr_in[]> addrs);
    ~SocketPool();

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

    void write(const void* data, size_t size, sockaddr_in addr);
    void read(void* data, size_t size);
};

class SocketServer {
private:
    int socket;
public:
    SocketServer(int port);
    ~SocketServer();
};

} // namespace udp

#endif // SOCKET_UDP_HPP