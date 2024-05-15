#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <atomic>
#include <cstddef>
#include <exception>
/*ReadSocketException class, derived from the standard std::exception.
    Parameters:
       code - An integer error code representing the type of error.
       message - A constant character pointer to a string describing the error in detail.
    It's used to handle socket reading errors.
    */
class ReadSocketException : public std::exception {
public:
    // the error code
    int code;
    // a message providing more details about the error.
    const char* message;
    // Constructor. It initializes the exception with an error code and a descriptive message.
    ReadSocketException(int code, const char* message);
};
/*
Parameters:
    code - An integer error code representing the type of error.
    message - A constant character pointer to a string describing the error in detail.*/
class WriteSocketException : public std::exception {
public:
    int code;
    const char* message;
    WriteSocketException(int code, const char* message);
};
// To encapsulate information for socket input/output operations.
struct SocketIo {
    // index of an array or vector of sockets.
    unsigned int socketIndex;
    // Pointer to data that is intended for sending or has been received.
    // The 'const' qualifier indicates it's not modified.
    const void* data;
    // The size of the data in bytes. (send size | max amout of receivable size)
    size_t size;
};

/*
 It manages a pool of socket connections.
 It provides functionalities to connect to multiple sockets, perform read/write operations,
 and gather statistics on the amount of data sent and received.*/
class SocketPool {
private:
    // Pointer to an array of socket file descriptors. This array stores the handles for all sockets managed by the pool.
    int* sockets;
    // Pointer to an array indicating whether each socket is in non-blocking mode.
    bool* isNonBlocking;
    // Atomic variable to track the total number of bytes sent across all sockets.
    // Using atomic ensures that updates are thread-safe.
    std::atomic_uint sentBytes;
    // Atomic variable to track the total number of bytes received across all sockets.
    // Using atomic ensures that updates are thread-safe.
    std::atomic_uint recvBytes;

public:
    /**
     * 
    Static method to create and connect a new SocketPool instance.
     Parameters:
       `nSockets` - Number of sockets to be managed.
       `hosts` - Array of host addresses.
       `ports` - Array of ports, each corresponding to a host in the 'hosts' array.
     Returns:
       Pointer to the newly created SocketPool instance.
    */
    static SocketPool* connect(unsigned int nSockets, char** hosts, int* ports);

    // The number of sockets currently managed by the pool.
    unsigned int nSockets;

    /*Constructor for the SocketPool.
     Parameters:
       `nSockets` - Number of sockets in the pool.
       `sockets` - Array of socket descriptors.
    */ 
    SocketPool(unsigned int nSockets, int* sockets);
    // Destructor to clean up resources. It closes all sockets and free allocated memory.
    ~SocketPool();

    void write(unsigned int socketIndex, const void* data, size_t size);
    void read(unsigned int socketIndex, void* data, size_t size);
    void writeMany(unsigned int n, SocketIo* ios);
    void readMany(unsigned int n, SocketIo* ios);
    
    /* Retrieve the total bytes sent and received.
     Parameters:
       `sentBytes` - Pointer to a size_t variable where the total sent bytes will be stored.
       `recvBytes` - Pointer to a size_t variable where the total received bytes will be stored.*/
    void getStats(size_t* sentBytes, size_t* recvBytes);
};

class Socket {
private:
    int socket; //private; int; it stores the socket file descriptors(index)
    bool isNonBlocking; //private; it indicates whether to use NonBlocking mode

public:
    // Constructor that takes an integer socket descriptor as an argument. 
    // Default: `isNonBlocking` = false;
    Socket(int socket); 
    // Destructor for cleaning up resources through closing the socket.
    ~Socket();
    //Method for writing data to the socket.  `data` is a pointer to the data to be sent, and `size` is in bytes.
    void write(const void* data, size_t size); 
    //Method for reading data from the socket.  `data` is a pointer to a buffer where the received data will be stored, and `size` is in bytes.
    void read(void* data, size_t size);
};

class SocketServer {
private:
    int socket; //private; socket descriptor
public:
    // Constructor for the SocketServer class.
    // Takes an integer 'port' to specify on which port the server should listen for incoming connections
    SocketServer(int port);
    // Destructor for the SocketServer class
    // Responsible for cleaning up resources through closing the server socket
    ~SocketServer();
    // to accept a new connection from a client
    Socket accept();
};

#endif
