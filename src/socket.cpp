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

#define SOCKET_LAST_ERRCODE errno
#define SOCKET_LAST_ERROR strerror(errno)

#define AUTO_NON_BLOCKING_MODULO 10000
#define AUTO_NON_BLOCKING_TIMEOUT_SECONDS 3

/*这个代码片段定义了一个函数 setNotBlocking，它用于将给定的套接字设置为非阻塞模式。

具体来说，这个函数执行以下操作:

调用 fcntl 函数获取套接字的当前文件状态标志。
将获取到的标志与 O_NONBLOCK 标志进行按位或运算,从而设置非阻塞标志。
调用 fcntl 函数将修改后的标志应用到套接字上。
如果在设置非阻塞标志时发生错误(返回值为 -1),该函数会打印一条错误消息,然后使用 exit(EXIT_FAILURE) 终止程序执行。

设置套接字为非阻塞模式是网络编程中一种常见的做法,它可以防止套接字操作(如接收或发送数据)被无限期阻塞,从而提高程序的响应能力。*/
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

/*这个代码片段定义了一个函数 setNoDelay，它用于禁用 Nagle 算法,即不延迟发送数据。

具体来说,这个函数执行以下操作:

创建一个整型变量 flag,并将其设置为 1。
调用 setsockopt 函数,将 TCP_NODELAY 选项应用到给定的套接字上。这个选项告诉内核不要使用 Nagle 算法来缓冲较小的数据包,而是立即发送。
在调用 setsockopt 时,传递了以下参数:
socket: 要设置选项的套接字描述符
IPPROTO_TCP: 表示要设置的是 TCP 层的选项
TCP_NODELAY: 表示要设置的选项是禁用 Nagle 算法
(char*)&flag: 选项值的指针,这里传递 flag 的地址
sizeof(int): 选项值的长度,这里为 int 类型的长度
禁用 Nagle 算法可以减少数据传输的延迟,因为它不会等待缓冲区填满才发送数据。但是,这可能会导致更多的小数据包被发送,从而增加带宽的使用。因此,在一些情况下(如实时应用程序或需要低延迟的应用程序),禁用 Nagle 算法是有益的,但在其他情况下,保留它可能会更加高效。*/
static inline void setNoDelay(int socket) {
    int flag = 1;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) < 0)
        throw std::runtime_error("Error setting socket to no-delay");
}

/*这个代码片段定义了一个函数 writeSocket，它用于向给定的套接字发送数据。

具体来说,这个函数执行以下操作:

使用一个 while 循环,重复发送数据,直到所有数据都被成功发送。
在每次循环中,调用 send 函数向套接字发送部分数据。
如果 send 函数返回负值,表示发生了错误:
如果错误代码是 EAGAIN,表示套接字发送缓冲区已满,暂时无法发送更多数据,函数继续循环。
对于其他错误代码,函数会打印错误信息,然后使用 exit(EXIT_FAILURE) 终止程序执行。
如果 send 函数返回 0,表示连接已经被对方关闭,函数会打印错误消息,然后使用 exit(EXIT_FAILURE) 终止程序执行。
如果 send 函数返回正值,表示成功发送了部分数据。函数会相应地更新 size 和 data 指针,准备发送剩余的数据。
这个函数的设计考虑了发送数据时可能会出现的各种情况,如发送缓冲区已满、连接被对方关闭等。它使用一个循环来确保所有数据都被成功发送,而不会丢失任何数据。

需要注意的是,这个函数假设 send 函数发送的字节数不会超过本次调用要求发送的字节数。*/
static inline void writeSocket(int socket, const char* data, size_t size) {
    while (size > 0) {
        int s = send(socket, (char*)data, size, 0);
        if (s < 0) {
            if (SOCKET_LAST_ERRCODE == EAGAIN) {
                continue;
            }
            throw WriteSocketException(SOCKET_LAST_ERRCODE, SOCKET_LAST_ERROR);
        } else if (s == 0) {
            throw WriteSocketException(0, "Socket closed");
        }
        size -= s;
        data = (char*)data + s;
    }
}

