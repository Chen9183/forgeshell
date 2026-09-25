// 与 GCC -O2 生成的字节填充向量链完全一致（t2.c / musl memset 内核同款）
// x0 = 目标 16 字节，x1 = 4×u32 种子
    .text
    .globl  vec_chain
    .type   vec_chain, %function
vec_chain:
    ldr     q2, [x1]
    movi    v17.4s, #0x10
    movi    v16.4s, #0x4
    movi    v7.4s, #0x8
    movi    v6.4s, #0xc
    movi    v5.16b, #0x1
    mov     v0.16b, v2.16b
    add     v2.4s, v2.4s, v17.4s
    add     v1.4s, v0.4s, v7.4s
    add     v3.4s, v0.4s, v6.4s
    add     v4.4s, v0.4s, v16.4s
    uzp1    v1.8h, v1.8h, v3.8h
    uzp1    v0.8h, v0.8h, v4.8h
    uzp1    v0.16b, v0.16b, v1.16b
    shl     v1.16b, v0.16b, #1
    add     v0.16b, v1.16b, v0.16b
    add     v0.16b, v0.16b, v5.16b
    str     q0, [x0]
    ret
    .size   vec_chain, .-vec_chain
