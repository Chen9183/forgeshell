/* emu_selftest.c —— 模拟器自检：把编译器实际会生成的各类运算各跑一遍并打印结果。
 *
 * 用法：
 *   musl-gcc -static -O2 -o /tmp/emu_selftest tests/emu_selftest.c
 *   /tmp/emu_selftest > 真机基准.txt          # 真机（正确值）
 *   bin/s2a -r /tmp/emu_selftest > 模拟器.txt  # 模拟器
 *   diff 两份 → 任何差异都指向模拟器某条指令语义错
 *
 * 覆盖：有/无符号加乘除余、移位、位运算、比较与条件选择、符号扩展、
 *       32/64 位混算、字节/半字/字/双字访存、结构体拷贝（SIMD 可能介入）、
 *       除法与乘长（smull/umulh）、条件标志链（ccmp/csel）。
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static uint64_t mix64(uint64_t x)
{
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

int main(void)
{
    volatile int64_t s1 = -1234567890123LL, s2 = 987654321LL;
    volatile uint64_t u1 = 0xfedcba9876543210ULL, u2 = 0x123456789ULL;
    volatile int32_t w1 = -2000000007, w2 = 1234567;
    volatile uint32_t v1 = 0xdeadbeefu, v2 = 0x1234u;

    /* 有/无符号四则与余数 */
    printf("s_add=%lld s_sub=%lld s_mul=%lld s_div=%lld s_rem=%lld\n",
           (long long)(s1 + s2), (long long)(s1 - s2), (long long)(s1 * s2),
           (long long)(s1 / s2), (long long)(s1 % s2));
    printf("u_add=%llu u_sub=%llu u_mul=%llu u_div=%llu u_rem=%llu\n",
           (unsigned long long)(u1 + u2), (unsigned long long)(u1 - u2),
           (unsigned long long)(u1 * u2), (unsigned long long)(u1 / u2),
           (unsigned long long)(u1 % u2));
    printf("w_div=%d w_rem=%d v_div=%u v_rem=%u\n", w1 / w2, w1 % w2, v1 / v2, v1 % v2);

    /* 移位与位运算 */
    printf("shl=%llx shr=%llx sar=%lld and=%llx orr=%llx eor=%llx\n",
           (unsigned long long)(u1 << 13), (unsigned long long)(u1 >> 17),
           (long long)(s1 >> 9), (unsigned long long)(u1 & u2),
           (unsigned long long)(u1 | u2), (unsigned long long)(u1 ^ u2));
    printf("vshl=%x vshr=%x asrv=%d ror=%llx\n",
           (unsigned)(v1 << 7), (unsigned)(v1 >> 3), (int)(w1 >> 5),
           (unsigned long long)((u1 >> 21) | (u1 << 43)));

    /* 比较 / 条件选择 / 标志链 */
    int c1 = s1 < s2, c2 = u1 > u2, c3 = (w1 == w2);
    printf("cmp=%d %d %d csel=%lld min=%lld max=%llu\n",
           c1, c2, c3,
           (long long)(c1 ? s1 : s2),
           (long long)(s1 < s2 ? s1 : s2),
           (unsigned long long)(u1 > u2 ? u1 : u2));

    /* 符号扩展与位域 */
    printf("sxtb=%lld sxth=%lld sxtw=%lld uxtb=%llu uxth=%llu\n",
           (long long)(int8_t)u1, (long long)(int16_t)u1, (long long)(int32_t)u1,
           (unsigned long long)(uint8_t)u1, (unsigned long long)(uint16_t)u1);
    printf("bfx=%llu %lld bfi=%llx\n",
           (unsigned long long)((u1 >> 12) & 0xFFFF),
           (long long)(((int64_t)u1 << 20) >> 44),
           (unsigned long long)((u2 & ~(0xFFULL << 8)) | (((u1 & 0xFF) << 8))));

    /* 乘长 / 乘高（会生成 smull/umulh 之类的指令） */
    {
        int64_t a = -1234567890123LL, b = 7654321;
        uint64_t x = 0xffffffff00000001ULL, y = 0x100000007ULL;
        __int128 p = (__int128)a * (__int128)b;
        unsigned __int128 q = (unsigned __int128)x * (unsigned __int128)y;
        printf("smull=%lld smulh=%lld umull=%llu umulh=%llu\n",
               (long long)(int64_t)p, (long long)(p >> 64),
               (unsigned long long)(uint64_t)q, (unsigned long long)(q >> 64));
    }

    /* 访存：字节/半字/字/双字 + 结构体拷贝（可能走 SIMD） */
    {
        unsigned char bytes[64];
        for (int i = 0; i < 64; i++) bytes[i] = (unsigned char)(i * 7 + 3);
        uint8_t b = bytes[5]; uint16_t h; uint32_t w; uint64_t d;
        memcpy(&h, bytes + 8, 2);
        memcpy(&w, bytes + 12, 4);
        memcpy(&d, bytes + 16, 8);
        printf("mem b=%u h=%u w=%u d=%llu\n", b, h, w, (unsigned long long)d);
        struct big { uint64_t v[8]; } src, dst;
        for (int i = 0; i < 8; i++) src.v[i] = mix64(0x1000 + (uint64_t)i);
        dst = src;
        printf("copy=%llx %llx\n", (unsigned long long)dst.v[0], (unsigned long long)dst.v[7]);
        void *p = malloc(100000);
        if (p) { memset(p, 0xAB, 100000); ((char *)p)[99999] = 1; free(p); printf("malloc ok\n"); }
    }

    /* 循环 + 索引（典型访存寻址模式） */
    {
        uint64_t acc = 0, tab[32];
        for (int i = 0; i < 32; i++) tab[i] = mix64((uint64_t)i * 0x9E3779B97F4A7C15ULL);
        for (int i = 0; i < 32; i++) acc += tab[i];
        for (int i = 0; i < 32; i++) acc ^= mix64(acc + (uint64_t)i);
        printf("loop=%llx\n", (unsigned long long)acc);
    }
    return 0;
}
