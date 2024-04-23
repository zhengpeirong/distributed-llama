#include <cmath>
#include <cassert>
#include <string.h>
#include "utils.hpp"
#include "funcs.hpp"
#include "socket.hpp"
#include "transformer-tasks.hpp"
#include <stdio.h>
static unsigned long long total_time_above_workers = 0;

#define TASK_ARGS unsigned int nThreads, unsigned int threadIndex, void* userData

#define TASK_VARIABLES \
    TransformerContext* ctx = (TransformerContext*)userData; \
    Transformer* transformer = ctx->transformer; \
    TransformerBlock* block = transformer->blocks[ctx->currentBlockIndex]; \
    TransformerSpec* spec = transformer->spec;

// scatter
void syncUnitBuffer(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex) {
    char* buffer = ctx->transformer->buffer->getUnit(bufferIndex);
    size_t bufferBytes = ctx->transformer->buffer->getUnitBytes(bufferIndex);

    if (ctx->socketPool != NULL) {
        // root

        unsigned int nSockets = ctx->socketPool->nSockets / nThreads + (ctx->socketPool->nSockets % nThreads > threadIndex ? 1 : 0);
        SocketIo ios[nSockets];
        for (int i = 0; i < nSockets; i++) {
            ios[i].socketIndex = threadIndex + i * nThreads;
            ios[i].data = buffer;
            ios[i].size = bufferBytes;
        }
        ctx->socketPool->writeMany(nSockets, ios);
    } else if (ctx->socket != NULL) {
        if (threadIndex != 0) return;

        // worker
        ctx->socket->read(buffer, bufferBytes);
    }
}

// gather
void syncSliceOfSlicedBuffer(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex) {
    size_t bufferBytes = ctx->transformer->buffer->getSlicedBytes(bufferIndex);
    if (ctx->socketPool != NULL) {
        // root

        unsigned int nSockets = ctx->socketPool->nSockets / nThreads + (ctx->socketPool->nSockets % nThreads > threadIndex ? 1 : 0);
        SocketIo ios[nSockets];
        for (int i = 0; i < nSockets; i++) {
            int socketIndex = threadIndex + i * nThreads;
            uint8_t workerSliceIndex = socketIndex + 1;
            ios[i].socketIndex = socketIndex;
            ios[i].data = ctx->transformer->buffer->getSliced(bufferIndex, workerSliceIndex);
            ios[i].size = bufferBytes;
        }

        ctx->socketPool->readMany(nSockets, ios);
    } else if (ctx->socket != NULL) {
        if (threadIndex != 0) return;

        // worker
        char* buffer = ctx->transformer->buffer->getSliced(bufferIndex, ctx->transformer->sliceIndex);
        ctx->socket->write(buffer, bufferBytes);
    }
}

// broadcast
void syncMissingSlicesOfSlicedBuffer(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t bufferIndex) {
    size_t sliceBytes = ctx->transformer->buffer->getSlicedBytes(bufferIndex);
    if (ctx->socketPool != NULL) {
        // root

        unsigned int nSockets = ctx->socketPool->nSockets / nThreads + (ctx->socketPool->nSockets % nThreads > threadIndex ? 1 : 0);
        SocketIo ios[nSockets];

        for (uint8_t si = 0; si < ctx->transformer->spec->nSlices - 1; si++) {
            for (unsigned int i = 0; i < nSockets; i++) {
                int socketIndex = threadIndex + i * nThreads;
                uint8_t workerSliceIndex = socketIndex + 1;
                uint8_t sliceIndex = si < workerSliceIndex ? si : si + 1;
                ios[i].socketIndex = socketIndex;
                ios[i].data = ctx->transformer->buffer->getSliced(bufferIndex, sliceIndex);
                ios[i].size = sliceBytes;
            }
            ctx->socketPool->writeMany(nSockets, ios);
        }
    } else if (ctx->socket != NULL) {
        if (threadIndex != 0) return;

        // worker
        for (uint8_t sliceIndex = 0; sliceIndex < ctx->transformer->spec->nSlices; sliceIndex++) {
            if (sliceIndex != ctx->transformer->sliceIndex) {
                char* buffer = ctx->transformer->buffer->getSliced(bufferIndex, sliceIndex);
                ctx->socket->read(buffer, sliceBytes);
            }
        }
    }
}

