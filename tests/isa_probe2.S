    .text
    .globl pr2_bsl
    .type pr2_bsl, %function
pr2_bsl:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    bsl   v0.16b, v1.16b, v2.16b
    str q0,[x1]
    ret

    .globl pr2_bit
    .type pr2_bit, %function
pr2_bit:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    bit   v0.16b, v1.16b, v2.16b
    str q0,[x1]
    ret

    .globl pr2_bif
    .type pr2_bif, %function
pr2_bif:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    bif   v0.16b, v1.16b, v2.16b
    str q0,[x1]
    ret

    .globl pr2_urshr2
    .type pr2_urshr2, %function
pr2_urshr2:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    urshr v0.16b, v1.16b, #2
    str q0,[x1]
    ret

    .globl pr2_sri3
    .type pr2_sri3, %function
pr2_sri3:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    sri   v0.16b, v1.16b, #3
    str q0,[x1]
    ret

    .globl pr2_sqshl3
    .type pr2_sqshl3, %function
pr2_sqshl3:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    sqshl v0.16b, v1.16b, #3
    str q0,[x1]
    ret

    .globl pr2_sminv
    .type pr2_sminv, %function
pr2_sminv:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    sminv s0, v1.4s
    str q0,[x1]
    ret

    .globl pr2_smaxv
    .type pr2_smaxv, %function
pr2_smaxv:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    smaxv s0, v1.4s
    str q0,[x1]
    ret

    .globl pr2_uminv
    .type pr2_uminv, %function
pr2_uminv:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    uminv s0, v1.4s
    str q0,[x1]
    ret

    .globl pr2_rbit
    .type pr2_rbit, %function
pr2_rbit:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    rbit  v0.16b, v1.16b
    str q0,[x1]
    ret

    .globl pr2_clz
    .type pr2_clz, %function
pr2_clz:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    clz   v0.16b, v1.16b
    str q0,[x1]
    ret

    .globl pr2_cls
    .type pr2_cls, %function
pr2_cls:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    cls   v0.16b, v1.16b
    str q0,[x1]
    ret

    .globl pr2_addp
    .type pr2_addp, %function
pr2_addp:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    addp  v0.16b, v1.16b, v2.16b
    str q0,[x1]
    ret

    .globl pr2_addlv
    .type pr2_addlv, %function
pr2_addlv:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    saddlv h0, v1.16b
    str q0,[x1]
    ret

    .globl pr2_uaddlv
    .type pr2_uaddlv, %function
pr2_uaddlv:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    uaddlv h0, v1.16b
    str q0,[x1]
    ret

    .globl pr2_uqxtn
    .type pr2_uqxtn, %function
pr2_uqxtn:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    uqxtn v0.8b, v1.8h
    str q0,[x1]
    ret

    .globl pr2_sqxtn
    .type pr2_sqxtn, %function
pr2_sqxtn:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    sqxtn v0.8b, v1.8h
    str q0,[x1]
    ret

    .globl pr2_fcvtl
    .type pr2_fcvtl, %function
pr2_fcvtl:
    ldr q0,[x0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    fcvtl v0.4s, v1.4h
    str q0,[x1]
    ret

