#include <cmath>
#include <cassert>
#include <cstdio>
#include <pthread.h>
#include "quants.hpp"
#include "funcs.hpp"

#if defined(__ARM_NEON)
    #include <arm_neon.h>
#elif defined(__AVX2__)
    #include <immintrin.h>
#endif

#if defined(__AVX2__)
    #define MM256_SET_M128I(a, b) _mm256_insertf128_si256(_mm256_castsi128_si256(b), (a), 1)

    static inline __m256i bytes_from_nibbles_32(const uint8_t* rsi) {
        // Load 16 bytes from memory
        __m128i tmpl = _mm_loadu_si128((const __m128i *)rsi);
        __m128i tmph = _mm_srli_epi16(tmpl, 4);
        const __m128i lowMask = _mm_set1_epi8(0xF);
        tmpl = _mm_and_si128(lowMask, tmpl);
        tmph = _mm_and_si128(lowMask, tmph);
        return MM256_SET_M128I(tmph, tmpl);
    }

    static inline float hsum_float_8(const __m256 x) {
        __m128 res = _mm256_extractf128_ps(x, 1);
        res = _mm_add_ps(res, _mm256_castps256_ps128(x));
        res = _mm_add_ps(res, _mm_movehl_ps(res, res));
        res = _mm_add_ss(res, _mm_movehdup_ps(res));
        return _mm_cvtss_f32(res);
    }

    // add int16_t pairwise and return as float vector
    static inline __m256 sum_i16_pairs_float(const __m128i xh, const __m128i xl) {
        const __m128i ones = _mm_set1_epi16(1);
        const __m128i summed_pairsl = _mm_madd_epi16(ones, xl);
        const __m128i summed_pairsh = _mm_madd_epi16(ones, xh);
        const __m256i summed_pairs = MM256_SET_M128I(summed_pairsh, summed_pairsl);
        return _mm256_cvtepi32_ps(summed_pairs);
    }

    // multiply int8_t, add results pairwise twice and return as float vector
    static inline __m256 mul_sum_i8_pairs_float(const __m256i x, const __m256i y) {
        const __m128i xl = _mm256_castsi256_si128(x);
        const __m128i xh = _mm256_extractf128_si256(x, 1);
        const __m128i yl = _mm256_castsi256_si128(y);
        const __m128i yh = _mm256_extractf128_si256(y, 1);
        // Get absolute values of x vectors
        const __m128i axl = _mm_sign_epi8(xl, xl);
        const __m128i axh = _mm_sign_epi8(xh, xh);
        // Sign the values of the y vectors
        const __m128i syl = _mm_sign_epi8(yl, xl);
        const __m128i syh = _mm_sign_epi8(yh, xh);
        // Perform multiplication and create 16-bit values
        const __m128i dotl = _mm_maddubs_epi16(axl, syl);
        const __m128i doth = _mm_maddubs_epi16(axh, syh);
        return sum_i16_pairs_float(doth, dotl);
    }
#endif

/*这个函数 softmax 实现了 Softmax 操作,它是一种常用的机器学习和深度学习技术,将一组实数值映射到 (0, 1) 区间内的概率分布上。

找到输入向量中的最大值 maxVal。这一步是为了防止指数运算时出现数值上溢的情况,提高数值稳定性。如果目标架构支持 NEON SIMD 指令集,则使用矢量指令进行加速。
对输入向量中的每个元素 x[i] 执行指数变换 exp(x[i] - maxVal)。这样做是为了将输入值映射到正区间,同时利用最大值的减法进行数值稳定化。
计算所有指数项之和 sum。
将每个指数项除以总和 sum,从而得到归一化的概率分布。
该函数的输入为一个浮点数数组 x 和数组大小 size。输出则是将 x 原地更新为对应的 Softmax 概率分布。*/
void softmax(float* x, const int size) {
    float maxVal;
#if defined(__ARM_NEON)
    float32x4_t fs;
    float32x4_t fmaxv = vld1q_f32(&x[0]);
    for (int i = 4; i < size; i += 4) {
        fs = vld1q_f32(&x[i]);
        fmaxv = vmaxq_f32(fmaxv, fs);
    }
    maxVal = vmaxvq_f32(fmaxv);
#else
    // find max value (for numerical stability)
    maxVal = x[0];
    for (int i = 1; i < size; i++) {
        if (x[i] > maxVal) {
            maxVal = x[i];
        }
    }
#endif
    // exp and sum
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - maxVal);
        sum += x[i];
    }
    // normalize
    for (int i = 0; i < size; i++) {
        x[i] /= sum;
    }
}

