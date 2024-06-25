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
    return SOCKET_LAST_ERRCODE == EAGAIN || SOCKET_LAST_ERRCODE == EWOULDBLOCK;
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
        if (s < 0) {
            if (isEagainError()) {
                continue;
            }
            throw WriteSocketException(0, "Error writing to socket");
        }
        size -= s;
        data = (const char*)data + s;
    }
}

static inline bool readSocket(int socket, void* data, size_t size, unsigned long maxAttempts) {
    // `maxAttempts` is not used in this function 
    size_t s = size;
    struct sockaddr_in from; // Store the address of the sender
    socklen_t fromlen = sizeof(from);  //socklen_t is value/result 
    memset(&from, 0, sizeof(from));
    while (s > 0 ) {
        // int r = recvfrom(socket, (char*)data, s, 0, (struct sockaddr*)&from, &fromlen);
        // not interested in the sender's address
        int r = recvfrom(socket, (char*)data, s, 0, NULL, NULL);
        if (r < 0) {
            if (isEagainError()) {
                if (s == size && maxAttempts > 0) {
                    maxAttempts--;
                    if (maxAttempts == 0) {
                        return false;
                    }
                }
                continue;
            }
            throw ReadSocketException(errno, "Error reading from socket");
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

void initSockets() {
    return;
}

void cleanupSockets() {
    return;
}


std::unique_ptr<SocketPool> SocketPool::connect(unsigned int nSockets, char** hosts, int* ports) {
    // Create a socket pool containing n client sockets with the given hosts and ports
    std::unique_ptr<int[]> sockets(new int[nSockets]);
    std::unique_ptr<sockaddr_in[]> addrs(new sockaddr_in[nSockets]);
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
    return std::unique_ptr<SocketPool>(new SocketPool(nSockets, std::move(sockets), std::move(addrs)));
}

SocketPool::SocketPool(unsigned int nSockets, std::unique_ptr<int[]> sockets, std::unique_ptr<sockaddr_in[]> addrs)
    : nSockets(nSockets), sockets(std::move(sockets)), addrs(std::move(addrs)), sentBytes(0), recvBytes(0) {}


SocketPool::~SocketPool() {
    for (unsigned int i = 0; i < nSockets; i++) {
        shutdown(sockets[i], 2);
        close(sockets[i]);
    }
}


void SocketPool::write(unsigned int socketIndex, const void* data, size_t size) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    sentBytes += size;
    writeSocket(sockets[socketIndex], data, size, addrs[socketIndex]);
}

void SocketPool::read(unsigned int socketIndex, void* data, size_t size) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    recvBytes += size;//TODO: change `size` to the actual size of the received data
    bool loss = readSocket(sockets[socketIndex], data, size, 0);
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
                ssize_t s = sendto(socket, (const char*)io->data, io->size, 0, 
                                   (struct sockaddr*)&io->addr, sizeof(io->addr));

                if (s < 0) {
                    if (isEagainError()) {
                        continue;
                    }
                    throw WriteSocketException(SOCKET_LAST_ERRCODE, SOCKET_LAST_ERROR);
                } else if (s == 0) {
                    throw WriteSocketException(0, "Socket closed");
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

Socket SocketServer::accept() {return;
}

Socket::Socket(int socket) {
    this->socket = socket;
}

Socket::~Socket() {
    close(socket);
}

void Socket::setTurbo(bool enabled) {return;
}

void Socket::write(const void* data, size_t size, sockaddr_in addr) {
    writeSocket(socket, data, size, addr);
}

void Socket::read(void* data, size_t size) {
    readSocket(socket, data, size, 0);
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