void quantizeUnitBuffer(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, uint8_t sourceBufferIndex, uint8_t targetBufferIndex) {
    if (ctx->transformer->spec->bufferFloatType == F32) return;
    assert(ctx->transformer->spec->bufferFloatType == Q80);

    quantizeQ80Row(
        (float*)ctx->transformer->buffer->getUnit(sourceBufferIndex),
        (BlockQ80*)ctx->transformer->buffer->getUnit(targetBufferIndex),
        ctx->transformer->buffer->getUnitBytes(sourceBufferIndex) / sizeof(float),
        nThreads,
        threadIndex);
}

void quantizeSlicedBuffer(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, bool quantizeRootSlice, uint8_t sourceBufferIndex, uint8_t targetBufferIndex) {
    if (ctx->transformer->spec->bufferFloatType == F32) return;
    if (ctx->transformer->sliceIndex == 0 && !quantizeRootSlice) return;
    assert(ctx->transformer->spec->bufferFloatType == Q80);

    quantizeQ80Row(
        (float*)ctx->transformer->buffer->getSliced(sourceBufferIndex, ctx->transformer->sliceIndex),
        (BlockQ80*)ctx->transformer->buffer->getSliced(targetBufferIndex, ctx->transformer->sliceIndex),
        ctx->transformer->buffer->getSlicedBytes(sourceBufferIndex) / sizeof(float),
        nThreads,
        threadIndex);
}

void dequantizeSlicedBuffer(unsigned int nThreads, unsigned int threadIndex, TransformerContext* ctx, bool dequantizeRootSlice, uint8_t sourceBufferIndex, uint8_t targetBufferIndex) {
    if (ctx->transformer->spec->bufferFloatType == F32) return;
    assert(ctx->transformer->spec->bufferFloatType == Q80);
    assert(ctx->socketPool != NULL); // This function may be called only by root.

    unsigned int sliceIndex = dequantizeRootSlice ? 0 : 1;
    for (; sliceIndex < ctx->transformer->spec->nSlices; sliceIndex++) {
        dequantizeQ80Row(
            (BlockQ80*)ctx->transformer->buffer->getSliced(sourceBufferIndex, sliceIndex),
            (float*)ctx->transformer->buffer->getSliced(targetBufferIndex, sliceIndex),
            (ctx->transformer->buffer->getSlicedBytes(sourceBufferIndex) / sizeof(BlockQ80)) * QK80,
            nThreads,
            threadIndex);
    }
}

//