/*这个函数 rms 计算给定浮点数数组 x 的均方根 (Root Mean Square, RMS)。具体来说,它执行以下步骤:

首先断言输入数组大小 size 是 4 的整数倍,这是为了方便使用 NEON SIMD 指令集进行加速。
初始化 ss 变量,用于累加平方和。
如果目标架构支持 NEON,则使用矢量指令遍历数组,每次加载 4 个元素,计算它们的平方和,并累加到 fs 向量中。最后通过 vaddvq_f32 指令将向量中的元素相加得到 ss。
将累加的平方和 ss 除以数组大小 size,得到均值。
为了增加数值稳定性,在均值上加上一个很小的常数 1e-5。
计算均值的平方根的倒数,即均方根的倒数。
返回均方根的倒数。*/
float rms(const float* x, const int size) {
    assert(size % 4 == 0);
    float ss;
#if defined(__ARM_NEON)
    float32x4_t fsq;
    float32x4_t fs = vmovq_n_f32(0);
    for (int j = 0; j < size; j += 4) {
        fsq = vld1q_f32(&x[j]);
        fs = vmlaq_f32(fs, fsq, fsq);
    }
    ss = vaddvq_f32(fs);
#else
    ss = 0;
    for (int j = 0; j < size; j++) {
        ss += x[j] * x[j];
    }
#endif
    ss /= size;
    ss += 1e-5f;
    ss = 1.0f / sqrtf(ss);
    return ss;
}

/*这个函数 rmsnorm 实现了对输入向量 x 进行 RMS 归一化 (Root Mean Square Normalization) 的操作,并将结果存储在输出向量 o 中。具体来说,它执行以下步骤:

断言输入向量大小 size 是 4 的整数倍,以方便使用 NEON SIMD 指令集。同时还断言输入大小能够被线程数 nThreads 整除,以支持并行计算。
根据线程数 nThreads 将输入向量划分为多个切片,每个线程处理一个切片。计算出当前线程需要处理的起始和结束索引。
如果目标架构支持 NEON,则使用矢量指令对切片中的数据进行并行处理。具体操作是:
加载 4 个权重值到矢量 fw
加载 4 个输入值到矢量 fx
将 fx 与 fw 逐元素相乘
将结果与均方根倒数 ms 相乘
将计算结果存储到输出向量 o 对应位置
如果不支持 NEON,则使用标量操作对切片中的数据进行序列处理,执行 o[j] = weight[j] * (ms * x[j])。
该函数实现了对输入向量进行归一化的操作,即将每个元素乘以一个权重,再除以输入向量的均方根。*/
void rmsnorm(float* o, const float* x, const float ms, const float* weight, const int size, unsigned int nThreads, unsigned int threadIndex) {
    assert(size % 4 == 0);
    assert(size % nThreads == 0);

    int slice = size / nThreads;
    int start = threadIndex * slice;
    int end = start + slice;

#if defined(__ARM_NEON)
    float32x4_t fw;
    float32x4_t fx;
    float32x4_t fss = vmovq_n_f32(ms);
    for (int j = start; j < end; j += 4) {
        fw = vld1q_f32(&weight[j]);
        fx = vld1q_f32(&x[j]);
        fx = vmulq_f32(fx, fw);
        fx = vmulq_f32(fx, fss);
        vst1q_f32(&o[j], fx);
    }
#else
    for (int j = start; j < end; j++) {
        o[j] = weight[j] * (ms * x[j]);
    }
#endif
}

