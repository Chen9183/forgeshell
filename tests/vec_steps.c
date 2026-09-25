#include <stdio.h>
#include <stdint.h>
extern void vec_steps(unsigned char *dst, const uint32_t *seed);
static const char *nm[11] = { "mov(ORR)","add v2.4s","add v1.4s","add v3.4s","add v4.4s",
                              "uzp1 v1.8h","uzp1 v0.8h","uzp1 v0.16b","shl v1.16b",
                              "add v0.16b","add v0.16b+v5" };
int main(void)
{
    uint32_t seed[4] = { 0, 1, 2, 3 };
    unsigned char out[16 * 11];
    vec_steps(out, seed);
    for (int s = 0; s < 11; s++) {
        printf("%-14s:", nm[s]);
        for (int i = 0; i < 16; i++) printf(" %02x", out[s * 16 + i]);
        printf("\n");
    }
    return 0;
}
