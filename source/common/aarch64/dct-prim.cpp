#include "dct-prim.h"


#if HAVE_NEON

#include <arm_neon.h>

#define X265_PRAGMA(text)       _Pragma(#text)
#if defined(__clang__)
#define X265_PRAGMA_UNROLL(n)   X265_PRAGMA(unroll(n))
#elif defined(__GNUC__)
#define X265_PRAGMA_UNROLL(n)   X265_PRAGMA(GCC unroll (n))
#else
#define X265_PRAGMA_UNROLL(n)
#endif

extern "C" void PFX(dct16_neon)(const int16_t *src, int16_t *dst, intptr_t srcStride);
extern "C" void PFX(idct16_neon)(const int16_t *src, int16_t *dst, intptr_t dstStride);

namespace
{
using namespace X265_NS;

static inline void transpose_4x4_s16(int16x4_t &s0, int16x4_t &s1, int16x4_t &s2, int16x4_t &s3)
{
    int16x8_t s0q = vcombine_s16(s0, vdup_n_s16(0));
    int16x8_t s1q = vcombine_s16(s1, vdup_n_s16(0));
    int16x8_t s2q = vcombine_s16(s2, vdup_n_s16(0));
    int16x8_t s3q = vcombine_s16(s3, vdup_n_s16(0));

    int16x8_t s02 = vzip1q_s16(s0q, s2q);
    int16x8_t s13 = vzip1q_s16(s1q, s3q);

    int16x8x2_t s0123 = vzipq_s16(s02, s13);

    s0 = vget_low_s16(s0123.val[0]);
    s1 = vget_high_s16(s0123.val[0]);
    s2 = vget_low_s16(s0123.val[1]);
    s3 = vget_high_s16(s0123.val[1]);
}

static inline void transpose_4x8_s16(int16x4_t s0, int16x4_t s1, int16x4_t s2, int16x4_t s3,
                                     int16x4_t s4, int16x4_t s5, int16x4_t s6, int16x4_t s7,
                                     int16x8_t &d0, int16x8_t &d1, int16x8_t &d2, int16x8_t &d3)
{
    int16x8_t s0q = vcombine_s16(s0, vdup_n_s16(0));
    int16x8_t s1q = vcombine_s16(s1, vdup_n_s16(0));
    int16x8_t s2q = vcombine_s16(s2, vdup_n_s16(0));
    int16x8_t s3q = vcombine_s16(s3, vdup_n_s16(0));
    int16x8_t s4q = vcombine_s16(s4, vdup_n_s16(0));
    int16x8_t s5q = vcombine_s16(s5, vdup_n_s16(0));
    int16x8_t s6q = vcombine_s16(s6, vdup_n_s16(0));
    int16x8_t s7q = vcombine_s16(s7, vdup_n_s16(0));

    int16x8_t s04 = vzip1q_s16(s0q, s4q);
    int16x8_t s15 = vzip1q_s16(s1q, s5q);
    int16x8_t s26 = vzip1q_s16(s2q, s6q);
    int16x8_t s37 = vzip1q_s16(s3q, s7q);

    int16x8x2_t s0246 = vzipq_s16(s04, s26);
    int16x8x2_t s1357 = vzipq_s16(s15, s37);

    d0 = vzip1q_s16(s0246.val[0], s1357.val[0]);
    d1 = vzip2q_s16(s0246.val[0], s1357.val[0]);
    d2 = vzip1q_s16(s0246.val[1], s1357.val[1]);
    d3 = vzip2q_s16(s0246.val[1], s1357.val[1]);
}

static int scanPosLast_opt(const uint16_t *scan, const coeff_t *coeff, uint16_t *coeffSign, uint16_t *coeffFlag,
                           uint8_t *coeffNum, int numSig, const uint16_t * /*scanCG4x4*/, const int /*trSize*/)
{

    // This is an optimized function for scanPosLast, which removes the rmw dependency, once integrated into mainline x265, should replace reference implementation
    // For clarity, left the original reference code in comments
    int scanPosLast = 0;

    uint16_t cSign = 0;
    uint16_t cFlag = 0;
    uint8_t cNum = 0;

    uint32_t prevcgIdx = 0;
    do
    {
        const uint32_t cgIdx = (uint32_t)scanPosLast >> MLS_CG_SIZE;

        const uint32_t posLast = scan[scanPosLast];

        const int curCoeff = coeff[posLast];
        const uint32_t isNZCoeff = (curCoeff != 0);
        /*
        NOTE: the new algorithm is complicated, so I keep reference code here
        uint32_t posy   = posLast >> log2TrSize;
        uint32_t posx   = posLast - (posy << log2TrSize);
        uint32_t blkIdx0 = ((posy >> MLS_CG_LOG2_SIZE) << codingParameters.log2TrSizeCG) + (posx >> MLS_CG_LOG2_SIZE);
        const uint32_t blkIdx = ((posLast >> (2 * MLS_CG_LOG2_SIZE)) & ~maskPosXY) + ((posLast >> MLS_CG_LOG2_SIZE) & maskPosXY);
        sigCoeffGroupFlag64 |= ((uint64_t)isNZCoeff << blkIdx);
        */

        // get L1 sig map
        numSig -= isNZCoeff;

        if (scanPosLast % (1 << MLS_CG_SIZE) == 0)
        {
            coeffSign[prevcgIdx] = cSign;
            coeffFlag[prevcgIdx] = cFlag;
            coeffNum[prevcgIdx] = cNum;
            cSign = 0;
            cFlag = 0;
            cNum = 0;
        }
        // TODO: optimize by instruction BTS
        cSign += (uint16_t)(((curCoeff < 0) ? 1 : 0) << cNum);
        cFlag = (cFlag << 1) + (uint16_t)isNZCoeff;
        cNum += (uint8_t)isNZCoeff;
        prevcgIdx = cgIdx;
        scanPosLast++;
    }
    while (numSig > 0);

    coeffSign[prevcgIdx] = cSign;
    coeffFlag[prevcgIdx] = cFlag;
    coeffNum[prevcgIdx] = cNum;
    return scanPosLast - 1;
}


#if (MLS_CG_SIZE == 4)
template<int log2TrSize>
static void nonPsyRdoQuant_neon(int16_t *m_resiDctCoeff, int64_t *costUncoded, int64_t *totalUncodedCost,
                                int64_t *totalRdCost, uint32_t blkPos)
{
    const int transformShift = MAX_TR_DYNAMIC_RANGE - X265_DEPTH -
                               log2TrSize; /* Represents scaling through forward transform */
    const int scaleBits = SCALE_BITS - 2 * transformShift;
    const uint32_t trSize = 1 << log2TrSize;

    int64x2_t vcost_sum_0 = vdupq_n_s64(0);
    int64x2_t vcost_sum_1 = vdupq_n_s64(0);
    for (int y = 0; y < MLS_CG_SIZE; y++)
    {
        int16x4_t in = vld1_s16(&m_resiDctCoeff[blkPos]);
        int32x4_t mul = vmull_s16(in, in);
        int64x2_t cost0, cost1;
        cost0 = vshll_n_s32(vget_low_s32(mul), scaleBits);
        cost1 = vshll_high_n_s32(mul, scaleBits);
        vst1q_s64(&costUncoded[blkPos + 0], cost0);
        vst1q_s64(&costUncoded[blkPos + 2], cost1);
        vcost_sum_0 = vaddq_s64(vcost_sum_0, cost0);
        vcost_sum_1 = vaddq_s64(vcost_sum_1, cost1);
        blkPos += trSize;
    }
    int64_t sum = vaddvq_s64(vaddq_s64(vcost_sum_0, vcost_sum_1));
    *totalUncodedCost += sum;
    *totalRdCost += sum;
}

template<int log2TrSize>
static void psyRdoQuant_neon(int16_t *m_resiDctCoeff, int16_t *m_fencDctCoeff, int64_t *costUncoded,
                             int64_t *totalUncodedCost, int64_t *totalRdCost, int64_t *psyScale, uint32_t blkPos)
{
    const int transformShift = MAX_TR_DYNAMIC_RANGE - X265_DEPTH -
                               log2TrSize; /* Represents scaling through forward transform */
    const int scaleBits = SCALE_BITS - 2 * transformShift;
    const uint32_t trSize = 1 << log2TrSize;
    //using preprocessor to bypass clang bug
    const int max = X265_MAX(0, (2 * transformShift + 1));

    int64x2_t vcost_sum_0 = vdupq_n_s64(0);
    int64x2_t vcost_sum_1 = vdupq_n_s64(0);
    int32x4_t vpsy = vdupq_n_s32(*psyScale);
    for (int y = 0; y < MLS_CG_SIZE; y++)
    {
        int32x4_t signCoef = vmovl_s16(vld1_s16(&m_resiDctCoeff[blkPos]));
        int32x4_t fencCoef = vmovl_s16(vld1_s16(&m_fencDctCoeff[blkPos]));
        int32x4_t predictedCoef = vsubq_s32(fencCoef, signCoef);
        int64x2_t cost0, cost1;
        cost0 = vmull_s32(vget_low_s32(signCoef), vget_low_s32(signCoef));
        cost1 = vmull_high_s32(signCoef, signCoef);
        cost0 = vshlq_n_s64(cost0, scaleBits);
        cost1 = vshlq_n_s64(cost1, scaleBits);
        int64x2_t neg0 = vmull_s32(vget_low_s32(predictedCoef), vget_low_s32(vpsy));
        int64x2_t neg1 = vmull_high_s32(predictedCoef, vpsy);
        if (max > 0)
        {
            int64x2_t shift = vdupq_n_s64(-max);
            neg0 = vshlq_s64(neg0, shift);
            neg1 = vshlq_s64(neg1, shift);
        }
        cost0 = vsubq_s64(cost0, neg0);
        cost1 = vsubq_s64(cost1, neg1);
        vst1q_s64(&costUncoded[blkPos + 0], cost0);
        vst1q_s64(&costUncoded[blkPos + 2], cost1);
        vcost_sum_0 = vaddq_s64(vcost_sum_0, cost0);
        vcost_sum_1 = vaddq_s64(vcost_sum_1, cost1);

        blkPos += trSize;
    }
    int64_t sum = vaddvq_s64(vaddq_s64(vcost_sum_0, vcost_sum_1));
    *totalUncodedCost += sum;
    *totalRdCost += sum;
}

#else
#error "MLS_CG_SIZE must be 4 for neon version"
#endif



template<int trSize>
int  count_nonzero_neon(const int16_t *quantCoeff)
{
    X265_CHECK(((intptr_t)quantCoeff & 15) == 0, "quant buffer not aligned\n");
    int count = 0;
    int16x8_t vcount = vdupq_n_s16(0);
    const int numCoeff = trSize * trSize;
    int i = 0;
    for (; (i + 8) <= numCoeff; i += 8)
    {
        int16x8_t in = vld1q_s16(&quantCoeff[i]);
        uint16x8_t tst = vtstq_s16(in, in);
        vcount = vaddq_s16(vcount, vreinterpretq_s16_u16(tst));
    }
    for (; i < numCoeff; i++)
    {
        count += quantCoeff[i] != 0;
    }

    return count - vaddvq_s16(vcount);
}

template<int trSize>
uint32_t copy_count_neon(int16_t *coeff, const int16_t *residual, intptr_t resiStride)
{
    uint32_t numSig = 0;
    int16x8_t vcount = vdupq_n_s16(0);
    for (int k = 0; k < trSize; k++)
    {
        int j = 0;
        for (; (j + 8) <= trSize; j += 8)
        {
            int16x8_t in = vld1q_s16(&residual[j]);
            vst1q_s16(&coeff[j], in);
            uint16x8_t tst = vtstq_s16(in, in);
            vcount = vaddq_s16(vcount, vreinterpretq_s16_u16(tst));
        }
        for (; j < trSize; j++)
        {
            coeff[j] = residual[j];
            numSig += (residual[j] != 0);
        }
        residual += resiStride;
        coeff += trSize;
    }

    return numSig - vaddvq_s16(vcount);
}

template<int shift>
static inline void fastForwardDst4_neon(const int16_t *src, int16_t *dst)
{
    int16x4_t s0 = vld1_s16(src + 0);
    int16x4_t s1 = vld1_s16(src + 4);
    int16x4_t s2 = vld1_s16(src + 8);
    int16x4_t s3 = vld1_s16(src + 12);

    transpose_4x4_s16(s0, s1, s2, s3);

    int32x4_t c0 = vaddl_s16(s0, s3);
    int32x4_t c1 = vaddl_s16(s1, s3);
    int32x4_t c2 = vsubl_s16(s0, s1);
    int32x4_t c3 = vmull_n_s16(s2, 74);

    int32x4_t t0 = vmlaq_n_s32(c3, c0, 29);
    t0 = vmlaq_n_s32(t0, c1, 55);

    int32x4_t t1 = vaddl_s16(s0, s1);
    t1 = vsubw_s16(t1, s3);
    t1 = vmulq_n_s32(t1, 74);

    int32x4_t t2 = vmulq_n_s32(c2, 29);
    t2 = vmlaq_n_s32(t2, c0, 55);
    t2 = vsubq_s32(t2, c3);

    int32x4_t t3 = vmlaq_n_s32(c3, c2, 55);
    t3 = vmlsq_n_s32(t3, c1, 29);

    int16x4_t d0 = vrshrn_n_s32(t0, shift);
    int16x4_t d1 = vrshrn_n_s32(t1, shift);
    int16x4_t d2 = vrshrn_n_s32(t2, shift);
    int16x4_t d3 = vrshrn_n_s32(t3, shift);

    vst1_s16(dst + 0, d0);
    vst1_s16(dst + 4, d1);
    vst1_s16(dst + 8, d2);
    vst1_s16(dst + 12, d3);
}

template<int shift>
static inline void inverseDst4_neon(const int16_t *src, int16_t *dst, intptr_t dstStride)
{
    int16x4_t s0 = vld1_s16(src + 0);
    int16x4_t s1 = vld1_s16(src + 4);
    int16x4_t s2 = vld1_s16(src + 8);
    int16x4_t s3 = vld1_s16(src + 12);

    int32x4_t c0 = vaddl_s16(s0, s2);
    int32x4_t c1 = vaddl_s16(s2, s3);
    int32x4_t c2 = vsubl_s16(s0, s3);
    int32x4_t c3 = vmull_n_s16(s1, 74);

    int32x4_t t0 = vmlaq_n_s32(c3, c0, 29);
    t0 = vmlaq_n_s32(t0, c1, 55);

    int32x4_t t1 = vmlaq_n_s32(c3, c2, 55);
    t1 = vmlsq_n_s32(t1, c1, 29);

    int32x4_t t2 = vaddl_s16(s0, s3);
    t2 = vsubw_s16(t2, s2);
    t2 = vmulq_n_s32(t2, 74);

    int32x4_t t3 = vmulq_n_s32(c0, 55);
    t3 = vmlaq_n_s32(t3, c2, 29);
    t3 = vsubq_s32(t3, c3);

    int16x4_t d0 = vqrshrn_n_s32(t0, shift);
    int16x4_t d1 = vqrshrn_n_s32(t1, shift);
    int16x4_t d2 = vqrshrn_n_s32(t2, shift);
    int16x4_t d3 = vqrshrn_n_s32(t3, shift);

    transpose_4x4_s16(d0, d1, d2, d3);

    vst1_s16(dst + 0 * dstStride, d0);
    vst1_s16(dst + 1 * dstStride, d1);
    vst1_s16(dst + 2 * dstStride, d2);
    vst1_s16(dst + 3 * dstStride, d3);
}

template<int shift>
static inline void partialButterfly4_neon(const int16_t *src, int16_t *dst)
{
    int16x4_t s0 = vld1_s16(src + 0);
    int16x4_t s1 = vld1_s16(src + 4);
    int16x4_t s2 = vld1_s16(src + 8);
    int16x4_t s3 = vld1_s16(src + 12);

    transpose_4x4_s16(s0, s1, s2, s3);

    int32x4_t E[2], O[2];
    E[0] = vaddl_s16(s0, s3);
    O[0] = vsubl_s16(s0, s3);
    E[1] = vaddl_s16(s1, s2);
    O[1] = vsubl_s16(s1, s2);

    // Multiply and accumulate with g_t4 constants.
    int32x4_t t0 = vaddq_s32(E[0], E[1]);
    t0 = vmulq_n_s32(t0, 64);
    int32x4_t t1 = vmulq_n_s32(O[0], 83);
    t1 = vmlaq_n_s32(t1, O[1], 36);
    int32x4_t t2 = vsubq_s32(E[0], E[1]);
    t2 = vmulq_n_s32(t2, 64);
    int32x4_t t3 = vmulq_n_s32(O[0], 36);
    t3 = vmlaq_n_s32(t3, O[1], -83);

    int16x4_t d0 = vrshrn_n_s32(t0, shift);
    int16x4_t d1 = vrshrn_n_s32(t1, shift);
    int16x4_t d2 = vrshrn_n_s32(t2, shift);
    int16x4_t d3 = vrshrn_n_s32(t3, shift);

    vst1_s16(dst + 0, d0);
    vst1_s16(dst + 4, d1);
    vst1_s16(dst + 8, d2);
    vst1_s16(dst + 12, d3);
}

template<int shift>
static inline void partialButterfly16_neon(const int16_t *src, int16_t *dst)
{
    const int line = 16;

    int16x8_t O[line];
    int32x4_t EO[line];
    int32x4_t EEE[line];
    int32x4_t EEO[line];

    for (int i = 0; i < line; i += 2)
    {
        int16x8_t s0_lo = vld1q_s16(src + i * line);
        int16x8_t s0_hi = rev16(vld1q_s16(src + i * line + 8));

        int16x8_t s1_lo = vld1q_s16(src + (i + 1) * line);
        int16x8_t s1_hi = rev16(vld1q_s16(src + (i + 1) * line + 8));

        int32x4_t E0[2];
        E0[0] = vaddl_s16(vget_low_s16(s0_lo), vget_low_s16(s0_hi));
        E0[1] = vaddl_s16(vget_high_s16(s0_lo), vget_high_s16(s0_hi));

        int32x4_t E1[2];
        E1[0] = vaddl_s16(vget_low_s16(s1_lo), vget_low_s16(s1_hi));
        E1[1] = vaddl_s16(vget_high_s16(s1_lo), vget_high_s16(s1_hi));

        O[i + 0] = vsubq_s16(s0_lo, s0_hi);
        O[i + 1] = vsubq_s16(s1_lo, s1_hi);

        int32x4_t EE0 = vaddq_s32(E0[0], rev32(E0[1]));
        int32x4_t EE1 = vaddq_s32(E1[0], rev32(E1[1]));
        EO[i + 0] = vsubq_s32(E0[0], rev32(E0[1]));
        EO[i + 1] = vsubq_s32(E1[0], rev32(E1[1]));

        int32x4_t t0 = vreinterpretq_s32_s64(
            vzip1q_s64(vreinterpretq_s64_s32(EE0), vreinterpretq_s64_s32(EE1)));
        int32x4_t t1 = vrev64q_s32(vreinterpretq_s32_s64(vzip2q_s64(
            vreinterpretq_s64_s32(EE0), vreinterpretq_s64_s32(EE1))));


        EEE[i / 2] = vaddq_s32(t0, t1);
        EEO[i / 2] = vsubq_s32(t0, t1);
    }

    for (int i = 0; i < line; i += 4)
    {
        for (int k = 1; k < 16; k += 2)
        {
            int16x8_t c0_c4 = vld1q_s16(&g_t16[k][0]);

            int32x4_t t0 = vmull_s16(vget_low_s16(c0_c4),
                                     vget_low_s16(O[i + 0]));
            int32x4_t t1 = vmull_s16(vget_low_s16(c0_c4),
                                     vget_low_s16(O[i + 1]));
            int32x4_t t2 = vmull_s16(vget_low_s16(c0_c4),
                                     vget_low_s16(O[i + 2]));
            int32x4_t t3 = vmull_s16(vget_low_s16(c0_c4),
                                     vget_low_s16(O[i + 3]));
            t0 = vmlal_s16(t0, vget_high_s16(c0_c4), vget_high_s16(O[i + 0]));
            t1 = vmlal_s16(t1, vget_high_s16(c0_c4), vget_high_s16(O[i + 1]));
            t2 = vmlal_s16(t2, vget_high_s16(c0_c4), vget_high_s16(O[i + 2]));
            t3 = vmlal_s16(t3, vget_high_s16(c0_c4), vget_high_s16(O[i + 3]));

            int32x4_t t = vpaddq_s32(vpaddq_s32(t0, t1), vpaddq_s32(t2, t3));
            int16x4_t res = vrshrn_n_s32(t, shift);
            vst1_s16(dst + k * line, res);
        }

        for (int k = 2; k < 16; k += 4)
        {
            int32x4_t c0 = vmovl_s16(vld1_s16(&g_t16[k][0]));
            int32x4_t t0 = vmulq_s32(c0, EO[i + 0]);
            int32x4_t t1 = vmulq_s32(c0, EO[i + 1]);
            int32x4_t t2 = vmulq_s32(c0, EO[i + 2]);
            int32x4_t t3 = vmulq_s32(c0, EO[i + 3]);
            int32x4_t t = vpaddq_s32(vpaddq_s32(t0, t1), vpaddq_s32(t2, t3));

            int16x4_t res = vrshrn_n_s32(t, shift);
            vst1_s16(dst + k * line, res);
        }

        int32x4_t c0 = vld1q_s32(t8_even[0]);
        int32x4_t c4 = vld1q_s32(t8_even[1]);
        int32x4_t c8 = vld1q_s32(t8_even[2]);
        int32x4_t c12 = vld1q_s32(t8_even[3]);

        int32x4_t t0 = vpaddq_s32(EEE[i / 2 + 0], EEE[i / 2 + 1]);
        int32x4_t t1 = vmulq_s32(c0, t0);
        int16x4_t res0 = vrshrn_n_s32(t1, shift);
        vst1_s16(dst + 0 * line, res0);

        int32x4_t t2 = vmulq_s32(c4, EEO[i / 2 + 0]);
        int32x4_t t3 = vmulq_s32(c4, EEO[i / 2 + 1]);
        int16x4_t res4 = vrshrn_n_s32(vpaddq_s32(t2, t3), shift);
        vst1_s16(dst + 4 * line, res4);

        int32x4_t t4 = vmulq_s32(c8, EEE[i / 2 + 0]);
        int32x4_t t5 = vmulq_s32(c8, EEE[i / 2 + 1]);
        int16x4_t res8 = vrshrn_n_s32(vpaddq_s32(t4, t5), shift);
        vst1_s16(dst + 8 * line, res8);

        int32x4_t t6 = vmulq_s32(c12, EEO[i / 2 + 0]);
        int32x4_t t7 = vmulq_s32(c12, EEO[i / 2 + 1]);
        int16x4_t res12 = vrshrn_n_s32(vpaddq_s32(t6, t7), shift);
        vst1_s16(dst + 12 * line, res12);

        dst += 4;
    }
}

template<int shift>
static inline void partialButterfly32_neon(const int16_t *src, int16_t *dst)
{
    const int line = 32;

    int16x8_t O[line][2];
    int32x4_t EO[line][2];
    int32x4_t EEO[line];
    int32x4_t EEEE[line / 2];
    int32x4_t EEEO[line / 2];

    for (int i = 0; i < line; i += 2)
    {
        int16x8x4_t in_lo = vld1q_s16_x4(src + (i + 0) * line);
        in_lo.val[2] = rev16(in_lo.val[2]);
        in_lo.val[3] = rev16(in_lo.val[3]);

        int16x8x4_t in_hi = vld1q_s16_x4(src + (i + 1) * line);
        in_hi.val[2] = rev16(in_hi.val[2]);
        in_hi.val[3] = rev16(in_hi.val[3]);

        int32x4_t E0[4];
        E0[0] = vaddl_s16(vget_low_s16(in_lo.val[0]),
                          vget_low_s16(in_lo.val[3]));
        E0[1] = vaddl_s16(vget_high_s16(in_lo.val[0]),
                          vget_high_s16(in_lo.val[3]));
        E0[2] = vaddl_s16(vget_low_s16(in_lo.val[1]),
                          vget_low_s16(in_lo.val[2]));
        E0[3] = vaddl_s16(vget_high_s16(in_lo.val[1]),
                          vget_high_s16(in_lo.val[2]));

        int32x4_t E1[4];
        E1[0] = vaddl_s16(vget_low_s16(in_hi.val[0]),
                          vget_low_s16(in_hi.val[3]));
        E1[1] = vaddl_s16(vget_high_s16(in_hi.val[0]),
                          vget_high_s16(in_hi.val[3]));
        E1[2] = vaddl_s16(vget_low_s16(in_hi.val[1]),
                          vget_low_s16(in_hi.val[2]));
        E1[3] = vaddl_s16(vget_high_s16(in_hi.val[1]),
                          vget_high_s16(in_hi.val[2]));

        O[i + 0][0] = vsubq_s16(in_lo.val[0], in_lo.val[3]);
        O[i + 0][1] = vsubq_s16(in_lo.val[1], in_lo.val[2]);

        O[i + 1][0] = vsubq_s16(in_hi.val[0], in_hi.val[3]);
        O[i + 1][1] = vsubq_s16(in_hi.val[1], in_hi.val[2]);

        int32x4_t EE0[2];
        E0[3] = rev32(E0[3]);
        E0[2] = rev32(E0[2]);
        EE0[0] = vaddq_s32(E0[0], E0[3]);
        EE0[1] = vaddq_s32(E0[1], E0[2]);
        EO[i + 0][0] = vsubq_s32(E0[0], E0[3]);
        EO[i + 0][1] = vsubq_s32(E0[1], E0[2]);

        int32x4_t EE1[2];
        E1[3] = rev32(E1[3]);
        E1[2] = rev32(E1[2]);
        EE1[0] = vaddq_s32(E1[0], E1[3]);
        EE1[1] = vaddq_s32(E1[1], E1[2]);
        EO[i + 1][0] = vsubq_s32(E1[0], E1[3]);
        EO[i + 1][1] = vsubq_s32(E1[1], E1[2]);

        int32x4_t EEE0;
        EE0[1] = rev32(EE0[1]);
        EEE0 = vaddq_s32(EE0[0], EE0[1]);
        EEO[i + 0] = vsubq_s32(EE0[0], EE0[1]);

        int32x4_t EEE1;
        EE1[1] = rev32(EE1[1]);
        EEE1 = vaddq_s32(EE1[0], EE1[1]);
        EEO[i + 1] = vsubq_s32(EE1[0], EE1[1]);

        int32x4_t t0 = vreinterpretq_s32_s64(
            vzip1q_s64(vreinterpretq_s64_s32(EEE0),
                       vreinterpretq_s64_s32(EEE1)));
        int32x4_t t1 = vrev64q_s32(vreinterpretq_s32_s64(
            vzip2q_s64(vreinterpretq_s64_s32(EEE0),
                       vreinterpretq_s64_s32(EEE1))));

        EEEE[i / 2] = vaddq_s32(t0, t1);
        EEEO[i / 2] = vsubq_s32(t0, t1);
    }

    for (int k = 1; k < 32; k += 2)
    {
        int16_t *d = dst + k * line;

        int16x8_t c0_c1 = vld1q_s16(&g_t32[k][0]);
        int16x8_t c2_c3 = vld1q_s16(&g_t32[k][8]);
        int16x4_t c0 = vget_low_s16(c0_c1);
        int16x4_t c1 = vget_high_s16(c0_c1);
        int16x4_t c2 = vget_low_s16(c2_c3);
        int16x4_t c3 = vget_high_s16(c2_c3);

        for (int i = 0; i < line; i += 4)
        {
            int32x4_t t[4];
            for (int j = 0; j < 4; ++j) {
                t[j] = vmull_s16(c0, vget_low_s16(O[i + j][0]));
                t[j] = vmlal_s16(t[j], c1, vget_high_s16(O[i + j][0]));
                t[j] = vmlal_s16(t[j], c2, vget_low_s16(O[i + j][1]));
                t[j] = vmlal_s16(t[j], c3, vget_high_s16(O[i + j][1]));
            }

            int32x4_t t0123 = vpaddq_s32(vpaddq_s32(t[0], t[1]),
                                         vpaddq_s32(t[2], t[3]));
            int16x4_t res = vrshrn_n_s32(t0123, shift);
            vst1_s16(d, res);

            d += 4;
        }
    }

    for (int k = 2; k < 32; k += 4)
    {
        int16_t *d = dst + k * line;

        int32x4_t c0 = vmovl_s16(vld1_s16(&g_t32[k][0]));
        int32x4_t c1 = vmovl_s16(vld1_s16(&g_t32[k][4]));

        for (int i = 0; i < line; i += 4)
        {
            int32x4_t t[4];
            for (int j = 0; j < 4; ++j) {
                t[j] = vmulq_s32(c0, EO[i + j][0]);
                t[j] = vmlaq_s32(t[j], c1, EO[i + j][1]);
            }

            int32x4_t t0123 = vpaddq_s32(vpaddq_s32(t[0], t[1]),
                                         vpaddq_s32(t[2], t[3]));
            int16x4_t res = vrshrn_n_s32(t0123, shift);
            vst1_s16(d, res);

            d += 4;
        }
    }

    for (int k = 4; k < 32; k += 8)
    {
        int16_t *d = dst + k * line;

        int32x4_t c = vmovl_s16(vld1_s16(&g_t32[k][0]));

        for (int i = 0; i < line; i += 4)
        {
            int32x4_t t0 = vmulq_s32(c, EEO[i + 0]);
            int32x4_t t1 = vmulq_s32(c, EEO[i + 1]);
            int32x4_t t2 = vmulq_s32(c, EEO[i + 2]);
            int32x4_t t3 = vmulq_s32(c, EEO[i + 3]);

            int32x4_t t = vpaddq_s32(vpaddq_s32(t0, t1), vpaddq_s32(t2, t3));
            int16x4_t res = vrshrn_n_s32(t, shift);
            vst1_s16(d, res);

            d += 4;
        }
    }

    int32x4_t c0 = vld1q_s32(t8_even[0]);
    int32x4_t c8 = vld1q_s32(t8_even[1]);
    int32x4_t c16 = vld1q_s32(t8_even[2]);
    int32x4_t c24 = vld1q_s32(t8_even[3]);

    for (int i = 0; i < line; i += 4)
    {
        int32x4_t t0 = vpaddq_s32(EEEE[i / 2 + 0], EEEE[i / 2 + 1]);
        int32x4_t t1 = vmulq_s32(c0, t0);
        int16x4_t res0 = vrshrn_n_s32(t1, shift);
        vst1_s16(dst + 0 * line, res0);

        int32x4_t t2 = vmulq_s32(c8, EEEO[i / 2 + 0]);
        int32x4_t t3 = vmulq_s32(c8, EEEO[i / 2 + 1]);
        int16x4_t res8 = vrshrn_n_s32(vpaddq_s32(t2, t3), shift);
        vst1_s16(dst + 8 * line, res8);

        int32x4_t t4 = vmulq_s32(c16, EEEE[i / 2 + 0]);
        int32x4_t t5 = vmulq_s32(c16, EEEE[i / 2 + 1]);
        int16x4_t res16 = vrshrn_n_s32(vpaddq_s32(t4, t5), shift);
        vst1_s16(dst + 16 * line, res16);

        int32x4_t t6 = vmulq_s32(c24, EEEO[i / 2 + 0]);
        int32x4_t t7 = vmulq_s32(c24, EEEO[i / 2 + 1]);
        int16x4_t res24 = vrshrn_n_s32(vpaddq_s32(t6, t7), shift);
        vst1_s16(dst + 24 * line, res24);

        dst += 4;
    }
}

template<int shift>
static inline void partialButterfly8_neon(const int16_t *src, int16_t *dst)
{
    const int line = 8;

    int16x4_t O[line];
    int32x4_t EE[line / 2];
    int32x4_t EO[line / 2];

    for (int i = 0; i < line; i += 2)
    {
        int16x4_t s0_lo = vld1_s16(src + i * line);
        int16x4_t s0_hi = vrev64_s16(vld1_s16(src + i * line + 4));

        int16x4_t s1_lo = vld1_s16(src + (i + 1) * line);
        int16x4_t s1_hi = vrev64_s16(vld1_s16(src + (i + 1) * line + 4));

        int32x4_t E0 = vaddl_s16(s0_lo, s0_hi);
        int32x4_t E1 = vaddl_s16(s1_lo, s1_hi);

        O[i + 0] = vsub_s16(s0_lo, s0_hi);
        O[i + 1] = vsub_s16(s1_lo, s1_hi);

        int32x4_t t0 = vreinterpretq_s32_s64(
            vzip1q_s64(vreinterpretq_s64_s32(E0), vreinterpretq_s64_s32(E1)));
        int32x4_t t1 = vrev64q_s32(vreinterpretq_s32_s64(
            vzip2q_s64(vreinterpretq_s64_s32(E0), vreinterpretq_s64_s32(E1))));

        EE[i / 2] = vaddq_s32(t0, t1);
        EO[i / 2] = vsubq_s32(t0, t1);
    }

    int16_t *d = dst;

    int32x4_t c0 = vld1q_s32(t8_even[0]);
    int32x4_t c2 = vld1q_s32(t8_even[1]);
    int32x4_t c4 = vld1q_s32(t8_even[2]);
    int32x4_t c6 = vld1q_s32(t8_even[3]);
    int16x4_t c1 = vld1_s16(g_t8[1]);
    int16x4_t c3 = vld1_s16(g_t8[3]);
    int16x4_t c5 = vld1_s16(g_t8[5]);
    int16x4_t c7 = vld1_s16(g_t8[7]);

    for (int j = 0; j < line; j += 4)
    {
        // O
        int32x4_t t01 = vpaddq_s32(vmull_s16(c1, O[j + 0]),
                                   vmull_s16(c1, O[j + 1]));
        int32x4_t t23 = vpaddq_s32(vmull_s16(c1, O[j + 2]),
                                   vmull_s16(c1, O[j + 3]));
        int16x4_t res1 = vrshrn_n_s32(vpaddq_s32(t01, t23), shift);
        vst1_s16(d + 1 * line, res1);

        t01 = vpaddq_s32(vmull_s16(c3, O[j + 0]), vmull_s16(c3, O[j + 1]));
        t23 = vpaddq_s32(vmull_s16(c3, O[j + 2]), vmull_s16(c3, O[j + 3]));
        int16x4_t res3 = vrshrn_n_s32(vpaddq_s32(t01, t23), shift);
        vst1_s16(d + 3 * line, res3);

        t01 = vpaddq_s32(vmull_s16(c5, O[j + 0]), vmull_s16(c5, O[j + 1]));
        t23 = vpaddq_s32(vmull_s16(c5, O[j + 2]), vmull_s16(c5, O[j + 3]));
        int16x4_t res5 = vrshrn_n_s32(vpaddq_s32(t01, t23), shift);
        vst1_s16(d + 5 * line, res5);

        t01 = vpaddq_s32(vmull_s16(c7, O[j + 0]), vmull_s16(c7, O[j + 1]));
        t23 = vpaddq_s32(vmull_s16(c7, O[j + 2]), vmull_s16(c7, O[j + 3]));
        int16x4_t res7 = vrshrn_n_s32(vpaddq_s32(t01, t23), shift);
        vst1_s16(d + 7 * line, res7);

        // EE and EO
        int32x4_t t0 = vpaddq_s32(EE[j / 2 + 0], EE[j / 2 + 1]);
        int32x4_t t1 = vmulq_s32(c0, t0);
        int16x4_t res0 = vrshrn_n_s32(t1, shift);
        vst1_s16(d + 0 * line, res0);

        int32x4_t t2 = vmulq_s32(c2, EO[j / 2 + 0]);
        int32x4_t t3 = vmulq_s32(c2, EO[j / 2 + 1]);
        int16x4_t res2 = vrshrn_n_s32(vpaddq_s32(t2, t3), shift);
        vst1_s16(d + 2 * line, res2);

        int32x4_t t4 = vmulq_s32(c4, EE[j / 2 + 0]);
        int32x4_t t5 = vmulq_s32(c4, EE[j / 2 + 1]);
        int16x4_t res4 = vrshrn_n_s32(vpaddq_s32(t4, t5), shift);
        vst1_s16(d + 4 * line, res4);

        int32x4_t t6 = vmulq_s32(c6, EO[j / 2 + 0]);
        int32x4_t t7 = vmulq_s32(c6, EO[j / 2 + 1]);
        int16x4_t res6 = vrshrn_n_s32(vpaddq_s32(t6, t7), shift);
        vst1_s16(d + 6 * line, res6);

        d += 4;
    }
}

template<int shift>
static inline void partialButterflyInverse4_neon(const int16_t *src, int16_t *dst,
                                                 intptr_t dstStride)
{
    int16x4_t s0 = vld1_s16(src + 0);
    int16x4_t s1 = vld1_s16(src + 4);
    int16x4_t s2 = vld1_s16(src + 8);
    int16x4_t s3 = vld1_s16(src + 12);

    // Multiply and accumulate with g_t4 constants.
    int32x4_t O[2];
    O[0] = vmull_n_s16(s1, 83);
    O[0] = vmlal_n_s16(O[0], s3, 36);
    O[1] = vmull_n_s16(s1, 36);
    O[1] = vmlal_n_s16(O[1], s3, -83);

    int32x4_t E[2];
    E[0] = vaddl_s16(s0, s2);
    E[0] = vmulq_n_s32(E[0], 64);
    E[1] = vsubl_s16(s0, s2);
    E[1] = vmulq_n_s32(E[1], 64);

    int32x4_t t0 = vaddq_s32(E[0], O[0]);
    int32x4_t t1 = vaddq_s32(E[1], O[1]);
    int32x4_t t2 = vsubq_s32(E[1], O[1]);
    int32x4_t t3 = vsubq_s32(E[0], O[0]);

    int16x4_t d0 = vqrshrn_n_s32(t0, shift);
    int16x4_t d1 = vqrshrn_n_s32(t1, shift);
    int16x4_t d2 = vqrshrn_n_s32(t2, shift);
    int16x4_t d3 = vqrshrn_n_s32(t3, shift);

    transpose_4x4_s16(d0, d1, d2, d3);

    vst1_s16(dst + 0 * dstStride, d0);
    vst1_s16(dst + 1 * dstStride, d1);
    vst1_s16(dst + 2 * dstStride, d2);
    vst1_s16(dst + 3 * dstStride, d3);
}

template<int shift>
static inline void partialButterflyInverse8_neon(const int16_t *src, int16_t *dst,
                                                 intptr_t dstStride)
{
    const int line = 8;

    const int16x8_t s0 = vld1q_s16(src + 0 * line);
    const int16x8_t s1 = vld1q_s16(src + 1 * line);
    const int16x8_t s2 = vld1q_s16(src + 2 * line);
    const int16x8_t s3 = vld1q_s16(src + 3 * line);
    const int16x8_t s4 = vld1q_s16(src + 4 * line);
    const int16x8_t s5 = vld1q_s16(src + 5 * line);
    const int16x8_t s6 = vld1q_s16(src + 6 * line);
    const int16x8_t s7 = vld1q_s16(src + 7 * line);

    int32x4_t O_lo[4], O_hi[4];
    const int16x4_t c_odd = vld1_s16(g_t8[1]);
    O_lo[0] = vmull_lane_s16(vget_low_s16(s1), c_odd, 0); // 89
    O_lo[1] = vmull_lane_s16(vget_low_s16(s1), c_odd, 1); // 75
    O_lo[2] = vmull_lane_s16(vget_low_s16(s1), c_odd, 2); // 50
    O_lo[3] = vmull_lane_s16(vget_low_s16(s1), c_odd, 3); // 18

    O_hi[0] = vmull_lane_s16(vget_high_s16(s1), c_odd, 0); // 89
    O_hi[1] = vmull_lane_s16(vget_high_s16(s1), c_odd, 1); // 75
    O_hi[2] = vmull_lane_s16(vget_high_s16(s1), c_odd, 2); // 50
    O_hi[3] = vmull_lane_s16(vget_high_s16(s1), c_odd, 3); // 18

    if (vaddlvq_u32(vreinterpretq_u32_s16(s3)) != 0)
    {
        O_lo[0] = vmlal_lane_s16(O_lo[0], vget_low_s16(s3), c_odd, 1); //  75
        O_lo[1] = vmlsl_lane_s16(O_lo[1], vget_low_s16(s3), c_odd, 3); // -18
        O_lo[2] = vmlsl_lane_s16(O_lo[2], vget_low_s16(s3), c_odd, 0); // -89
        O_lo[3] = vmlsl_lane_s16(O_lo[3], vget_low_s16(s3), c_odd, 2); // -50

        O_hi[0] = vmlal_lane_s16(O_hi[0], vget_high_s16(s3), c_odd, 1); //  75
        O_hi[1] = vmlsl_lane_s16(O_hi[1], vget_high_s16(s3), c_odd, 3); // -18
        O_hi[2] = vmlsl_lane_s16(O_hi[2], vget_high_s16(s3), c_odd, 0); // -89
        O_hi[3] = vmlsl_lane_s16(O_hi[3], vget_high_s16(s3), c_odd, 2); // -50
    }

    if (vaddlvq_u32(vreinterpretq_u32_s16(s5)) != 0)
    {
        O_lo[0] = vmlal_lane_s16(O_lo[0], vget_low_s16(s5), c_odd, 2); //  50
        O_lo[1] = vmlsl_lane_s16(O_lo[1], vget_low_s16(s5), c_odd, 0); // -89
        O_lo[2] = vmlal_lane_s16(O_lo[2], vget_low_s16(s5), c_odd, 3); //  18
        O_lo[3] = vmlal_lane_s16(O_lo[3], vget_low_s16(s5), c_odd, 1); //  75

        O_hi[0] = vmlal_lane_s16(O_hi[0], vget_high_s16(s5), c_odd, 2); //  50
        O_hi[1] = vmlsl_lane_s16(O_hi[1], vget_high_s16(s5), c_odd, 0); // -89
        O_hi[2] = vmlal_lane_s16(O_hi[2], vget_high_s16(s5), c_odd, 3); //  18
        O_hi[3] = vmlal_lane_s16(O_hi[3], vget_high_s16(s5), c_odd, 1); //  75
    }

    if (vaddlvq_u32(vreinterpretq_u32_s16(s7)) != 0)
    {
        O_lo[0] = vmlal_lane_s16(O_lo[0], vget_low_s16(s7), c_odd, 3); //  18
        O_lo[1] = vmlsl_lane_s16(O_lo[1], vget_low_s16(s7), c_odd, 2); // -50
        O_lo[2] = vmlal_lane_s16(O_lo[2], vget_low_s16(s7), c_odd, 1); //  75
        O_lo[3] = vmlsl_lane_s16(O_lo[3], vget_low_s16(s7), c_odd, 0); // -89

        O_hi[0] = vmlal_lane_s16(O_hi[0], vget_high_s16(s7), c_odd, 3); //  18
        O_hi[1] = vmlsl_lane_s16(O_hi[1], vget_high_s16(s7), c_odd, 2); // -50
        O_hi[2] = vmlal_lane_s16(O_hi[2], vget_high_s16(s7), c_odd, 1); //  75
        O_hi[3] = vmlsl_lane_s16(O_hi[3], vget_high_s16(s7), c_odd, 0); // -89
    }

    int32x4_t EO_lo[2], EO_hi[2];
    const int16x4_t c_even = vld1_s16(g_t8[2]);
    EO_lo[0] = vmull_lane_s16(vget_low_s16(s2), c_even, 0); // 83
    EO_lo[1] = vmull_lane_s16(vget_low_s16(s2), c_even, 1); // 36

    EO_hi[0] = vmull_lane_s16(vget_high_s16(s2), c_even, 0); // 83
    EO_hi[1] = vmull_lane_s16(vget_high_s16(s2), c_even, 1); // 36

    EO_lo[0] = vmlal_lane_s16(EO_lo[0], vget_low_s16(s6), c_even, 1); //  36
    EO_lo[1] = vmlsl_lane_s16(EO_lo[1], vget_low_s16(s6), c_even, 0); // -83

    EO_hi[0] = vmlal_lane_s16(EO_hi[0], vget_high_s16(s6), c_even, 1); //  36
    EO_hi[1] = vmlsl_lane_s16(EO_hi[1], vget_high_s16(s6), c_even, 0); // -83

    // Replace multiply by 64 with left shift by 6.
    int32x4_t EE_lo[2], EE_hi[2];
    EE_lo[0] = vshlq_n_s32(vaddl_s16(vget_low_s16(s0), vget_low_s16(s4)), 6);
    EE_hi[0] = vshlq_n_s32(vaddl_s16(vget_high_s16(s0), vget_high_s16(s4)), 6);

    EE_lo[1] = vshll_n_s16(vget_low_s16(vsubq_s16(s0, s4)), 6);
    EE_hi[1] = vshll_n_s16(vget_high_s16(vsubq_s16(s0, s4)), 6);

    int32x4_t E_lo[4], E_hi[4];
    E_lo[0] = vaddq_s32(EE_lo[0], EO_lo[0]);
    E_lo[1] = vaddq_s32(EE_lo[1], EO_lo[1]);
    E_lo[2] = vsubq_s32(EE_lo[1], EO_lo[1]);
    E_lo[3] = vsubq_s32(EE_lo[0], EO_lo[0]);

    E_hi[0] = vaddq_s32(EE_hi[0], EO_hi[0]);
    E_hi[1] = vaddq_s32(EE_hi[1], EO_hi[1]);
    E_hi[2] = vsubq_s32(EE_hi[1], EO_hi[1]);
    E_hi[3] = vsubq_s32(EE_hi[0], EO_hi[0]);

    int16x4_t d_lo[8], d_hi[8];

    for (int i = 0; i < 4; i++)
    {
        int32x4_t t_lo = vaddq_s32(E_lo[i], O_lo[i]);
        int32x4_t t_hi = vaddq_s32(E_hi[i], O_hi[i]);
        d_lo[i + 0] = vqrshrn_n_s32(t_lo, shift);
        d_hi[i + 0] = vqrshrn_n_s32(t_hi, shift);

        t_lo = vsubq_s32(E_lo[3 - i], O_lo[3 - i]);
        t_hi = vsubq_s32(E_hi[3 - i], O_hi[3 - i]);
        d_lo[i + 4] = vqrshrn_n_s32(t_lo, shift);
        d_hi[i + 4] = vqrshrn_n_s32(t_hi, shift);
    }

    int16x8_t d0, d1, d2, d3, d4, d5, d6, d7;
    transpose_4x8_s16(d_lo[0], d_lo[1], d_lo[2], d_lo[3], d_lo[4], d_lo[5], d_lo[6], d_lo[7],
                      d0, d1, d2, d3);

    transpose_4x8_s16(d_hi[0], d_hi[1], d_hi[2], d_hi[3], d_hi[4], d_hi[5], d_hi[6], d_hi[7],
                      d4, d5, d6, d7);

    vst1q_s16(dst + 0 * dstStride, d0);
    vst1q_s16(dst + 1 * dstStride, d1);
    vst1q_s16(dst + 2 * dstStride, d2);
    vst1q_s16(dst + 3 * dstStride, d3);
    vst1q_s16(dst + 4 * dstStride, d4);
    vst1q_s16(dst + 5 * dstStride, d5);
    vst1q_s16(dst + 6 * dstStride, d6);
    vst1q_s16(dst + 7 * dstStride, d7);
}

template<int shift>
static inline void partialButterflyInverse16_neon(const int16_t *src, int16_t *dst,
                                                  intptr_t dstStride)
{
    const int line = 16;

    for (int i = 0; i < 4; i++)
    {
        int32x4_t EEE[2];
        const int16x4_t s0 = vld1_s16(src + 0 * line + 4 * i);
        const int16x4_t s8 = vld1_s16(src + 8 * line + 4 * i);
        // Replace multiply by 64 with left shift by 6.
        EEE[0] = vshlq_n_s32(vaddl_s16(s0, s8), 6);
        EEE[1] = vshlq_n_s32(vsubl_s16(s0, s8), 6);

        int32x4_t EEO[2];
        const int16x4_t c4_even = vld1_s16(g_t16[4]);
        const int16x4_t s4 = vld1_s16(src + 4 * line + 4 * i);
        EEO[0] = vmull_lane_s16(s4, c4_even, 0); // 83
        EEO[1] = vmull_lane_s16(s4, c4_even, 1); // 36

        const int16x4_t s12 = vld1_s16(src + 12 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s12), 0) != 0)
        {
            EEO[0] = vmlal_lane_s16(EEO[0], s12, c4_even, 1); //  36
            EEO[1] = vmlsl_lane_s16(EEO[1], s12, c4_even, 0); // -83
        }

