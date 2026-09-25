    .text
    .globl fp_cmp_imm
fp_cmp_imm:                       // x0=输入(8B) x1=输出
    ldr x2,[x0]
    cmp x2,#1
    cset w4,cs                    // 期望 1（无借位）
    cset w5,cc
    stp w4,w5,[x1]
    ret
    .globl fp_cmp_reg
fp_cmp_reg:
    ldp x2,x3,[x0]
    cmp x2,x3
    cset w4,cs
    cset w5,cc
    stp w4,w5,[x1]
    ret
    .globl fp_adds_imm
fp_adds_imm:
    ldr x2,[x0]
    adds x3,x2,#0x100
    cset w4,cs                    // 0x0102...0708 + 0x100 → 不进位(64位)
    str w4,[x1]
    ret
