#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <ctime>
#include <unistd.h>
#include "socket.hpp"
#include <stdexcept>
#include "network_utils.hpp"
#include "utils.hpp"

#define SOCKET_LAST_ERRCODE errno
#define SOCKET_LAST_ERROR strerror(errno)

#define AUTO_NON_BLOCKING_MODULO 10000
#define AUTO_NON_BLOCKING_TIMEOUT_SECONDS 3

static inline void setNonBlocking(int socket, bool enabled) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags = flags & (~O_NONBLOCK);
    }
    if (fcntl(socket, F_SETFL, flags) < 0)
        throw std::runtime_error("Error setting socket to non-blocking");
}

static inline void setNoDelay(int socket) {
    int flag = 1;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) < 0)
        throw std::runtime_error("Error setting socket to no-delay");
}

static inline void writeSocket(int socket, const void* data, size_t size) {
    while (size > 0) {
        int s = send_with_info(socket, (char*)data, size, 0);
        if (s < 0) {
            if (SOCKET_LAST_ERRCODE == EAGAIN) {
                continue;
            }
            throw WriteSocketException(SOCKET_LAST_ERRCODE, SOCKET_LAST_ERROR);
        } else if (s == 0) {
            throw WriteSocketException(0, "Socket closed");
        }
        size -= s;
        data = (char*)data + s; // move char type pointer of `data` to `s` bytes. This updates the position of sending `data`.
    }
}