// 单机单线程reduction求rms
int rmsAtt(TASK_ARGS) {
    TASK_VARIABLES;
    if (threadIndex == 0) {
        transformer->rms = rms(transformer->x, spec->dim);
    }
    return TASK_CONTINUE;
}
// rmsNorm
int rmsAttNorm(TASK_ARGS) {
    TASK_VARIABLES;
    float* xb = (float*)transformer->buffer->getUnit(TB_UNIT_XB);
    rmsnorm(xb, transformer->x, transformer->rms, block->rmsAtt, spec->dim, nThreads, threadIndex);
    return TASK_CONTINUE;
}
// 量化RMSNorm的结果
int quantizeRmsAtt(TASK_ARGS) {
    TASK_VARIABLES;
    quantizeUnitBuffer(nThreads, threadIndex, ctx, TB_UNIT_XB, TB_UNIT_XB_QUANTIZED);
    return TASK_CONTINUE;
}
// 将RMSNorm结果分发
int syncRmsAtt(TASK_ARGS) {
    TASK_VARIABLES;
    syncUnitBuffer(nThreads, threadIndex, ctx, TB_UNIT_XB_QUANTIZED);
    return TASK_CONTINUE;
}
// 分布式多线程算Q、K、V
int qkv(TASK_ARGS) {
    TASK_VARIABLES;

    float *xbq = (float*)transformer->buffer->getUnit(TB_UNIT_XB_QUANTIZED);
    float *q0 = (float*)transformer->buffer->getSliced(TB_SLICED_Q, transformer->sliceIndex);
    float *k0 = (float*)transformer->buffer->getSliced(TB_SLICED_K, transformer->sliceIndex);
    float *v0 = (float*)transformer->buffer->getSliced(TB_SLICED_V, transformer->sliceIndex);

    matmul(spec->weightsFloatType, spec->bufferFloatType, q0, xbq, block->q0, block->q0Slice->n, block->q0Slice->d0, nThreads, threadIndex);
    matmul(spec->weightsFloatType, spec->bufferFloatType, k0, xbq, block->k0, block->k0Slice->n, block->k0Slice->d0, nThreads, threadIndex);
    matmul(spec->weightsFloatType, spec->bufferFloatType, v0, xbq, block->v0, block->v0Slice->n, block->v0Slice->d0, nThreads, threadIndex);
    return TASK_CONTINUE;
}
// 分布式多线程量化
int quantizeQkv(TASK_ARGS) {
    TASK_VARIABLES;
    quantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_Q, TB_SLICED_Q_QUANTIZED);
    quantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_K, TB_SLICED_K_QUANTIZED);
    quantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_V, TB_SLICED_V_QUANTIZED);
    return TASK_CONTINUE;
}
// gather QKV
int syncQkv(TASK_ARGS) {
    TASK_VARIABLES;
    syncSliceOfSlicedBuffer(nThreads, threadIndex, ctx, TB_SLICED_Q_QUANTIZED);
    syncSliceOfSlicedBuffer(nThreads, threadIndex, ctx, TB_SLICED_K_QUANTIZED);
    syncSliceOfSlicedBuffer(nThreads, threadIndex, ctx, TB_SLICED_V_QUANTIZED);
    // if (ctx->socketPool != NULL && threadIndex == 0) { float* v = (float*)block->q0; printf("q0 (%d): %f %f %f %f %f %f\n", ctx->currentBlockIndex, v[0], v[1], v[2], v[3], v[4], v[5]); }
    return TASK_CONTINUE;
}
// 反量化 QKV
int dequantizeQkv(TASK_ARGS) {
    TASK_VARIABLES;
    dequantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_Q_QUANTIZED, TB_SLICED_Q);
    dequantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_K_QUANTIZED, TB_SLICED_K);
    dequantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_V_QUANTIZED, TB_SLICED_V);
    return TASK_CONTINUE;
}
// 单线程单机多头注意力
int multiheadAtt(TASK_ARGS) {
    TASK_VARIABLES;
    if (threadIndex != 0) {
        return TASK_CONTINUE;
    }
    // TODO: 计时
    unsigned long startTime = timeMs();    // 增加计时函数

    int dim = spec->dim;
    int kvDim = spec->kvDim;
    int kvMul = spec->nHeads / spec->nKvHeads; // integer multiplier of the kv sharing in multiquery
    int hiddenDim =  spec->hiddenDim;
    int headSize = dim / spec->nHeads;
    int pos = transformer->pos;
    // 获取q
    float* q = (float*)transformer->buffer->getUnit(TB_SLICED_Q);
    // 获取输入buffer
    float* xb = (float*)transformer->buffer->getUnit(TB_UNIT_XB);
    // 保存kvCache
    float* k = block->keyCache + pos * kvDim;
    float* v = block->valueCache + pos * kvDim;

    memcpy(k, transformer->buffer->getUnit(TB_SLICED_K), dim * sizeof(float));
    memcpy(v, transformer->buffer->getUnit(TB_SLICED_V), dim * sizeof(float));

    // RoPE relative positional encoding: complex-valued rotate q and k in each head
    for (int i = 0; i < dim; i+=2) {
        int head_dim = i % headSize;
        float freq = 1.0f / powf(10000.0f, head_dim / (float)headSize);
        float val = pos * freq;
        float fcr = cosf(val);
        float fci = sinf(val);
        int rotn = i < kvDim ? 2 : 1; // how many vectors? 2 = q & k, 1 = q only
        for (int _v = 0; _v < rotn; _v++) {
            float* vec = _v == 0 ? q : k; // the vector to rotate (query or key)
            float v0 = vec[i];
            float v1 = vec[i+1];
            vec[i]   = v0 * fcr - v1 * fci;
            vec[i+1] = v0 * fci + v1 * fcr;
        }
    }

    // multihead attention. iterate over all heads
    int h;
    for (h = 0; h < spec->nHeads; h++) {
        // get the query vector for this head
        float* _q = q + h * headSize;
        // attention scores for this head
        float* _att = block->att + h * spec->seqLen; // attention = softmax(Q dot K)
        // iterate over all timesteps, including the current one
        for (int t = 0; t <= pos; t++) {
            // get the key vector for this head and at this timestep
            float* k = block->keyCache + t * kvDim + (h / kvMul) * headSize;
            // calculate the attention score as the dot product of q and k
            float score = dotProduct(_q, k, headSize) / sqrtf(headSize);
            // 除以根号dhead用于平滑所以叫做scaled 点积注意力机制
            _att[t] = score;
        }

        // softmax the scores to get attention weights, from 0..pos inclusively
        softmax(_att, pos + 1);

        // weighted sum of the values, store back into xb
        float* _xb = xb + h * headSize; // xb = (attention dot V)
        memset(_xb, 0, headSize * sizeof(float));
        for (int t = 0; t <= pos; t++) {
            // get the value vector for this head and at this timestep
            float* _v = block->valueCache + t * kvDim + (h / kvMul) * headSize;
            // get the attention weight for this timestep
            float a = _att[t];
            // accumulate the weighted value into xb
            for (int i = 0; i < headSize; i++) {
                _xb[i] += a * _v[i];
            }
        }
    }

    unsigned long executionTime = timeMs() - startTime; 
    // printf("🔶 multiheadAtt %4ld ms", executionTime);
    total_time_above_workers += executionTime; // 累加运行时间
    // printf("🔶Total multiheadAtt %4ld ms", total_time_above_workers);
    return TASK_CONTINUE;
}

