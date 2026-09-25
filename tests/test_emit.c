/* test_emit.c —— 发射器自检：把同一批指令分别用自研编码器和 GNU as 生成，逐字节比对
 * 用法: ./test_emit      （需要 as / objcopy 在 PATH 里；只在开发机跑，产物不依赖它们）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emit_a64.h"

static emit_buf eb;
#define E(x) do { x; } while (0)

/* 生成一批指令（自研编码器） */
static void emit_all(void)
{
    emit_init(&eb);
    E(emit_mov_imm64(&eb, X0, 0x123456789ABCDEF0ull));
    E(emit_mov_imm32(&eb, X1, 0xDEADBEEF));
    E(emit_mov_reg(&eb, X2, X3));
    E(emit_add_imm(&eb, X4, X5, 123));
    E(emit_sub_imm(&eb, X6, X7, 4095));
    E(emit_add_reg(&eb, X8, X9, X10));
    E(emit_sub_reg(&eb, X11, X12, X13));
    E(emit_mul(&eb, X14, X15, X16));
    E(emit_sdiv(&eb, X17, X18, X19));
    E(emit_msub(&eb, X20, X21, X22, X23));
    E(emit_neg(&eb, X24, X25));
    E(emit_and_reg(&eb, X0, X1, X2));
    E(emit_orr_reg(&eb, X3, X4, X5));
    E(emit_eor_reg(&eb, X6, X7, X8));
    E(emit_lsl_imm(&eb, X9, X10, 7));
    E(emit_lsr_imm(&eb, X11, X12, 9));
    E(emit_asr_imm(&eb, X13, X14, 11));
    E(emit_lslv(&eb, X15, X16, X17));
    E(emit_asrv(&eb, X18, X19, X20));
    E(emit_mvn(&eb, X21, X22));
    E(emit_cmp_reg(&eb, X23, X24));
    E(emit_cmp_imm(&eb, X25, 77));
    E(emit_tst_reg(&eb, X26, X27));
    E(emit_str_imm(&eb, X0, X1, 16));
    E(emit_ldr_imm(&eb, X2, X3, 24));
    E(emit_strb_imm(&eb, X4, X5, 7));
    E(emit_ldrb_imm(&eb, X6, X7, 9));
    E(emit_strb_reg(&eb, X8, X9, X10));
    E(emit_ldrb_reg(&eb, X11, X12, X13));
    E(emit_strw_imm(&eb, X14, X15, 12));
    E(emit_stp_pre(&eb, X29, X30, SP, -16));
    E(emit_ldp_post(&eb, X29, X30, SP, 16));
    E(emit_cset(&eb, X0, COND_EQ));
    E(emit_cset(&eb, X1, COND_NE));
    E(emit_ret(&eb));
    E(emit_nop(&eb));
}

/* 同样的指令写成汇编文本，交给 GNU as */
static const char *asm_text =
    "    movz x0, #0xDEF0\n    movk x0, #0x9ABC, lsl #16\n    movk x0, #0x5678, lsl #32\n    movk x0, #0x1234, lsl #48\n"
    "    movz w1, #0xBEEF\n    movk w1, #0xDEAD, lsl #16\n"
    "    mov x2, x3\n"
    "    add x4, x5, #123\n    sub x6, x7, #4095\n"
    "    add x8, x9, x10\n    sub x11, x12, x13\n"
    "    mul x14, x15, x16\n    sdiv x17, x18, x19\n"
    "    msub x20, x21, x22, x23\n    neg x24, x25\n"
    "    and x0, x1, x2\n    orr x3, x4, x5\n    eor x6, x7, x8\n"
    "    lsl x9, x10, #7\n    lsr x11, x12, #9\n    asr x13, x14, #11\n"
    "    lslv x15, x16, x17\n    asrv x18, x19, x20\n    mvn x21, x22\n"
    "    cmp x23, x24\n    cmp x25, #77\n    tst x26, x27\n"
    "    str x0, [x1, #16]\n    ldr x2, [x3, #24]\n"
    "    strb w4, [x5, #7]\n    ldrb w6, [x7, #9]\n"
    "    strb w8, [x9, x10]\n    ldrb w11, [x12, x13]\n"
    "    str w14, [x15, #12]\n"
    "    stp x29, x30, [sp, #-16]!\n    ldp x29, x30, [sp], #16\n"
    "    cset x0, eq\n    cset x1, ne\n"
    "    ret\n    nop\n";

int main(void)
{
    emit_all();
    FILE *f = fopen("/tmp/s2a_emit_ref.s", "w");
    fputs(asm_text, f);
    fclose(f);
    int rc = system("as -o /tmp/s2a_emit_ref.o /tmp/s2a_emit_ref.s && "
                    "objcopy -O binary --only-section=.text /tmp/s2a_emit_ref.o /tmp/s2a_emit_ref.bin");
    if (rc != 0) { fprintf(stderr, "调用 as/objcopy 失败（本测试只在开发机跑）\n"); return 2; }
    FILE *g = fopen("/tmp/s2a_emit_ref.bin", "rb");
    unsigned char ref[8192];
    size_t rn = fread(ref, 1, sizeof ref, g);
    fclose(g);

    size_t n = eb.len < rn ? eb.len : rn;
    int bad = 0;
    for (size_t i = 0; i + 4 <= n; i += 4) {
        unsigned int a, b;
        memcpy(&a, eb.data + i, 4);
        memcpy(&b, ref + i, 4);
        if (a != b) {
            char d1[96], d2[96];
            emit_disasm_one(0, a, d1, sizeof d1);
            emit_disasm_one(0, b, d2, sizeof d2);
            printf("✗ 偏移 %3zu: 自研 %08x (%s)   as %08x (%s)\n", i, a, d1, b, d2);
            bad++;
        }
    }
    printf("指令数 %zu（自研 %zu 字节 / as %zu 字节），不一致 %d 条\n", n / 4, (size_t)eb.len, rn, bad);
    return bad ? 1 : 0;
}
