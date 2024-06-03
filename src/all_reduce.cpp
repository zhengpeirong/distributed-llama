#include "all_reduce.hpp"
#include<vector>
#include <iostream>

void AllReduceStrategy::reduceScatter(AllReduceType type, unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex) {
    switch (type) {
        case AllReduceType::RootReduce:
            // Call the original reduceScatter implementation
            // syncUnitBuffer(nThreads, threadIndex, ctx, bufferIndex);
            break;
        case AllReduceType::RingReduce:
            RingReduce::reduceScatter(nThreads, threadIndex, ctx, bufferIndex);
            break;
        // More cases can be added here to handle other AllReduceType
    }
}

void AllReduceStrategy::allGather(AllReduceType type, unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex) {
    switch (type) {
        case AllReduceType::RootReduce:
            // Call the original allGather implementation
            // syncSliceOfSlicedBuffer(nThreads, threadIndex, ctx, bufferIndex);
            break;
        case AllReduceType::RingReduce:
            RingReduce::allGather(nThreads, threadIndex, ctx, bufferIndex);
            break;
        // More cases can be added here to handle other AllReduceType
    }
}

// std::vector<Topology> AllReduceStrategy::constructTopology(AllReduceType type, unsigned int nodeCount) {
//             return RingReduce::constructTopology(nodeCount);
// }

void RingReduce::reduceScatter(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex) {
    // Implement the reduceScatter method of the ring reduce algorithm
}

void RingReduce::allGather(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex) {
    // Implement the allGather method of the ring reduce algorithm
}

std::vector<RingReduceTopology> RingReduce::constructTopology(unsigned int nodeCount) {
    std::vector<RingReduceTopology> topologies(nodeCount);
    for (unsigned int i = 0; i < nodeCount; ++i) {
        topologies[i].nodeId = i+1;
        topologies[i].nextNodeID= (i-1 + nodeCount) % nodeCount+1;
        topologies[i].prevNodeID = (i + 1) % nodeCount+1;
        std::cout<<topologies[i].prevNodeID<<"<-----"<<topologies[i].nodeId<<"<-----"<<topologies[i].nextNodeID<<std::endl;
    }
    return topologies;
}