#!/usr/bin/env python3
"""批量指令差分测试生成器。

思路（一次扫掉所有解码错误，而不是跟着真实二进制一条条撞）：
  每条测试 = 一小段汇编，统一接口 x0=输入(256B) x1=输出(256B)，
  跑完把结果写进 out。原生跑一遍得到期望值，模拟器跑一遍对拍。
  输出用 write(2) 手写十六进制，绕开 printf（模拟器 printf 自身也可能有 bug）。
用法: python3 gen_isa_bulk.py  → 生成 isa_bulk.S 与 isa_bulk.c
"""
import sys

# ---- 每条测试: (名字, [汇编行]) ----
T = []
def t(name, *lines): T.append((name, list(lines)))

V = "ldr q0,[x0]\n ldr q1,[x0,#16]\n ldr q2,[x0,#32]\n"
V3 = V
def vt(name, op):   t(name, *((V + " " + op + "\n str q0,[x1]").split("\n")))
def vq(name, op):   t(name, *((V + " " + op + "\n str q0,[x1]").split("\n")))
def vd(name, op):   t(name, *((V + " " + op + "\n str q0,[x1]\n str q1,[x1,#16]").split("\n")))

# ---- 向量三同组：加减乘、逻辑、比较 ----
for op in ["add","sub","mul","smax","smin","umax","umin","sabd","uabd","cmgt","cmge","cmeq",
           "cmhi","cmhs","cmlt","cmle","sqadd","uqadd","sqsub","uqsub","shadd","uhadd",
           "sqdmlal","mla","mls","smaxp","addp"]:
    for f in ["16b","8h","4s","2d"]:
        vt(f"{op}_{f}", f"{op} v0.{f}, v1.{f}, v2.{f}")
for op in ["and","bic","orr","orn","eor","bsl","bit","bif"]:
    for f in ["16b","8h"]:
        vt(f"{op}_{f}", f"{op} v0.{f}, v1.{f}, v2.{f}")
for op in ["add","sub","mul","cmeq","cmgt","and","orr","eor"]:
    for f in ["8b","4h","2s"]:      # Q=0（64 位）形式
        vt(f"{op}_{f}", f"{op} v0.{f}, v1.{f}, v2.{f}")
# ---- 置换 / EXT ----
for op in ["uzp1","uzp2","zip1","zip2","trn1","trn2"]:
    for f in ["16b","8h","4s","2d","8b","4h"]:
        vt(f"{op}_{f}", f"{op} v0.{f}, v1.{f}, v2.{f}")
for k in [0,1,3,7,8,15]:
    vt(f"ext16b_{k}", f"ext v0.16b, v1.16b, v2.16b, #{k}")
vt("ext8b_2", "ext v0.8b, v1.8b, v2.8b, #2")
# ---- 按立即数移位 / 加宽 / 窄化 ----
for sh in [1,3,7]:
    vt(f"shl_16b_{sh}", f"shl v0.16b, v1.16b, #{sh}")
    vt(f"ushr_16b_{sh}", f"ushr v0.16b, v1.16b, #{sh}")
    vt(f"sshr_16b_{sh}", f"sshr v0.16b, v1.16b, #{sh}")
    vt(f"sri_16b_{sh}",  f"sri  v0.16b, v1.16b, #{sh}")
for sh in [1,5,11]:
    vt(f"shl_8h_{sh}", f"shl v0.8h, v1.8h, #{sh}")
    vt(f"sshr_8h_{sh}", f"sshr v0.8h, v1.8h, #{sh}")
    vt(f"ushr_8h_{sh}", f"ushr v0.8h, v1.8h, #{sh}")
for sh in [1,10]:
    vt(f"shl_4s_{sh}", f"shl v0.4s, v1.4s, #{sh}")
    vt(f"sshr_4s_{sh}", f"sshr v0.4s, v1.4s, #{sh}")
