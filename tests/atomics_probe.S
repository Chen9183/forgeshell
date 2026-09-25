    .text
    .globl  at_bic_stlxr
at_bic_stlxr:                  // x0=内存(8B) x1=输出
    ldr     w2, [x0]
    ldaxr   w3, [x0]
    bic     w3, w3, #0x4       // 清 bit2
    stlxr   w4, w3, [x0]
    stp     w4, w3, [x1]       // 状态与存进去的值
    ldr     w5, [x0]
    str     w5, [x1, #8]       // 重新读回
    ret
    .globl  at_orr_stlxr
at_orr_stlxr:
    ldr     w2, [x0]
    ldaxr   w3, [x0]
    orr     w3, w3, #0x10      // 置 bit4
    stlxr   w4, w3, [x0]
    stp     w4, w3, [x1]
    ldr     w5, [x0]
    str     w5, [x1, #8]
    ret
    .globl  at_add_stlxr
at_add_stlxr:
    ldaxr   x3, [x0]
    add     x3, x3, #1
    stlxr   w4, x3, [x0]
    stp     w4, w3, [x1]
    ldr     x5, [x0]
    str     x5, [x1, #8]
    ret
    .globl  at_cas_loop
at_cas_loop:                   // 模拟 a_cas：循环直到 store 成功
    mov     w6, #0
1:  ldaxr   w3, [x0]
    add     w3, w3, #1
    stlxr   w4, w3, [x0]
    cbnz    w4, 1b
    add     w6, w6, #1
    str     w6, [x1]           // 循环了几次
    ldr     w5, [x0]
    str     w5, [x1, #4]
    ret
