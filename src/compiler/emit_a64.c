/* emit_a64.c —— ARM64 指令编码（见 emit_a64.h 的说明） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emit_a64.h"

/* 修正类型 */
enum { FX_B = 1, FX_BL, FX_BCOND, FX_CBZ, FX_CBNZ, FX_CBZ_W, FX_CBNZ_W };

/* ============================ 缓冲与标签 ============================ */
void emit_init(emit_buf *b)
{
    memset(b, 0, sizeof *b);
    b->cap = 4096;
    b->data = malloc(b->cap);
    b->caplabels = 64;
    b->label_off = malloc(sizeof(int32_t) * b->caplabels);
    b->label_used = calloc(b->caplabels, 1);
    for (uint32_t i = 0; i < b->caplabels; i++) b->label_off[i] = -1;
    b->capfixups = 128;
    b->fixups = malloc(sizeof(*b->fixups) * b->capfixups);
}

void emit_free(emit_buf *b)
{
    free(b->data); free(b->label_off); free(b->label_used); free(b->fixups);
    memset(b, 0, sizeof *b);
}

static void grow(emit_buf *b, uint32_t extra)
{
    if (b->len + extra <= b->cap) return;
    while (b->len + extra > b->cap) b->cap *= 2;
    b->data = realloc(b->data, b->cap);
}

void emit_u32(emit_buf *b, uint32_t insn)
{
    if (b->error) return;
    grow(b, 4);
    memcpy(b->data + b->len, &insn, 4);
    b->len += 4;
}

uint32_t emit_len(const emit_buf *b) { return b->len; }
uint32_t emit_pos(const emit_buf *b) { return b->len; }

void emit_align(emit_buf *b, uint32_t align)
{
    while (b->len % align) emit_u32(b, 0xD503201F);   /* nop 填充 */
}

uint32_t emit_new_label(emit_buf *b)
{
    if (b->nlabels == b->caplabels) {
        uint32_t old = b->caplabels;
        b->caplabels *= 2;
        b->label_off = realloc(b->label_off, sizeof(int32_t) * b->caplabels);
        b->label_used = realloc(b->label_used, b->caplabels);
        for (uint32_t i = old; i < b->caplabels; i++) { b->label_off[i] = -1; b->label_used[i] = 0; }
    }
    b->label_off[b->nlabels] = -1;
    b->label_used[b->nlabels] = 1;
    return b->nlabels++;
}

void emit_bind(emit_buf *b, uint32_t label)
{
    if (label < b->nlabels && b->label_off[label] >= 0) {
        fprintf(stderr, "s2a 内部错误：标签 %u 被重复定义\n", label);
        b->error = 1;
        return;
    }
    if (label < b->nlabels) b->label_off[label] = (int32_t)b->len;
}

static void add_fixup(emit_buf *b, uint32_t insn_idx, uint32_t label, uint8_t kind)
{
    if (b->nfixups == b->capfixups) {
        b->capfixups *= 2;
        b->fixups = realloc(b->fixups, sizeof(*b->fixups) * b->capfixups);
    }
    b->fixups[b->nfixups].insn_idx = insn_idx;
    b->fixups[b->nfixups].label = label;
    b->fixups[b->nfixups].kind = kind;
    b->nfixups++;
}