        int32x4_t EE[4];
        for (int j = 0; j < 2; j++)
        {
            EE[j] = vaddq_s32(EEE[j], EEO[j]);
            EE[j + 2] = vsubq_s32(EEE[1 - j], EEO[1 - j]);
        }

        int32x4_t EO[4];
        const int16x4_t c2_even = vld1_s16(g_t16[2]);
        const int16x4_t s2 = vld1_s16(src + 2 * line + 4 * i);
        EO[0] = vmull_lane_s16(s2, c2_even, 0); // 89
        EO[1] = vmull_lane_s16(s2, c2_even, 1); // 75
        EO[2] = vmull_lane_s16(s2, c2_even, 2); // 50
        EO[3] = vmull_lane_s16(s2, c2_even, 3); // 18

        const int16x4_t s6 = vld1_s16(src + 6 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s6), 0) != 0)
        {
            EO[0] = vmlal_lane_s16(EO[0], s6, c2_even, 1); //  75
            EO[1] = vmlsl_lane_s16(EO[1], s6, c2_even, 3); // -18
            EO[2] = vmlsl_lane_s16(EO[2], s6, c2_even, 0); // -89
            EO[3] = vmlsl_lane_s16(EO[3], s6, c2_even, 2); // -50
        }

        const int16x4_t s10 = vld1_s16(src + 10 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s10), 0) != 0)
        {
            EO[0] = vmlal_lane_s16(EO[0], s10, c2_even, 2); //  50
            EO[1] = vmlsl_lane_s16(EO[1], s10, c2_even, 0); // -89
            EO[2] = vmlal_lane_s16(EO[2], s10, c2_even, 3); //  18
            EO[3] = vmlal_lane_s16(EO[3], s10, c2_even, 1); //  75
        }

        const int16x4_t s14 = vld1_s16(src + 14 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s14), 0) != 0)
        {
            EO[0] = vmlal_lane_s16(EO[0], s14, c2_even, 3); //  18
            EO[1] = vmlsl_lane_s16(EO[1], s14, c2_even, 2); // -50
            EO[2] = vmlal_lane_s16(EO[2], s14, c2_even, 1); //  75
            EO[3] = vmlsl_lane_s16(EO[3], s14, c2_even, 0); // -89
        }

        int32x4_t E[8];
        for (int j = 0; j < 4; j++)
        {
            E[j] = vaddq_s32(EE[j], EO[j]);
            E[j + 4] = vsubq_s32(EE[3 - j], EO[3 - j]);
        }

        int32x4_t O[8];
        const int16x8_t c_odd = vld1q_s16(g_t16[1]);
        const int16x4_t s1 = vld1_s16(src + 1 * line + 4 * i);
        O[0] = vmull_laneq_s16(s1, c_odd, 0); // 90
        O[1] = vmull_laneq_s16(s1, c_odd, 1); // 87
        O[2] = vmull_laneq_s16(s1, c_odd, 2); // 80
        O[3] = vmull_laneq_s16(s1, c_odd, 3); // 70
        O[4] = vmull_laneq_s16(s1, c_odd, 4); // 57
        O[5] = vmull_laneq_s16(s1, c_odd, 5); // 43
        O[6] = vmull_laneq_s16(s1, c_odd, 6); // 25
        O[7] = vmull_laneq_s16(s1, c_odd, 7); //  9

        const int16x4_t s3 = vld1_s16(src + 3 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s3), 0) != 0)
        {
            O[0] = vmlal_laneq_s16(O[0], s3, c_odd, 1); //  87
            O[1] = vmlal_laneq_s16(O[1], s3, c_odd, 4); //  57
            O[2] = vmlal_laneq_s16(O[2], s3, c_odd, 7); //   9
            O[3] = vmlsl_laneq_s16(O[3], s3, c_odd, 5); // -43
            O[4] = vmlsl_laneq_s16(O[4], s3, c_odd, 2); // -80
            O[5] = vmlsl_laneq_s16(O[5], s3, c_odd, 0); // -90
            O[6] = vmlsl_laneq_s16(O[6], s3, c_odd, 3); // -70
            O[7] = vmlsl_laneq_s16(O[7], s3, c_odd, 6); // -25
        }

        const int16x4_t s5 = vld1_s16(src + 5 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s5), 0) != 0)
        {
            O[0] = vmlal_laneq_s16(O[0], s5, c_odd, 2); //  80
            O[1] = vmlal_laneq_s16(O[1], s5, c_odd, 7); //   9
            O[2] = vmlsl_laneq_s16(O[2], s5, c_odd, 3); // -70
            O[3] = vmlsl_laneq_s16(O[3], s5, c_odd, 1); // -87
            O[4] = vmlsl_laneq_s16(O[4], s5, c_odd, 6); // -25
            O[5] = vmlal_laneq_s16(O[5], s5, c_odd, 4); //  57
            O[6] = vmlal_laneq_s16(O[6], s5, c_odd, 0); //  90
            O[7] = vmlal_laneq_s16(O[7], s5, c_odd, 5); //  43
        }

        const int16x4_t s7 = vld1_s16(src + 7 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s7), 0) != 0)
        {
            O[0] = vmlal_laneq_s16(O[0], s7, c_odd, 3); //  70
            O[1] = vmlsl_laneq_s16(O[1], s7, c_odd, 5); // -43
            O[2] = vmlsl_laneq_s16(O[2], s7, c_odd, 1); // -87
            O[3] = vmlal_laneq_s16(O[3], s7, c_odd, 7); //   9
            O[4] = vmlal_laneq_s16(O[4], s7, c_odd, 0); //  90
            O[5] = vmlal_laneq_s16(O[5], s7, c_odd, 6); //  25
            O[6] = vmlsl_laneq_s16(O[6], s7, c_odd, 2); // -80
            O[7] = vmlsl_laneq_s16(O[7], s7, c_odd, 4); // -57
        }

        const int16x4_t s9 = vld1_s16(src + 9 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s9), 0) != 0)
        {
            O[0] = vmlal_laneq_s16(O[0], s9, c_odd, 4); //  57
            O[1] = vmlsl_laneq_s16(O[1], s9, c_odd, 2); // -80
            O[2] = vmlsl_laneq_s16(O[2], s9, c_odd, 6); // -25
            O[3] = vmlal_laneq_s16(O[3], s9, c_odd, 0); //  90
            O[4] = vmlsl_laneq_s16(O[4], s9, c_odd, 7); //  -9
            O[5] = vmlsl_laneq_s16(O[5], s9, c_odd, 1); // -87
            O[6] = vmlal_laneq_s16(O[6], s9, c_odd, 5); //  43
            O[7] = vmlal_laneq_s16(O[7], s9, c_odd, 3); //  70
        }

        const int16x4_t s11 = vld1_s16(src + 11 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s11), 0) != 0)
        {
            O[0] = vmlal_laneq_s16(O[0], s11, c_odd, 5); //  43
            O[1] = vmlsl_laneq_s16(O[1], s11, c_odd, 0); // -90
            O[2] = vmlal_laneq_s16(O[2], s11, c_odd, 4); //  57
            O[3] = vmlal_laneq_s16(O[3], s11, c_odd, 6); //  25
            O[4] = vmlsl_laneq_s16(O[4], s11, c_odd, 1); // -87
            O[5] = vmlal_laneq_s16(O[5], s11, c_odd, 3); //  70
            O[6] = vmlal_laneq_s16(O[6], s11, c_odd, 7); //   9
            O[7] = vmlsl_laneq_s16(O[7], s11, c_odd, 2); // -80
        }

        const int16x4_t s13 = vld1_s16(src + 13 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s13), 0) != 0)
        {
            O[0] = vmlal_laneq_s16(O[0], s13, c_odd, 6); //  25
            O[1] = vmlsl_laneq_s16(O[1], s13, c_odd, 3); // -70
            O[2] = vmlal_laneq_s16(O[2], s13, c_odd, 0); //  90
            O[3] = vmlsl_laneq_s16(O[3], s13, c_odd, 2); // -80
            O[4] = vmlal_laneq_s16(O[4], s13, c_odd, 5); //  43
            O[5] = vmlal_laneq_s16(O[5], s13, c_odd, 7); //   9
            O[6] = vmlsl_laneq_s16(O[6], s13, c_odd, 4); // -57
            O[7] = vmlal_laneq_s16(O[7], s13, c_odd, 1); //  87
        }

        const int16x4_t s15 = vld1_s16(src + 15 * line + 4 * i);
        if (vget_lane_u64(vreinterpret_u64_s16(s15), 0) != 0)
        {
            O[0] = vmlal_laneq_s16(O[0], s15, c_odd, 7); //   9
            O[1] = vmlsl_laneq_s16(O[1], s15, c_odd, 6); // -25
            O[2] = vmlal_laneq_s16(O[2], s15, c_odd, 5); //  43
            O[3] = vmlsl_laneq_s16(O[3], s15, c_odd, 4); // -57
            O[4] = vmlal_laneq_s16(O[4], s15, c_odd, 3); //  70
            O[5] = vmlsl_laneq_s16(O[5], s15, c_odd, 2); // -80
            O[6] = vmlal_laneq_s16(O[6], s15, c_odd, 1); //  87
            O[7] = vmlsl_laneq_s16(O[7], s15, c_odd, 0); // -90
        }

        int16x4_t d_lo[8];
        int16x4_t d_hi[8];
        for (int j = 0; j < 8; j++)
        {
            int32x4_t t_lo = vaddq_s32(E[j], O[j]);
            d_lo[j] = vqrshrn_n_s32(t_lo, shift);

            int32x4_t t_hi = vsubq_s32(E[7 - j], O[7 - j]);
            d_hi[j] = vqrshrn_n_s32(t_hi, shift);
        }

        int16x8_t d0_lo, d1_lo, d2_lo, d3_lo;
        int16x8_t d0_hi, d1_hi, d2_hi, d3_hi;
        transpose_4x8_s16(d_lo[0], d_lo[1], d_lo[2], d_lo[3], d_lo[4], d_lo[5], d_lo[6], d_lo[7],
                          d0_lo, d1_lo, d2_lo, d3_lo);
        transpose_4x8_s16(d_hi[0], d_hi[1], d_hi[2], d_hi[3], d_hi[4], d_hi[5], d_hi[6], d_hi[7],
                          d0_hi, d1_hi, d2_hi, d3_hi);

        vst1q_s16(dst + (4 * i + 0) * dstStride + 8 * 0, d0_lo);
        vst1q_s16(dst + (4 * i + 0) * dstStride + 8 * 1, d0_hi);

        vst1q_s16(dst + (4 * i + 1) * dstStride + 8 * 0, d1_lo);
        vst1q_s16(dst + (4 * i + 1) * dstStride + 8 * 1, d1_hi);

        vst1q_s16(dst + (4 * i + 2) * dstStride + 8 * 0, d2_lo);
        vst1q_s16(dst + (4 * i + 2) * dstStride + 8 * 1, d2_hi);

        vst1q_s16(dst + (4 * i + 3) * dstStride + 8 * 0, d3_lo);
        vst1q_s16(dst + (4 * i + 3) * dstStride + 8 * 1, d3_hi);
    }
}