int quantizeMultiheadAtt(TASK_ARGS) {
    TASK_VARIABLES;
    quantizeUnitBuffer(nThreads, threadIndex, ctx, TB_UNIT_XB, TB_UNIT_XB_QUANTIZED);
    return TASK_CONTINUE;
}

int syncMultiheadAtt(TASK_ARGS) {
    TASK_VARIABLES;
    syncUnitBuffer(nThreads, threadIndex, ctx, TB_UNIT_XB_QUANTIZED);
    return TASK_CONTINUE;
}

// 输出的投影
int att(TASK_ARGS) {
    TASK_VARIABLES;

    char* xb = transformer->buffer->getUnit(TB_UNIT_XB_QUANTIZED);
    float* xb2 = (float*)transformer->buffer->getSliced(TB_SLICED_XB2, transformer->sliceIndex);

    matmul(spec->weightsFloatType, spec->bufferFloatType, xb2, xb, block->wo0, block->wo0Slice->n, block->wo0Slice->d0, nThreads, threadIndex);

    return TASK_CONTINUE;
}

int quantizeAtt(TASK_ARGS) {
    TASK_VARIABLES;
    quantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_XB2, TB_SLICED_XB2_QUANTIZED);
    return TASK_CONTINUE;
}

int syncAtt(TASK_ARGS) {
    TASK_VARIABLES;
    syncSliceOfSlicedBuffer(nThreads, threadIndex, ctx, TB_SLICED_XB2_QUANTIZED);
    return TASK_CONTINUE;
}

int dequantizeAtt(TASK_ARGS) {
    TASK_VARIABLES;
    dequantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_XB2_QUANTIZED, TB_SLICED_XB2);    
    return TASK_CONTINUE;
}

int rmfFfn(TASK_ARGS) {
    TASK_VARIABLES;

    if (threadIndex == 0) {


        float* xb2 = (float*)transformer->buffer->getUnit(TB_SLICED_XB2);
        float* xb = (float*)transformer->buffer->getUnit(TB_UNIT_XB);
        float* x = (float*)transformer->x;
        // shortcut
        for (int i = 0; i < spec->dim; i++) {
            x[i] += xb2[i];
        }
        transformer->rms = rms(x, spec->dim);
    }
    return TASK_CONTINUE;
}