/*这个结构体 MatmulThreadInfo 用于存储矩阵乘法操作所需的信息,并被用于多线程计算。它包含以下字段:

pthread_t handler: 一个线程句柄,用于标识和管理线程。
float* output: 指向输出矩阵的指针。
void* input: 指向输入矩阵的指针,类型为 void* 以支持不同的数据类型。
void* weights: 指向权重矩阵的指针,同样类型为 void* 以支持不同的数据类型。
int n: 输入矩阵的行数。
int ds: 当前线程需要处理的输出矩阵行的起始索引。
int de: 当前线程需要处理的输出矩阵行的结束索引(不包括该索引)。
这个结构体是为了方便地将矩阵乘法操作分配给多个线程执行。每个线程都会获得一个 MatmulThreadInfo 实例,其中包含了该线程需要处理的输出矩阵行范围、输入矩阵和权重矩阵等必要信息。*/
struct MatmulThreadInfo {
    pthread_t handler;
    float* output;
    void* input;
    void* weights;
    int n;
    int ds;
    int de;
};

/*这个函数 matmulF32 实现了矩阵乘法操作,其中输入矩阵和权重矩阵都使用 32 位浮点数 (F32) 格式存储。它是一个多线程函数,每个线程负责计算输出矩阵的一部分行。

从 MatmulThreadInfo 结构体中获取输入矩阵、权重矩阵和输出矩阵的指针,以及需要计算的输出矩阵行范围。
如果目标架构支持 NEON SIMD 指令集,则使用矢量化操作来加速计算。对于每一行:
初始化一个 NEON 向量 z 为 0
使用 vld1q_f32 指令每次加载 4 个输入元素到 NEON 向量 q
使用 vld1q_f32 指令每次加载 4 个权重元素到 NEON 向量 p
使用 vfmaq_f32 指令计算 q 和 p 的矢量乘积,并累加到 z
使用 vaddvq_f32 指令将 z 向量中的元素相加,得到该行的输出值
该函数利用了多线程并行计算和 NEON SIMD 指令集(如果可用)来加速矩阵乘法操作。每个线程负责计算输出矩阵的一部分行,而 NEON 指令则通过向量化操作来提高单线程的计算效率。这种优化方式在处理大型矩阵时可以显著提升性能。
*/
void matmulF32(MatmulThreadInfo* a) {
    const float* input = (float*)a->input;
    float* w = (float*)a->weights;
    int d;

#if defined(__ARM_NEON)
    float32x4_t q;
    float32x4_t p;
    float32x4_t z;
    for (d = a->ds; d < a->de; d++) {
        z = vmovq_n_f32(0);
        for (int j = 0; j < a->n; j += 4) {
            q = vld1q_f32(&input[j]);
            p = vld1q_f32(&w[d * a->n + j]);
            z = vfmaq_f32(z, q, p);
        }
        a->output[d] = vaddvq_f32(z);
    }
#else
    for (d = a->ds; d < a->de; d++) {
        float val = 0.0f;
        for (int j = 0; j < a->n; j++) {
            val += w[d * a->n + j] * input[j];
        }
        a->output[d] = val;
    }
#endif
}

