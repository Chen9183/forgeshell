/* emit_a64.h —— ARM64 指令发射器（编译器侧）
 *
 * 设计：所有指令按 32 位编码写进一个可增长缓冲；跳转用「标签 + 修正表」，
 * 最后统一回填（因为生成代码是单块直线代码，回填只需一次遍历）。
 *
 * 为什么不用汇编器：本项目的卖点就是自包含——编译时不需要 binutils。
 * 正确性由 tests/test_emit.c 逐条与 `as` 的产物比对保证。
 */
#ifndef EMIT_A64_H
#define EMIT_A64_H

#include <stdint.h>
#include <stddef.h>

/* 通用寄存器编号 */
enum {
    X0 = 0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15,
    X16, X17, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27, X28, X29, X30,
    XZR = 31, SP = 31
};

/* 条件码 */
enum {
    COND_EQ = 0, COND_NE = 1, COND_CS = 2, COND_CC = 3,
    COND_MI = 4, COND_PL = 5, COND_VS = 6, COND_VC = 7,
    COND_HI = 8, COND_LS = 9, COND_GE = 10, COND_LT = 11,
    COND_GT = 12, COND_LE = 13, COND_AL = 14
};

typedef struct {
    unsigned char *data;
    uint32_t len;
    uint32_t cap;
    /* 标签与修正 */
    int32_t *label_off;      /* 标签 → 字节偏移；-1 = 未定义 */
    uint8_t *label_used;
    uint32_t nlabels, caplabels;
    struct { uint32_t insn_idx; uint32_t label; uint8_t kind; } *fixups;
    uint32_t nfixups, capfixups;
    int error;               /* 置位后不再发射，由调用方检查 */
} emit_buf;

void emit_init(emit_buf *b);
void emit_free(emit_buf *b);

/* ---------- 原始发射 ---------- */
void emit_u32(emit_buf *b, uint32_t insn);
uint32_t emit_len(const emit_buf *b);
uint32_t emit_pos(const emit_buf *b);           /* 当前字节偏移 */
void emit_align(emit_buf *b, uint32_t align);

/* ---------- 标签与跳转 ---------- */
uint32_t emit_new_label(emit_buf *b);
void     emit_bind(emit_buf *b, uint32_t label);
void     emit_fixup_all(emit_buf *b);           /* 回填全部跳转 */

/* ---------- 数据搬运 ---------- */
void emit_movz(emit_buf *b, int rd, uint32_t imm16, int shift);
void emit_movk(emit_buf *b, int rd, uint32_t imm16, int shift);
void emit_mov_imm64(emit_buf *b, int rd, uint64_t v);
void emit_mov_imm32(emit_buf *b, int rd, uint32_t v);
void emit_mov_reg(emit_buf *b, int rd, int rm);

/* ---------- 算术 / 逻辑 ---------- */
void emit_add_imm(emit_buf *b, int rd, int rn, uint32_t imm);
void emit_sub_imm(emit_buf *b, int rd, int rn, uint32_t imm);
void emit_add_reg(emit_buf *b, int rd, int rn, int rm);
void emit_sub_reg(emit_buf *b, int rd, int rn, int rm);
void emit_mul(emit_buf *b, int rd, int rn, int rm);
void emit_sdiv(emit_buf *b, int rd, int rn, int rm);
void emit_msub(emit_buf *b, int rd, int rn, int rm, int ra);   /* rd = ra - rn*rm */
void emit_neg(emit_buf *b, int rd, int rm);
void emit_and_reg(emit_buf *b, int rd, int rn, int rm);
void emit_orr_reg(emit_buf *b, int rd, int rn, int rm);
void emit_eor_reg(emit_buf *b, int rd, int rn, int rm);
void emit_lsl_imm(emit_buf *b, int rd, int rn, uint32_t sh);
void emit_asr_imm(emit_buf *b, int rd, int rn, uint32_t sh);
void emit_lsr_imm(emit_buf *b, int rd, int rn, uint32_t sh);
void emit_lslv(emit_buf *b, int rd, int rn, int rm);
void emit_asrv(emit_buf *b, int rd, int rn, int rm);
void emit_mvn(emit_buf *b, int rd, int rm);
void emit_cmp_reg(emit_buf *b, int rn, int rm);
void emit_cmp_imm(emit_buf *b, int rn, uint32_t imm);
void emit_tst_reg(emit_buf *b, int rn, int rm);

/* ---------- 内存 ---------- */
void emit_str_imm(emit_buf *b, int rt, int rn, uint32_t off);
void emit_ldr_imm(emit_buf *b, int rt, int rn, uint32_t off);
void emit_strb_imm(emit_buf *b, int rt, int rn, uint32_t off);
void emit_ldrb_imm(emit_buf *b, int rt, int rn, uint32_t off);
void emit_strb_reg(emit_buf *b, int rt, int rn, int rm);
void emit_ldrb_reg(emit_buf *b, int rt, int rn, int rm);
void emit_strw_imm(emit_buf *b, int rt, int rn, uint32_t off);

/* 栈帧：stp/ldp 前索引式（[sp, #-16]! 与 [sp], #16） */
void emit_stp_pre(emit_buf *b, int rt, int rt2, int rn, int32_t off);
void emit_ldp_post(emit_buf *b, int rt, int rt2, int rn, int32_t off);

/* ---------- 分支 ---------- */
void emit_b(emit_buf *b, uint32_t label);
void emit_bl(emit_buf *b, uint32_t label);
void emit_b_abs(emit_buf *b, uint64_t addr);        /* 已知绝对地址的 bl（先算偏移） */
void emit_b_cond(emit_buf *b, int cond, uint32_t label);
void emit_cbz(emit_buf *b, int rt, uint32_t label);
void emit_cbnz(emit_buf *b, int rt, uint32_t label);
void emit_cbz_w(emit_buf *b, int rt, uint32_t label);
void emit_cbnz_w(emit_buf *b, int rt, uint32_t label);
void emit_cset(emit_buf *b, int rd, int cond);
void emit_ret(emit_buf *b);
void emit_br(emit_buf *b, int rn);
void emit_blr(emit_buf *b, int rn);
void emit_svc(emit_buf *b, uint32_t imm);
void emit_nop(emit_buf *b);
void emit_udf(emit_buf *b, uint32_t imm);

/* ---------- 反汇编（-g4 转储与自检用） ---------- */
int  emit_disasm_one(uint64_t va, uint32_t insn, char *out, size_t cap);

#endif /* EMIT_A64_H */
