#ifndef NODE_HPP
#define NODE_HPP

#include <vector>
#include <string>
#include "socket.hpp"
#include "all_reduce.hpp"

enum class AllReduceType;

const int MAX_NODES = 32;
const int MAX_HOST_LEN = 256;

struct Package{
    AllReduceType type;
    int socketID;
    int inPort;
    int outPort;
    char outHost[256]; 
};

struct RingReduceTopology;


class Node {
public:
    AllReduceType allReduceType;
    int inPort;
    int outPort;
    Socket* inSocket;
    SocketPool* socketPool;
    std::vector<RingReduceTopology> topos;
    Socket* rootSocket;

    Node(AllReduceType type, SocketPool* pool, int nodeCount); // for root init
    Node(AllReduceType type, int in, int out, SocketPool* pool,Socket* socket,Socket* rootSocket); // for worker init
    // TODO:: ~Node();

    // *************************** methods for building topology
    void sendCommPackage(unsigned int nodeCount);
    void proceedIfAllSuccess(int nodeCount)
        //root统计成功信号与pool中节点数是否match
        //如果不match||超时 抛出异常
    ;

    // Method to initialize node connections based on RingReduceTopology
    static Node* initializeNodeAndConnections(Socket* socket)
        // worker等待从与root连接的socket 接收commPackage
        // 接收到之后根据commPackage.inPorts 创建socketPool
        // 然后根据commPackage.outPorts， 进行以下操作 
        // SocketServer server(args->port);
        // Socket socket = server.accept();
        // 直到所有Port成功
        // 由参数socket发送信号给root代表成功创建，
    ;

    // *************************** methods used during ring reduce
    void recv(void* data, size_t size);

    void send(const void* data, size_t size);

    void storeRecvBuffer(char* buffer) {
            std::lock_guard<std::mutex> lock(mutex_);
            recvBuffer_.reset(buffer);
        }

    const char* getRecvBuffer() {
        std::lock_guard<std::mutex> lock(mutex_);
        return recvBuffer_.get();
    }

    void clearRecvBuffer() {
        std::lock_guard<std::mutex> lock(mutex_);
        recvBuffer_.reset();
    }

private:
    std::mutex mutex_;
    std::unique_ptr<char[]> recvBuffer_;
};

SocketIo* preparePackageSizeIOs(unsigned int nodeCount, SocketIo* ios);

#endif // NODE_HPP