int rmfFfnNorm(TASK_ARGS) {
    TASK_VARIABLES;
    float* xb = (float*)transformer->buffer->getUnit(TB_UNIT_XB);
    float* x = (float*)transformer->x;

    rmsnorm(xb, x, transformer->rms, block->rmsFfn, spec->dim, nThreads, threadIndex);
    return TASK_CONTINUE;
}

int quantizeRmfFfn(TASK_ARGS) {
    TASK_VARIABLES;
    quantizeUnitBuffer(nThreads, threadIndex, ctx, TB_UNIT_XB, TB_UNIT_XB_QUANTIZED);
    return TASK_CONTINUE;
}

int syncRmfFfn(TASK_ARGS) {
    TASK_VARIABLES;
    syncUnitBuffer(nThreads, threadIndex, ctx, TB_UNIT_XB_QUANTIZED);
    return TASK_CONTINUE;
}

// 单隐层FFN feed-forword-network
// 输入->隐藏层->输出
// 有两个权重
// FFN算隐层
int ffn(TASK_ARGS) {
    TASK_VARIABLES;

    float* xb = (float*)transformer->buffer->getUnit(TB_UNIT_XB_QUANTIZED);
    float* hb0 = (float*)transformer->buffer->getSliced(TB_SLICED_HB, transformer->sliceIndex);

    matmul(spec->weightsFloatType, spec->bufferFloatType, hb0, xb, block->w10, block->w10Slice->n, block->w10Slice->d0, nThreads, threadIndex);
    matmul(spec->weightsFloatType, spec->bufferFloatType, block->hb20, xb, block->w30, block->w30Slice->n, block->w30Slice->d0, nThreads, threadIndex);

    // SwiGLU non-linearity
    int d00 = block->w10Slice->d0 / nThreads;
    int d0Offset = d00 * threadIndex;
    for (int i = 0; i < d00; i++) {
        float val = hb0[i + d0Offset];
        // silu(x)=x*σ(x), where σ(x) is the logistic sigmoid
        val *= (1.0f / (1.0f + expf(-val)));
        // elementwise multiply with w3(x)
        val *= block->hb20[i + d0Offset];
        hb0[i + d0Offset] = val;
    }
    return TASK_CONTINUE;
}

int quantizeFfnA(TASK_ARGS) {
    TASK_VARIABLES;
    quantizeSlicedBuffer(nThreads, threadIndex, ctx, true, TB_SLICED_HB, TB_SLICED_HB_QUANTIZED);
    return TASK_CONTINUE;
}

int syncFfnA(TASK_ARGS) {
    TASK_VARIABLES;
    syncSliceOfSlicedBuffer(nThreads, threadIndex, ctx, TB_SLICED_HB_QUANTIZED);
    return TASK_CONTINUE;
}

int syncFfnB(TASK_ARGS) {
    TASK_VARIABLES;
    syncMissingSlicesOfSlicedBuffer(nThreads, threadIndex, ctx, TB_SLICED_HB_QUANTIZED);
    return TASK_CONTINUE;
}

// FFN算输出
int ffn2(TASK_ARGS) {
    TASK_VARIABLES;

    float *hb = (float*)transformer->buffer->getUnit(TB_SLICED_HB_QUANTIZED);
    float *xb2 = (float*)transformer->buffer->getSliced(TB_SLICED_XB2, transformer->sliceIndex);

    matmul(spec->weightsFloatType, spec->bufferFloatType, xb2, hb, block->w20, block->w20Slice->n, block->w20Slice->d0, nThreads, threadIndex);
    return TASK_CONTINUE;
}

int quantizeFfn2(TASK_ARGS) {
    TASK_VARIABLES;
    quantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_XB2, TB_SLICED_XB2_QUANTIZED);
    return TASK_CONTINUE;
}

