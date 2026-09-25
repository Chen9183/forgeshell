/* 向量链单元测试：原生与 s2a -r 逐字节对比 */
#include <stdio.h>
#include <stdint.h>
extern void vec_chain(unsigned char *dst, const uint32_t *seed);
int main(void)
{
    uint32_t seed[4] = { 0, 1, 2, 3 };
    unsigned char out[16];
    long acc = 0;
    for (int it = 0; it < 3; it++) {
        vec_chain(out, seed);
        for (int i = 0; i < 16; i++) { printf("%02x ", out[i]); acc += out[i]; }
        printf("| sum=%ld\n", acc);
        seed[0] += 16; seed[1] += 16; seed[2] += 16; seed[3] += 16;
    }
    return 0;
}
