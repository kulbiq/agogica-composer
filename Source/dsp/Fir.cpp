#include "Fir.h"
#include <algorithm>

#if defined (_M_X64) || defined (__x86_64__)
 #define COMPOSER_FIR_X64 1
 #include <immintrin.h>
 #if defined (_MSC_VER) && ! defined (__clang__)
  #include <intrin.h>
  #define COMPOSER_TARGET_AVX2 // MSVC compiles AVX intrinsics without /arch (which would let the
                               // compiler put AVX anywhere in the file)
 #else
  #define COMPOSER_TARGET_AVX2 __attribute__ ((target ("avx2,fma")))
 #endif
#endif

namespace dsp
{
namespace
{
// ---- Plain C++: `width` outputs per group, one running sum each -----------------------------

template <int width>
void plainGroup (const float* irLeft, const float* irRight, int taps, const float* x,
                 float* left, float* right, int count) noexcept
{
    float sumL[width] {}, sumR[width] {};

    for (int j = 0; j < taps; ++j)
    {
        const auto l = irLeft[j], r = irRight[j];
        const auto* window = x + j;

        for (int m = 0; m < width; ++m)
        {
            sumL[m] += l * window[m];
            sumR[m] += r * window[m];
        }
    }

    std::copy (sumL, sumL + count, left);
    std::copy (sumR, sumR + count, right);
}

void plain (const float* irLeft, const float* irRight, int taps, const float* x, float* left, float* right, int n) noexcept
{
    int m = 0;

    for (; m + 32 <= n; m += 32)
        plainGroup<32> (irLeft, irRight, taps, x + m, left + m, right + m, 32);

    for (; m < n; m += 8)
        plainGroup<8> (irLeft, irRight, taps, x + m, left + m, right + m, std::min (8, n - m));
}

#if COMPOSER_FIR_X64
// ---- SSE2 (every x64 CPU): 16 outputs per group, 8 running sums of 4 ----------------------------

void sse2 (const float* irLeft, const float* irRight, int taps, const float* x, float* left, float* right, int n) noexcept
{
    int m = 0;

    for (; m + 16 <= n; m += 16)
    {
        const auto* w = x + m;
        auto l0 = _mm_setzero_ps(), l1 = l0, l2 = l0, l3 = l0, r0 = l0, r1 = l0, r2 = l0, r3 = l0;

        for (int j = 0; j < taps; ++j)
        {
            const auto hl = _mm_set1_ps (irLeft[j]), hr = _mm_set1_ps (irRight[j]);
            const auto x0 = _mm_loadu_ps (w + j), x1 = _mm_loadu_ps (w + j + 4);
            const auto x2 = _mm_loadu_ps (w + j + 8), x3 = _mm_loadu_ps (w + j + 12);
            l0 = _mm_add_ps (l0, _mm_mul_ps (hl, x0)); r0 = _mm_add_ps (r0, _mm_mul_ps (hr, x0));
            l1 = _mm_add_ps (l1, _mm_mul_ps (hl, x1)); r1 = _mm_add_ps (r1, _mm_mul_ps (hr, x1));
            l2 = _mm_add_ps (l2, _mm_mul_ps (hl, x2)); r2 = _mm_add_ps (r2, _mm_mul_ps (hr, x2));
            l3 = _mm_add_ps (l3, _mm_mul_ps (hl, x3)); r3 = _mm_add_ps (r3, _mm_mul_ps (hr, x3));
        }

        _mm_storeu_ps (left + m, l0); _mm_storeu_ps (left + m + 4, l1); _mm_storeu_ps (left + m + 8, l2); _mm_storeu_ps (left + m + 12, l3);
        _mm_storeu_ps (right + m, r0); _mm_storeu_ps (right + m + 4, r1); _mm_storeu_ps (right + m + 8, r2); _mm_storeu_ps (right + m + 12, r3);
    }

    for (; m < n; m += 8) // two sums of 4 per ear; a group of 4 would leave the adder waiting
    {
        const auto* w = x + m;
        auto l0 = _mm_setzero_ps(), l1 = l0, r0 = l0, r1 = l0;

        for (int j = 0; j < taps; ++j)
        {
            const auto hl = _mm_set1_ps (irLeft[j]), hr = _mm_set1_ps (irRight[j]);
            const auto x0 = _mm_loadu_ps (w + j), x1 = _mm_loadu_ps (w + j + 4);
            l0 = _mm_add_ps (l0, _mm_mul_ps (hl, x0)); r0 = _mm_add_ps (r0, _mm_mul_ps (hr, x0));
            l1 = _mm_add_ps (l1, _mm_mul_ps (hl, x1)); r1 = _mm_add_ps (r1, _mm_mul_ps (hr, x1));
        }

        alignas (16) float l[8], r[8];
        _mm_store_ps (l, l0); _mm_store_ps (l + 4, l1); _mm_store_ps (r, r0); _mm_store_ps (r + 4, r1);
        const auto count = std::min (8, n - m);
        std::copy (l, l + count, left + m);
        std::copy (r, r + count, right + m);
    }
}

// ---- AVX2 + FMA: 32 outputs per group, 8 running sums of 8 (as many as the FMA units keep busy) --

COMPOSER_TARGET_AVX2 void avx2 (const float* irLeft, const float* irRight, int taps, const float* x,
                                float* left, float* right, int n) noexcept
{
    int m = 0;

    for (; m + 32 <= n; m += 32)
    {
        const auto* w = x + m;
        auto l0 = _mm256_setzero_ps(), l1 = l0, l2 = l0, l3 = l0, r0 = l0, r1 = l0, r2 = l0, r3 = l0;

        for (int j = 0; j < taps; ++j)
        {
            const auto hl = _mm256_broadcast_ss (irLeft + j), hr = _mm256_broadcast_ss (irRight + j);
            const auto x0 = _mm256_loadu_ps (w + j), x1 = _mm256_loadu_ps (w + j + 8);
            const auto x2 = _mm256_loadu_ps (w + j + 16), x3 = _mm256_loadu_ps (w + j + 24);
            l0 = _mm256_fmadd_ps (hl, x0, l0); r0 = _mm256_fmadd_ps (hr, x0, r0);
            l1 = _mm256_fmadd_ps (hl, x1, l1); r1 = _mm256_fmadd_ps (hr, x1, r1);
            l2 = _mm256_fmadd_ps (hl, x2, l2); r2 = _mm256_fmadd_ps (hr, x2, r2);
            l3 = _mm256_fmadd_ps (hl, x3, l3); r3 = _mm256_fmadd_ps (hr, x3, r3);
        }

        _mm256_storeu_ps (left + m, l0); _mm256_storeu_ps (left + m + 8, l1); _mm256_storeu_ps (left + m + 16, l2); _mm256_storeu_ps (left + m + 24, l3);
        _mm256_storeu_ps (right + m, r0); _mm256_storeu_ps (right + m + 8, r1); _mm256_storeu_ps (right + m + 16, r2); _mm256_storeu_ps (right + m + 24, r3);
    }

    alignas (32) float l[40], r[40]; // the rest (a run that isn't a multiple of 32), 8 at a time
    const auto rest = n - m;

    for (int k = 0; k < rest; k += 8)
    {
        const auto* w = x + m + k;
        auto sl = _mm256_setzero_ps(), sr = sl;

        for (int j = 0; j < taps; ++j)
        {
            const auto x0 = _mm256_loadu_ps (w + j);
            sl = _mm256_fmadd_ps (_mm256_broadcast_ss (irLeft + j), x0, sl);
            sr = _mm256_fmadd_ps (_mm256_broadcast_ss (irRight + j), x0, sr);
        }

        _mm256_store_ps (l + k, sl);
        _mm256_store_ps (r + k, sr);
    }

    _mm256_zeroupper(); // before any SSE code runs again (scalar copies included)

    std::copy (l, l + rest, left + m);
    std::copy (r, r + rest, right + m);
}

bool cpuHasAvx2AndFma()
{
   #if defined (_MSC_VER) && ! defined (__clang__)
    int info[4];
    __cpuid (info, 1);
    const auto osSavesAvx = (info[2] & (1 << 27)) != 0, avx = (info[2] & (1 << 28)) != 0, fma = (info[2] & (1 << 12)) != 0;

    if (! (osSavesAvx && avx && fma) || (_xgetbv (0) & 6) != 6) // the OS must save the YMM registers
        return false;

    __cpuidex (info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
   #else
    __builtin_cpu_init();
    return __builtin_cpu_supports ("avx2") && __builtin_cpu_supports ("fma"); // OS support included
   #endif
}
#endif
} // namespace

int availableConvolveTwo (NamedConvolveTwo* out, int capacity)
{
    NamedConvolveTwo all[3];
    int count = 0;
    all[count++] = { "plain", plain };
   #if COMPOSER_FIR_X64
    all[count++] = { "sse2", sse2 };
    if (cpuHasAvx2AndFma())
        all[count++] = { "avx2", avx2 };
   #endif

    count = std::min (count, capacity);
    std::copy (all, all + count, out);
    return count;
}

ConvolveTwo bestConvolveTwo()
{
    NamedConvolveTwo all[3];
    const auto count = availableConvolveTwo (all, 3);
    return all[count - 1].convolve; // fastest last
}
} // namespace dsp
