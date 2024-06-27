#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <ctime>
#include <stdexcept>
#include <vector>
#include <string>
#include <iostream>
#include "socket.hpp"
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SOCKET_LAST_ERRCODE errno
#define SOCKET_LAST_ERROR strerror(errno)

static inline bool isEagainError() {
    printf("Error code: %d\n", SOCKET_LAST_ERRCODE);
    return (SOCKET_LAST_ERRCODE == EAGAIN) || (SOCKET_LAST_ERRCODE == EWOULDBLOCK) || (SOCKET_LAST_ERRCODE == EINTR);
}

static inline void setNonBlocking(int socket, bool enabled) {
}

static inline void setNoDelay(int socket) {
}

static inline void setQuickAck(int socket) {
}

static inline void setReuseAddr(int socket) {
    int opt = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(socket);
        throw std::runtime_error("setsockopt failed: " + std::string(strerror(errno)));
    }
}

static inline void writeSocket(int socket, const void* data, size_t size, struct sockaddr_in addr) {
    while (size > 0) {
        // int s = send(socket, (const char*)data, size, 0);
        int s = sendto(socket, (const char*)data, size, 0, \
        (struct sockaddr*)&addr, sizeof(addr));
        printf("send s: %d\n", s);
        if (s < 0) {
            if (isEagainError()) {
                continue;
            }
            else{
                throw WriteSocketException(errno, "Error writing to socket");
            }
        }
        size -= s;
        data = (const char*)data + s;
    }
    printf("--------Data sent--------.\n");
}

static inline bool tryReadSocket(int socket, void* data, size_t size, unsigned long maxAttempts=0,  struct sockaddr_in* from = nullptr) {
    size_t s = size;
    // struct sockaddr_in from; // Store the address of the sender
    socklen_t fromlen = from ? sizeof(*from) : 0;  //socklen_t is value/result 
    if (from) {
        memset(from, 0, sizeof(*from));
    }
    while (s > 0 ) {
        // depends on whether interested in the sender's address or not
        int r = recvfrom(socket, (char*)data, s, 0, (struct sockaddr*)from, from ? &fromlen : nullptr);
        if (r < 0) {
            if (isEagainError()) {
                if (s == size && maxAttempts > 0) {
                    maxAttempts--;
                    if (maxAttempts == 0) {
                        return false;
                    }
                }
                continue;
            }else{
                printf("Not Eagain Error.\n");
                throw ReadSocketException(errno, "Error reading from socket");
            }
        } else if (r == 0) {
            // If no data is received or the data size is not as expected, fill with zeros
            printf("No data received.\n");
            memset(data, 0, size);
            return false;
        } else if(r != size){
            printf(sizeof(r)+"Data size is not as expected.\n");
            memset(data, 0, size);
            return false;
        }
        data = (char*)data + r;
        s -= r;
    }
    return true;
}

static inline bool readSocket(int socket, void* data, size_t size) {
    bool success = tryReadSocket(socket, data, size, 0);
    if (!success) {
        throw std::runtime_error("Error reading from socket");
    }else{
        return success;
    }
}
void initSockets() {
}

void cleanupSockets() {
}


SocketPool* SocketPool::connect(unsigned int nSockets, char** hosts, int* ports) {
    // Create a socket pool containing n client sockets with the given hosts and ports
    int* sockets(new int[nSockets]);
    sockaddr_in* addrs(new sockaddr_in[nSockets]);
    for (unsigned int i = 0; i < nSockets; i++) {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockets[i] < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        memset(&addrs[i], 0, sizeof(addrs[i]));
        addrs[i].sin_family = AF_INET;
        addrs[i].sin_port = htons(ports[i]);
        if (inet_pton(AF_INET, hosts[i], &addrs[i].sin_addr) <= 0) {
            throw std::runtime_error("Invalid address");
        }
    }
    return new SocketPool(nSockets, sockets, addrs); 
}

SocketPool::SocketPool(unsigned int nSockets, int* sockets, sockaddr_in* addrs)
    : nSockets(nSockets), sockets(sockets), addrs(addrs), sentBytes(0), recvBytes(0) {}


SocketPool::~SocketPool() {
    for (unsigned int i = 0; i < nSockets; i++) {
        shutdown(sockets[i], 2);
        close(sockets[i]);
    }
}


void printWrite(unsigned int socketIndex, const void* data, size_t size, sockaddr_in* addrs){
    printf("Socket index: %d\n", socketIndex);
    printf("size: %ld\n", size);
    // 将sockaddr_in结构体转换为字符串
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addrs[socketIndex].sin_addr), ip, INET_ADDRSTRLEN);
    // 打印地址信息
    printf("addrs[socketIndex]: %s\n", ip);
    printf("port: %d\n", ntohs(addrs[socketIndex].sin_port));
    // 打印二进制数据
    const unsigned char* byteData = static_cast<const unsigned char*>(data);
    printf("Data: ");
    for (size_t i = 0; i < size; ++i) {
        printf("%02x ", byteData[i]);
    }
    printf("\n");
}
void printSend(int socket, const void *data, size_t size, int flags, const struct sockaddr *addr, socklen_t addrlen) {
    // 打印 socket 文件描述符
    printf("Socket: %d\n", socket);
    // 打印数据缓冲区,二进制数据
    const unsigned char* byteData = static_cast<const unsigned char*>(data);
    printf("Data: ");
    for (size_t i = 0; i < size; ++i) {
        printf("%02x ", byteData[i]);
    }
    printf("\n");
    // 打印数据大小
    printf("Size: %zu\n", size);
    // 打印 flags 参数
    printf("Flags: %d\n", flags);
    // 打印 sockaddr 结构体中的地址信息
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(addr_in->sin_addr), ip, INET_ADDRSTRLEN);
        printf("Address (IPv4): %s\n", ip);
        printf("Port: %d\n", ntohs(addr_in->sin_port));
    } else {
        printf("Address: Unknown family %d\n", addr->sa_family);
    }
    // 打印 sockaddr 结构体的长度
    printf("Address length: %u\n", addrlen);
}