/**
 * AUTO_NON_BLOCKING_MODULO: 这个常量用于确定在多少次接收失败后（由于数据尚未准备好，recv 函数返回 EAGAIN），函数应该检查是否超过了设定的时间限制。
 * AUTO_NON_BLOCKING_TIMEOUT_SECONDS: 如果自从第一次遇到 EAGAIN 错误后，已经过去了超过 AUTO_NON_BLOCKING_TIMEOUT_SECONDS 定义的秒数，那么代码会将套接字从非阻塞模式切换回阻塞模式。
 * 程序根据实际的网络条件动态调整套接字的行为。
 * 在非阻塞模式下，如果数据短时间内未到达，程序会尝试通过周期性检查来决定是否继续等待或是切换到阻塞模式，这样可以更有效地管理资源，同时避免无谓的CPU循环等待。
*/
static inline void readSocket(bool* isNonBlocking, int socket, void* data, size_t size) {
    unsigned int attempt = 0;
    time_t startTime;
    while (size > 0) {
        int r = recv(socket, (char*)data, size, 0);
        if (r < 0) {
            if (*isNonBlocking && SOCKET_LAST_ERRCODE == EAGAIN) {
                attempt++;
                if (attempt % AUTO_NON_BLOCKING_MODULO == 0) {
                    time_t now = time(NULL);
                    if (attempt == AUTO_NON_BLOCKING_MODULO) {
                        startTime = now;
                    } else if (now - startTime > AUTO_NON_BLOCKING_TIMEOUT_SECONDS) {
                        setNonBlocking(socket, false);
                        *isNonBlocking = false;
                    }
                }
                continue;
            }
            throw ReadSocketException(SOCKET_LAST_ERRCODE, SOCKET_LAST_ERROR);
        } else if (r == 0) {
            throw ReadSocketException(0, "Socket closed");
        }
        data = (char*)data + r;
        size -= r;

        if (!*isNonBlocking) {
            setNonBlocking(socket, true);
            *isNonBlocking = true;
        }
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

SocketPool* SocketPool::connect(unsigned int nSockets, char** hosts, int* ports) {
    int* sockets = new int[nSockets];
    struct sockaddr_in addr;

    for (unsigned int i = 0; i < nSockets; i++) {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(hosts[i]);
        addr.sin_port = htons(ports[i]);

        int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket < 0)
            throw std::runtime_error("Cannot create socket");

        int connectResult = ::connect(clientSocket, (struct sockaddr*)&addr, sizeof(addr));
        if (connectResult != 0) {
            printf("Cannot connect to %s:%d (%s)\n", hosts[i], ports[i], SOCKET_LAST_ERROR);
            throw std::runtime_error("Cannot connect");
        }

        setNoDelay(clientSocket);
        sockets[i] = clientSocket;
    }
    return new SocketPool(nSockets, sockets);
}

SocketPool::SocketPool(unsigned int nSockets, int* sockets) {
    this->nSockets = nSockets;
    this->sockets = sockets;
    this->isNonBlocking = new bool[nSockets]; // array of bool value
    this->sentBytes.exchange(0);//exchange() is an atomic function to set the `sentBytes` to 0
    this->recvBytes.exchange(0);
}

SocketPool::~SocketPool() {
    for (unsigned int i = 0; i < nSockets; i++) {
        shutdown(sockets[i], 2);
        close(sockets[i]);
    }
    delete[] sockets;
    delete[] isNonBlocking;
}

ssize_t recv_with_info(int fd, void *buf, size_t n, int flags) {
    // 开始计时
    unsigned long start = timeMs();
    // 接收数据
    ssize_t bytes_received = recv(fd, buf, n, flags);
    // 结束计时
    unsigned long stop = timeMs();
    // 计算持续时间
    unsigned long duration = stop - start;
    // 检查数据是否成功接收
    if (bytes_received != -1 and duration > 0) {
        // 计算传输速率，单位为 kB/s
        double rate = (bytes_received * 8.0 / 1000) / (duration);
        // 打印接收的数据量、持续时间和传输速率
        printf("Socket %d: Received %ld bytes in %ld milliseconds at %.2f Mb/s\n", fd, bytes_received, duration, rate);
    }
    return bytes_received;
}

ssize_t send_with_info(int fd, const void *buf, size_t n, int flags) {
    // 开始计时
    unsigned long start = timeMs();
    // 发送数据
    ssize_t bytes_sent = send(fd, buf, n, flags);
    // 结束计时
    unsigned long stop = timeMs();
    // 计算持续时间
    unsigned long duration = stop - start;
    // 检查数据是否成功发送
    if (bytes_sent != -1 and duration > 0) {
        double rate = (bytes_sent * 8.0 / 1000) / (duration);
        // 打印发送的数据量、持续时间和传输速率
        printf("Socket %d: Sent %ld bytes in %ld milliseconds at %.2f  Mb/s\n", fd, bytes_sent, duration, rate);
    }
    return bytes_sent;
}

void SocketPool::write(unsigned int socketIndex, const void* data, size_t size) {
    // Start timing
    // auto start = std::chrono::high_resolution_clock::now();
    assert(socketIndex >= 0 && socketIndex < nSockets);
    sentBytes += size;
    writeSocket(sockets[socketIndex], data, size);
    // Stop timing
    // auto stop = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    // double rate = size / 1024.0 / (duration.count() / 1000.0f);
    // printf("Sent %6ld kB in %lld milliseconds at %.2f kB/ms.\n", size / 1024, duration.count(), rate);
}

void SocketPool::read(unsigned int socketIndex, void* data, size_t size) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    recvBytes += size;
    readSocket(&isNonBlocking[socketIndex], sockets[socketIndex], data, size);
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
                ssize_t s = send_with_info(socket, io->data, io->size, 0);
                if (s < 0) {
                    if (SOCKET_LAST_ERRCODE == EAGAIN) {
                        printf("***Send resource temporarily unavailable; try again later.***");
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
        printf("Recv many %6ld kB", io->size / 1024);
    }
    do {
        isReading = false;
        for (unsigned int i = 0; i < n; i++) {
            SocketIo* io = &ios[i];
            if (io->size > 0) {
                isReading = true;
                int socket = sockets[io->socketIndex];
                ssize_t r = recv(socket, (char*)io->data, io->size, 0);
                if (r < 0) {
                    if (SOCKET_LAST_ERRCODE == EAGAIN) {
                        printf("***Recv resource temporarily unavailable; try again later.***");
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

Socket SocketServer::accept() {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);
    int clientSocket = ::accept(socket, (struct sockaddr*)&clientAddr, &clientAddrSize);
    if (clientSocket < 0)
        throw std::runtime_error("Error accepting connection");
    setNoDelay(clientSocket);
    printf("Client connected\n");
    return Socket(clientSocket);
}

Socket::Socket(int socket) {
    this->socket = socket;
    this->isNonBlocking = false;
}

Socket::~Socket() {
    shutdown(socket, 2);
    close(socket);
}

void Socket::write(const void* data, size_t size) {
    writeSocket(socket, data, size);
}

void Socket::read(void* data, size_t size) {
    readSocket(&isNonBlocking, socket, data, size);
}

SocketServer::SocketServer(int port) {
    const char* host = "0.0.0.0";
    struct sockaddr_in serverAddr;

    socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0)
        throw std::runtime_error("Cannot create socket");

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(host);

    int bindResult = bind(socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (bindResult < 0) {
        printf("Cannot bind %s:%d\n", host, port);
        throw std::runtime_error("Cannot bind port");
    }

    int listenResult = listen(socket, 1);
    if (listenResult != 0) {
        printf("Cannot listen %s:%d\n", host, port);
        throw std::runtime_error("Cannot listen port");
    }
    printf("Listening on %s:%d...\n", host, port);
}

SocketServer::~SocketServer() {
    shutdown(socket, 2);
    close(socket);
}