static void partialButterflyInverse32_neon(const int16_t *src, int16_t *orig_dst, int shift, int line)
{
#define MUL(x) vmull_s16(vdup_n_s16(g_t32[x][k]),vld1_s16(&src[x*line]));
#define FMA(x) s = vmlal_s16(s,vdup_n_s16(g_t32[x][k]),vld1_s16(&src[x*line]));
#define FMAK(x,l) s[l] = vmlal_lane_s16(s[l],vld1_s16(&src[x*line]),vld1_s16(&g_t32[x][k]),l);
#define MULK(x,l) vmull_lane_s16(vld1_s16(&src[x*line]),vld1_s16(&g_t32[x][k]),l);
#define ODD31(k) FMAK(3,k);FMAK(5,k);FMAK(7,k);FMAK(9,k);FMAK(11,k);FMAK(13,k);FMAK(15,k);FMAK(17,k);FMAK(19,k);FMAK(21,k);FMAK(23,k);FMAK(25,k);FMAK(27,k);FMAK(29,k);FMAK(31,k);

#define ODD15(k) FMAK(6,k);FMAK(10,k);FMAK(14,k);FMAK(18,k);FMAK(22,k);FMAK(26,k);FMAK(30,k);
#define ODD7(k) FMAK(12,k);FMAK(20,k);FMAK(28,k);


    int j, k;
    int32x4_t E[16], O[16];
    int32x4_t EE[8], EO[8];
    int32x4_t EEE[4], EEO[4];
    int32x4_t EEEE[2], EEEO[2];
    int16x4_t dst[32];
    int add = 1 << (shift - 1);

X265_PRAGMA_UNROLL(8)
    for (j = 0; j < line; j += 4)
    {
X265_PRAGMA_UNROLL(4)
        for (k = 0; k < 16; k += 4)
        {
            int32x4_t s[4];
            s[0] = MULK(1, 0);
            s[1] = MULK(1, 1);
            s[2] = MULK(1, 2);
            s[3] = MULK(1, 3);
            ODD31(0);
            ODD31(1);
            ODD31(2);
            ODD31(3);
            O[k] = s[0];
            O[k + 1] = s[1];
            O[k + 2] = s[2];
            O[k + 3] = s[3];


        }


X265_PRAGMA_UNROLL(2)
        for (k = 0; k < 8; k += 4)
        {
            int32x4_t s[4];
            s[0] = MULK(2, 0);
            s[1] = MULK(2, 1);
            s[2] = MULK(2, 2);
            s[3] = MULK(2, 3);

            ODD15(0);
            ODD15(1);
            ODD15(2);
            ODD15(3);

            EO[k] = s[0];
            EO[k + 1] = s[1];
            EO[k + 2] = s[2];
            EO[k + 3] = s[3];
        }


        for (k = 0; k < 4; k += 4)
        {
            int32x4_t s[4];
            s[0] = MULK(4, 0);
            s[1] = MULK(4, 1);
            s[2] = MULK(4, 2);
            s[3] = MULK(4, 3);

            ODD7(0);
            ODD7(1);
            ODD7(2);
            ODD7(3);

            EEO[k] = s[0];
            EEO[k + 1] = s[1];
            EEO[k + 2] = s[2];
            EEO[k + 3] = s[3];
        }

X265_PRAGMA_UNROLL(2)
        for (k = 0; k < 2; k++)
        {
            int32x4_t s;
            s = MUL(8);
            EEEO[k] = FMA(24);
            s = MUL(0);
            EEEE[k] = FMA(16);
        }
        /* Combining even and odd terms at each hierarchy levels to calculate the final spatial domain vector */
        EEE[0] = vaddq_s32(EEEE[0], EEEO[0]);
        EEE[3] = vsubq_s32(EEEE[0], EEEO[0]);
        EEE[1] = vaddq_s32(EEEE[1], EEEO[1]);
        EEE[2] = vsubq_s32(EEEE[1], EEEO[1]);

X265_PRAGMA_UNROLL(4)
        for (k = 0; k < 4; k++)
        {
            EE[k] = vaddq_s32(EEE[k], EEO[k]);
            EE[k + 4] = vsubq_s32((EEE[3 - k]), (EEO[3 - k]));
        }

X265_PRAGMA_UNROLL(8)
        for (k = 0; k < 8; k++)
        {
            E[k] = vaddq_s32(EE[k], EO[k]);
            E[k + 8] = vsubq_s32((EE[7 - k]), (EO[7 - k]));
        }

        static const int32x4_t min = vdupq_n_s32(-32768);
        static const int32x4_t max = vdupq_n_s32(32767);



X265_PRAGMA_UNROLL(16)
        for (k = 0; k < 16; k++)
        {
            int32x4_t adde = vaddq_s32(vdupq_n_s32(add), E[k]);
            int32x4_t s = vaddq_s32(adde, O[k]);
            s = vshlq_s32(s, vdupq_n_s32(-shift));
            s = vmaxq_s32(s, min);
            s = vminq_s32(s, max);



            dst[k] = vmovn_s32(s);
            adde = vaddq_s32(vdupq_n_s32(add), (E[15 - k]));
            s  = vsubq_s32(adde, (O[15 - k]));
            s = vshlq_s32(s, vdupq_n_s32(-shift));
            s = vmaxq_s32(s, min);
            s = vminq_s32(s, max);

            dst[k + 16] = vmovn_s32(s);
        }


X265_PRAGMA_UNROLL(8)
        for (k = 0; k < 32; k += 4)
        {
            int16x4_t x0 = dst[k + 0];
            int16x4_t x1 = dst[k + 1];
            int16x4_t x2 = dst[k + 2];
            int16x4_t x3 = dst[k + 3];
            transpose_4x4_s16(x0, x1, x2, x3);
            vst1_s16(&orig_dst[0 * 32 + k], x0);
            vst1_s16(&orig_dst[1 * 32 + k], x1);
            vst1_s16(&orig_dst[2 * 32 + k], x2);
            vst1_s16(&orig_dst[3 * 32 + k], x3);
        }
        orig_dst += 4 * 32;
        src += 4;
    }
#undef MUL
#undef FMA
#undef FMAK
#undef MULK
#undef ODD31
#undef ODD15
#undef ODD7

}


}