/*这个代码片段定义了一个函数 readSocket，它用于从给定的套接字接收数据。

具体来说,这个函数执行以下操作:

使用一个 while 循环,重复接收数据,直到接收到指定大小的数据。
在每次循环中,调用 recv 函数从套接字接收部分数据。
如果 recv 函数返回负值,表示发生了错误:
如果错误代码是 EAGAIN,表示套接字接收缓冲区当前没有数据可读,函数继续循环。
对于其他错误代码,函数会打印错误信息,然后使用 exit(EXIT_FAILURE) 终止程序执行。
如果 recv 函数返回 0,表示连接已经被对方关闭,函数会打印错误消息,然后使用 exit(EXIT_FAILURE) 终止程序执行。
如果 recv 函数返回正值,表示成功接收了部分数据。函数会相应地更新 data 指针和 size 变量,准备接收剩余的数据。
这个函数的设计考虑了接收数据时可能会出现的各种情况,如接收缓冲区暂时没有数据、连接被对方关闭等。它使用一个循环来确保接收到指定大小的数据,而不会丢失任何数据。*/
static inline void readSocket(bool* isNonBlocking, int socket, char* data, size_t size) {
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

/*这段代码定义了 SocketPool 类中的 connect 方法。该方法的目的是创建一个套接字池,并连接到多个主机和端口。

方法执行以下操作:

接收三个参数:

nSockets: 一个无符号整数,表示要创建的套接字数量
hosts: 一个字符串数组,包含要连接的主机名或IP地址
ports: 一个整数数组,包含相应的端口号
为存储套接字文件描述符的整数数组(sockets)分配内存。

进入一个循环,重复执行 nSockets 次。

在循环内部,对于每个主机和端口组合:

使用 inet_addr 将主机字符串转换为IP地址,并使用 htons 将端口号转换为网络字节序,初始化一个 sockaddr_in 结构体。
使用 socket 函数创建一个新的套接字,指定 AF_INET(IPv4)地址族和 SOCK_STREAM(TCP)类型。
调用 connect 函数尝试将套接字连接到指定的主机和端口。
如果套接字创建或连接失败,打印错误消息并使用 exit(EXIT_FAILURE) 终止程序。
如果连接成功,将套接字文件描述符存储在 sockets 数组中。
循环成功完成后,该方法使用套接字数量和套接字文件描述符数组创建一个新的 SocketPool 对象,并返回指向该对象的指针。

这个方法可能用于在客户端应用程序中建立多个网络连接。SocketPool 类可能提供了其他方法来与已连接的套接字交互,例如发送和接收数据。*/
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

/*这段代码是 SocketPool 类的构造函数,用于初始化一个 SocketPool 对象。

具体来说,它执行以下操作:

接收两个参数:

nSockets: 一个无符号整数,表示该池中包含的套接字数量。
sockets: 一个整数数组,包含这些套接字的文件描述符。
将 nSockets 的值赋给当前对象的 nSockets 成员变量。

将 sockets 数组的指针赋给当前对象的 sockets 成员变量。这样,SocketPool 对象就获得了对这些套接字的引用。

使用 std::atomic<uint64_t>::exchange(0) 将 sentBytes 和 recvBytes 成员变量初始化为 0。这两个变量很可能是原子类型的 64 位无符号整数,用于跟踪通过该套接字池发送和接收的总字节数。

该构造函数的作用是根据提供的套接字数量和文件描述符初始化一个 SocketPool 对象。初始化后,该对象将持有这些套接字的引用,并可以通过其他成员函数进行进一步操作,例如发送和接收数据。*/
SocketPool::SocketPool(unsigned int nSockets, int* sockets) {
    this->nSockets = nSockets;
    this->sockets = sockets;
    this->isNonBlocking = new bool[nSockets];
    this->sentBytes.exchange(0);
    this->recvBytes.exchange(0);
}
/*这段代码定义了 SocketPool 类的析构函数。析构函数在 SocketPool 对象被销毁时自动调用,用于执行必要的清理操作。

具体来说,析构函数执行以下操作:

遍历 SocketPool 对象中的所有套接字文件描述符。

对于每个套接字文件描述符,调用 shutdown 函数,将其标记为已关闭。

shutdown 函数的第一个参数是要关闭的套接字文件描述符。
第二个参数是一个标志,值为 2(在某些系统上也可能是 SHUT_RDWR)表示同时关闭读取和写入操作。
循环结束后,析构函数退出,不执行任何其他操作。

需要注意的是,这个析构函数只是关闭了套接字,并没有释放由 SocketPool 对象持有的套接字文件描述符数组的内存。这是因为在 SocketPool 的构造函数中,它只是获得了指向该数组的指针,而没有显式分配内存。因此,释放该数组的内存的责任应该由创建 SocketPool 对象的代码来承担。*/
SocketPool::~SocketPool() {
    for (unsigned int i = 0; i < nSockets; i++) {
        shutdown(sockets[i], 2);
        close(sockets[i]);
    }

    delete[] sockets;
    delete[] isNonBlocking;
}

/*这段代码定义了 SocketPool 类的 write 方法,它用于向池中指定索引的套接字发送数据。

具体来说,这个方法执行以下操作:

首先使用 assert 语句检查 socketIndex 参数是否在有效范围内,即大于等于 0 且小于 nSockets。如果断言失败,程序将终止并输出错误信息。

将要发送的数据大小 size 累加到 sentBytes 成员变量中。sentBytes 很可能是一个原子类型的 64 位无符号整数,用于跟踪通过该套接字池总共发送了多少字节的数据。

调用 writeSocket 函数,向索引为 socketIndex 的套接字发送指定的数据。writeSocket 函数的实现在之前已经展示过,它会循环调用 send 函数,直到所有数据都被成功发送。

这个 write 方法提供了一种向特定套接字发送数据的接口,同时还跟踪了总共发送的字节数。通过使用索引来指定要发送数据的套接字,它实现了对多个套接字的管理。*/
void SocketPool::write(unsigned int socketIndex, const char* data, size_t size) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    sentBytes += size;
    writeSocket(sockets[socketIndex], data, size);
}

