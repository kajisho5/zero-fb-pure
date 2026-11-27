// デノーマル（極小値）による処理落ちを防ぐための、スコープ限定の設定。
#pragma once

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
 #include <immintrin.h>
 #define ZFP_DENORM_X86 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
 #define ZFP_DENORM_A64 1
#endif

namespace zfp
{
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
       #if ZFP_DENORM_X86
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u); // FTZ | DAZ
       #elif ZFP_DENORM_A64
        unsigned long long v;
        __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (v));
        saved = v;
        v |= (1ull << 24); // FZ
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (v));
       #endif
    }

    ~ScopedNoDenormals() noexcept
    {
       #if ZFP_DENORM_X86
        _mm_setcsr (saved);
       #elif ZFP_DENORM_A64
        unsigned long long v = saved;
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (v));
       #endif
    }

    ScopedNoDenormals (const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator= (const ScopedNoDenormals&) = delete;

private:
   #if ZFP_DENORM_X86
    unsigned int saved = 0;
   #elif ZFP_DENORM_A64
    unsigned long long saved = 0;
   #endif
};
} // namespace zfp
