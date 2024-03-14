#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <atomic>
#include <cstddef>
#include <exception>

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

/*SocketIo结构体用于存储一个套接字连接的索引、数据缓冲区和数据长度。*/
struct SocketIo {
    unsigned int socketIndex;
    const char* data;
    size_t size;
};

/*SocketPool类用于管理多个套接字连接:

connect静态方法用于创建一个新的SocketPool实例,并连接到指定的主机和端口。
nSockets成员变量表示池中的套接字数量。
sentBytes和recvBytes分别跟踪已发送和已接收的字节数。
write和read方法用于向指定的套接字连接发送和接收数据。
writeMany和readMany方法分别用于向多个套接字连接批量发送和接收数据。
getStats方法用于获取已发送和已接收的字节数统计信息,并重置这些统计值。
enableTurbo方法用于启用加速机制: 非阻塞模式;禁用 Nagle 算法,即不延迟发送数据。*/
class SocketPool {
private:
    int* sockets;
    bool* isNonBlocking;
    std::atomic_uint sentBytes;
    std::atomic_uint recvBytes;

public:
    static SocketPool* connect(unsigned int nSockets, char** hosts, int* ports);

    unsigned int nSockets;

    SocketPool(unsigned int nSockets, int* sockets);
    ~SocketPool();

    void write(unsigned int socketIndex, const char* data, size_t size);
    void read(unsigned int socketIndex, char* data, size_t size);
    void writeMany(unsigned int n, SocketIo* ios);
    void readMany(unsigned int n, SocketIo* ios);
    void getStats(size_t* sentBytes, size_t* recvBytes);
};

/*Socket类表示单个套接字连接:

accept静态方法用于在指定端口上创建服务器套接字,并等待和接受传入的客户端连接。
write和read方法用于向该套接字连接发送和接收数据。
enableTurbo方法用于启用加速机制。*/
class Socket {
private:
    int socket;
    bool isNonBlocking;

public:
    Socket(int socket);
    ~Socket();

    void write(const char* data, size_t size);
    void read(char* data, size_t size);
};

class SocketServer {
private:
    int socket;
public:
    SocketServer(int port);
    ~SocketServer();
    Socket accept();
};

#endif