/*这个函数 matmulF16 也实现了矩阵乘法操作,但与 matmulF32 不同的是,权重矩阵使用 16 位浮点数 (F16) 格式存储,以节省内存空间。
从 MatmulThreadInfo 结构体中获取输入矩阵、权重矩阵和输出矩阵的指针,以及需要计算的输出矩阵行范围。
对于每一行:
初始化一个浮点数变量 val 为 0,用于累加该行的结果。
遍历该行的所有列:
使用 convertF16ToF32 函数将当前 F16 权重值转换为 F32 格式,得到浮点数 ww。
将 ww 与对应的输入元素相乘,并累加到 val。
将 val 存储到输出矩阵对应位置。
由于权重矩阵使用 F16 格式存储,因此在计算过程中需要首先将 F16 权重转换为 F32 格式,然后再与 F32 输入矩阵相乘。这个转换操作由 convertF16ToF32 函数完成。*/
void matmulF16(MatmulThreadInfo* a) {
    const float* input = (float*)a->input;
    uint16_t* w = (uint16_t*)a->weights;
    int d;
    for (d = a->ds; d < a->de; d++) {
        float val = 0.0f;
        for (int j = 0; j < a->n; j++) {
            float ww = convertF16ToF32(w[d * a->n + j]);
            val += ww * input[j];
        }
        a->output[d] = val;
    }
}

/*这段代码实现了一种利用 Q40 格式存储权重矩阵的矩阵乘法算法。Q40 是一种定点数格式,使用 40 位整数来表示浮点数,可以大幅减少内存占用。

首先定义了一个名为 BlockQ40 的结构体,用于存储 Q40 格式的数据。每个 BlockQ40 包含一个 16 位的 delta 值和 16 个 4 位的 quants 值。

matmulQ40 函数的工作流程如下:

从 MatmulThreadInfo 结构体中获取输入矩阵、权重矩阵和输出矩阵的指针,以及需要计算的输出矩阵行范围。
将权重矩阵分成多个块,每个块包含 8 个 BlockQ40 结构体,即 128 个 Q40 值。
如果目标架构支持 NEON SIMD 指令集,则使用矢量化操作来加速计算。对于每一行:
初始化一个 NEON 向量 u 为 0。
遍历输入矩阵的每一行:
调用 dequantizeQ40Row 函数将当前权重块解码为浮点数,存储在 group 数组中。
使用 vld1q_f32 指令加载 4 个输入元素到 NEON 向量 a0。
使用 vld1q_f32 指令加载 4 个解码后的权重值到 NEON 向量 b0。
使用 vfmaq_f32 指令计算 a0 和 b0 的矢量乘积,并累加到 u。
使用 vaddvq_f32 指令将 u 向量中的元素相加,得到该行的输出值。

通过使用 Q40 格式存储权重矩阵,可以大幅减少内存占用,但需要在计算时进行解码操作,增加了一定的 CPU 开销。不过,该实现利用了 NEON SIMD 指令集(如果可用)来加速计算,从而在一定程度上缓解了这一开销。*/
void matmulQ40(MatmulThreadInfo* a) {
    const int blocksPerRow = 8;
    const int k = QK40 * blocksPerRow;
    BlockQ40* w = (BlockQ40*)a->weights;
    assert(a->n % k == 0);
    const float* input = (float*)a->input;
    const int n = a->n / k;
    float group[k];

#if defined(__ARM_NEON)
    assert(k % 16 == 0);
    float32x4_t a0;
    float32x4_t b0;
    float32x4_t u;
    for (int d = a->ds; d < a->de; d++) {
        u = vmovq_n_f32(0);
        for (int j = 0; j < n; j++) {
            dequantizeQ40Row(&w[d * n * blocksPerRow + j * blocksPerRow], group, k);
            for (int z = 0; z < k; z += 4) {
                a0 = vld1q_f32(&input[j * k + z]);
                b0 = vld1q_f32(&group[z]);
                u = vfmaq_f32(u, a0, b0);
            }
        }
        a->output[d] = vaddvq_f32(u);
    }
#else
    for (int d = a->ds; d < a->de; d++) {
        float val = 0.0f;
        for (int j = 0; j < n; j++) {
            dequantizeQ40Row(&w[d * n * blocksPerRow + j * blocksPerRow], group, k);
            for (int z = 0; z < k; z++) {
                val += group[z] * input[j * k + z];
            }
        }
        a->output[d] = val;
    }
#endif
}