vt("shl_2d_3", "shl v0.2d, v1.2d, #3")
vt("sshr_2d_5", "sshr v0.2d, v1.2d, #5")
vt("usra_16b_2", "usra v0.16b, v1.16b, #2")
vt("ssra_16b_2", "ssra v0.16b, v1.16b, #2")
vt("urshr_16b_2", "urshr v0.16b, v1.16b, #2")
vt("srsra_16b_2", "srsra v0.16b, v1.16b, #2")
vt("sqshl_16b_1", "sqshl v0.16b, v1.16b, #1")
vt("ushll_8h_1", "ushll v0.8h, v1.8b, #1")
vt("sshll_8h_1", "sshll v0.8h, v1.8b, #1")
vt("ushll_4s_2", "ushll v0.4s, v1.4h, #2")
vt("sshll_4s_2", "sshll v0.4s, v1.4h, #2")
vt("xtn_8b",  "xtn  v0.8b, v1.8h")
vt("xtn2_16b", "xtn2 v0.16b, v1.8h")
vt("sqxtn_8b", "sqxtn v0.8b, v1.8h")
vt("uqxtn_8b", "uqxtn v0.8b, v1.8h")
vt("sqxtun_8b", "sqxtun v0.8b, v1.8h")
# ---- MOVI / MVNI / DUP / INS / UMOV / 立即数 ----
vt("movi_16b_3f", "movi v0.16b, #0x3f")
vt("movi_8b_1",   "movi v0.8b, #0x1")
vt("movi_4s_12",  "movi v0.4s, #0xc")
vt("movi_2s_1",   "movi v0.2s, #0x1")
vt("movi_4h_5",   "movi v0.4h, #0x5")
vt("movi_8h_5",   "movi v0.8h, #0x5")
vt("movi_2d_1",   "movi v0.2d, #0x1")
vt("mvni_4s_1",   "mvni v0.4s, #0x1")
vt("movi_4s_lsl8", "movi v0.4s, #0x1, lsl #8")
t("dup_16b_w", " ldr x2,[x0]\n dup v0.16b, w2\n str q0,[x1]")
t("dup_8h_w",  " ldr w2,[x0]\n dup v0.8h, w2\n str q0,[x1]")
t("dup_4s_w",  " ldr w2,[x0]\n dup v0.4s, w2\n str q0,[x1]")
t("dup_2d_x",  " ldr x2,[x0]\n dup v0.2d, x2\n str q0,[x1]")
t("dup_elem_b", " ldr q0,[x0]\n dup v1.16b, v0.b[5]\n str q1,[x1]")
t("dup_elem_h", " ldr q0,[x0]\n dup v1.8h, v0.h[3]\n str q1,[x1]")
t("dup_elem_s", " ldr q0,[x0]\n dup v1.4s, v0.s[2]\n str q1,[x1]")
t("ins_elem", " ldr q0,[x0]\n ldr q1,[x0,#16]\n ins v0.b[2], v1.b[7]\n str q0,[x1]")
t("umov_b", " ldr q0,[x0]\n umov w2, v0.b[9]\n str w2,[x1]")
t("umov_h", " ldr q0,[x0]\n umov w2, v0.h[3]\n str w2,[x1]")
t("umov_s", " ldr q0,[x0]\n umov w2, v0.s[2]\n str w2,[x1]")
t("umov_d", " ldr q0,[x0]\n umov x2, v0.d[1]\n str x2,[x1]")
t("smov_b", " ldr q0,[x0]\n smov w2, v0.b[9]\n str w2,[x1]")
t("ins_gen_b", " ldr q0,[x0]\n ldr w2,[x0,#20]\n ins v0.b[3], w2\n str q0,[x1]")
t("ins_gen_s", " ldr q0,[x0]\n ldr w2,[x0,#20]\n ins v0.s[2], w2\n str q0,[x1]")
t("mov_d1_x", " ldr q0,[x0]\n ldr x2,[x0,#24]\n mov v0.d[1], x2\n str q0,[x1]")
t("mov_x_d1", " ldr q0,[x0]\n mov x2, v0.d[1]\n str x2,[x1]")
# ---- 向量归约 / 成对 ----
vt("addv_16b", "addv b0, v1.16b")
vt("addv_8h",  "addv h0, v1.8h")
vt("addv_4s",  "addv s0, v1.4s")
vt("saddlv_16b", "saddlv h0, v1.16b")
vt("uaddlv_16b", "uaddlv h0, v1.16b")
vt("smaxv_16b", "smaxv b0, v1.16b")
vt("umaxv_16b", "umaxv b0, v1.16b")
vt("sminv_4s", "sminv s0, v1.4s")
vt("cnt_16b", "cnt v0.16b, v1.16b")
vt("rbit_16b", "rbit v0.16b, v1.16b")
vt("rev16_16b", "rev16 v0.16b, v1.16b")
vt("rev32_16b", "rev32 v0.16b, v1.16b")
vt("rev64_16b", "rev64 v0.16b, v1.16b")
vt("abs_16b", "abs v0.16b, v1.16b")
vt("neg_16b", "neg v0.16b, v1.16b")
vt("mvn_16b", "mvn v0.16b, v1.16b")
vt("not_16b", "not v0.16b, v1.16b")
vt("cls_16b", "cls v0.16b, v1.16b")
vt("clz_16b", "clz v0.16b, v1.16b")
vt("saddlp_16b", "saddlp v0.8h, v1.16b")
vt("uaddlp_16b", "uaddlp v0.8h, v1.16b")
vt("xtl_8h", "mov v0.8b, v1.8b")   # mov 别名
# ---- 向量浮点 ----
vt("fadd_4s", "fadd v0.4s, v1.4s, v2.4s")
vt("fsub_4s", "fsub v0.4s, v1.4s, v2.4s")
vt("fmul_4s", "fmul v0.4s, v1.4s, v2.4s")
vt("fdiv_4s", "fdiv v0.4s, v1.4s, v2.4s")
vt("fadd_2d", "fadd v0.2d, v1.2d, v2.2d")
vt("fmul_2d", "fmul v0.2d, v1.2d, v2.2d")
vt("fmax_4s", "fmax v0.4s, v1.4s, v2.4s")
vt("fmin_4s", "fmin v0.4s, v1.4s, v2.4s")
vt("fmla_4s", "fmla v0.4s, v1.4s, v2.4s")
vt("fabs_4s", "fabs v0.4s, v1.4s")
vt("fneg_4s", "fneg v0.4s, v1.4s")
vt("fsqrt_4s", "fsqrt v0.4s, v1.4s")
vt("fcmeq_4s", "fcmeq v0.4s, v1.4s, v2.4s")
vt("fcvtl_4s", "fcvtl v0.4s, v1.4h")
vt("scvtf_4s", "scvtf v0.4s, v1.4s")
vt("ucvtf_4s", "ucvtf v0.4s, v1.4s")
vt("fcvtzs_4s", "fcvtzs v0.4s, v1.4s")
# ---- 标量浮点 ----
t("fadd_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n fadd s2, s0, s1\n str s2,[x1]")
t("fsub_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n fsub s2, s0, s1\n str s2,[x1]")
t("fmul_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n fmul s2, s0, s1\n str s2,[x1]")
t("fdiv_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n fdiv s2, s0, s1\n str s2,[x1]")
t("fadd_d", " ldr d0,[x0]\n ldr d1,[x0,#8]\n fadd d2, d0, d1\n str d2,[x1]")
t("fmul_d", " ldr d0,[x0]\n ldr d1,[x0,#8]\n fmul d2, d0, d1\n str d2,[x1]")
t("fabs_s", " ldr s0,[x0]\n fabs s1, s0\n str s1,[x1]")
t("fneg_s", " ldr s0,[x0]\n fneg s1, s0\n str s1,[x1]")
t("fsqrt_s", " ldr s0,[x0]\n fsqrt s1, s0\n str s1,[x1]")
t("fmin_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n fmin s2, s0, s1\n str s2,[x1]")
t("fmax_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n fmax s2, s0, s1\n str s2,[x1]")
t("frintm_s", " ldr s0,[x0]\n frintm s1, s0\n str s1,[x1]")
t("frintn_s", " ldr s0,[x0]\n frintn s1, s0\n str s1,[x1]")
t("fcvtzs_s_w", " ldr s0,[x0]\n fcvtzs w2, s0\n str w2,[x1]")
t("fcvtzu_s_w", " ldr s0,[x0]\n fcvtzu w2, s0\n str w2,[x1]")
t("scvtf_w_s", " ldr w2,[x0]\n scvtf s0, w2\n str s0,[x1]")
t("ucvtf_w_s", " ldr w2,[x0]\n ucvtf s0, w2\n str s0,[x1]")
t("fcvt_d_s", " ldr s0,[x0]\n fcvt d0, s0\n str d0,[x1]")
t("fcvt_s_d", " ldr d0,[x0]\n fcvt s0, d0\n str s0,[x1]")
t("fmov_w_s", " ldr s0,[x0]\n fmov w2, s0\n str w2,[x1]")
t("fmov_s_w", " ldr w2,[x0]\n fmov s0, w2\n str s0,[x1]")
t("fmov_x_d", " ldr d0,[x0]\n fmov x2, d0\n str x2,[x1]")
t("fmov_d_x", " ldr x2,[x0]\n fmov d0, x2\n str d0,[x1]")
t("fcmp_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n fcmp s0, s1\n cset w3, gt\n cset w4, eq\n stp w3,w4,[x1]")
t("fmadd_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n ldr s2,[x0,#8]\n fmadd s3,s0,s1,s2\n str s3,[x1]")
t("fmsub_s", " ldr s0,[x0]\n ldr s1,[x0,#4]\n ldr s2,[x0,#8]\n fmsub s3,s0,s1,s2\n str s3,[x1]")
# ---- 向量访存：各种寻址 ----
t("ldr_q_off", " ldr q0,[x0,#32]\n str q0,[x1]")
t("ldr_q_post", " ldr q0,[x0],#16\n str q0,[x1]")
t("ldr_q_pre", " ldr q0,[x0,#16]!\n str q0,[x1]")
t("str_q_post", " ldr q0,[x0]\n str q0,[x1],#16\n str q0,[x1]")
t("str_q_pre", " ldr q0,[x0]\n str q0,[x1,#16]!\n sub x1,x1,#16\n str q0,[x1]")
t("ldr_d_off", " ldr d0,[x0,#24]\n str d0,[x1]")
t("ldr_s_off", " ldr s0,[x0,#12]\n str s0,[x1]")
t("ldr_h_off", " ldr h0,[x0,#6]\n str h0,[x1]")
t("ldr_b_off", " ldr b0,[x0,#3]\n str b0,[x1]")
t("ldr_q_reg", " mov x2,#16\n ldr q0,[x0,x2]\n str q0,[x1]")
t("str_q_reg", " mov x2,#32\n ldr q0,[x0]\n str q0,[x0,x2]\n ldr q1,[x0,#32]\n str q1,[x1]")
t("ldp_q", " ldp q0,q1,[x0,#32]\n str q0,[x1]\n str q1,[x1,#16]")
t("stp_q", " ldp q0,q1,[x0]\n stp q0,q1,[x1]")
t("stp_q_post", " ldp q0,q1,[x0]\n stp q0,q1,[x1],#32\n stp q0,q1,[x1]")
t("ldp_d", " ldp d0,d1,[x0,#16]\n str d0,[x1]\n str d1,[x1,#8]")
t("stp_s", " ldp s0,s1,[x0]\n stp s0,s1,[x1]")
t("ld1_16b", " ld1 {v0.16b},[x0]\n str q0,[x1]")
t("st1_16b", " ld1 {v0.16b},[x0]\n st1 {v0.16b},[x1]")
t("ld1_multi", " ld1 {v0.16b,v1.16b},[x0]\n str q0,[x1]\n str q1,[x1,#16]")
t("ld2_16b", " ld2 {v0.16b,v1.16b},[x0]\n str q0,[x1]\n str q1,[x1,#16]")
t("st2_16b", " ld2 {v0.16b,v1.16b},[x0]\n st2 {v0.16b,v1.16b},[x1]")
t("ld3_16b", " ld3 {v0.16b,v1.16b,v2.16b},[x0]\n str q0,[x1]\n str q1,[x1,#16]\n str q2,[x1,#32]")
t("ld4_16b", " ld4 {v0.16b,v1.16b,v2.16b,v3.16b},[x0]\n str q0,[x1]\n str q1,[x1,#16]\n str q2,[x1,#32]\n str q3,[x1,#48]")
t("ld1r_16b", " ld1r {v0.16b},[x0]\n str q0,[x1]")
t("ld2r_8h", " ld2r {v0.8h,v1.8h},[x0]\n str q0,[x1]\n str q1,[x1,#16]")
t("ld1_single", " ld1 {v0.b}[3],[x0]\n str q0,[x1]")
t("st1_single", " ld1 {v0.16b},[x0]\n st1 {v0.b}[3],[x1]\n ldr q1,[x1]\n str q1,[x1,#16]")
# ---- 通用访存寻址 ----
t("ldr_x_off", " ldr x2,[x0,#40]\n str x2,[x1]")
t("ldr_w_off", " ldr w2,[x0,#44]\n str w2,[x1]")
t("ldr_x_post", " ldr x2,[x0],#8\n str x2,[x1]")
t("ldr_x_pre", " ldr x2,[x0,#8]!\n str x2,[x1]")
t("ldur_x", " ldur x2,[x0,#13]\n str x2,[x1]")
t("ldrsb_w", " ldrsb w2,[x0,#7]\n str w2,[x1]")
t("ldrsb_x", " ldrsb x2,[x0,#7]\n str x2,[x1]")
t("ldrsh_w", " ldrsh w2,[x0,#6]\n str w2,[x1]")
t("ldrsh_x", " ldrsh x2,[x0,#6]\n str x2,[x1]")
t("ldrsw_x", " ldrsw x2,[x0,#12]\n str x2,[x1]")
t("ldrb_w", " ldrb w2,[x0,#9]\n str w2,[x1]")
t("ldrh_w", " ldrh w2,[x0,#10]\n str w2,[x1]")
t("ldr_reg_off", " mov x3,#24\n ldr x2,[x0,x3]\n str x2,[x1]")
t("ldr_reg_off_ext", " mov w3,#24\n ldr x2,[x0,w3,uxtw]\n str x2,[x1]")
t("ldr_reg_lsl", " mov w3,#3\n ldr x2,[x0,w3,sxtw #3]\n str x2,[x1]")
t("stp_post", " ldp x2,x3,[x0]\n stp x2,x3,[x1],#16\n stp x2,x3,[x1]")
t("strb_post", " ldrb w2,[x0]\n strb w2,[x1],#1\n ldrb w3,[x1]\n strb w3,[x1,#1]")
# ---- 位域 / 移位 / 杂项 ----
t("sbfx", " ldr x2,[x0]\n sbfx x3,x2,#5,#9\n str x3,[x1]")
t("ubfx", " ldr x2,[x0]\n ubfx x3,x2,#5,#9\n str x3,[x1]")
t("bfi", " ldr x2,[x0]\n ldr x3,[x0,#8]\n bfi x2,x3,#5,#9\n str x2,[x1]")
t("bfxil", " ldr x2,[x0]\n ldr x3,[x0,#8]\n bfxil x2,x3,#5,#9\n str x2,[x1]")
t("ubfiz", " ldr x2,[x0]\n ubfiz x3,x2,#3,#20\n str x3,[x1]")
t("sbfiz", " ldr x2,[x0]\n sbfiz x3,x2,#3,#20\n str x3,[x1]")
t("extr", " ldr x2,[x0]\n ldr x3,[x0,#8]\n extr x4,x2,x3,#13\n str x4,[x1]")
t("udiv", " ldr x2,[x0]\n ldr x3,[x0,#8]\n udiv x4,x2,x3\n str x4,[x1]")
t("sdiv", " ldr x2,[x0]\n ldr x3,[x0,#8]\n sdiv x4,x2,x3\n str x4,[x1]")
t("madd", " ldp x2,x3,[x0]\n ldr x4,[x0,#16]\n madd x5,x2,x3,x4\n str x5,[x1]")
t("msub", " ldp x2,x3,[x0]\n ldr x4,[x0,#16]\n msub x5,x2,x3,x4\n str x5,[x1]")
t("smull", " ldp w2,w3,[x0]\n smull x4,w2,w3\n str x4,[x1]")
t("umull", " ldp w2,w3,[x0]\n umull x4,w2,w3\n str x4,[x1]")
t("smulh", " ldp x2,x3,[x0]\n smulh x4,x2,x3\n str x4,[x1]")
t("umulh", " ldp x2,x3,[x0]\n umulh x4,x2,x3\n str x4,[x1]")
t("smaddl", " ldp w2,w3,[x0]\n ldr x4,[x0,#16]\n smaddl x5,w2,w3,x4\n str x5,[x1]")
t("umaddl", " ldp w2,w3,[x0]\n ldr x4,[x0,#16]\n umaddl x5,w2,w3,x4\n str x5,[x1]")
t("smulh_neg", " ldr x2,[x0]\n ldr x3,[x0,#8]\n smulh x4,x2,x3\n str x4,[x1]")
t("clz", " ldr x2,[x0]\n clz x3,x2\n str x3,[x1]")
t("rbit", " ldr x2,[x0]\n rbit x3,x2\n str x3,[x1]")
t("rev16", " ldr x2,[x0]\n rev16 x3,x2\n str x3,[x1]")
t("rev32", " ldr x2,[x0]\n rev32 x3,x2\n str x3,[x1]")
t("rev", " ldr x2,[x0]\n rev x3,x2\n str x3,[x1]")
t("csel", " ldp x2,x3,[x0]\n cmp x2,x3\n csel x4,x2,x3,hi\n str x4,[x1]")
t("csinc", " ldp x2,x3,[x0]\n cmp x2,x3\n csinc x4,x2,x3,lt\n str x4,[x1]")
t("csneg", " ldp x2,x3,[x0]\n cmp x2,x3\n csneg x4,x2,x3,eq\n str x4,[x1]")
t("csinv", " ldp x2,x3,[x0]\n cmp x2,x3\n csinv x4,x2,x3,ne\n str x4,[x1]")
t("cset", " ldr x2,[x0]\n cmp x2,#5\n cset x3,gt\n str x3,[x1]")
t("csetm", " ldr x2,[x0]\n cmp x2,#5\n csetm x3,ls\n str x3,[x1]")
t("ccmp", " ldr x2,[x0]\n ldr x3,[x0,#8]\n cmp x2,#7\n ccmp x3,#9,#2,eq\n cset x4,le\n str x4,[x1]")
t("ccmn", " ldr x2,[x0]\n ldr x3,[x0,#8]\n cmp x2,#7\n ccmn x3,#9,#2,eq\n cset x4,gt\n str x4,[x1]")
t("adc", " ldp x2,x3,[x0]\n adds x2,x2,x3\n adc x4,x2,x3\n str x4,[x1]")
t("sbc", " ldp x2,x3,[x0]\n subs x2,x2,x3\n sbc x4,x2,x3\n str x4,[x1]")
t("ngc", " ldr x2,[x0]\n cmp x2,#1\n ngc x3,x2\n str x3,[x1]")
t("tst_br", " ldr x2,[x0]\n tst x2,#0xf0\n cset x3,ne\n str x3,[x1]")
t("cmn", " ldr x2,[x0]\n cmn x2,#7\n cset x3,mi\n str x3,[x1]")
t("tbz", " ldr x2,[x0]\n tbz w2,#3,1f\n mov x3,#1\n b 2f\n1:\n mov x3,#0\n2:\n str x3,[x1]")
t("tbnz", " ldr x2,[x0]\n tbnz w2,#7,1f\n mov x3,#1\n b 2f\n1:\n mov x3,#0\n2:\n str x3,[x1]")
t("madd32", " ldp w2,w3,[x0]\n ldr w4,[x0,#16]\n madd w5,w2,w3,w4\n str w5,[x1]")
t("neg_lsl", " ldr x2,[x0]\n neg x3,x2, lsl #3\n str x3,[x1]")
t("lsl_reg", " ldr x2,[x0]\n ldr x3,[x0,#8]\n lsl x4,x2,x3\n str x4,[x1]")
t("asr_reg", " ldr x2,[x0]\n ldr x3,[x0,#8]\n asr x4,x2,x3\n str x4,[x1]")
t("lsr_reg", " ldr x2,[x0]\n ldr x3,[x0,#8]\n lsr x4,x2,x3\n str x4,[x1]")
t("ror_reg", " ldr x2,[x0]\n ldr x3,[x0,#8]\n ror x4,x2,x3\n str x4,[x1]")
t("movk", " movz x2,#0x1234\n movk x2,#0xabcd, lsl #16\n str x2,[x1]")
t("movn", " movn x2,#0x1234\n str x2,[x1]")
t("adr_adrp", " adr x2,1f\n1:\n adr x3,1b\n sub x2,x2,x3\n str x2,[x1]")
t("ands_flags", " ldr x2,[x0]\n ands x3,x2,#0xf0000000\n cset x4,ne\n str x4,[x1]")
t("sxtb_sxth", " ldrb w2,[x0,#7]\n sxtb x3,w2\n sxth x4,w2\n stp x3,x4,[x1]")
t("ubfiz_32", " ldr w2,[x0]\n ubfiz w3,w2,#3,#20\n str w3,[x1]")
# ---- 原子 / 独占 ----
t("ldxr_stxr", " ldr x2,[x0]\n ldaxr x3,[x0]\n stlxr w4,x2,[x1]\n stp x3,w4,[x1,#16]")
t("ldxrb_stxrb", " ldrb w2,[x0]\n ldxrb w3,[x0]\n stxrb w4,w2,[x1]\n stp x3,x4,[x1,#16]")
t("swp_s", " ldr w2,[x0]\n ldaxr w3,[x0]\n stlxr w4,w2,[x1]\n str w3,[x1,#16]\n str w4,[x1,#20]")
t("ldar_stlr", " ldar x2,[x0]\n stlr x2,[x1]")
t("dmb_barriers", " ldr x2,[x0]\n dmb ish\n dsb sy\n isb\n str x2,[x1]")
t("ldapr", " ldapr x2,[x0]\n str x2,[x1]")
# ---- 生成汇编与驱动 ----
def render(tests):
    asm = ["// 由 tests/gen_isa_bulk.py 生成：批量指令差分测试（勿手改）",
           "    .text", ""]
    for name, lines in tests:
        asm.append(f"    .globl  isa_{name}")
        asm.append(f"    .type   isa_{name}, %function")
        asm.append(f"isa_{name}:")
        for ln in lines:
            asm.append("    " + ln)
        asm.append("    ret")
        asm.append("")
    return "\n".join(asm) + "\n"