void SocketPool::write(unsigned int socketIndex, const void* data, size_t size) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    sentBytes += size;
    printWrite(socketIndex, data, size, addrs);
    writeSocket(sockets[socketIndex], data, size, addrs[socketIndex]);
}

void SocketPool::read(unsigned int socketIndex, void* data, size_t size) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    recvBytes += size;//TODO: change `size` to the actual size of the received data
    bool notLoss = readSocket(sockets[socketIndex], data, size);
    // TODO: record the loss rate
}
void SocketPool::writeMany(unsigned int n, SocketIo* ios) {
    bool isWriting;
    for (unsigned int i = 0; i < n; i++) {
        SocketIo* io = &ios[i];
        assert(io->socketIndex >= 0 && io->socketIndex < nSockets);
        sentBytes += io->size;
    }
    do {
        isWriting = false;
        for (unsigned int i = 0; i < n; i++) {
            SocketIo* io = &ios[i];
            if (io->size > 0) {
                isWriting = true;
                int socket = sockets[io->socketIndex];
                printSend(socket, (const char*)io->data, io->size, 0, 
                                   (struct sockaddr*)&addrs[io->socketIndex], sizeof(addrs[io->socketIndex]));
                ssize_t s = sendto(socket, (const char*)io->data, io->size, 0, 
                                   (struct sockaddr*)&addrs[io->socketIndex], sizeof(addrs[io->socketIndex]));

                if (s < 0) {
                    if (isEagainError()) {
                        continue;
                    }else{
                        printf("Error writing to socket.\n");
                        throw WriteSocketException(SOCKET_LAST_ERRCODE, SOCKET_LAST_ERROR);
                    }
                } else if (s == 0) {
                    throw WriteSocketException(SOCKET_LAST_ERRCODE, "Socket closed");
                }
                io->size -= s;
                io->data = (char*)io->data + s;
            }
        }
    } while (isWriting);
}

void SocketPool::readMany(unsigned int n, SocketIo* ios) {
    bool isReading;
    for (unsigned int i = 0; i < n; i++) {
        SocketIo* io = &ios[i];
        assert(io->socketIndex >= 0 && io->socketIndex < nSockets);
        recvBytes += io->size;
    }
    do {
        isReading = false;
        for (unsigned int i = 0; i < n; i++) {
            SocketIo* io = &ios[i];
            if (io->size > 0) {
                isReading = true;
                int socket = sockets[io->socketIndex];
                socklen_t addrLen = sizeof(io->addr);
                ssize_t r = recvfrom(socket, (char*)io->data, io->size, 0,
                                     (struct sockaddr*)&io->addr, &addrLen);
                if (r < 0) {
                    if (isEagainError()) {
                        continue;
                    }
                    throw ReadSocketException(SOCKET_LAST_ERRCODE, SOCKET_LAST_ERROR);
                } else if (r == 0 || r != io->size) {
                    printf("No data received or the size is not true.\n");
                    memset((char*)r, 0, io->size);
                    // TODO: record the loss rate
                }
                io->size -= r;
                io->data = (char*)io->data + r;
            }
        }
    } while (isReading);
}

void SocketPool::getStats(size_t* sentBytes, size_t* recvBytes) {
    *sentBytes = this->sentBytes;
    *recvBytes = this->recvBytes;
    this->sentBytes.exchange(0);
    this->recvBytes.exchange(0);
}

Socket SocketServer::accept() {
    return Socket(socket);
}

Socket::Socket(int socket) {
    this->socket = socket;
    bool is_root_addr_initialized = false;
}

Socket::~Socket() {
    close(socket);
}

void Socket::setTurbo(bool enabled) {return;
}

void Socket::write(const void* data, size_t size) {
    writeSocket(socket, data, size, this->root_addr);
}

bool Socket::tryRead(void* data, size_t size, unsigned long maxAttempts=0) {
    if (!this->is_root_addr_initialized)
    {
        return tryReadSocket(socket, data, size, maxAttempts, &this->root_addr);
        this->is_root_addr_initialized = true;
    }else{
        return tryReadSocket(socket, data, size, maxAttempts, nullptr);
    }
}
void Socket::read(void* data, size_t size) {
    readSocket(socket, data, size);
}
SocketServer::SocketServer(int port) {
    const char* host = "0.0.0.0";
    struct sockaddr_in serverAddr;

    socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket < 0)
        throw std::runtime_error("Cannot create socket");
    setReuseAddr(socket);

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(host);

    int bindResult;
    bindResult = bind(socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (bindResult < 0) {
        close(socket);
        throw std::runtime_error("Cannot bind port: " + std::string(strerror(errno)));
    }

    printf("Listening on %s:%d...\n", host, port);
}

SocketServer::~SocketServer() {
    shutdown(socket, 2);
    close(socket);
}