int syncFfn2(TASK_ARGS) {
    TASK_VARIABLES;
    syncSliceOfSlicedBuffer(nThreads, threadIndex, ctx, TB_SLICED_XB2_QUANTIZED);
    return TASK_CONTINUE;
}

int dequantizeFfn2(TASK_ARGS) {
    TASK_VARIABLES;
    dequantizeSlicedBuffer(nThreads, threadIndex, ctx, false, TB_SLICED_XB2_QUANTIZED, TB_SLICED_XB2);
    return TASK_CONTINUE;
}

int mergeFfn2(TASK_ARGS) {
    TASK_VARIABLES;

    if (threadIndex == 0) {
        float* x = transformer->x;
        float* xb2 = (float*)transformer->buffer->getUnit(TB_SLICED_XB2);
        // shortcut
        for (int i = 0; i < spec->dim; i++) {
            x[i] += xb2[i];
        }
    }
    return TASK_CONTINUE;
}

int nextBlock(TASK_ARGS) {
    TASK_VARIABLES;

    if (threadIndex == 0) {
        ctx->currentBlockIndex++;
        if (ctx->currentBlockIndex == spec->nLayers) {
            ctx->currentBlockIndex = 0;
            ctx->finalize = true;
        }
    }
    return TASK_CONTINUE;
}

int rmsFinal(TASK_ARGS) {
    TASK_VARIABLES;
    if (ctx->finalize && threadIndex == 0) {
        float* x = transformer->x;
        transformer->rms = rms(x, spec->dim);
    }
    return TASK_CONTINUE;
}

int rmsFinalNorm(TASK_ARGS) {
    TASK_VARIABLES;
    if (ctx->finalize) {
        float* x = transformer->x;
        rmsnorm(x, x, transformer->rms, (float*)transformer->rmsFinal, spec->dim, nThreads, threadIndex);
    }
    return TASK_CONTINUE;
}

int finalize(TASK_ARGS) {
    TASK_VARIABLES;

    if (ctx->finalize) {
        float* x = transformer->x;
        matmul(spec->weightsFloatType, F32, transformer->logits, x, transformer->wcls, spec->dim, spec->vocabSize, nThreads, threadIndex);
        return TASK_STOP;
    }
    return TASK_CONTINUE;
}

// 从embedding到logits的核心推理执行过程
static TaskLoopTask inferenceTasks[] = {
    { rmsAtt, TASK_TYPE_INFERENCE },
    { rmsAttNorm, TASK_TYPE_INFERENCE },
    { quantizeRmsAtt, TASK_TYPE_INFERENCE },
    { syncRmsAtt, TASK_TYPE_TRANSFER },
    { qkv, TASK_TYPE_INFERENCE },
    { quantizeQkv, TASK_TYPE_INFERENCE },
    { syncQkv, TASK_TYPE_TRANSFER },
    { dequantizeQkv, TASK_TYPE_INFERENCE },
    { multiheadAtt, TASK_TYPE_INFERENCE },
    { quantizeMultiheadAtt, TASK_TYPE_INFERENCE },
    { syncMultiheadAtt, TASK_TYPE_TRANSFER },
    { att, TASK_TYPE_INFERENCE },
    { quantizeAtt, TASK_TYPE_INFERENCE },
    { syncAtt, TASK_TYPE_TRANSFER },
    { dequantizeAtt, TASK_TYPE_INFERENCE },
    { rmfFfn, TASK_TYPE_INFERENCE },
    { rmfFfnNorm, TASK_TYPE_INFERENCE },
    { quantizeRmfFfn, TASK_TYPE_INFERENCE },
    { syncRmfFfn, TASK_TYPE_TRANSFER },
    { ffn, TASK_TYPE_INFERENCE },
    { quantizeFfnA, TASK_TYPE_INFERENCE },
    { syncFfnA, TASK_TYPE_TRANSFER },
    { syncFfnB, TASK_TYPE_TRANSFER },
    { ffn2, TASK_TYPE_INFERENCE },
    { quantizeFfn2, TASK_TYPE_INFERENCE },
    { syncFfn2, TASK_TYPE_TRANSFER },
    { dequantizeFfn2, TASK_TYPE_INFERENCE },
    { mergeFfn2, TASK_TYPE_INFERENCE },
    { nextBlock, TASK_TYPE_INFERENCE },
    { rmsFinal, TASK_TYPE_INFERENCE },
    { rmsFinalNorm, TASK_TYPE_INFERENCE },
    { finalize, TASK_TYPE_INFERENCE },
};

