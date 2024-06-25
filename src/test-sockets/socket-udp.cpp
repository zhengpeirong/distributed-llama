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
#include "socket-udp.hpp"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 15


struct sockaddr_in server_addr, *client_addr;

static inline void initServerSockets(int port) {
    // Server starts to listen on the port
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);
    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
}
static inline void initClientSockets(int port) {
    // Clients start to listen on the port
    client_addr = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
    client_addr->sin_family = AF_INET;
    client_addr->sin_port = htons(port);
    client_addr->sin_addr.s_addr = inet_addr();
}


SocketPool* SocketPool::connect(unsigned int nSockets, char** hosts, int* ports) {
    // SocketPool::connect should be `init sockets`
    int* sockets = new int[nSockets];
    struct sockaddr_in addr;

    for (unsigned int i = 0; i < nSockets; i++) {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(hosts[i]);
        addr.sin_port = htons(ports[i]);

        int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (clientSocket < 0)
            throw std::runtime_error("Cannot create socket");
        sockets[i] = clientSocket;
    }
    return new SocketPool(nSockets, sockets);
}

static inline void writeSocket(int socket, const void* data, size_t size, const struct sockaddr_in* addr){
    while (size>0)
    {
        ssize_t s = sendto(socket, data, size, 0, (struct sockaddr*)addr, sizeof(*addr));
        if (s<0)
        {
            throw WriteSocketException(0, "Error writing to socket");
        {
            /* code */
        }
        
    }
    
}



static inline void setReuseAddr(int socket) {
    int opt = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(socket);
        throw std::runtime_error("setsockopt failed: " + std::string(strerror(errno)));
    }
}

static inline void writeSocket(int socket, const void* data, size_t size, const struct sockaddr_in* addr) {
    while (size > 0) {
        ssize_t s = sendto(socket, data, size, 0, (struct sockaddr*)addr, sizeof(*addr));
        if (s < 0) {
            if (isEagainError()) {
                continue;
            }
            throw WriteSocketException(0, "Error writing to socket");
        } else if (s == 0) {
            throw WriteSocketException(0, "Socket closed");
        }
        size -= s;
        data = (const char*)data + s;
    }
}
static inline bool tryReadSocket(int socket, void* data, size_t size, unsigned long maxAttempts, struct sockaddr_in* addr) {
    socklen_t addrlen = sizeof(*addr);
    size_t s = size;
    while (s > 0) {
        ssize_t r = recvfrom(socket, (char*)data, s, 0, (struct sockaddr*)addr, &addrlen);
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
            throw ReadSocketException(0, "Error reading from socket");
        } else if (r == 0) {
            throw ReadSocketException(0, "Socket closed");
        }
        data = (char*)data + r;
        s -= r;
    }
    return true;
}

static inline void readSocket(int socket, void* data, size_t size, struct sockaddr_in* addr) {
    if (!tryReadSocket(socket, data, size, 0, addr)) {
        throw std::runtime_error("Error reading from socket");
    }
}

ReadSocketException::ReadSocketException(int code, const char* message) {
    this->code = code;
    this->message = message;
}

WriteSocketException::WriteSocketException(int code, const char* message) {
    this->code = code;
    this->message = message;
}


SocketPool::SocketPool(unsigned int nSockets, int* sockets) {
    this->nSockets = nSockets;
    this->sockets = sockets;
    this->sentBytes.exchange(0);
    this->recvBytes.exchange(0);
}

SocketPool::~SocketPool() {
    for (unsigned int i = 0; i < nSockets; i++) {
        shutdown(sockets[i], 2);
        close(sockets[i]);
    }
    delete[] sockets;
}

void SocketPool::write(unsigned int socketIndex, const void* data, size_t size, const struct sockaddr_in* addr) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    sentBytes += size;
    writeSocket(sockets[socketIndex], data, size, addr);
}

void SocketPool::read(unsigned int socketIndex, void* data, size_t size, const struct sockaddr_in* addr) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    recvBytes += size;
    readSocket(sockets[socketIndex], data, size, addr);
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
                struct sockaddr_in addr = io->addr;
                ssize_t s = sendto(socket, io->data, io->size, 0, (struct sockaddr*)&addr, sizeof(addr));
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
                struct sockaddr_in addr;
                socklen_t addrlen = sizeof(addr);
                ssize_t r = recvfrom(socket, io->data, io->size, 0, (struct sockaddr*)&addr, &addrlen);
                if (r < 0) {
                    if (isEagainError()) {
                        continue;
                    }
                    throw ReadSocketException(SOCKET_LAST_ERRCODE, SOCKET_LAST_ERROR);
                } else if (r == 0) {
                    throw ReadSocketException(0, "Socket closed");
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

Socket::Socket(int socket) {
    this->socket = socket;
}

Socket::~Socket() {
    shutdown(socket, 2);
    close(socket);
}

void Socket::write(const void* data, size_t size) {
    writeSocket(socket, data, size);
}

void Socket::read(void* data, size_t size) {
    readSocket(socket, data, size);
}

bool Socket::tryRead(void* data, size_t size, unsigned long maxAttempts) {
    return tryReadSocket(socket, data, size, maxAttempts);
}

std::vector<char> Socket::readHttpRequest() {
        std::vector<char> httpRequest;
        char buffer[1024 * 1024]; // TODO: this should be refactored asap
        ssize_t bytesRead;
        
        // Peek into the socket buffer to check available data
        bytesRead = recv(socket, buffer, sizeof(buffer), MSG_PEEK);
        if (bytesRead <= 0) {
            // No data available or error occurred
            if (bytesRead == 0) {
                // No more data to read
                return httpRequest;
            } else {
                // Error while peeking
                throw std::runtime_error("Error while peeking into socket");
            }
        }
        
        // Resize buffer according to the amount of data available
        std::vector<char> peekBuffer(bytesRead);
        bytesRead = recv(socket, peekBuffer.data(), bytesRead, 0);
        if (bytesRead <= 0) {
            // Error while reading
            throw std::runtime_error("Error while reading from socket");
        }

        // Append data to httpRequest
        httpRequest.insert(httpRequest.end(), peekBuffer.begin(), peekBuffer.end());
        
        return httpRequest;
    }

SocketServer::SocketServer(int port) {
    const char* host = "0.0.0.0";
    struct sockaddr_in serverAddr;

    int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
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

    // No need to call listen for UDP
    printf("Listening on %s:%d...\n", host, port);
}

SocketServer::~SocketServer() {
    close(socket);
}