namespace X265_NS
{
// x265 private namespace
void dst4_neon(const int16_t *src, int16_t *dst, intptr_t srcStride)
{
    const int shift_pass1 = 1 + X265_DEPTH - 8;
    const int shift_pass2 = 8;

    ALIGN_VAR_32(int16_t, coef[4 * 4]);
    ALIGN_VAR_32(int16_t, block[4 * 4]);

    for (int i = 0; i < 4; i++)
    {
        memcpy(&block[i * 4], &src[i * srcStride], 4 * sizeof(int16_t));
    }

    fastForwardDst4_neon<shift_pass1>(block, coef);
    fastForwardDst4_neon<shift_pass2>(coef, dst);
}

void dct4_neon(const int16_t *src, int16_t *dst, intptr_t srcStride)
{
    const int shift_pass1 = 1 + X265_DEPTH - 8;
    const int shift_pass2 = 8;

    ALIGN_VAR_32(int16_t, coef[4 * 4]);
    ALIGN_VAR_32(int16_t, block[4 * 4]);

    for (int i = 0; i < 4; i++)
    {
        memcpy(&block[i * 4], &src[i * srcStride], 4 * sizeof(int16_t));
    }

    partialButterfly4_neon<shift_pass1>(block, coef);
    partialButterfly4_neon<shift_pass2>(coef, dst);
}

void dct8_neon(const int16_t *src, int16_t *dst, intptr_t srcStride)
{
    const int shift_pass1 = 2 + X265_DEPTH - 8;
    const int shift_pass2 = 9;

    ALIGN_VAR_32(int16_t, coef[8 * 8]);
    ALIGN_VAR_32(int16_t, block[8 * 8]);

    for (int i = 0; i < 8; i++)
    {
        memcpy(&block[i * 8], &src[i * srcStride], 8 * sizeof(int16_t));
    }

    partialButterfly8_neon<shift_pass1>(block, coef);
    partialButterfly8_neon<shift_pass2>(coef, dst);
}

void dct16_neon(const int16_t *src, int16_t *dst, intptr_t srcStride)
{
    const int shift_pass1 = 3 + X265_DEPTH - 8;
    const int shift_pass2 = 10;

    ALIGN_VAR_32(int16_t, coef[16 * 16]);
    ALIGN_VAR_32(int16_t, block[16 * 16]);

    for (int i = 0; i < 16; i++)
    {
        memcpy(&block[i * 16], &src[i * srcStride], 16 * sizeof(int16_t));
    }

    partialButterfly16_neon<shift_pass1>(block, coef);
    partialButterfly16_neon<shift_pass2>(coef, dst);
}

void dct32_neon(const int16_t *src, int16_t *dst, intptr_t srcStride)
{
    const int shift_pass1 = 4 + X265_DEPTH - 8;
    const int shift_pass2 = 11;

    ALIGN_VAR_32(int16_t, coef[32 * 32]);
    ALIGN_VAR_32(int16_t, block[32 * 32]);

    for (int i = 0; i < 32; i++)
    {
        memcpy(&block[i * 32], &src[i * srcStride], 32 * sizeof(int16_t));
    }

    partialButterfly32_neon<shift_pass1>(block, coef);
    partialButterfly32_neon<shift_pass2>(coef, dst);
}

void idst4_neon(const int16_t *src, int16_t *dst, intptr_t dstStride)
{
    const int shift_pass1 = 7;
    const int shift_pass2 = 12 - (X265_DEPTH - 8);

    ALIGN_VAR_32(int16_t, coef[4 * 4]);

    inverseDst4_neon<shift_pass1>(src, coef, 4);
    inverseDst4_neon<shift_pass2>(coef, dst, dstStride);
}

void idct4_neon(const int16_t *src, int16_t *dst, intptr_t dstStride)
{
    const int shift_pass1 = 7;
    const int shift_pass2 = 12 - (X265_DEPTH - 8);

    ALIGN_VAR_32(int16_t, coef[4 * 4]);

    partialButterflyInverse4_neon<shift_pass1>(src, coef, 4);
    partialButterflyInverse4_neon<shift_pass2>(coef, dst, dstStride);
}

void idct8_neon(const int16_t *src, int16_t *dst, intptr_t dstStride)
{
    const int shift_pass1 = 7;
    const int shift_pass2 = 12 - (X265_DEPTH - 8);

    ALIGN_VAR_32(int16_t, coef[8 * 8]);

    partialButterflyInverse8_neon<shift_pass1>(src, coef, 8);
    partialButterflyInverse8_neon<shift_pass2>(coef, dst, dstStride);
}

void idct16_neon(const int16_t *src, int16_t *dst, intptr_t dstStride)
{
    const int shift_pass1 = 7;
    const int shift_pass2 = 12 - (X265_DEPTH - 8);

    ALIGN_VAR_32(int16_t, coef[16 * 16]);

    partialButterflyInverse16_neon<shift_pass1>(src, coef, 16);
    partialButterflyInverse16_neon<shift_pass2>(coef, dst, dstStride);
}

void idct32_neon(const int16_t *src, int16_t *dst, intptr_t dstStride)
{
    const int shift_1st = 7;
    const int shift_2nd = 12 - (X265_DEPTH - 8);

    ALIGN_VAR_32(int16_t, coef[32 * 32]);
    ALIGN_VAR_32(int16_t, block[32 * 32]);

    partialButterflyInverse32_neon(src, coef, shift_1st, 32);
    partialButterflyInverse32_neon(coef, block, shift_2nd, 32);

    for (int i = 0; i < 32; i++)
    {
        memcpy(&dst[i * dstStride], &block[i * 32], 32 * sizeof(int16_t));
    }
}

void setupDCTPrimitives_neon(EncoderPrimitives &p)
{
    p.cu[BLOCK_4x4].nonPsyRdoQuant   = nonPsyRdoQuant_neon<2>;
    p.cu[BLOCK_8x8].nonPsyRdoQuant   = nonPsyRdoQuant_neon<3>;
    p.cu[BLOCK_16x16].nonPsyRdoQuant = nonPsyRdoQuant_neon<4>;
    p.cu[BLOCK_32x32].nonPsyRdoQuant = nonPsyRdoQuant_neon<5>;
    p.cu[BLOCK_4x4].psyRdoQuant = psyRdoQuant_neon<2>;
    p.cu[BLOCK_8x8].psyRdoQuant = psyRdoQuant_neon<3>;
    p.cu[BLOCK_16x16].psyRdoQuant = psyRdoQuant_neon<4>;
    p.cu[BLOCK_32x32].psyRdoQuant = psyRdoQuant_neon<5>;
    p.dst4x4 = dst4_neon;
    p.cu[BLOCK_4x4].dct   = dct4_neon;
    p.cu[BLOCK_8x8].dct   = dct8_neon;
    p.cu[BLOCK_16x16].dct = PFX(dct16_neon);
    p.cu[BLOCK_32x32].dct = dct32_neon;
    p.idst4x4 = idst4_neon;
    p.cu[BLOCK_4x4].idct   = idct4_neon;
    p.cu[BLOCK_8x8].idct   = idct8_neon;
    p.cu[BLOCK_16x16].idct = idct16_neon;
    p.cu[BLOCK_32x32].idct = idct32_neon;
    p.cu[BLOCK_4x4].count_nonzero = count_nonzero_neon<4>;
    p.cu[BLOCK_8x8].count_nonzero = count_nonzero_neon<8>;
    p.cu[BLOCK_16x16].count_nonzero = count_nonzero_neon<16>;
    p.cu[BLOCK_32x32].count_nonzero = count_nonzero_neon<32>;

    p.cu[BLOCK_4x4].copy_cnt   = copy_count_neon<4>;
    p.cu[BLOCK_8x8].copy_cnt   = copy_count_neon<8>;
    p.cu[BLOCK_16x16].copy_cnt = copy_count_neon<16>;
    p.cu[BLOCK_32x32].copy_cnt = copy_count_neon<32>;
    p.cu[BLOCK_4x4].psyRdoQuant_1p = nonPsyRdoQuant_neon<2>;
    p.cu[BLOCK_4x4].psyRdoQuant_2p = psyRdoQuant_neon<2>;
    p.cu[BLOCK_8x8].psyRdoQuant_1p = nonPsyRdoQuant_neon<3>;
    p.cu[BLOCK_8x8].psyRdoQuant_2p = psyRdoQuant_neon<3>;
    p.cu[BLOCK_16x16].psyRdoQuant_1p = nonPsyRdoQuant_neon<4>;
    p.cu[BLOCK_16x16].psyRdoQuant_2p = psyRdoQuant_neon<4>;
    p.cu[BLOCK_32x32].psyRdoQuant_1p = nonPsyRdoQuant_neon<5>;
    p.cu[BLOCK_32x32].psyRdoQuant_2p = psyRdoQuant_neon<5>;

    p.scanPosLast  = scanPosLast_opt;

}

};


#endif