TaskLoopTask* Inference::tasks = inferenceTasks;
int Inference::nTasks = sizeof(inferenceTasks) / sizeof(TaskLoopTask);

Inference::Inference(unsigned int nThreads, Transformer* transformer, SocketPool* socketPool) {
    this->transformer = transformer;
    context.transformer = transformer;
    context.socket = NULL;
    context.socketPool = socketPool;
    taskLoop = new TaskLoop(nThreads, nTasks, TASK_N_TYPES, tasks, (void*)&context);
}

Inference::~Inference() {
    delete taskLoop;
}

float* Inference::infer(int token, int pos) {
    transformer->pos = pos;
    // 获取当前token的embedding
    float* contentRow = ((float*)transformer->tokenEmbeddingTable) + token * transformer->spec->dim;
    // 将embedding赋值到transformer的输入x
    memcpy(transformer->x, contentRow, transformer->spec->dim * sizeof(float));
    // 重置transformer的推理状态
    context.finalize = false;
    context.currentBlockIndex = 0;
    // 执行推理
    taskLoop->run();
    // 返回logits，即为每个token的概率
    return transformer->logits;
}

void Inference::getStats(unsigned long* inferenceTime, unsigned long* transferTime) {
    *inferenceTime = taskLoop->executionTime[TASK_TYPE_INFERENCE];
    *transferTime = taskLoop->executionTime[TASK_TYPE_TRANSFER];
}

void Inference::getDetailedStats(unsigned long* inferenceTime, unsigned long* transferTime, unsigned long* detailedTime) {
    *inferenceTime = taskLoop->executionTime[TASK_TYPE_INFERENCE];
    *transferTime = taskLoop->executionTime[TASK_TYPE_TRANSFER];
    
    // unsigned int nTasks = sizeof(detailedTime) / sizeof(detailedTime[0]);
    for (unsigned int i = 0; i < nTasks; i++) {
        detailedTime[i] = taskLoop->detailedTime[i];
    }
}

static TaskLoopTask workerTasks[] = {
    { syncRmsAtt, TASK_TYPE_TRANSFER },
    { qkv, TASK_TYPE_INFERENCE },
    { quantizeQkv, TASK_TYPE_INFERENCE },
    { syncQkv, TASK_TYPE_TRANSFER },
    { syncMultiheadAtt, TASK_TYPE_TRANSFER },
    { att, TASK_TYPE_INFERENCE },
    { quantizeAtt, TASK_TYPE_INFERENCE },
    { syncAtt, TASK_TYPE_TRANSFER },
    { syncRmfFfn, TASK_TYPE_TRANSFER },
    { ffn, TASK_TYPE_INFERENCE },
    { quantizeFfnA, TASK_TYPE_INFERENCE },
    { syncFfnA, TASK_TYPE_TRANSFER },
    { syncFfnB, TASK_TYPE_TRANSFER },
    { ffn2, TASK_TYPE_INFERENCE },
    { quantizeFfn2, TASK_TYPE_INFERENCE },
    { syncFfn2, TASK_TYPE_TRANSFER },
    { nextBlock, TASK_TYPE_INFERENCE },
};

TaskLoopTask* Worker::tasks = workerTasks;
int Worker::nTasks = sizeof(workerTasks) / sizeof(TaskLoopTask);

Worker::Worker(unsigned int nThreads, Transformer* transformer, Socket* socket) {
    this->transformer = transformer;
    context.transformer = transformer;
    context.socket = socket;
    context.socketPool = NULL;
    taskLoop = new TaskLoop(nThreads, nTasks, TASK_N_TYPES, tasks, (void*)&context);
}

Worker::~Worker() {
    delete taskLoop;
}

void Worker::work() {
    context.finalize = false;
    context.currentBlockIndex = 0;

    taskLoop->run();
}