def prune(tests):
    """用汇编器逐条验证，剔除本架构不支持的形式（比按行号定位稳）。"""
    import subprocess, tempfile, os
    keep, drop = [], []
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "t.S")
        for name, lines in tests:
            body = "    .text\n" + f"isa_{name}:\n" + "".join("    " + l + "\n" for l in lines) + "    ret\n"
            open(src, "w").write(body)
            r = subprocess.run(["as", "-o", os.devnull, src], capture_output=True)
            (keep if r.returncode == 0 else drop).append((name, lines))
    if drop:
        print(f"  剔除 {len(drop)} 条本架构不支持的形式：" + ", ".join(n for n, _ in drop[:8])
              + (" …" if len(drop) > 8 else ""))
    return keep

def main():
    tests = prune(T)
    open("isa_bulk.S", "w").write(render(tests))

    c = ['/* 由 tests/gen_isa_bulk.py 生成：批量指令差分测试驱动（勿手改） */',
         '#include <unistd.h>', '#include <string.h>', '',
         'typedef void (*fn_t)(unsigned char *, unsigned char *);', '',
         'struct ent { const char *name; fn_t fn; };']
    for name, _ in tests:
        c.append(f'extern void isa_{name}(unsigned char *, unsigned char *);')
    c.append("")
    c.append("static struct ent tbl[] = {")
    for name, _ in tests:
        c.append(f'    {{ "{name}", isa_{name} }},')
    c.append("};")
    c.append("""
static char out[1 << 16];
static int  olen;
static const char hx[] = "0123456789abcdef";
static void put(const char *s) { while (*s) out[olen++] = *s++; }
static void puthex(const unsigned char *p, int n)
{
    for (int i = 0; i < n; i++) { out[olen++] = hx[p[i] >> 4]; out[olen++] = hx[p[i] & 15]; }
}
int main(int argc, char **argv)
{
    unsigned lo = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
    unsigned n0 = argc > 2 ? (unsigned)atoi(argv[2]) : (unsigned)(sizeof tbl / sizeof tbl[0]);
    unsigned char in[256], res[256];
    /* 固定伪随机输入 + 边界值，保证可复现 */
    unsigned x = 0x12345678u;
    for (int i = 0; i < 256; i++) { x = x * 1103515245u + 12345u; in[i] = (unsigned char)(x >> 16); }
    in[0] = 0x00; in[1] = 0x01; in[2] = 0x7f; in[3] = 0x80; in[4] = 0xff; in[5] = 0xfe;
    in[8] = 0x00; in[9] = 0x00; in[10] = 0x80; in[11] = 0xff; in[12] = 0x7f; in[13] = 0xff;
    for (unsigned i = lo; i < lo + n0 && i < sizeof tbl / sizeof tbl[0]; i++) {
        memset(res, 0xa5, sizeof res);
        tbl[i].fn(in, res);
        olen = 0;
        put(tbl[i].name); put(" ");
        puthex(res, 32);
        put("\\n");
        ssize_t w = write(1, out, (size_t)olen);   /* 每条立刻输出：崩在哪条一眼可见 */
        (void)w;
    }
    return 0;
}
""")
    open("isa_bulk.c", "w").write("\n".join(c))
    print(f"生成 {len(tests)}/{len(T)} 条测试：isa_bulk.S / isa_bulk.c")

if __name__ == "__main__":
    main()