/*这段代码定义了 SocketPool 类的 read 方法,它用于从池中指定索引的套接字接收数据。

具体来说,这个方法执行以下操作:

首先使用 assert 语句检查 socketIndex 参数是否在有效范围内,即大于等于 0 且小于 nSockets。如果断言失败,程序将终止并输出错误信息。

将要接收的数据大小 size 累加到 recvBytes 成员变量中。recvBytes 很可能是一个原子类型的 64 位无符号整数,用于跟踪通过该套接字池总共接收了多少字节的数据。

调用 readSocket 函数,从索引为 socketIndex 的套接字接收指定大小的数据到 data 缓冲区中。readSocket 函数的实现在之前已经展示过,它会循环调用 recv 函数,直到接收到指定大小的数据。

这个 read 方法提供了一种从特定套接字接收数据的接口,同时还跟踪了总共接收的字节数。通过使用索引来指定要接收数据的套接字,它实现了对多个套接字的管理。*/
void SocketPool::read(unsigned int socketIndex, char* data, size_t size) {
    assert(socketIndex >= 0 && socketIndex < nSockets);
    recvBytes += size;
    readSocket(&isNonBlocking[socketIndex], sockets[socketIndex], data, size);
}

/*这段代码定义了 SocketPool 类的 writeMany 方法,它用于向多个套接字发送数据。该方法采用了一种批量写入的方式,可以在一次调用中同时向多个套接字发送数据,从而提高了效率。

具体来说,该方法执行以下操作:

接收两个参数:

n: 一个无符号整数,表示要写入的 SocketIo 结构体的数量。
ios: 一个指向 SocketIo 结构体数组的指针,每个结构体包含了要写入的套接字索引、数据缓冲区和数据大小。
使用 assert 语句检查每个 SocketIo 结构体中的 socketIndex 是否在有效范围内。

将每个 SocketIo 结构体中的数据大小 size 累加到 sentBytes 成员变量中。

进入一个 do-while 循环,循环条件由 isWriting 标志控制。

在循环内部,遍历所有 SocketIo 结构体:

如果当前结构体中还有剩余数据需要发送,则将 isWriting 标志设置为 true。
获取当前结构体对应的套接字文件描述符。
调用 send 函数向该套接字发送数据。
根据 send 函数的返回值进行错误处理或更新已发送的数据。
如果发生 EAGAIN 错误,则继续处理下一个 SocketIo 结构体。
如果发生其他错误或连接被关闭,则打印错误消息并终止程序执行。
循环结束后,如果 isWriting 标志为 true,则继续进行下一次循环,直到所有数据都被成功发送。

这个 writeMany 方法实现了批量写入的功能,可以在一次调用中向多个套接字发送数据。它利用了非阻塞模式下的写操作,如果发生 EAGAIN 错误,它会继续处理下一个 SocketIo 结构体,而不是阻塞等待。这种方式可以提高写操作的效率和响应性。*/
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
                ssize_t s = send(socket, io->data, io->size, 0);
                if (s < 0) {
                    if (SOCKET_LAST_ERRCODE == EAGAIN) {
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

/*这段代码定义了 SocketPool 类的 readMany 方法,它用于从多个套接字接收数据。该方法采用了一种批量读取的方式,可以在一次调用中同时从多个套接字接收数据,从而提高了效率。

具体来说,该方法执行以下操作:

接收两个参数:

n: 一个无符号整数,表示要读取的 SocketIo 结构体的数量。
ios: 一个指向 SocketIo 结构体数组的指针,每个结构体包含了要读取的套接字索引、数据缓冲区和期望读取的数据大小。
使用 assert 语句检查每个 SocketIo 结构体中的 socketIndex 是否在有效范围内。

将每个 SocketIo 结构体中的期望读取的数据大小 size 累加到 recvBytes 成员变量中。

进入一个 do-while 循环,循环条件由 isReading 标志控制。

在循环内部,遍历所有 SocketIo 结构体:

如果当前结构体中还有剩余数据需要读取,则将 isReading 标志设置为 true。
获取当前结构体对应的套接字文件描述符。
调用 recv 函数从该套接字接收数据。
根据 recv 函数的返回值进行错误处理或更新已读取的数据。
如果发生 EAGAIN 错误,则继续处理下一个 SocketIo 结构体。
如果发生其他错误或连接被关闭,则打印错误消息并终止程序执行。
循环结束后,如果 isReading 标志为 true,则继续进行下一次循环,直到所有期望的数据都被成功读取。

这个 readMany 方法实现了批量读取的功能,可以在一次调用中从多个套接字接收数据。它利用了非阻塞模式下的读操作,如果发生 EAGAIN 错误,它会继续处理下一个 SocketIo 结构体,而不是阻塞等待。这种方式可以提高读操作的效率和响应性。*/
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
                ssize_t r = recv(socket, (char*)io->data, io->size, 0);
                if (r < 0) {
                    if (SOCKET_LAST_ERRCODE == EAGAIN) {
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

/*这段代码定义了 SocketPool 类的 getStats 方法,它用于获取套接字池中已发送和已接收的字节数统计信息,并重置这些统计值。*/
void SocketPool::getStats(size_t* sentBytes, size_t* recvBytes) {
    *sentBytes = this->sentBytes;
    *recvBytes = this->recvBytes;
    this->sentBytes.exchange(0);
    this->recvBytes.exchange(0);
}


/*这段代码定义了 Socket 类的一个静态方法 accept。它用于在指定的端口上创建一个服务器套接字并等待传入的客户端连接。该方法执行以下操作:
初始化服务器套接字、服务器地址结构和客户端地址结构的局部变量。
使用 socket 系统调用创建一个新的 TCP 套接字。
设置服务器地址结构,使用指定的端口和通配符 IP 地址 (0.0.0.0) 监听所有可用的网络接口。
使用 bind 系统调用将服务器套接字绑定到指定的地址和端口上。
使用 listen 系统调用将服务器套接字置于监听状态,最多允许 1 个待处理连接排队。
打印一条消息,表明服务器正在监听指定的主机和端口。
使用 accept 系统调用接受一个传入的连接,并返回一个新的用于与客户端通信的套接字。
如果 accept 调用成功,打印一条消息表示有客户端已连接。
使用 shutdown 系统调用关闭服务器套接字,因为它不再需要了。
创建一个新的 Socket 对象,封装已接受的客户端套接字,并返回该对象。
这个方法通常在服务器应用程序中使用,用于创建监听套接字并等待传入的客户端连接。一旦有客户端连接,该方法就会返回一个新的 Socket 对象,可以用于与客户端进行通信。*/
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

void Socket::write(const char* data, size_t size) {
    writeSocket(socket, data, size);
}

void Socket::read(char* data, size_t size) {
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
