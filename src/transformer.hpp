#ifndef TRANSFORMER_HPP
#define TRANSFORMER_HPP

#include <cstddef>
#include <cstdint>
#include "quants.hpp"
#include "socket.hpp"

class MatmulSlice {
public:
    FloatType type;
    int nSlices;
    int d0;
    int n;
    size_t bytes;
    size_t sliceBytes;

    MatmulSlice(FloatType type, int nSlices, int n, int d);
    size_t splitWeights(uint8_t sliceIndex, char* weights, char* weights0);
    long mergeOutputs(uint8_t sliceIndex, float* output, float* output0);
};

struct TransformerFileHeader {
    int dim;
    int hiddenDim;
    int nLayers;
    int nHeads;
    int nKvHeads;
    int vocabSize;
    int seqLen;
};

struct TransformerSpec {
    size_t fileSize;
    int dim; // embedding dim
    int nLayers; // num of transfomer layers
    int nHeads; // num of Q heads
    int headSize; // embedding dim / num of heads
    int nKvHeads; // num of KV heads
    int seqLen; // kvcache长度
    int hiddenDim; // dim of FFN hidden layer
    int kvDim; // headSize * nKvHeads
    int vocabSize;

    FloatType weightsFloatType;
    FloatType bufferFloatType;
    uint8_t nSlices;
};

class TransformerBlock {
public:
    uint8_t sliceIndex;

    size_t rmsAttBytes;
    float* rmsAtt;
    size_t rmsFfnBytes;
    float* rmsFfn;

    char* q0;
    MatmulSlice* q0Slice;
    char* k0;
    MatmulSlice* k0Slice;
    char* v0;
    MatmulSlice* v0Slice;
    char* wo0;
    MatmulSlice* wo0Slice;
    char* w10;
    MatmulSlice* w10Slice;
    char* w20;
    MatmulSlice* w20Slice;
    char* w30;
    MatmulSlice* w30Slice;

    float* keyCache;
    float* valueCache;
    float* att;
    float* hb20;

    TransformerBlock(TransformerSpec* spec, uint8_t sliceIndex);
    ~TransformerBlock();
};

#define TB_LENGTH 12
#define TB_UNIT_XB 0
#define TB_UNIT_XB_QUANTIZED 1
#define TB_SLICED_XB2 2
#define TB_SLICED_XB2_QUANTIZED 3
#define TB_SLICED_Q 4
#define TB_SLICED_Q_QUANTIZED 5
#define TB_SLICED_K 6
#define TB_SLICED_K_QUANTIZED 7
#define TB_SLICED_V 8
#define TB_SLICED_V_QUANTIZED 9
#define TB_SLICED_HB 10
#define TB_SLICED_HB_QUANTIZED 11

class TransformerBuffer {
public:
    uint8_t nSlices;
    char** buffers;
    size_t* bufferBytes;

    TransformerBuffer(TransformerSpec* spec);
    ~TransformerBuffer();
    char* getUnit(uint8_t bufferIndex);
    size_t getUnitBytes(uint8_t bufferIndex);
    char* getSliced(uint8_t bufferIndex, uint8_t sliceIndex);
    size_t getSlicedBytes(uint8_t bufferIndex);
};

class Transformer {
public:
    TransformerSpec* spec;
    TransformerBlock** blocks;
    TransformerBuffer* buffer;
    uint8_t sliceIndex;
    // 主机专属的部分非attention内部的计算
    size_t tokenEmbeddingTableBytes;
    char* tokenEmbeddingTable;
    size_t rmsFinalBytes;
    char* rmsFinal;
    size_t wclsBytes;
    char* wcls;

    // rms的λ即scale
    float rms;
    // 当前token的position
    int pos;
    float* x;
    float* logits;

    // 释放Transformer buffer
    ~Transformer();

    static TransformerSpec loadSpecFromFile(const char* path, const unsigned int nSlices, FloatType weightsFloatType, FloatType bufferFloatType);
    static Transformer loadRootFromFile(const char* path, TransformerSpec* spec, SocketPool* socketPool);
    // 主机加载权重
    static Transformer loadRoot(char* data, TransformerSpec* spec, SocketPool* socketPool);
    // 从机加载权重
    static Transformer loadSlice(TransformerSpec* spec, Socket* socket);

private:
    // 开 Transformer buffer
    Transformer(TransformerSpec* spec, uint8_t sliceIndex);
};

#endif
