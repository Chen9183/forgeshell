// 受控输入探针：x0=输入(256B, 递增模式) x1=输出(256B)
    .text
    .macro PROBE name
    .globl  pr_\name
    .type   pr_\name, %function
pr_\name:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    .endm
    PROBE sri3
    sri     v0.16b, v1.16b, #3
    str q0,[x1]
    ret
    PROBE urshr2
    urshr   v0.16b, v1.16b, #2
    str q0,[x1]
    ret
    PROBE sqshl1
    sqshl   v0.16b, v1.16b, #1
    str q0,[x1]
    ret
    PROBE ushll1
    ushll   v0.8h, v1.8b, #1
    str q0,[x1]
    ret
    PROBE movi4h
    movi    v0.4h, #0x5
    str q0,[x1]
    ret
    PROBE mvni4s
    mvni    v0.4s, #0x1
    str q0,[x1]
    ret
    PROBE dupelem
    dup     v1.16b, v0.b[5]
    str q1,[x1]
    ret
    PROBE uqxtn
    uqxtn   v0.8b, v1.8h
    str q0,[x1]
    ret
    PROBE rbit
    rbit    v0.16b, v1.16b
    str q0,[x1]
    ret
    PROBE fcvtl
    fcvtl   v0.4s, v1.4h
    str q0,[x1]
    ret
    PROBE sminv
    sminv   s0, v1.4s
    str q0,[x1]
    ret
    PROBE smaxv
    smaxv   b0, v1.16b
    str q0,[x1]
    ret
    PROBE addp
    addp    v0.16b, v1.16b, v2.16b
    str q0,[x1]
    ret
    PROBE bsl
    bsl     v0.16b, v1.16b, v2.16b
    str q0,[x1]
    ret
    PROBE bif
    bif     v0.16b, v1.16b, v2.16b
    str q0,[x1]
    ret
    PROBE ext0
    ext     v0.16b, v1.16b, v2.16b, #0
    str q0,[x1]
    ret
    PROBE ext2
    ext     v0.8b, v1.8b, v2.8b, #2
    str q0,[x1]
    ret
    PROBE ushr2
    ushr    v0.16b, v1.16b, #2
    str q0,[x1]
    ret
    PROBE sshr2
    sshr    v0.16b, v1.16b, #2
    str q0,[x1]
    ret
    PROBE shl1
    shl     v0.16b, v1.16b, #1
    str q0,[x1]
    ret
