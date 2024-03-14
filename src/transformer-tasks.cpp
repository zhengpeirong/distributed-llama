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
/*
syncUnitBuffer函数用于在多线程环境下同步缓冲区数据。

首先获取缓冲区指针buffer和字节大小bufferBytes。
如果ctx->socketPool不为空,表示是主节点(root),将缓冲区数据通过socketPool分发给工作节点。
如果ctx->socket不为空且当前是工作节点(threadIndex不为0),则从主节点读取缓冲区数据。
该代码似乎是一个并行计算框架的一部分,用于在多线程环境下高效地进行数据传输和计算。它利用了socket进行线程间通信,使用缓冲区存储中间数据,并通过同步机制来保证数据的一致性。*/
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
/*这段代码定义了一个名为syncSliceOfSlicedBuffer的函数,功能是在多线程环境下同步切片缓冲区的数据。
首先获取缓冲区的字节大小bufferBytes。

如果ctx->socketPool不为空,表示是主节点(root):
计算需要通信的socket数量nSockets。
根据线程索引和socket索引,为每个socket准备一个SocketIo结构体,存储socket索引、数据指针和大小。
从工作节点读取切片缓冲区数据,通过ctx->socketPool->readMany完成读取操作。

如果ctx->socket不为空且当前是工作节点(threadIndex为0):
获取当前工作节点的切片缓冲区指针buffer。
通过ctx->socket->write将切片缓冲区数据写回主节点。

该函数的作用是在多线程环境下,主节点从工作节点收集切片后的缓冲区数据,而工作节点则向主节点发送自己的切片缓冲区数据。
代码使用了SocketPool和Socket进行线程间通信,并使用切片缓冲区技术来分散数据处理的负担,提高并行计算效率。
总的来说,这是一个线程并行计算框架的同步函数,用于在主节点和工作节点之间传递切片缓冲区数据,保证并行计算的正确性和高效性。*/
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
/*这段代码定义了一个名为syncMissingSlicesOfSlicedBuffer的函数,用于在多节点(树莓派)环境下广播(broadcast)缺失的切片缓冲区数据。
首先获取切片缓冲区的字节大小sliceBytes。

如果ctx->socketPool不为空,表示是主节点(root,代表一个树莓派):
计算需要通信的工作节点(其他树莓派)数量nSockets。
遍历所有切片索引si(除了最后一个切片)。
对于每个切片索引si,为每个工作节点准备一个SocketIo结构体,其中:
socketIndex表示工作节点索引。
workerSliceIndex表示工作节点应该处理的切片索引。
sliceIndex则是当前切片的索引,根据workerSliceIndex确定。
通过ctx->socketPool->writeMany将当前切片广播给所有工作节点。

如果ctx->socket不为空且当前是工作节点(threadIndex为0,代表一个树莓派):
遍历所有切片索引sliceIndex(包括自己的切片)。
如果sliceIndex不等于自己的切片索引,则从主节点读取该切片的缓冲区数据。
该函数的作用是让主节点将其他所有切片的缓冲区数据广播给工作节点,而工作节点则从主节点接收除自己切片之外的所有其他切片数据。

通过这种广播机制,每个工作节点(树莓派)不仅拥有自己的切片数据,还拥有其他所有切片的数据,从而能够进行进一步的并行计算和处理。
代码使用了SocketPool和Socket进行节点间通信,并利用切片缓冲区技术来分散数据处理的负担,提高并行计算效率。
总的来说,这是一个分布式并行计算框架中的广播同步函数,用于在主节点(一个树莓派)和工作节点(其他树莓派)之间广播切片缓冲区数据,保证所有节点拥有完整的数据,为后续的并行计算做准备。*/
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

/*这段代码定义了一个名为quantizeUnitBuffer的函数,用于对缓冲区数据进行量化(quantization)操作。
首先检查transformer->spec->bufferFloatType的值。如果是F32(32位浮点数),则直接返回,不进行量化操作。

如果bufferFloatType是Q80(定点数),则进行量化操作。

调用quantizeQ80Row函数,对缓冲区数据进行量化:

第一个参数是源缓冲区(32位浮点数)的指针。
第二个参数是目标缓冲区(定点数Q80)的指针。
第三个参数是源缓冲区中浮点数的个数。
第四个和第五个参数分别是线程数和当前线程索引,用于并行化量化操作。
quantizeQ80Row函数的作用是将32位浮点数数组量化为Q80格式的定点数数组,Q80表示80位定点数(1位符号位,15位整数位,64位小数位)。

量化操作可以减小数据大小,提高计算效率,但也会引入一定的精度损失。在深度学习等领域,常采用量化技术来加速计算和节省内存。

该函数在多线程环境下并行执行量化操作,提高计算性能。*/
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

