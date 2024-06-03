#include "node.hpp"
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <thread>
#include "all_reduce.hpp"
#include <string.h>
#include <future>

Node::Node(AllReduceType type, SocketPool* pool, int nodeCount)
    : allReduceType(type), socketPool(pool) {
    this->topos = RingReduce::constructTopology(nodeCount);
    
    std::cout << "Initializing Node with AllReduceType: " << static_cast<int>(type) << "\n";
    std::cout << "Constructed topologies for " << nodeCount << " nodes.\n";

    int prevSocketIndex = topos[0].prevNodeID - 2;
    int nextSocketIndex = topos[0].nextNodeID - 2;

    this->inPort = socketPool->getPort(nextSocketIndex);
    std::cout << "Added inPort: " << socketPool->getPort(nextSocketIndex) << "\n";
    this->outPort = socketPool->getPort(prevSocketIndex);
    std::cout << "Added outPort: " << socketPool->getPort(prevSocketIndex) << "\n";
}

Node::Node(AllReduceType type, int in, int out, SocketPool* pool,Socket* socket,Socket* rootSocket)
    : allReduceType(type),inPort(in),outPort(out), socketPool(pool),inSocket(socket),rootSocket(rootSocket) {}

void Node::sendCommPackage(unsigned int nodeCount) {
    SocketIo ios[nodeCount-1];
    std::vector<Package*> packages;

    for (unsigned int i = 1; i < nodeCount; ++i) { 
        Package* package = new Package();
        packages.push_back(package);  // store it, free later

        package->type = AllReduceType::RingReduce;
        
        package->socketID = socketPool->getPort(topos[i].nodeId-2);

        int inPortIndex = topos[i].nextNodeID-2;
        int outPortIndex = topos[i].prevNodeID-2;

        volatile bool isOutPortRoot = outPortIndex==-1;
        volatile bool isInPortRoot = inPortIndex==-1;
        // std::cout<<outPortIndex<<"<----<"<<package->socketID<<"<----<"<<inPortIndex<<std::endl;
        package->outPort = isOutPortRoot? -1 : socketPool->getPort(outPortIndex)-200;
        package->inPort = isInPortRoot? -1 : socketPool->getPort(inPortIndex)-200;

        std::cout<<package->outPort<<"<----<"<<package->socketID<<"<----<"<<package->inPort<<std::endl;
        strncpy(package->outHost, isOutPortRoot?"root":socketPool->getHost(outPortIndex).c_str(), sizeof(package->outHost) - 1);
        package->outHost[sizeof(package->outHost) - 1] = '\0';
        
        ios[i-1].socketIndex = topos[i].nodeId-2;
        ios[i-1].data = package;
        ios[i-1].size = sizeof(*package);
    }
    std::cout << "Sending communication package to " << nodeCount - 1 << " nodes.\n";
    socketPool->writeMany(nodeCount-1, ios);

    // release all package
    for (Package* package : packages) {
        std::cout<<package->inPort<<package->outPort<<std::endl;
        delete package;
    }
}

void Node::proceedIfAllSuccess(int nodeCount) {
    unsigned int successCount = 0;
    auto start = std::chrono::high_resolution_clock::now();

    std::cout << "Waiting for success signals from " << nodeCount << " worker nodes.\n";

    while (successCount < nodeCount) {
        bool success;
        socketPool->read(successCount, &success, sizeof(bool));
        if (success) {
            successCount++;
            std::cout << "Received success signal from node " << successCount << ".\n";
        }

        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = now - start;
        if (elapsed.count() > 3) {
            throw std::runtime_error("Timeout waiting for worker nodes to initialize.");
        }
    }

    std::cout << "All worker nodes successfully initialized.\n";
}

Node* Node::initializeNodeAndConnections(Socket* socket) {
    Package package;
    socket->read((char*)&package, sizeof(package));

    std::cout << "in: "<< package.inPort<<" out: "<<package.outPort<<"Received Package from root.\n";

    char* host_ptr = package.outHost;
    bool isOutNodeRoot = package.outPort==-1;
    bool isInNodeRoot = package.inPort==-1;

    // init socketPool for output
    auto futureSocketPool = isOutNodeRoot ? std::future<SocketPool*>() : std::async(std::launch::async, [&host_ptr, &package]() {
        return SocketPool::connect(1, &host_ptr, &package.outPort);
    });

    // get socket for input

    auto futureSocket = isInNodeRoot ? std::future<Socket>() : std::async(std::launch::async, [&package]() {
        SocketServer server(package.socketID-200);
        return server.accept();
    });

    // wait connection to finish
    printf("wait\n");
    SocketPool* socketPool = isOutNodeRoot ? nullptr : futureSocketPool.get();
    printf("built socket pool\n");
    Socket* inSocket = isInNodeRoot ? socket : new Socket(futureSocket.get());

    bool success = true;
    socket->write(&success, sizeof(bool));
    std::cout << "Sent success signal to root.\n";
    return (new Node(package.type, package.inPort, package.outPort, socketPool,inSocket,socket));
}