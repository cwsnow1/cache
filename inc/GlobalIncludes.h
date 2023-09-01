#pragma once
#define COMMA ,

#ifdef __GNUC__
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

PACK(enum CacheLevel {
    kL1 COMMA
    kL2 COMMA
    kL3 COMMA
    kMainMemory COMMA
    kMaxNumberOfCacheLevels = kMainMemory COMMA
});

template <typename T>
constexpr bool isPowerOfTwo(T n) {
    return (n & (n - 1)) == 0;
}