/*这段代码定义了一个名为quantizeSlicedBuffer的函数,用于对切片缓冲区数据进行量化操作。
首先检查transformer->spec->bufferFloatType的值,如果是F32(32位浮点数),则直接返回,不进行量化操作。
如果当前线程是根切片(sliceIndex为0),且quantizeRootSlice参数为false,则也直接返回,不对根切片进行量化。
如果bufferFloatType是Q80(定点数),则进行量化操作。
调用quantizeQ80Row函数,对切片缓冲区数据进行量化:
第一个参数是源切片缓冲区(32位浮点数)的指针,通过getSliced获取。
第二个参数是目标切片缓冲区(定点数Q80)的指针,也是通过getSliced获取。
第三个参数是源切片缓冲区中浮点数的个数。
第四个和第五个参数分别是线程数和当前线程索引,用于并行化量化操作。
quantizeQ80Row函数的作用是将32位浮点数数组量化为Q80格式的定点数数组,Q80表示80位定点数(1位符号位,15位整数位,64位小数位)。

量化操作可以减小数据大小,提高计算效率,但也会引入一定的精度损失。在深度学习等领域,常采用量化技术来加速计算和节省内存。
该函数在多线程环境下并行执行量化操作,提高计算性能。与quantizeUnitBuffer不同的是,quantizeSlicedBuffer对切片缓冲区进行量化,可以选择是否对根切片进行量化。*/
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

/*这段代码定义了一个名为dequantizeSlicedBuffer的函数,用于对切片缓冲区数据进行反量化(dequantization)操作。反量化是将离散值(如定点数)转换为连续值(如浮点数)的过程,是量化的逆过程。

首先检查transformer->spec->bufferFloatType的值,如果是F32(32位浮点数),则直接返回,不进行反量化操作。

如果bufferFloatType是Q80(定点数),则进行断言检查。

还进行了一个断言,确保ctx->socketPool不为空,即这个函数只能由主节点调用。

根据dequantizeRootSlice参数,确定从哪个切片索引sliceIndex开始进行反量化。如果dequantizeRootSlice为真,从切片0(根切片)开始,否则从切片1开始。

在一个循环中,对每个切片执行以下操作:

调用dequantizeQ80Row函数进行反量化。
第一个参数是源切片缓冲区(定点数Q80)的指针,通过getSliced获取。
第二个参数是目标切片缓冲区(32位浮点数)的指针,也是通过getSliced获取。
第三个参数是源切片缓冲区中定点数的个数,乘以QK80(一个常数,值为2^-64)进行缩放。
第四个和第五个参数分别是线程数和当前线程索引,用于并行化反量化操作。
dequantizeQ80Row函数的作用是将Q80格式的定点数数组反量化为32位浮点数数组。

反量化操作是量化的逆过程,可以从压缩的定点数数据恢复出原始的浮点数数据,但也会引入一定的精度损失。

该函数在多线程环境下并行执行反量化操作,提高计算性能。它提供了一个选项,允许决定是否对根切片进行反量化。*/
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

// 单机单线程reduction求rms
// TODO: 改成多线程
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
/* 单线程单机多头注意力 
TODO: 改成多线程
*/
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
    total_time_above_workers += executionTime; // 累加运行时间
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

/*前馈网络是Transformer架构中的另一个关键组件,通常由两层全连接层组成,中间使用非线性激活函数。该函数的主要功能和实现细节如下:

功能:

将多头注意力机制的输出(xb)作为输入。
通过两层全连接层和非线性激活函数,实现前馈网络的变换。
输出结果存储在hb0中。
输入:

xb: 多头注意力机制的输出,维度为[batch_size, seq_len, hidden_dim]。
hb0: 输出缓冲区,用于存储前馈网络的输出,维度与xb相同。
实现细节:

使用matmul函数实现矩阵乘法运算,完成两层全连接层的线性变换。
第一层全连接层: hb0 = xb * block->w10
第二层全连接层: block->hb20 = xb * block->w30
应用SwiGLU(Sigmoid-Weighted Linear Unit)非线性激活函数。
对hb0中的每个元素应用SwiGLU激活函数,公式为: x * sigmoid(x)。
与block->hb20(第二层全连接层的输出)逐元素相乘,得到最终的前馈网络输出。
该函数支持多线程并行计算,每个线程处理hb0的一部分数据。
并行性:

函数利用多线程并行计算,每个线程处理hb0的一部分数据。
使用nThreads和threadIndex控制线程数量和线程索引。
在SwiGLU非线性激活函数的计算中,将hb0的数据均匀划分给每个线程进行计算。
性能优化:

该函数没有直接使用SIMD指令集(如NEON)进行向量化计算,但是matmul函数可能已经对矩阵乘法操作进行了优化。
使用多线程并行计算可以提高计算性能,尤其在处理大量数据时更有优势。
总的来说,这个ffn函数实现了Transformer架构中的前馈网络模块,包括两层全连接层的线性变换和SwiGLU非线性激活函数。它利用了多线程并行计算,可以提高计算性能。*/
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
