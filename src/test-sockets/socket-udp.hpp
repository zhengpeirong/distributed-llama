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

void initSockets();
void cleanupSockets();

class ReadSocketException : public std::exception {
public:
    int code;
    const char* message;
    ReadSocketException(int code, const char* message);
};

class WriteSocketException : public std::exception {
public:
    int code;
    const char* message;
    WriteSocketException(int code, const char* message);
};

struct SocketIo {
    unsigned int socketIndex;
    const void* data;
    size_t size;
    sockaddr_in addr;  // Added addr to specify destination address for UDP
};

class SocketPool {
private:
    int* sockets;
    std::atomic<size_t> sentBytes;
    std::atomic<size_t> recvBytes;

public:
    static SocketPool* connect(unsigned int nSockets, char** hosts, int* ports);

    unsigned int nSockets;

    SocketPool(unsigned int nSockets, int* sockets);
    ~SocketPool();

    void write(unsigned int socketIndex, const void* data, size_t size, const sockaddr_in* addr);
    void read(unsigned int socketIndex, void* data, size_t size, sockaddr_in* addr);
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

    void write(const void* data, size_t size, const sockaddr_in* addr);
    void read(void* data, size_t size, sockaddr_in* addr);
};

class SocketServer {
private:
    int socket;
public:
    SocketServer(int port);
    ~SocketServer();
};

#endif // SOCKET_UDP_HPP