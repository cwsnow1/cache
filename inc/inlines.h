


// Divides two intergers giving the rounded up version rather than normal rounded down
#define CEILING_DIVIDE(numerator, denominator) ((numerator + denominator - 1) / denominator)


// Returns number of bits needed to store val number of elements
#define BITS(val) \
    (val < 2 ? 1 : (val < 4 ? 2 : (val < 8 ? 3 : (val < 16 ? 4 : (val < 32 ? 5 : (val < 64 ? 6 : \
    (val < 128 ? 7 : (val < 256 ? 8 : (val < 512 ? 9 : (val < 1024 ? 10 : (val < 2048 ? 11 : (val < 4096 ? 12 : \
    (val < 8192 ? 13 : (val < 16384 ? 14 : (val < 32768 ? 15 : (val < 65536 ? 16 : (val < 131072 ? 17 : \
    (val < 262144 ? 18 : (val < 524288 ? 19 : (val < 1048576 ? 20 : (val < 2097152 ? 21 : (val < 4194304 ? 22 : \
    (val < 8388608 ? 23 : (val < 16777216 ? 24 : (val < 33554432 ? 25 : (val < 67108864 ? 26 : \
    (val < 134217728 ? 27 : (val < 268435456 ? 28 : (val < 536870912 ? 29 : (val < 1073741824 ? 30 : \
    (val < 2147483648 ? 31 : (val < 4294967296 ? 32 : (val < 8589934592 ? 33 : (val < 17179869184 ? 34 : \
    (val < 34359738368 ? 35 : (val < 68719476736 ? 36 : (val < 137438953472 ? 37 : (val < 274877906944 ? 38 : \
    (val < 549755813888 ? 39 : (val < 1099511627776 ? 40 : (val < 2199023255552 ? 41 : (val < 4398046511104 ? 42 : \
    (val < 8796093022208 ? 43 : (val < 17592186044416 ? 44 : (val < 35184372088832 ? 45 : \
    (val < 70368744177664 ? 46 : (val < 140737488355328 ? 47 : (val < 281474976710656 ? 48 : \
    (val < 562949953421312 ? 49 : (val < 1125899906842624 ? 50 : (val < 2251799813685248 ? 51 : \
    (val < 4503599627370496 ? 52 : (val < 9007199254740992 ? 53 : (val < 18014398509481984 ? 54 : \
    (val < 36028797018963968 ? 55 : (val < 72057594037927936 ? 56 : (val < 144115188075855872 ? 57 : \
    (val < 288230376151711744 ? 58 : (val < 576460752303423488 ? 59 : (val < 1152921504606846976U ? 60 : \
    (val < 2305843009213693952U ? 61 : (val < 4611686018427387904U ? 62 : (val < 9223372036854775808U ? 63 : 64 \
    )))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