void matmulQ40vQ80(MatmulThreadInfo* a) {
    const BlockQ40* w = (BlockQ40*)a->weights;
    const BlockQ80* input = (BlockQ80*)a->input;
    assert(a->n % QK40 == 0);
    const int n = a->n / QK40;

#if defined(__ARM_NEON)
    float32x4_t sumv0;
    float32x4_t sumv1;
    for (int d = a->ds; d < a->de; d++) {
        sumv0 = vmovq_n_f32(0);
        sumv1 = vmovq_n_f32(0);
        for (int j = 0; j < n; j += 2) {
            const BlockQ40* x0 = &w[d * n + j];
            const BlockQ40* x1 = &w[d * n + j + 1];
            const BlockQ80* y0 = &input[j];
            const BlockQ80* y1 = &input[j + 1];

            const uint8x16_t m4b = vdupq_n_u8(0x0F);
            const int8x16_t  s8b = vdupq_n_s8(0x8);

            const uint8x16_t v0_0 = vld1q_u8(x0->qs);
            const uint8x16_t v0_1 = vld1q_u8(x1->qs);

            // 4-bit -> 8-bit
            const int8x16_t v0_0l = vreinterpretq_s8_u8(vandq_u8  (v0_0, m4b));
            const int8x16_t v0_0h = vreinterpretq_s8_u8(vshrq_n_u8(v0_0, 4));
            const int8x16_t v0_1l = vreinterpretq_s8_u8(vandq_u8  (v0_1, m4b));
            const int8x16_t v0_1h = vreinterpretq_s8_u8(vshrq_n_u8(v0_1, 4));

            // sub 8
            const int8x16_t v0_0ls = vsubq_s8(v0_0l, s8b);
            const int8x16_t v0_0hs = vsubq_s8(v0_0h, s8b);
            const int8x16_t v0_1ls = vsubq_s8(v0_1l, s8b);
            const int8x16_t v0_1hs = vsubq_s8(v0_1h, s8b);

            // load y
            const int8x16_t v1_0l = vld1q_s8(y0->qs);
            const int8x16_t v1_0h = vld1q_s8(y0->qs + 16);
            const int8x16_t v1_1l = vld1q_s8(y1->qs);
            const int8x16_t v1_1h = vld1q_s8(y1->qs + 16);


#if defined(__ARM_FEATURE_DOTPROD)
            const int32x4_t p_0 = vdotq_s32(vdotq_s32(vdupq_n_s32(0), v0_0ls, v1_0l), v0_0hs, v1_0h);
            const int32x4_t p_1 = vdotq_s32(vdotq_s32(vdupq_n_s32(0), v0_1ls, v1_1l), v0_1hs, v1_1h);

            sumv0 = vmlaq_n_f32(sumv0, vcvtq_f32_s32(p_0), convertF16ToF32(x0->d)*convertF16ToF32(y0->d));
            sumv1 = vmlaq_n_f32(sumv1, vcvtq_f32_s32(p_1), convertF16ToF32(x1->d)*convertF16ToF32(y1->d));
#else
            const int16x8_t pl0l = vmull_s8(vget_low_s8 (v0_0ls), vget_low_s8 (v1_0l));
            const int16x8_t pl0h = vmull_s8(vget_high_s8(v0_0ls), vget_high_s8(v1_0l));
            const int16x8_t ph0l = vmull_s8(vget_low_s8 (v0_0hs), vget_low_s8 (v1_0h));
            const int16x8_t ph0h = vmull_s8(vget_high_s8(v0_0hs), vget_high_s8(v1_0h));

            const int16x8_t pl1l = vmull_s8(vget_low_s8 (v0_1ls), vget_low_s8 (v1_1l));
            const int16x8_t pl1h = vmull_s8(vget_high_s8(v0_1ls), vget_high_s8(v1_1l));
            const int16x8_t ph1l = vmull_s8(vget_low_s8 (v0_1hs), vget_low_s8 (v1_1h));
            const int16x8_t ph1h = vmull_s8(vget_high_s8(v0_1hs), vget_high_s8(v1_1h));

            const int32x4_t pl0 = vaddq_s32(vpaddlq_s16(pl0l), vpaddlq_s16(pl0h));
            const int32x4_t ph0 = vaddq_s32(vpaddlq_s16(ph0l), vpaddlq_s16(ph0h));
            const int32x4_t pl1 = vaddq_s32(vpaddlq_s16(pl1l), vpaddlq_s16(pl1h));
            const int32x4_t ph1 = vaddq_s32(vpaddlq_s16(ph1l), vpaddlq_s16(ph1h));

            sumv0 = vmlaq_n_f32(sumv0, vcvtq_f32_s32(vaddq_s32(pl0, ph0)), convertF16ToF32(x0->d) * convertF16ToF32(y0->d));
            sumv1 = vmlaq_n_f32(sumv1, vcvtq_f32_s32(vaddq_s32(pl1, ph1)), convertF16ToF32(x1->d) * convertF16ToF32(y1->d));
#endif
        }
        a->output[d] = vaddvq_f32(sumv0) + vaddvq_f32(sumv1);
    }
#elif defined(__AVX2__)
    for (int d = a->ds; d < a->de; d++) {
        __m256 acc = _mm256_setzero_ps();

        for (int j = 0; j < n; j++) {
            /* Compute combined scale for the block */
            const __m256 cd = _mm256_set1_ps( convertF16ToF32(w[d * n + j].d) * convertF16ToF32(input[j].d) );

            __m256i bx = bytes_from_nibbles_32(w[d * n + j].qs);

            // Now we have a vector with bytes in [ 0 .. 15 ] interval. Offset them into [ -8 .. +7 ] interval.
            const __m256i off = _mm256_set1_epi8( 8 );
            bx = _mm256_sub_epi8(bx, off);

            __m256i by = _mm256_loadu_si256((const __m256i *)input[j].qs);

            const __m256 q = mul_sum_i8_pairs_float(bx, by);

            /* Multiply q with scale and accumulate */
            acc = _mm256_fmadd_ps( cd, q, acc );
        }

        a->output[d] = hsum_float_8(acc);
    }
#else
    printf("matmulQ40vQ80 - not implemented\n");
    exit(EXIT_FAILURE);
#endif
}

