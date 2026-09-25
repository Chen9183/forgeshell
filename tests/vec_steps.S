// 逐条快照：把向量链的每个中间结果存到 x0 + 16*k
    .text
    .globl  vec_steps
    .type   vec_steps, %function
vec_steps:
    ldr     q2, [x1]
    movi    v17.4s, #0x10
    movi    v16.4s, #0x4
    movi    v7.4s, #0x8
    movi    v6.4s, #0xc
    movi    v5.16b, #0x1
    mov     v0.16b, v2.16b
    str     q0, [x0, #0]        // 1 mov(ORR)
    add     v2.4s, v2.4s, v17.4s
    str     q2, [x0, #16]       // 2 add 4s
    add     v1.4s, v0.4s, v7.4s
    str     q1, [x0, #32]       // 3 add 4s
    add     v3.4s, v0.4s, v6.4s
    str     q3, [x0, #48]       // 4 add 4s
    add     v4.4s, v0.4s, v16.4s
    str     q4, [x0, #64]       // 5 add 4s
    uzp1    v1.8h, v1.8h, v3.8h
    str     q1, [x0, #80]       // 6 uzp1 8h
    uzp1    v0.8h, v0.8h, v4.8h
    str     q0, [x0, #96]       // 7 uzp1 8h
    uzp1    v0.16b, v0.16b, v1.16b
    str     q0, [x0, #112]      // 8 uzp1 16b
    shl     v1.16b, v0.16b, #1
    str     q1, [x0, #128]      // 9 shl
    add     v0.16b, v1.16b, v0.16b
    str     q0, [x0, #144]      // 10 add 16b
    add     v0.16b, v0.16b, v5.16b
    str     q0, [x0, #160]      // 11 add 16b
    ret
    .size   vec_steps, .-vec_steps