void emit_fixup_all(emit_buf *b)
{
    for (uint32_t i = 0; i < b->nfixups; i++) {
        uint32_t idx = b->fixups[i].insn_idx;
        uint32_t lb = b->fixups[i].label;
        if (lb >= b->nlabels || b->label_off[lb] < 0) {
            fprintf(stderr, "s2a 内部错误：跳转目标标签 %u 未定义\n", lb);
            b->error = 1;
            continue;
        }
        int64_t pc = (int64_t)idx;
        int64_t target = (int64_t)b->label_off[lb];
        int64_t delta = target - pc;
        uint32_t insn;
        memcpy(&insn, b->data + idx, 4);
        switch (b->fixups[i].kind) {
        case FX_B:
        case FX_BL: {
            int64_t off = delta >> 2;
            if (off < -(1 << 25) || off >= (1 << 25)) {
                fprintf(stderr, "s2a: 跳转超出 ±128MB 范围\n");
                b->error = 1;
                continue;
            }
            insn |= (uint32_t)(off & 0x3FFFFFF);
            break;
        }
        case FX_BCOND: {
            int64_t off = delta >> 2;
            if (off < -(1 << 18) || off >= (1 << 18)) {
                fprintf(stderr, "s2a: 条件跳转超出 ±1MB 范围\n");
                b->error = 1;
                continue;
            }
            insn |= (uint32_t)(off & 0x7FFFF) << 5;
            break;
        }
        case FX_CBZ:
        case FX_CBNZ: {
            int64_t off = delta >> 2;
            insn |= (uint32_t)(off & 0x7FFFF) << 5;
            break;
        }
        case FX_CBZ_W:
        case FX_CBNZ_W: {
            int64_t off = delta >> 2;
            insn |= (uint32_t)(off & 0x7FFFF) << 5;
            break;
        }
        default:
            break;
        }
        memcpy(b->data + idx, &insn, 4);
    }
    b->nfixups = 0;
}

/* ============================ 数据搬运 ============================ */
void emit_movz(emit_buf *b, int rd, uint32_t imm16, int shift)
{
    emit_u32(b, 0xD2800000u | ((uint32_t)(shift / 16) << 21) | ((imm16 & 0xFFFF) << 5) | (uint32_t)(rd & 31));
}

void emit_movk(emit_buf *b, int rd, uint32_t imm16, int shift)
{
    emit_u32(b, 0xF2800000u | ((uint32_t)(shift / 16) << 21) | ((imm16 & 0xFFFF) << 5) | (uint32_t)(rd & 31));
}

void emit_mov_imm64(emit_buf *b, int rd, uint64_t v)
{
    emit_movz(b, rd, (uint32_t)(v & 0xFFFF), 0);
    if ((v >> 16) & 0xFFFF) emit_movk(b, rd, (uint32_t)((v >> 16) & 0xFFFF), 16);
    if ((v >> 32) & 0xFFFF) emit_movk(b, rd, (uint32_t)((v >> 32) & 0xFFFF), 32);
    if ((v >> 48) & 0xFFFF) emit_movk(b, rd, (uint32_t)((v >> 48) & 0xFFFF), 48);
}

void emit_mov_imm32(emit_buf *b, int rd, uint32_t v)
{
    /* 32 位立即数：MOVZ/MOVK 的 32 位形式 */
    emit_u32(b, 0x52800000u | ((uint32_t)((v & 0xFFFF) << 5)) | (uint32_t)(rd & 31));
    if ((v >> 16) & 0xFFFF)
        emit_u32(b, 0x72A00000u | ((uint32_t)(((v >> 16) & 0xFFFF) << 5)) | (uint32_t)(rd & 31));
}

void emit_mov_reg(emit_buf *b, int rd, int rm)
{
    emit_u32(b, 0xAA0003E0u | ((uint32_t)(rm & 31) << 16) | (uint32_t)(rd & 31));  /* ORR Xd, XZR, Xm */
}

