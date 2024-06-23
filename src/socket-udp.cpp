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


ReadSocketException::ReadSocketException(int code, const char* message) {
    this->code = code;
    this->message = message;
}

WriteSocketException::WriteSocketException(int code, const char* message) {
    this->code = code;
    this->message = message;
}

SocketPool* SocketPool::connect(unsigned int nSockets, char** hosts, int* ports) {
    // Create a socket pool containing n client sockets with the given hosts and ports
    int* sockets = new int[nSockets];
    struct sockaddr_in* addrs = new sockaddr_in[nSockets];

    for (unsigned int i = 0; i < nSockets; i++) {
        memset(&addrs[i], 0, sizeof(addrs[i]));
        addrs[i].sin_family = AF_INET;
        if (inet_pton(AF_INET, hosts[i], &addrs[i].sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            delete[] sockets;
            delete[] addrs;
            return nullptr;
        }
        addrs[i].sin_port = htons(ports[i]);
        // Create a socket
        int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (clientSocket == -1) {
            perror("Failed to create socket");
            delete[] sockets;
            delete[] addrs;
            return nullptr;
        }
        sockets[i] = clientSocket;

        // Enable broadcast on the sockets
        int broadcastEnable = 1;
        if (setsockopt(sockets[i], SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
            perror("Error enabling broadcast");
            throw std::runtime_error("Error enabling broadcast");
        }
    }
    return new SocketPool(nSockets, sockets, addrs);
}


SocketPool::SocketPool(unsigned int nSockets, int* sockets, sockaddr_in* addrs) {
    this->nSockets = nSockets;
    this->sockets = sockets;
    this->addrs = addrs;
    this->sentBytes.exchange(0);
    this->recvBytes.exchange(0);
}

SocketPool::~SocketPool() {
    for (unsigned int i = 0; i < nSockets; i++) {
        close(sockets[i]);
    }
    delete[] sockets;
    delete[] addrs;
}

void SocketPool::write(unsigned int socketIndex, const void* data, size_t size) {
    assert(socketIndex < nSockets);

    size_t totalSent = 0;
    while (totalSent < size) {
        ssize_t sent = sendto(sockets[socketIndex], (const char*)data + totalSent, size - totalSent, 0, 
                              (struct sockaddr*)&addrs[socketIndex], sizeof(addrs[socketIndex]));
        if (sent == -1) {
            throw WriteSocketException(errno, strerror(errno));
        }
        totalSent += sent;
    }

    sentBytes += totalSent;
}
void SocketPool::read(unsigned int socketIndex, void* data, size_t size) {
    assert(socketIndex < nSockets);
    recvBytes += size;

    size_t totalReceived = 0;
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    // TODO: set a timeout for the recvfrom call
    // Set socket to non-blocking mode
    int flags = fcntl(sockets[socketIndex], F_GETFL, 0);
    fcntl(sockets[socketIndex], F_SETFL, flags | O_NONBLOCK);

    while (totalReceived < size) {
        ssize_t received = recvfrom(sockets[socketIndex], (char*)data + totalReceived, size - totalReceived, 0, 
                                    (struct sockaddr*)&from, &fromlen);
        if (received < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // No data available, sleep for a short period and try again
                usleep(1000); // Sleep for 1 millisecond
                continue;
            } else {
                throw ReadSocketException(errno, "Error reading from socket");
            }
        } else if (received == 0) {
            throw ReadSocketException(0, "Socket closed");
        }
        totalReceived += received;
    }

    // Restore socket to blocking mode
    fcntl(sockets[socketIndex], F_SETFL, flags);
}


// void SocketPool::writeMany(unsigned int n, SocketIo* ios) {
//     for (unsigned int i = 0; i < n; i++) {
//         unsigned int socketIndex = ios[i].socketIndex;
//         assert(socketIndex < nSockets);
        
//         void* data = const_cast<void*>(ios[i].data);
//         size_t size = ios[i].size;
        
//         size_t totalSent = 0;

//         // 设置套接字为非阻塞模式
//         int flags = fcntl(sockets[socketIndex], F_GETFL, 0);
//         fcntl(sockets[socketIndex], F_SETFL, flags | O_NONBLOCK);

//         while (totalSent < size) {
//             ssize_t sent = send(sockets[socketIndex], const_cast<char*>(static_cast<const char*>(data)) + totalSent, size - totalSent, 0);
//             if (sent < 0) {
//                 if (errno == EWOULDBLOCK || errno == EAGAIN) {
//                     // 套接字缓冲区已满，短暂休眠后重试
//                     usleep(1000); // 休眠1毫秒
//                     continue;
//                 } else {
//                     // 发生了其他错误
//                     throw WriteSocketException(errno, "Error writing to socket");
//                 }
//             }
//             totalSent += sent;
//         }

//         // 恢复套接字为阻塞模式
//         fcntl(sockets[socketIndex], F_SETFL, flags);
//     }
// }
void SocketPool::writeMany(unsigned int n, SocketIo* ios) {
    // Set up the broadcast address
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST); // Broadcast address
    broadcast_addr.sin_port = htons(addrs[0].sin_port); // Use the common port

    for (unsigned int i = 0; i < n; i++) {
        unsigned int socketIndex = ios[i].socketIndex;
        assert(socketIndex < nSockets);

        void* data = const_cast<void*>(ios[i].data);
        size_t size = ios[i].size;

        size_t totalSent = 0;

        while (totalSent < size) {
            ssize_t sent = sendto(sockets[socketIndex], static_cast<const char*>(data) + totalSent, size - totalSent, 0,
                                  (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
            if (sent < 0) {
                throw WriteSocketException(errno, strerror(errno));
            }
            totalSent += sent;
        }

        sentBytes += totalSent;
    }
}

void SocketPool::readMany(unsigned int n, SocketIo* ios) {
    for (unsigned int i = 0; i < n; i++) {
        unsigned int socketIndex = ios[i].socketIndex;
        assert(socketIndex < nSockets);

        void* data = const_cast<void*>(ios[i].data);
        size_t size = ios[i].size;
        recvBytes += size;

        size_t totalReceived = 0;
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);

        // 设置套接字为非阻塞模式
        int flags = fcntl(sockets[socketIndex], F_GETFL, 0);
        fcntl(sockets[socketIndex], F_SETFL, flags | O_NONBLOCK);

        while (totalReceived < size) {
            ssize_t received = recvfrom(sockets[socketIndex], (char*)data + totalReceived, size - totalReceived, 0, 
                                        (struct sockaddr*)&from, &fromlen);
            if (received < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    // 没有数据可读，短暂休眠后重试
                    usleep(1000); // 休眠1毫秒
                    continue;
                } else {
                    // 发生了其他错误
                    throw ReadSocketException(errno, "Error reading from socket");
                }
            } else if (received == 0) {
                throw ReadSocketException(0, "Socket closed");
            }
            totalReceived += received;
        }

        // 恢复套接字为阻塞模式
        fcntl(sockets[socketIndex], F_SETFL, flags);
    }
}

void SocketPool::getStats(size_t* sentBytes, size_t* recvBytes) {
    *sentBytes = this->sentBytes;
    *recvBytes = this->recvBytes;
}

SocketServer::~SocketServer() {
    close(socket);
}