//     weights      input    output
//   ___________     ___      ___
//   |         |     | |      | |
// d |         | *   | |  = d | |
//   |_________|   n | |      |_|
//        n          |_|       1
//                    1
/*
根据输入矩阵和权重矩阵的数据类型调用相应的矩阵乘法。支持32位浮点数、16位浮点数、Q40和Q80格式的数。
首先初始化一个 MatmulThreadInfo 结构体 s,用于存储矩阵乘法所需的信息,包括输出矩阵指针、输入矩阵指针、权重矩阵指针、输入矩阵行数 n,以及当前线程需要计算的输出矩阵行范围。

根据输入矩阵和权重矩阵的数据类型,调用对应的专门矩阵乘法函数:
如果输入矩阵为 F32 格式:
如果权重矩阵也为 F32 格式,调用 matmulF32 函数。
如果权重矩阵为 F16 格式,调用 matmulF16 函数。
如果权重矩阵为 Q40 格式,调用 matmulQ40 函数。
如果输入矩阵为 Q80 格式,且权重矩阵为 Q40 格式,调用 matmulQ40vQ80 函数。
如果输入矩阵和权重矩阵的数据类型组合不受支持,则打印错误消息并退出程序。
利用 NEON SIMD 指令集加速 F32 格式的矩阵乘法,而 matmulQ40 函数则针对压缩的 Q40 格式，先dequantize再计算。

通过这种方式,该实现可以在内存使用和计算效率之间进行权衡。使用较低精度的数据类型(如 F16 和 Q40)可以减少内存占用,而使用较高精度的数据类型(如 F32)则可以获得更高的计算精度。
该函数还支持多线程并行计算,每个线程负责计算输出矩阵的一部分行,以充分利用现代 CPU 的并行计算能力。
*/
void matmul(FloatType weightsFloatType, FloatType inputFloatType, float* output, void* input, void* weights, int n, int d, unsigned int nThreads, unsigned int threadIndex) {
    MatmulThreadInfo s;
    s.output = output;
    s.input = input;
    s.weights = weights;
    s.n = n;
    s.ds = threadIndex * d / nThreads;
    s.de = (threadIndex + 1) * d / nThreads;

    if (inputFloatType == F32) {
        if (weightsFloatType == F32) {
            matmulF32(&s);
            return;
        }
        if (weightsFloatType == F16) {
            matmulF16(&s);
            return;
        }
        if (weightsFloatType == Q40) {
            matmulQ40(&s);
            return;
        }
    }
    if (inputFloatType == Q80 && weightsFloatType == Q40) {
        matmulQ40vQ80(&s);
        return;
    }

    printf("Unsupported float types: %d/%d\n", weightsFloatType, inputFloatType);
    exit(EXIT_FAILURE);
}