/* ============================ 算术 / 逻辑 ============================ */
void emit_add_imm(emit_buf *b, int rd, int rn, uint32_t imm)
{
    emit_u32(b, 0x91000000u | ((imm & 0xFFF) << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_sub_imm(emit_buf *b, int rd, int rn, uint32_t imm)
{
    emit_u32(b, 0xD1000000u | ((imm & 0xFFF) << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_add_reg(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0x8B000000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_sub_reg(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0xCB000000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_mul(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0x9B007C00u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_sdiv(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0x9AC00C00u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_msub(emit_buf *b, int rd, int rn, int rm, int ra)
{
    emit_u32(b, 0x9B008000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(ra & 31) << 10)
                | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_neg(emit_buf *b, int rd, int rm)
{
    emit_u32(b, 0xCB0003E0u | ((uint32_t)(rm & 31) << 16) | (uint32_t)(rd & 31));
}

void emit_and_reg(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0x8A000000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_orr_reg(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0xAA000000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_eor_reg(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0xCA000000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_lsl_imm(emit_buf *b, int rd, int rn, uint32_t sh)
{
    /* LSL Xd, Xn, #sh  ==  UBFM Xd, Xn, #(-sh mod 64), #(63-sh) */
    uint32_t immr = (64u - (sh & 63)) & 63u;
    uint32_t imms = 63u - (sh & 63);
    emit_u32(b, 0xD3400000u | (immr << 16) | (imms << 10)
                | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_lsr_imm(emit_buf *b, int rd, int rn, uint32_t sh)
{
    emit_u32(b, 0xD340FC00u | ((sh & 63) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_asr_imm(emit_buf *b, int rd, int rn, uint32_t sh)
{
    emit_u32(b, 0x9340FC00u | ((sh & 63) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_lslv(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0x9AC02000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_asrv(emit_buf *b, int rd, int rn, int rm)
{
    emit_u32(b, 0x9AC02800u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31));
}

void emit_mvn(emit_buf *b, int rd, int rm)
{
    emit_u32(b, 0xAA2003E0u | ((uint32_t)(rm & 31) << 16) | (uint32_t)(rd & 31));
}

void emit_cmp_reg(emit_buf *b, int rn, int rm)
{
    emit_u32(b, 0xEB00001Fu | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5));
}

void emit_cmp_imm(emit_buf *b, int rn, uint32_t imm)
{
    emit_u32(b, 0xF100001Fu | ((imm & 0xFFF) << 10) | ((uint32_t)(rn & 31) << 5));
}

void emit_tst_reg(emit_buf *b, int rn, int rm)
{
    emit_u32(b, 0xEA00001Fu | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5));
}

/* ============================ 内存 ============================ */
void emit_str_imm(emit_buf *b, int rt, int rn, uint32_t off)
{
    emit_u32(b, 0xF9000000u | (((off / 8) & 0xFFF) << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

void emit_ldr_imm(emit_buf *b, int rt, int rn, uint32_t off)
{
    emit_u32(b, 0xF9400000u | (((off / 8) & 0xFFF) << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

void emit_strb_imm(emit_buf *b, int rt, int rn, uint32_t off)
{
    emit_u32(b, 0x39000000u | ((off & 0xFFF) << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

void emit_ldrb_imm(emit_buf *b, int rt, int rn, uint32_t off)
{
    emit_u32(b, 0x39400000u | ((off & 0xFFF) << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

void emit_strb_reg(emit_buf *b, int rt, int rn, int rm)
{
    emit_u32(b, 0x38206800u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

void emit_ldrb_reg(emit_buf *b, int rt, int rn, int rm)
{
    emit_u32(b, 0x38606800u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

void emit_strw_imm(emit_buf *b, int rt, int rn, uint32_t off)
{
    emit_u32(b, 0xB9000000u | (((off / 4) & 0xFFF) << 10) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

void emit_stp_pre(emit_buf *b, int rt, int rt2, int rn, int32_t off)
{
    /* STP <Xt>, <Xt2>, [<Xn|SP>, #<imm>]!  （imm 为有符号，按 imm/8 编码） */
    uint32_t imm7 = (uint32_t)((off / 8) & 0x7F);
    emit_u32(b, 0xA9800000u | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10)
                | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

void emit_ldp_post(emit_buf *b, int rt, int rt2, int rn, int32_t off)
{
    uint32_t imm7 = (uint32_t)((off / 8) & 0x7F);
    emit_u32(b, 0xA8C00000u | (imm7 << 15) | ((uint32_t)(rt2 & 31) << 10)
                | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rt & 31));
}

/* ============================ 分支 ============================ */
void emit_b(emit_buf *b, uint32_t label)
{
    add_fixup(b, b->len, label, FX_B);
    emit_u32(b, 0x14000000u);
}

void emit_bl(emit_buf *b, uint32_t label)
{
    add_fixup(b, b->len, label, FX_BL);
    emit_u32(b, 0x94000000u);
}

void emit_b_abs(emit_buf *b, uint64_t addr)
{
    /* 用于调用运行时函数：目标地址在模板符号表里已知，直接算相对偏移 */
    extern uint64_t s2a_code_va;      /* 当前代码块虚拟地址（codegen 设置） */
    int64_t delta = (int64_t)addr - (int64_t)(s2a_code_va + b->len);
    int64_t off = delta >> 2;
    if (off < -(1 << 25) || off >= (1 << 25)) {
        fprintf(stderr, "s2a: 调用目标 %#llx 超出 bl 范围（距当前 %#llx）×\n",
                (unsigned long long)addr,
                (unsigned long long)(addr - (s2a_code_va + b->len)));
        b->error = 1;
        return;
    }
    emit_u32(b, 0x94000000u | (uint32_t)(off & 0x3FFFFFF));
}

void emit_b_cond(emit_buf *b, int cond, uint32_t label)
{
    add_fixup(b, b->len, label, FX_BCOND);
    emit_u32(b, 0x54000000u | ((uint32_t)(cond & 15)));
}

void emit_cbz(emit_buf *b, int rt, uint32_t label)
{
    add_fixup(b, b->len, label, FX_CBZ);
    emit_u32(b, 0xB4000000u | (uint32_t)(rt & 31));
}

void emit_cbnz(emit_buf *b, int rt, uint32_t label)
{
    add_fixup(b, b->len, label, FX_CBNZ);
    emit_u32(b, 0xB5000000u | (uint32_t)(rt & 31));
}

void emit_cbz_w(emit_buf *b, int rt, uint32_t label)
{
    add_fixup(b, b->len, label, FX_CBZ_W);
    emit_u32(b, 0x34000000u | (uint32_t)(rt & 31));
}

void emit_cbnz_w(emit_buf *b, int rt, uint32_t label)
{
    add_fixup(b, b->len, label, FX_CBNZ_W);
    emit_u32(b, 0x35000000u | (uint32_t)(rt & 31));
}

void emit_cset(emit_buf *b, int rd, int cond)
{
    /* CSET Xd, cond  ==  CSINC Xd, XZR, XZR, invert(cond) */
    emit_u32(b, 0x9A9F07E0u | ((uint32_t)((cond ^ 1) & 15) << 12) | (uint32_t)(rd & 31));
}

void emit_ret(emit_buf *b) { emit_u32(b, 0xD65F03C0u); }
void emit_br(emit_buf *b, int rn) { emit_u32(b, 0xD61F0000u | ((uint32_t)(rn & 31) << 5)); }
void emit_blr(emit_buf *b, int rn) { emit_u32(b, 0xD63F0000u | ((uint32_t)(rn & 31) << 5)); }
void emit_svc(emit_buf *b, uint32_t imm) { emit_u32(b, 0xD4000001u | ((imm & 0xFFFF) << 5)); }
void emit_nop(emit_buf *b) { emit_u32(b, 0xD503201Fu); }
void emit_udf(emit_buf *b, uint32_t imm) { emit_u32(b, 0x00000000u | ((imm & 0xFFFF) << 5)); }

/* ============================ 反汇编（够用即可，用于 -g4 转储） ============================ */
static const char *cond_name(int c)
{
    static const char *n[] = { "eq","ne","cs","cc","mi","pl","vs","vc",
                               "hi","ls","ge","lt","gt","le","al","nv" };
    return n[c & 15];
}

int emit_disasm_one(uint64_t va, uint32_t insn, char *out, size_t cap)
{
    (void)va;
    uint32_t top = insn >> 24;
    int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
    int64_t imm;

    if ((insn & 0xFFE00000u) == 0xD4200000u) { snprintf(out, cap, "brk #%u", (insn >> 5) & 0xFFFF); return 1; }
    if (insn == 0xD503201F) { snprintf(out, cap, "nop"); return 1; }
    if (insn == 0xD65F03C0) { snprintf(out, cap, "ret"); return 1; }
    if ((insn & 0xFFFFFC1Fu) == 0xD65F0000u) { snprintf(out, cap, "ret x%d", rn); return 1; }
    if ((insn & 0xFFFFFC1Fu) == 0xD61F0000u) { snprintf(out, cap, "br x%d", rn); return 1; }
    if ((insn & 0xFFFFFC1Fu) == 0xD63F0000u) { snprintf(out, cap, "blr x%d", rn); return 1; }
    if ((insn & 0xFFE0001Fu) == 0xD4000001u) { snprintf(out, cap, "svc #%u", (insn >> 5) & 0xFFFF); return 1; }
    if ((insn & 0xFC000000u) == 0x94000000u) {
        imm = (int64_t)(insn & 0x3FFFFFF);
        if (imm & (1 << 25)) imm |= ~((int64_t)0x3FFFFFF);
        snprintf(out, cap, "bl %+lld", (long long)(imm * 4));
        return 1;
    }
    if ((insn & 0xFC000000u) == 0x14000000u) {
        imm = (int64_t)(insn & 0x3FFFFFF);
        if (imm & (1 << 25)) imm |= ~((int64_t)0x3FFFFFF);
        snprintf(out, cap, "b %+lld", (long long)(imm * 4));
        return 1;
    }
    if ((insn & 0xFF000010u) == 0x54000000u) {
        imm = (int64_t)((insn >> 5) & 0x7FFFF);
        if (imm & (1 << 18)) imm |= ~((int64_t)0x7FFFF);
        snprintf(out, cap, "b.%s %+lld", cond_name((int)(insn & 15)), (long long)(imm * 4));
        return 1;
    }
    if ((insn & 0x7E000000u) == 0x34000000u) {
        imm = (int64_t)((insn >> 5) & 0x7FFFF);
        if (imm & (1 << 18)) imm |= ~((int64_t)0x7FFFF);
        snprintf(out, cap, "cb%sz %s%d, %+lld", (insn & 0x01000000) ? "n" : "",
                 (insn & 0x80000000u) ? "x" : "w", rd, (long long)(imm * 4));
        return 1;
    }
    if ((insn & 0xFFE0001Fu) == 0xD2800000u) {
        snprintf(out, cap, "movz x%d, #%#x, lsl #%u", rd, (insn >> 5) & 0xFFFF, ((insn >> 21) & 3) * 16);
        return 1;
    }
    if ((insn & 0xFFE0001Fu) == 0xF2800000u) {
        snprintf(out, cap, "movk x%d, #%#x, lsl #%u", rd, (insn >> 5) & 0xFFFF, ((insn >> 21) & 3) * 16);
        return 1;
    }
    if ((insn & 0xFFE0FFE0u) == 0xAA0003E0u) { snprintf(out, cap, "mov x%d, x%d", rd, rm); return 1; }
    if ((insn & 0xFFC00000u) == 0x91000000u) { snprintf(out, cap, "add x%d, x%d, #%u", rd, rn, (insn >> 10) & 0xFFF); return 1; }
    if ((insn & 0xFFC00000u) == 0xD1000000u) { snprintf(out, cap, "sub x%d, x%d, #%u", rd, rn, (insn >> 10) & 0xFFF); return 1; }
    if ((insn & 0xFF200000u) == 0x8B000000u) { snprintf(out, cap, "add x%d, x%d, x%d", rd, rn, rm); return 1; }
    if ((insn & 0xFF200000u) == 0xCB000000u) { snprintf(out, cap, "sub x%d, x%d, x%d", rd, rn, rm); return 1; }
    if ((insn & 0xFF200000u) == 0xCA000000u) { snprintf(out, cap, "eor x%d, x%d, x%d", rd, rn, rm); return 1; }
    if ((insn & 0xFF200000u) == 0x8A000000u) { snprintf(out, cap, "and x%d, x%d, x%d", rd, rn, rm); return 1; }
    if ((insn & 0xFF200000u) == 0xAA000000u) { snprintf(out, cap, "orr x%d, x%d, x%d", rd, rn, rm); return 1; }
    if ((insn & 0xFFE08000u) == 0x9B000000u) { snprintf(out, cap, "mul x%d, x%d, x%d", rd, rn, rm); return 1; }
    if ((insn & 0xFFE0FC00u) == 0x9AC00C00u) { snprintf(out, cap, "sdiv x%d, x%d, x%d", rd, rn, rm); return 1; }
    if ((insn & 0xFFE0001Fu) == 0xEB00001Fu) { snprintf(out, cap, "cmp x%d, x%d", rn, rm); return 1; }
    if ((insn & 0xFFC0001Fu) == 0xF100001Fu) { snprintf(out, cap, "cmp x%d, #%u", rn, (insn >> 10) & 0xFFF); return 1; }
    if ((insn & 0xFFC00000u) == 0xF9000000u) { snprintf(out, cap, "str x%d, [x%d, #%u]", rd, rn, ((insn >> 10) & 0xFFF) * 8); return 1; }
    if ((insn & 0xFFC00000u) == 0xF9400000u) { snprintf(out, cap, "ldr x%d, [x%d, #%u]", rd, rn, ((insn >> 10) & 0xFFF) * 8); return 1; }
    if ((insn & 0xFFC00000u) == 0x39000000u) { snprintf(out, cap, "strb w%d, [x%d, #%u]", rd, rn, (insn >> 10) & 0xFFF); return 1; }
    if ((insn & 0xFFC00000u) == 0x39400000u) { snprintf(out, cap, "ldrb w%d, [x%d, #%u]", rd, rn, (insn >> 10) & 0xFFF); return 1; }
    if ((insn & 0xFFE00C00u) == 0x38200800u) { snprintf(out, cap, "strb w%d, [x%d, x%d]", rd, rn, rm); return 1; }
    if ((insn & 0x7FC00000u) == 0x28000000u || (insn & 0x7FC00000u) == 0x28800000u) {
        int32_t o = (int32_t)(((insn >> 15) & 0x7F) << 25) >> 25;
        snprintf(out, cap, "%s x%d, x%d, [sp%s#%d]", (insn & 0x00800000) ? "stp" : "ldp",
                 (int)(insn & 31), (int)((insn >> 10) & 31),
                 (insn & 0x00C00000) == 0x00C00000 ? "], " : ", ", (int)(o * 8));
        return 1;
    }
    if ((insn & 0xFFE00C00u) == 0x9A800400u && ((insn >> 10) & 3) == 1 && rn == 31 && rm == 31)
        { snprintf(out, cap, "cset x%d, %s", rd, cond_name((int)(((insn >> 12) & 15) ^ 1))); return 1; }
    if ((insn & 0xFFE0FFE0u) == 0xAA2003E0u) { snprintf(out, cap, "mvn x%d, x%d", rd, rm); return 1; }
    if ((insn & 0xFFE0FC00u) == 0xD3400000u) { snprintf(out, cap, "lsl x%d, x%d, #%u", rd, rn, (insn >> 16) & 63); return 1; }
    (void)top;
    snprintf(out, cap, ".word %#08x", insn);
    return 0;
}
