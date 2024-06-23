#ifndef ALL_REDUCE_HPP
#define ALL_REDUCE_HPP

#include "transformer.hpp"
#include "tasks.hpp"
#include "node.hpp"

// Define different All Reduce types
// OriginalReduce is used to be compatible with the original strategy (syncUnitBuffer, syncSliceOfSlicedBuffer)
// RingReduce is used to adopt the ring reduce algorithm, and other algorithms can be added later
enum class AllReduceType {
    RootReduce,
    RingReduce,
};

struct RingReduceTopology
{
    int nodeId;
    int nextNodeID;
    int prevNodeID;
};

struct Topology;
// Forward declaration of TransformerContext struct
struct TransformerContext;

// The AllReduceStrategy class serves as the base class for all All Reduce algorithms
// By defining the reduceScatter and allGather static methods interface, it facilitates the extension of other All Reduce algorithms
class AllReduceStrategy {
public:
    static void reduceScatter(AllReduceType type, unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex);
    static void allGather(AllReduceType type, unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex);
    // static std::vector<Topology> constructTopology(AllReduceType type, unsigned int nodeCount); // return an array of topologies

};

// The RingReduce class implements the AllReduceStrategy interface
// This class specifically implements the reduceScatter and allGather methods of the ring reduce algorithm
class RingReduce {
public:
    static void reduceScatter(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex);
    static void allGather(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex);
    static std::vector<RingReduceTopology> constructTopology(unsigned int nodeCount);

};




#endif // ALL_REDUCE_HPP