float dotProduct(const float* a, const float* b, const int size) {
    assert(size % 4 == 0);
#if defined(__ARM_NEON)
    float32x4_t fa;
    float32x4_t fb;
    float32x4_t fs = vmovq_n_f32(0);
    for (int i = 0; i < size; i += 4) {
        fa = vld1q_f32(&a[i]);
        fb = vld1q_f32(&b[i]);
        fs = vmlaq_f32(fs, fa, fb);
    }
    return vaddvq_f32(fs);
#else
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        sum += a[i] * b[i];
    }
    return sum;
#endif
}

#define SQRT_2_OVER_PI 0.79788456080286535587989211986876f
#define GELU_COEF_A 0.044715f

void gelu(float* t, int n, unsigned int nThreads, unsigned int threadIndex) {
    assert(n % nThreads == 0);
    int m = n / nThreads;
    int start = m * threadIndex;
    int end = start + m;

    for (int i = start; i < end; i++) {
        float x = t[i];
        t[i] = 0.5f * x * (1.0f + tanhf(SQRT_2_OVER_PI * x * (1.0f + GELU_COEF_A * x * x)));
    }
}

void silu(float* t, int n, unsigned int nThreads, unsigned int threadIndex) {
    assert(n % nThreads == 0);
    int m = n / nThreads;
    int start = m * threadIndex;
    int end = start + m;

    for (int i = start; i < end; i++) {
        float x = t[i];
        t[i] = x / (1.0f + expf(-x));
    }
}

void mul(float* output, float* input, int n, unsigned int nThreads, unsigned int threadIndex) {
    assert(n % nThreads == 0);
    int m = n / nThreads;
    int start = m * threadIndex;
    int end = start + m;

    for (int i = start; i < end; i++) {
        output[i] *= input[i];
    }
}

void mulScalar(float* output, float c, int n, unsigned int nThreads, unsigned int threadIndex) {
    assert(n % nThreads == 0);
    int m = n / nThreads;
    int start = m * threadIndex;
    int end = start + m;
    
    for (int i = start; i < end; i++) {
        output[i] *= c;
    }
}

void add(float* output, float* input, int n, unsigned int nThreads, unsigned int threadIndex) {
    assert(n % nThreads == 0);
    int m = n / nThreads;
    int start = m * threadIndex;
    int end = start + m;

    for (int i = start; i < end; i++) {
        output[i] += input[i];
    }
}
