#ifndef SOCKET_UDP_HPP
#define SOCKET_UDP_HPP

#include <atomic>
#include <cstddef>
#include <exception>
#include <vector>
#include <netinet/in.h>

// TODO:
// 1. UDP server 
    // 1. store every Client's address 
    // 2. multicast to every Client
    // 3. receive a single message from the Client simultaneouly
// 2. UDP Client
    // 1. store the server's address
    // 2. send a single message to the Server simultaneouly
    // 3. receive a single message from the Server
// 3. Interface:
    // 1. SocketPool
    // 2. readMany(), writeMany()
    // 3. read(), write()
    // 4. getStats(): to record the `sentBytes` and `recvBytes`

// void initSockets();
// void cleanupSockets();

namespace udp {
class ReadSocketException : public std::exception {
public:
    int code;
    const char* message;
    // TODO: change the definition
    ReadSocketException(int code, const char* message);
};

class WriteSocketException : public std::exception {
public:
    int code;
    const char* message;
    // TODO: change the definition
    WriteSocketException(int code, const char* message);
};

struct SocketIo {
    unsigned int socketIndex;
    const void* data;
    size_t size;
    sockaddr_in* addr;
};

class SocketPool {
private:
    int* sockets;
    struct sockaddr_in* addrs;  // Store addresses
    std::atomic_uint sentBytes;
    std::atomic_uint recvBytes;

public:
    // connect of `UDP` is actually initializing the SocketPool
    static SocketPool* connect(unsigned int nSockets, char** hosts, int* ports);
    unsigned int nSockets;

    SocketPool(unsigned int nSockets, int* sockets, struct sockaddr_in* addrs);
    ~SocketPool();

    // void setTurbo(bool enabled);
    void write(unsigned int socketIndex, const void* data, size_t size);
    void read(unsigned int socketIndex, void* data, size_t size);
    // broadcast the data to all the sockets
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

    // void setTurbo(bool enabled);
    void write(const void* data, size_t size);
    void read(void* data, size_t size);
    // bool tryRead(void* data, size_t size, unsigned long maxAttempts);
    // std::vector<char> readHttpRequest();
};

class SocketServer {
private:
    int socket;
public:
    SocketServer(int port);
    ~SocketServer();
    // Socket accept();
};

#endif // SOCKET_UDP_HPP
}