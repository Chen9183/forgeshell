// 由 tests/gen_isa_bulk.py 生成：批量指令差分测试（勿手改）
    .text

    .globl  isa_add_16b
    .type   isa_add_16b, %function
isa_add_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     add v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_add_8h
    .type   isa_add_8h, %function
isa_add_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     add v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_add_4s
    .type   isa_add_4s, %function
isa_add_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     add v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_add_2d
    .type   isa_add_2d, %function
isa_add_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     add v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_sub_16b
    .type   isa_sub_16b, %function
isa_sub_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sub v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_sub_8h
    .type   isa_sub_8h, %function
isa_sub_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sub v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_sub_4s
    .type   isa_sub_4s, %function
isa_sub_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sub v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_sub_2d
    .type   isa_sub_2d, %function
isa_sub_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sub v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_mul_16b
    .type   isa_mul_16b, %function
isa_mul_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mul v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_mul_8h
    .type   isa_mul_8h, %function
isa_mul_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mul v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_mul_4s
    .type   isa_mul_4s, %function
isa_mul_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mul v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_smax_16b
    .type   isa_smax_16b, %function
isa_smax_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smax v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_smax_8h
    .type   isa_smax_8h, %function
isa_smax_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smax v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_smax_4s
    .type   isa_smax_4s, %function
isa_smax_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smax v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_smin_16b
    .type   isa_smin_16b, %function
isa_smin_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smin v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_smin_8h
    .type   isa_smin_8h, %function
isa_smin_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smin v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_smin_4s
    .type   isa_smin_4s, %function
isa_smin_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smin v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_umax_16b
    .type   isa_umax_16b, %function
isa_umax_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     umax v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_umax_8h
    .type   isa_umax_8h, %function
isa_umax_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     umax v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_umax_4s
    .type   isa_umax_4s, %function
isa_umax_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     umax v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_umin_16b
    .type   isa_umin_16b, %function
isa_umin_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     umin v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_umin_8h
    .type   isa_umin_8h, %function
isa_umin_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     umin v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_umin_4s
    .type   isa_umin_4s, %function
isa_umin_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     umin v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_sabd_16b
    .type   isa_sabd_16b, %function
isa_sabd_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sabd v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_sabd_8h
    .type   isa_sabd_8h, %function
isa_sabd_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sabd v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_sabd_4s
    .type   isa_sabd_4s, %function
isa_sabd_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sabd v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_uabd_16b
    .type   isa_uabd_16b, %function
isa_uabd_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uabd v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_uabd_8h
    .type   isa_uabd_8h, %function
isa_uabd_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uabd v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_uabd_4s
    .type   isa_uabd_4s, %function
isa_uabd_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uabd v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_cmgt_16b
    .type   isa_cmgt_16b, %function
isa_cmgt_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmgt v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_cmgt_8h
    .type   isa_cmgt_8h, %function
isa_cmgt_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmgt v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_cmgt_4s
    .type   isa_cmgt_4s, %function
isa_cmgt_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmgt v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_cmgt_2d
    .type   isa_cmgt_2d, %function
isa_cmgt_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmgt v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_cmge_16b
    .type   isa_cmge_16b, %function
isa_cmge_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmge v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_cmge_8h
    .type   isa_cmge_8h, %function
isa_cmge_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmge v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_cmge_4s
    .type   isa_cmge_4s, %function
isa_cmge_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmge v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_cmge_2d
    .type   isa_cmge_2d, %function
isa_cmge_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmge v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_cmeq_16b
    .type   isa_cmeq_16b, %function
isa_cmeq_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmeq v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_cmeq_8h
    .type   isa_cmeq_8h, %function
isa_cmeq_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmeq v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_cmeq_4s
    .type   isa_cmeq_4s, %function
isa_cmeq_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmeq v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_cmeq_2d
    .type   isa_cmeq_2d, %function
isa_cmeq_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmeq v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_cmhi_16b
    .type   isa_cmhi_16b, %function
isa_cmhi_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmhi v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_cmhi_8h
    .type   isa_cmhi_8h, %function
isa_cmhi_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmhi v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_cmhi_4s
    .type   isa_cmhi_4s, %function
isa_cmhi_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmhi v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_cmhi_2d
    .type   isa_cmhi_2d, %function
isa_cmhi_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmhi v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_cmhs_16b
    .type   isa_cmhs_16b, %function
isa_cmhs_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmhs v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_cmhs_8h
    .type   isa_cmhs_8h, %function
isa_cmhs_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmhs v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_cmhs_4s
    .type   isa_cmhs_4s, %function
isa_cmhs_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmhs v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_cmhs_2d
    .type   isa_cmhs_2d, %function
isa_cmhs_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmhs v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_sqadd_16b
    .type   isa_sqadd_16b, %function
isa_sqadd_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqadd v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_sqadd_8h
    .type   isa_sqadd_8h, %function
isa_sqadd_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqadd v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_sqadd_4s
    .type   isa_sqadd_4s, %function
isa_sqadd_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqadd v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_sqadd_2d
    .type   isa_sqadd_2d, %function
isa_sqadd_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqadd v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_uqadd_16b
    .type   isa_uqadd_16b, %function
isa_uqadd_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqadd v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_uqadd_8h
    .type   isa_uqadd_8h, %function
isa_uqadd_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqadd v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_uqadd_4s
    .type   isa_uqadd_4s, %function
isa_uqadd_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqadd v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_uqadd_2d
    .type   isa_uqadd_2d, %function
isa_uqadd_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqadd v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_sqsub_16b
    .type   isa_sqsub_16b, %function
isa_sqsub_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqsub v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_sqsub_8h
    .type   isa_sqsub_8h, %function
isa_sqsub_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqsub v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_sqsub_4s
    .type   isa_sqsub_4s, %function
isa_sqsub_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqsub v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_sqsub_2d
    .type   isa_sqsub_2d, %function
isa_sqsub_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqsub v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_uqsub_16b
    .type   isa_uqsub_16b, %function
isa_uqsub_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqsub v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_uqsub_8h
    .type   isa_uqsub_8h, %function
isa_uqsub_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqsub v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_uqsub_4s
    .type   isa_uqsub_4s, %function
isa_uqsub_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqsub v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_uqsub_2d
    .type   isa_uqsub_2d, %function
isa_uqsub_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqsub v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_shadd_16b
    .type   isa_shadd_16b, %function
isa_shadd_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shadd v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_shadd_8h
    .type   isa_shadd_8h, %function
isa_shadd_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shadd v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_shadd_4s
    .type   isa_shadd_4s, %function
isa_shadd_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shadd v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_uhadd_16b
    .type   isa_uhadd_16b, %function
isa_uhadd_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uhadd v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_uhadd_8h
    .type   isa_uhadd_8h, %function
isa_uhadd_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uhadd v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_uhadd_4s
    .type   isa_uhadd_4s, %function
isa_uhadd_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uhadd v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_mla_16b
    .type   isa_mla_16b, %function
isa_mla_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mla v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_mla_8h
    .type   isa_mla_8h, %function
isa_mla_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mla v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_mla_4s
    .type   isa_mla_4s, %function
isa_mla_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mla v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_mls_16b
    .type   isa_mls_16b, %function
isa_mls_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mls v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_mls_8h
    .type   isa_mls_8h, %function
isa_mls_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mls v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_mls_4s
    .type   isa_mls_4s, %function
isa_mls_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mls v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_smaxp_16b
    .type   isa_smaxp_16b, %function
isa_smaxp_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smaxp v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_smaxp_8h
    .type   isa_smaxp_8h, %function
isa_smaxp_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smaxp v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_smaxp_4s
    .type   isa_smaxp_4s, %function
isa_smaxp_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smaxp v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_addp_16b
    .type   isa_addp_16b, %function
isa_addp_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     addp v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_addp_8h
    .type   isa_addp_8h, %function
isa_addp_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     addp v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_addp_4s
    .type   isa_addp_4s, %function
isa_addp_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     addp v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_addp_2d
    .type   isa_addp_2d, %function
isa_addp_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     addp v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_and_16b
    .type   isa_and_16b, %function
isa_and_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     and v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_bic_16b
    .type   isa_bic_16b, %function
isa_bic_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     bic v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_orr_16b
    .type   isa_orr_16b, %function
isa_orr_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     orr v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_orn_16b
    .type   isa_orn_16b, %function
isa_orn_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     orn v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_eor_16b
    .type   isa_eor_16b, %function
isa_eor_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     eor v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_bsl_16b
    .type   isa_bsl_16b, %function
isa_bsl_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     bsl v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_bit_16b
    .type   isa_bit_16b, %function
isa_bit_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     bit v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_bif_16b
    .type   isa_bif_16b, %function
isa_bif_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     bif v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_add_8b
    .type   isa_add_8b, %function
isa_add_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     add v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_add_4h
    .type   isa_add_4h, %function
isa_add_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     add v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_add_2s
    .type   isa_add_2s, %function
isa_add_2s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     add v0.2s, v1.2s, v2.2s
     str q0,[x1]
    ret

    .globl  isa_sub_8b
    .type   isa_sub_8b, %function
isa_sub_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sub v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_sub_4h
    .type   isa_sub_4h, %function
isa_sub_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sub v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_sub_2s
    .type   isa_sub_2s, %function
isa_sub_2s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sub v0.2s, v1.2s, v2.2s
     str q0,[x1]
    ret

    .globl  isa_mul_8b
    .type   isa_mul_8b, %function
isa_mul_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mul v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_mul_4h
    .type   isa_mul_4h, %function
isa_mul_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mul v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_mul_2s
    .type   isa_mul_2s, %function
isa_mul_2s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mul v0.2s, v1.2s, v2.2s
     str q0,[x1]
    ret

    .globl  isa_cmeq_8b
    .type   isa_cmeq_8b, %function
isa_cmeq_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmeq v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_cmeq_4h
    .type   isa_cmeq_4h, %function
isa_cmeq_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmeq v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_cmeq_2s
    .type   isa_cmeq_2s, %function
isa_cmeq_2s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmeq v0.2s, v1.2s, v2.2s
     str q0,[x1]
    ret

    .globl  isa_cmgt_8b
    .type   isa_cmgt_8b, %function
isa_cmgt_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmgt v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_cmgt_4h
    .type   isa_cmgt_4h, %function
isa_cmgt_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmgt v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_cmgt_2s
    .type   isa_cmgt_2s, %function
isa_cmgt_2s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cmgt v0.2s, v1.2s, v2.2s
     str q0,[x1]
    ret

    .globl  isa_and_8b
    .type   isa_and_8b, %function
isa_and_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     and v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_orr_8b
    .type   isa_orr_8b, %function
isa_orr_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     orr v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_eor_8b
    .type   isa_eor_8b, %function
isa_eor_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     eor v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_uzp1_16b
    .type   isa_uzp1_16b, %function
isa_uzp1_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp1 v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_uzp1_8h
    .type   isa_uzp1_8h, %function
isa_uzp1_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp1 v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_uzp1_4s
    .type   isa_uzp1_4s, %function
isa_uzp1_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp1 v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_uzp1_2d
    .type   isa_uzp1_2d, %function
isa_uzp1_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp1 v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_uzp1_8b
    .type   isa_uzp1_8b, %function
isa_uzp1_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp1 v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_uzp1_4h
    .type   isa_uzp1_4h, %function
isa_uzp1_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp1 v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_uzp2_16b
    .type   isa_uzp2_16b, %function
isa_uzp2_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp2 v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_uzp2_8h
    .type   isa_uzp2_8h, %function
isa_uzp2_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp2 v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_uzp2_4s
    .type   isa_uzp2_4s, %function
isa_uzp2_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp2 v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_uzp2_2d
    .type   isa_uzp2_2d, %function
isa_uzp2_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp2 v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_uzp2_8b
    .type   isa_uzp2_8b, %function
isa_uzp2_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp2 v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_uzp2_4h
    .type   isa_uzp2_4h, %function
isa_uzp2_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uzp2 v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_zip1_16b
    .type   isa_zip1_16b, %function
isa_zip1_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip1 v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_zip1_8h
    .type   isa_zip1_8h, %function
isa_zip1_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip1 v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_zip1_4s
    .type   isa_zip1_4s, %function
isa_zip1_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip1 v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_zip1_2d
    .type   isa_zip1_2d, %function
isa_zip1_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip1 v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_zip1_8b
    .type   isa_zip1_8b, %function
isa_zip1_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip1 v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_zip1_4h
    .type   isa_zip1_4h, %function
isa_zip1_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip1 v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_zip2_16b
    .type   isa_zip2_16b, %function
isa_zip2_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip2 v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_zip2_8h
    .type   isa_zip2_8h, %function
isa_zip2_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip2 v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_zip2_4s
    .type   isa_zip2_4s, %function
isa_zip2_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip2 v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_zip2_2d
    .type   isa_zip2_2d, %function
isa_zip2_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip2 v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_zip2_8b
    .type   isa_zip2_8b, %function
isa_zip2_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip2 v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_zip2_4h
    .type   isa_zip2_4h, %function
isa_zip2_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     zip2 v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_trn1_16b
    .type   isa_trn1_16b, %function
isa_trn1_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn1 v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_trn1_8h
    .type   isa_trn1_8h, %function
isa_trn1_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn1 v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_trn1_4s
    .type   isa_trn1_4s, %function
isa_trn1_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn1 v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_trn1_2d
    .type   isa_trn1_2d, %function
isa_trn1_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn1 v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_trn1_8b
    .type   isa_trn1_8b, %function
isa_trn1_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn1 v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_trn1_4h
    .type   isa_trn1_4h, %function
isa_trn1_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn1 v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_trn2_16b
    .type   isa_trn2_16b, %function
isa_trn2_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn2 v0.16b, v1.16b, v2.16b
     str q0,[x1]
    ret

    .globl  isa_trn2_8h
    .type   isa_trn2_8h, %function
isa_trn2_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn2 v0.8h, v1.8h, v2.8h
     str q0,[x1]
    ret

    .globl  isa_trn2_4s
    .type   isa_trn2_4s, %function
isa_trn2_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn2 v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_trn2_2d
    .type   isa_trn2_2d, %function
isa_trn2_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn2 v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_trn2_8b
    .type   isa_trn2_8b, %function
isa_trn2_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn2 v0.8b, v1.8b, v2.8b
     str q0,[x1]
    ret

    .globl  isa_trn2_4h
    .type   isa_trn2_4h, %function
isa_trn2_4h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     trn2 v0.4h, v1.4h, v2.4h
     str q0,[x1]
    ret

    .globl  isa_ext16b_0
    .type   isa_ext16b_0, %function
isa_ext16b_0:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ext v0.16b, v1.16b, v2.16b, #0
     str q0,[x1]
    ret

    .globl  isa_ext16b_1
    .type   isa_ext16b_1, %function
isa_ext16b_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ext v0.16b, v1.16b, v2.16b, #1
     str q0,[x1]
    ret

    .globl  isa_ext16b_3
    .type   isa_ext16b_3, %function
isa_ext16b_3:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ext v0.16b, v1.16b, v2.16b, #3
     str q0,[x1]
    ret

    .globl  isa_ext16b_7
    .type   isa_ext16b_7, %function
isa_ext16b_7:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ext v0.16b, v1.16b, v2.16b, #7
     str q0,[x1]
    ret

    .globl  isa_ext16b_8
    .type   isa_ext16b_8, %function
isa_ext16b_8:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ext v0.16b, v1.16b, v2.16b, #8
     str q0,[x1]
    ret

    .globl  isa_ext16b_15
    .type   isa_ext16b_15, %function
isa_ext16b_15:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ext v0.16b, v1.16b, v2.16b, #15
     str q0,[x1]
    ret

    .globl  isa_ext8b_2
    .type   isa_ext8b_2, %function
isa_ext8b_2:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ext v0.8b, v1.8b, v2.8b, #2
     str q0,[x1]
    ret

    .globl  isa_shl_16b_1
    .type   isa_shl_16b_1, %function
isa_shl_16b_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.16b, v1.16b, #1
     str q0,[x1]
    ret

    .globl  isa_ushr_16b_1
    .type   isa_ushr_16b_1, %function
isa_ushr_16b_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ushr v0.16b, v1.16b, #1
     str q0,[x1]
    ret

    .globl  isa_sshr_16b_1
    .type   isa_sshr_16b_1, %function
isa_sshr_16b_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.16b, v1.16b, #1
     str q0,[x1]
    ret

    .globl  isa_sri_16b_1
    .type   isa_sri_16b_1, %function
isa_sri_16b_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sri  v0.16b, v1.16b, #1
     str q0,[x1]
    ret

    .globl  isa_shl_16b_3
    .type   isa_shl_16b_3, %function
isa_shl_16b_3:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.16b, v1.16b, #3
     str q0,[x1]
    ret

    .globl  isa_ushr_16b_3
    .type   isa_ushr_16b_3, %function
isa_ushr_16b_3:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ushr v0.16b, v1.16b, #3
     str q0,[x1]
    ret

    .globl  isa_sshr_16b_3
    .type   isa_sshr_16b_3, %function
isa_sshr_16b_3:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.16b, v1.16b, #3
     str q0,[x1]
    ret

    .globl  isa_sri_16b_3
    .type   isa_sri_16b_3, %function
isa_sri_16b_3:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sri  v0.16b, v1.16b, #3
     str q0,[x1]
    ret

    .globl  isa_shl_16b_7
    .type   isa_shl_16b_7, %function
isa_shl_16b_7:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.16b, v1.16b, #7
     str q0,[x1]
    ret

    .globl  isa_ushr_16b_7
    .type   isa_ushr_16b_7, %function
isa_ushr_16b_7:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ushr v0.16b, v1.16b, #7
     str q0,[x1]
    ret

    .globl  isa_sshr_16b_7
    .type   isa_sshr_16b_7, %function
isa_sshr_16b_7:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.16b, v1.16b, #7
     str q0,[x1]
    ret

    .globl  isa_sri_16b_7
    .type   isa_sri_16b_7, %function
isa_sri_16b_7:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sri  v0.16b, v1.16b, #7
     str q0,[x1]
    ret

    .globl  isa_shl_8h_1
    .type   isa_shl_8h_1, %function
isa_shl_8h_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.8h, v1.8h, #1
     str q0,[x1]
    ret

    .globl  isa_sshr_8h_1
    .type   isa_sshr_8h_1, %function
isa_sshr_8h_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.8h, v1.8h, #1
     str q0,[x1]
    ret

    .globl  isa_ushr_8h_1
    .type   isa_ushr_8h_1, %function
isa_ushr_8h_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ushr v0.8h, v1.8h, #1
     str q0,[x1]
    ret

    .globl  isa_shl_8h_5
    .type   isa_shl_8h_5, %function
isa_shl_8h_5:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.8h, v1.8h, #5
     str q0,[x1]
    ret

    .globl  isa_sshr_8h_5
    .type   isa_sshr_8h_5, %function
isa_sshr_8h_5:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.8h, v1.8h, #5
     str q0,[x1]
    ret

    .globl  isa_ushr_8h_5
    .type   isa_ushr_8h_5, %function
isa_ushr_8h_5:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ushr v0.8h, v1.8h, #5
     str q0,[x1]
    ret

    .globl  isa_shl_8h_11
    .type   isa_shl_8h_11, %function
isa_shl_8h_11:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.8h, v1.8h, #11
     str q0,[x1]
    ret

    .globl  isa_sshr_8h_11
    .type   isa_sshr_8h_11, %function
isa_sshr_8h_11:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.8h, v1.8h, #11
     str q0,[x1]
    ret

    .globl  isa_ushr_8h_11
    .type   isa_ushr_8h_11, %function
isa_ushr_8h_11:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ushr v0.8h, v1.8h, #11
     str q0,[x1]
    ret

    .globl  isa_shl_4s_1
    .type   isa_shl_4s_1, %function
isa_shl_4s_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.4s, v1.4s, #1
     str q0,[x1]
    ret

    .globl  isa_sshr_4s_1
    .type   isa_sshr_4s_1, %function
isa_sshr_4s_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.4s, v1.4s, #1
     str q0,[x1]
    ret

    .globl  isa_shl_4s_10
    .type   isa_shl_4s_10, %function
isa_shl_4s_10:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.4s, v1.4s, #10
     str q0,[x1]
    ret

    .globl  isa_sshr_4s_10
    .type   isa_sshr_4s_10, %function
isa_sshr_4s_10:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.4s, v1.4s, #10
     str q0,[x1]
    ret

    .globl  isa_shl_2d_3
    .type   isa_shl_2d_3, %function
isa_shl_2d_3:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     shl v0.2d, v1.2d, #3
     str q0,[x1]
    ret

    .globl  isa_sshr_2d_5
    .type   isa_sshr_2d_5, %function
isa_sshr_2d_5:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshr v0.2d, v1.2d, #5
     str q0,[x1]
    ret

    .globl  isa_usra_16b_2
    .type   isa_usra_16b_2, %function
isa_usra_16b_2:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     usra v0.16b, v1.16b, #2
     str q0,[x1]
    ret

    .globl  isa_ssra_16b_2
    .type   isa_ssra_16b_2, %function
isa_ssra_16b_2:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ssra v0.16b, v1.16b, #2
     str q0,[x1]
    ret

    .globl  isa_urshr_16b_2
    .type   isa_urshr_16b_2, %function
isa_urshr_16b_2:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     urshr v0.16b, v1.16b, #2
     str q0,[x1]
    ret

    .globl  isa_srsra_16b_2
    .type   isa_srsra_16b_2, %function
isa_srsra_16b_2:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     srsra v0.16b, v1.16b, #2
     str q0,[x1]
    ret

    .globl  isa_sqshl_16b_1
    .type   isa_sqshl_16b_1, %function
isa_sqshl_16b_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqshl v0.16b, v1.16b, #1
     str q0,[x1]
    ret

    .globl  isa_ushll_8h_1
    .type   isa_ushll_8h_1, %function
isa_ushll_8h_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ushll v0.8h, v1.8b, #1
     str q0,[x1]
    ret

    .globl  isa_sshll_8h_1
    .type   isa_sshll_8h_1, %function
isa_sshll_8h_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshll v0.8h, v1.8b, #1
     str q0,[x1]
    ret

    .globl  isa_ushll_4s_2
    .type   isa_ushll_4s_2, %function
isa_ushll_4s_2:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ushll v0.4s, v1.4h, #2
     str q0,[x1]
    ret

    .globl  isa_sshll_4s_2
    .type   isa_sshll_4s_2, %function
isa_sshll_4s_2:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sshll v0.4s, v1.4h, #2
     str q0,[x1]
    ret

    .globl  isa_xtn_8b
    .type   isa_xtn_8b, %function
isa_xtn_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     xtn  v0.8b, v1.8h
     str q0,[x1]
    ret

    .globl  isa_xtn2_16b
    .type   isa_xtn2_16b, %function
isa_xtn2_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     xtn2 v0.16b, v1.8h
     str q0,[x1]
    ret

    .globl  isa_sqxtn_8b
    .type   isa_sqxtn_8b, %function
isa_sqxtn_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqxtn v0.8b, v1.8h
     str q0,[x1]
    ret

    .globl  isa_uqxtn_8b
    .type   isa_uqxtn_8b, %function
isa_uqxtn_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uqxtn v0.8b, v1.8h
     str q0,[x1]
    ret

    .globl  isa_sqxtun_8b
    .type   isa_sqxtun_8b, %function
isa_sqxtun_8b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sqxtun v0.8b, v1.8h
     str q0,[x1]
    ret

    .globl  isa_movi_16b_3f
    .type   isa_movi_16b_3f, %function
isa_movi_16b_3f:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     movi v0.16b, #0x3f
     str q0,[x1]
    ret

    .globl  isa_movi_8b_1
    .type   isa_movi_8b_1, %function
isa_movi_8b_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     movi v0.8b, #0x1
     str q0,[x1]
    ret

    .globl  isa_movi_4s_12
    .type   isa_movi_4s_12, %function
isa_movi_4s_12:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     movi v0.4s, #0xc
     str q0,[x1]
    ret

    .globl  isa_movi_2s_1
    .type   isa_movi_2s_1, %function
isa_movi_2s_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     movi v0.2s, #0x1
     str q0,[x1]
    ret

    .globl  isa_movi_4h_5
    .type   isa_movi_4h_5, %function
isa_movi_4h_5:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     movi v0.4h, #0x5
     str q0,[x1]
    ret

    .globl  isa_movi_8h_5
    .type   isa_movi_8h_5, %function
isa_movi_8h_5:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     movi v0.8h, #0x5
     str q0,[x1]
    ret

    .globl  isa_mvni_4s_1
    .type   isa_mvni_4s_1, %function
isa_mvni_4s_1:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mvni v0.4s, #0x1
     str q0,[x1]
    ret

    .globl  isa_movi_4s_lsl8
    .type   isa_movi_4s_lsl8, %function
isa_movi_4s_lsl8:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     movi v0.4s, #0x1, lsl #8
     str q0,[x1]
    ret

    .globl  isa_dup_16b_w
    .type   isa_dup_16b_w, %function
isa_dup_16b_w:
     ldr x2,[x0]
 dup v0.16b, w2
 str q0,[x1]
    ret

    .globl  isa_dup_8h_w
    .type   isa_dup_8h_w, %function
isa_dup_8h_w:
     ldr w2,[x0]
 dup v0.8h, w2
 str q0,[x1]
    ret

    .globl  isa_dup_4s_w
    .type   isa_dup_4s_w, %function
isa_dup_4s_w:
     ldr w2,[x0]
 dup v0.4s, w2
 str q0,[x1]
    ret

    .globl  isa_dup_2d_x
    .type   isa_dup_2d_x, %function
isa_dup_2d_x:
     ldr x2,[x0]
 dup v0.2d, x2
 str q0,[x1]
    ret

    .globl  isa_dup_elem_b
    .type   isa_dup_elem_b, %function
isa_dup_elem_b:
     ldr q0,[x0]
 dup v1.16b, v0.b[5]
 str q1,[x1]
    ret

    .globl  isa_dup_elem_h
    .type   isa_dup_elem_h, %function
isa_dup_elem_h:
     ldr q0,[x0]
 dup v1.8h, v0.h[3]
 str q1,[x1]
    ret

    .globl  isa_dup_elem_s
    .type   isa_dup_elem_s, %function
isa_dup_elem_s:
     ldr q0,[x0]
 dup v1.4s, v0.s[2]
 str q1,[x1]
    ret

    .globl  isa_ins_elem
    .type   isa_ins_elem, %function
isa_ins_elem:
     ldr q0,[x0]
 ldr q1,[x0,#16]
 ins v0.b[2], v1.b[7]
 str q0,[x1]
    ret

    .globl  isa_umov_b
    .type   isa_umov_b, %function
isa_umov_b:
     ldr q0,[x0]
 umov w2, v0.b[9]
 str w2,[x1]
    ret

    .globl  isa_umov_h
    .type   isa_umov_h, %function
isa_umov_h:
     ldr q0,[x0]
 umov w2, v0.h[3]
 str w2,[x1]
    ret

    .globl  isa_umov_s
    .type   isa_umov_s, %function
isa_umov_s:
     ldr q0,[x0]
 umov w2, v0.s[2]
 str w2,[x1]
    ret

    .globl  isa_umov_d
    .type   isa_umov_d, %function
isa_umov_d:
     ldr q0,[x0]
 umov x2, v0.d[1]
 str x2,[x1]
    ret

    .globl  isa_smov_b
    .type   isa_smov_b, %function
isa_smov_b:
     ldr q0,[x0]
 smov w2, v0.b[9]
 str w2,[x1]
    ret

    .globl  isa_ins_gen_b
    .type   isa_ins_gen_b, %function
isa_ins_gen_b:
     ldr q0,[x0]
 ldr w2,[x0,#20]
 ins v0.b[3], w2
 str q0,[x1]
    ret

    .globl  isa_ins_gen_s
    .type   isa_ins_gen_s, %function
isa_ins_gen_s:
     ldr q0,[x0]
 ldr w2,[x0,#20]
 ins v0.s[2], w2
 str q0,[x1]
    ret

    .globl  isa_mov_d1_x
    .type   isa_mov_d1_x, %function
isa_mov_d1_x:
     ldr q0,[x0]
 ldr x2,[x0,#24]
 mov v0.d[1], x2
 str q0,[x1]
    ret

    .globl  isa_mov_x_d1
    .type   isa_mov_x_d1, %function
isa_mov_x_d1:
     ldr q0,[x0]
 mov x2, v0.d[1]
 str x2,[x1]
    ret

    .globl  isa_addv_16b
    .type   isa_addv_16b, %function
isa_addv_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     addv b0, v1.16b
     str q0,[x1]
    ret

    .globl  isa_addv_8h
    .type   isa_addv_8h, %function
isa_addv_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     addv h0, v1.8h
     str q0,[x1]
    ret

    .globl  isa_addv_4s
    .type   isa_addv_4s, %function
isa_addv_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     addv s0, v1.4s
     str q0,[x1]
    ret

    .globl  isa_saddlv_16b
    .type   isa_saddlv_16b, %function
isa_saddlv_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     saddlv h0, v1.16b
     str q0,[x1]
    ret

    .globl  isa_uaddlv_16b
    .type   isa_uaddlv_16b, %function
isa_uaddlv_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uaddlv h0, v1.16b
     str q0,[x1]
    ret

    .globl  isa_smaxv_16b
    .type   isa_smaxv_16b, %function
isa_smaxv_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     smaxv b0, v1.16b
     str q0,[x1]
    ret

    .globl  isa_umaxv_16b
    .type   isa_umaxv_16b, %function
isa_umaxv_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     umaxv b0, v1.16b
     str q0,[x1]
    ret

    .globl  isa_sminv_4s
    .type   isa_sminv_4s, %function
isa_sminv_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     sminv s0, v1.4s
     str q0,[x1]
    ret

    .globl  isa_cnt_16b
    .type   isa_cnt_16b, %function
isa_cnt_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cnt v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_rbit_16b
    .type   isa_rbit_16b, %function
isa_rbit_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     rbit v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_rev16_16b
    .type   isa_rev16_16b, %function
isa_rev16_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     rev16 v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_rev32_16b
    .type   isa_rev32_16b, %function
isa_rev32_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     rev32 v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_rev64_16b
    .type   isa_rev64_16b, %function
isa_rev64_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     rev64 v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_abs_16b
    .type   isa_abs_16b, %function
isa_abs_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     abs v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_neg_16b
    .type   isa_neg_16b, %function
isa_neg_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     neg v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_mvn_16b
    .type   isa_mvn_16b, %function
isa_mvn_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mvn v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_not_16b
    .type   isa_not_16b, %function
isa_not_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     not v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_cls_16b
    .type   isa_cls_16b, %function
isa_cls_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     cls v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_clz_16b
    .type   isa_clz_16b, %function
isa_clz_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     clz v0.16b, v1.16b
     str q0,[x1]
    ret

    .globl  isa_saddlp_16b
    .type   isa_saddlp_16b, %function
isa_saddlp_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     saddlp v0.8h, v1.16b
     str q0,[x1]
    ret

    .globl  isa_uaddlp_16b
    .type   isa_uaddlp_16b, %function
isa_uaddlp_16b:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     uaddlp v0.8h, v1.16b
     str q0,[x1]
    ret

    .globl  isa_xtl_8h
    .type   isa_xtl_8h, %function
isa_xtl_8h:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     mov v0.8b, v1.8b
     str q0,[x1]
    ret

    .globl  isa_fadd_4s
    .type   isa_fadd_4s, %function
isa_fadd_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fadd v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_fsub_4s
    .type   isa_fsub_4s, %function
isa_fsub_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fsub v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_fmul_4s
    .type   isa_fmul_4s, %function
isa_fmul_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fmul v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_fdiv_4s
    .type   isa_fdiv_4s, %function
isa_fdiv_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fdiv v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_fadd_2d
    .type   isa_fadd_2d, %function
isa_fadd_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fadd v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_fmul_2d
    .type   isa_fmul_2d, %function
isa_fmul_2d:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fmul v0.2d, v1.2d, v2.2d
     str q0,[x1]
    ret

    .globl  isa_fmax_4s
    .type   isa_fmax_4s, %function
isa_fmax_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fmax v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_fmin_4s
    .type   isa_fmin_4s, %function
isa_fmin_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fmin v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_fmla_4s
    .type   isa_fmla_4s, %function
isa_fmla_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fmla v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_fabs_4s
    .type   isa_fabs_4s, %function
isa_fabs_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fabs v0.4s, v1.4s
     str q0,[x1]
    ret

    .globl  isa_fneg_4s
    .type   isa_fneg_4s, %function
isa_fneg_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fneg v0.4s, v1.4s
     str q0,[x1]
    ret

    .globl  isa_fsqrt_4s
    .type   isa_fsqrt_4s, %function
isa_fsqrt_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fsqrt v0.4s, v1.4s
     str q0,[x1]
    ret

    .globl  isa_fcmeq_4s
    .type   isa_fcmeq_4s, %function
isa_fcmeq_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fcmeq v0.4s, v1.4s, v2.4s
     str q0,[x1]
    ret

    .globl  isa_fcvtl_4s
    .type   isa_fcvtl_4s, %function
isa_fcvtl_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fcvtl v0.4s, v1.4h
     str q0,[x1]
    ret

    .globl  isa_scvtf_4s
    .type   isa_scvtf_4s, %function
isa_scvtf_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     scvtf v0.4s, v1.4s
     str q0,[x1]
    ret

    .globl  isa_ucvtf_4s
    .type   isa_ucvtf_4s, %function
isa_ucvtf_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     ucvtf v0.4s, v1.4s
     str q0,[x1]
    ret

    .globl  isa_fcvtzs_4s
    .type   isa_fcvtzs_4s, %function
isa_fcvtzs_4s:
    ldr q0,[x0]
     ldr q1,[x0,#16]
     ldr q2,[x0,#32]
     fcvtzs v0.4s, v1.4s
     str q0,[x1]
    ret

    .globl  isa_fadd_s
    .type   isa_fadd_s, %function
isa_fadd_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 fadd s2, s0, s1
 str s2,[x1]
    ret

    .globl  isa_fsub_s
    .type   isa_fsub_s, %function
isa_fsub_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 fsub s2, s0, s1
 str s2,[x1]
    ret

    .globl  isa_fmul_s
    .type   isa_fmul_s, %function
isa_fmul_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 fmul s2, s0, s1
 str s2,[x1]
    ret

    .globl  isa_fdiv_s
    .type   isa_fdiv_s, %function
isa_fdiv_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 fdiv s2, s0, s1
 str s2,[x1]
    ret

    .globl  isa_fadd_d
    .type   isa_fadd_d, %function
isa_fadd_d:
     ldr d0,[x0]
 ldr d1,[x0,#8]
 fadd d2, d0, d1
 str d2,[x1]
    ret

    .globl  isa_fmul_d
    .type   isa_fmul_d, %function
isa_fmul_d:
     ldr d0,[x0]
 ldr d1,[x0,#8]
 fmul d2, d0, d1
 str d2,[x1]
    ret

    .globl  isa_fabs_s
    .type   isa_fabs_s, %function
isa_fabs_s:
     ldr s0,[x0]
 fabs s1, s0
 str s1,[x1]
    ret

    .globl  isa_fneg_s
    .type   isa_fneg_s, %function
isa_fneg_s:
     ldr s0,[x0]
 fneg s1, s0
 str s1,[x1]
    ret

    .globl  isa_fsqrt_s
    .type   isa_fsqrt_s, %function
isa_fsqrt_s:
     ldr s0,[x0]
 fsqrt s1, s0
 str s1,[x1]
    ret

    .globl  isa_fmin_s
    .type   isa_fmin_s, %function
isa_fmin_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 fmin s2, s0, s1
 str s2,[x1]
    ret

    .globl  isa_fmax_s
    .type   isa_fmax_s, %function
isa_fmax_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 fmax s2, s0, s1
 str s2,[x1]
    ret

    .globl  isa_frintm_s
    .type   isa_frintm_s, %function
isa_frintm_s:
     ldr s0,[x0]
 frintm s1, s0
 str s1,[x1]
    ret

    .globl  isa_frintn_s
    .type   isa_frintn_s, %function
isa_frintn_s:
     ldr s0,[x0]
 frintn s1, s0
 str s1,[x1]
    ret

    .globl  isa_fcvtzs_s_w
    .type   isa_fcvtzs_s_w, %function
isa_fcvtzs_s_w:
     ldr s0,[x0]
 fcvtzs w2, s0
 str w2,[x1]
    ret

    .globl  isa_fcvtzu_s_w
    .type   isa_fcvtzu_s_w, %function
isa_fcvtzu_s_w:
     ldr s0,[x0]
 fcvtzu w2, s0
 str w2,[x1]
    ret

    .globl  isa_scvtf_w_s
    .type   isa_scvtf_w_s, %function
isa_scvtf_w_s:
     ldr w2,[x0]
 scvtf s0, w2
 str s0,[x1]
    ret

    .globl  isa_ucvtf_w_s
    .type   isa_ucvtf_w_s, %function
isa_ucvtf_w_s:
     ldr w2,[x0]
 ucvtf s0, w2
 str s0,[x1]
    ret

    .globl  isa_fcvt_d_s
    .type   isa_fcvt_d_s, %function
isa_fcvt_d_s:
     ldr s0,[x0]
 fcvt d0, s0
 str d0,[x1]
    ret

    .globl  isa_fcvt_s_d
    .type   isa_fcvt_s_d, %function
isa_fcvt_s_d:
     ldr d0,[x0]
 fcvt s0, d0
 str s0,[x1]
    ret

    .globl  isa_fmov_w_s
    .type   isa_fmov_w_s, %function
isa_fmov_w_s:
     ldr s0,[x0]
 fmov w2, s0
 str w2,[x1]
    ret

    .globl  isa_fmov_s_w
    .type   isa_fmov_s_w, %function
isa_fmov_s_w:
     ldr w2,[x0]
 fmov s0, w2
 str s0,[x1]
    ret

    .globl  isa_fmov_x_d
    .type   isa_fmov_x_d, %function
isa_fmov_x_d:
     ldr d0,[x0]
 fmov x2, d0
 str x2,[x1]
    ret

    .globl  isa_fmov_d_x
    .type   isa_fmov_d_x, %function
isa_fmov_d_x:
     ldr x2,[x0]
 fmov d0, x2
 str d0,[x1]
    ret

    .globl  isa_fcmp_s
    .type   isa_fcmp_s, %function
isa_fcmp_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 fcmp s0, s1
 cset w3, gt
 cset w4, eq
 stp w3,w4,[x1]
    ret

    .globl  isa_fmadd_s
    .type   isa_fmadd_s, %function
isa_fmadd_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 ldr s2,[x0,#8]
 fmadd s3,s0,s1,s2
 str s3,[x1]
    ret

    .globl  isa_fmsub_s
    .type   isa_fmsub_s, %function
isa_fmsub_s:
     ldr s0,[x0]
 ldr s1,[x0,#4]
 ldr s2,[x0,#8]
 fmsub s3,s0,s1,s2
 str s3,[x1]
    ret

    .globl  isa_ldr_q_off
    .type   isa_ldr_q_off, %function
isa_ldr_q_off:
     ldr q0,[x0,#32]
 str q0,[x1]
    ret

    .globl  isa_ldr_q_post
    .type   isa_ldr_q_post, %function
isa_ldr_q_post:
     ldr q0,[x0],#16
 str q0,[x1]
    ret

    .globl  isa_ldr_q_pre
    .type   isa_ldr_q_pre, %function
isa_ldr_q_pre:
     ldr q0,[x0,#16]!
 str q0,[x1]
    ret

    .globl  isa_str_q_post
    .type   isa_str_q_post, %function
isa_str_q_post:
     ldr q0,[x0]
 str q0,[x1],#16
 str q0,[x1]
    ret

    .globl  isa_str_q_pre
    .type   isa_str_q_pre, %function
isa_str_q_pre:
     ldr q0,[x0]
 str q0,[x1,#16]!
 sub x1,x1,#16
 str q0,[x1]
    ret

    .globl  isa_ldr_d_off
    .type   isa_ldr_d_off, %function
isa_ldr_d_off:
     ldr d0,[x0,#24]
 str d0,[x1]
    ret

    .globl  isa_ldr_s_off
    .type   isa_ldr_s_off, %function
isa_ldr_s_off:
     ldr s0,[x0,#12]
 str s0,[x1]
    ret

    .globl  isa_ldr_h_off
    .type   isa_ldr_h_off, %function
isa_ldr_h_off:
     ldr h0,[x0,#6]
 str h0,[x1]
    ret

    .globl  isa_ldr_b_off
    .type   isa_ldr_b_off, %function
isa_ldr_b_off:
     ldr b0,[x0,#3]
 str b0,[x1]
    ret

    .globl  isa_ldr_q_reg
    .type   isa_ldr_q_reg, %function
isa_ldr_q_reg:
     mov x2,#16
 ldr q0,[x0,x2]
 str q0,[x1]
    ret

    .globl  isa_str_q_reg
    .type   isa_str_q_reg, %function
isa_str_q_reg:
     mov x2,#32
 ldr q0,[x0]
 str q0,[x0,x2]
 ldr q1,[x0,#32]
 str q1,[x1]
    ret

    .globl  isa_ldp_q
    .type   isa_ldp_q, %function
isa_ldp_q:
     ldp q0,q1,[x0,#32]
 str q0,[x1]
 str q1,[x1,#16]
    ret

    .globl  isa_stp_q
    .type   isa_stp_q, %function
isa_stp_q:
     ldp q0,q1,[x0]
 stp q0,q1,[x1]
    ret

    .globl  isa_stp_q_post
    .type   isa_stp_q_post, %function
isa_stp_q_post:
     ldp q0,q1,[x0]
 stp q0,q1,[x1],#32
 stp q0,q1,[x1]
    ret

    .globl  isa_ldp_d
    .type   isa_ldp_d, %function
isa_ldp_d:
     ldp d0,d1,[x0,#16]
 str d0,[x1]
 str d1,[x1,#8]
    ret

    .globl  isa_stp_s
    .type   isa_stp_s, %function
isa_stp_s:
     ldp s0,s1,[x0]
 stp s0,s1,[x1]
    ret

    .globl  isa_ld1_16b
    .type   isa_ld1_16b, %function
isa_ld1_16b:
     ld1 {v0.16b},[x0]
 str q0,[x1]
    ret

    .globl  isa_st1_16b
    .type   isa_st1_16b, %function
isa_st1_16b:
     ld1 {v0.16b},[x0]
 st1 {v0.16b},[x1]
    ret

    .globl  isa_ld1_multi
    .type   isa_ld1_multi, %function
isa_ld1_multi:
     ld1 {v0.16b,v1.16b},[x0]
 str q0,[x1]
 str q1,[x1,#16]
    ret

    .globl  isa_ld2_16b
    .type   isa_ld2_16b, %function
isa_ld2_16b:
     ld2 {v0.16b,v1.16b},[x0]
 str q0,[x1]
 str q1,[x1,#16]
    ret

    .globl  isa_st2_16b
    .type   isa_st2_16b, %function
isa_st2_16b:
     ld2 {v0.16b,v1.16b},[x0]
 st2 {v0.16b,v1.16b},[x1]
    ret

    .globl  isa_ld3_16b
    .type   isa_ld3_16b, %function
isa_ld3_16b:
     ld3 {v0.16b,v1.16b,v2.16b},[x0]
 str q0,[x1]
 str q1,[x1,#16]
 str q2,[x1,#32]
    ret

    .globl  isa_ld4_16b
    .type   isa_ld4_16b, %function
isa_ld4_16b:
     ld4 {v0.16b,v1.16b,v2.16b,v3.16b},[x0]
 str q0,[x1]
 str q1,[x1,#16]
 str q2,[x1,#32]
 str q3,[x1,#48]
    ret

    .globl  isa_ld1r_16b
    .type   isa_ld1r_16b, %function
isa_ld1r_16b:
     ld1r {v0.16b},[x0]
 str q0,[x1]
    ret

    .globl  isa_ld2r_8h
    .type   isa_ld2r_8h, %function
isa_ld2r_8h:
     ld2r {v0.8h,v1.8h},[x0]
 str q0,[x1]
 str q1,[x1,#16]
    ret

    .globl  isa_ld1_single
    .type   isa_ld1_single, %function
isa_ld1_single:
     ld1 {v0.b}[3],[x0]
 str q0,[x1]
    ret

    .globl  isa_st1_single
    .type   isa_st1_single, %function
isa_st1_single:
     ld1 {v0.16b},[x0]
 st1 {v0.b}[3],[x1]
 ldr q1,[x1]
 str q1,[x1,#16]
    ret

    .globl  isa_ldr_x_off
    .type   isa_ldr_x_off, %function
isa_ldr_x_off:
     ldr x2,[x0,#40]
 str x2,[x1]
    ret

    .globl  isa_ldr_w_off
    .type   isa_ldr_w_off, %function
isa_ldr_w_off:
     ldr w2,[x0,#44]
 str w2,[x1]
    ret

    .globl  isa_ldr_x_post
    .type   isa_ldr_x_post, %function
isa_ldr_x_post:
     ldr x2,[x0],#8
 str x2,[x1]
    ret

    .globl  isa_ldr_x_pre
    .type   isa_ldr_x_pre, %function
isa_ldr_x_pre:
     ldr x2,[x0,#8]!
 str x2,[x1]
    ret

    .globl  isa_ldur_x
    .type   isa_ldur_x, %function
isa_ldur_x:
     ldur x2,[x0,#13]
 str x2,[x1]
    ret

    .globl  isa_ldrsb_w
    .type   isa_ldrsb_w, %function
isa_ldrsb_w:
     ldrsb w2,[x0,#7]
 str w2,[x1]
    ret

    .globl  isa_ldrsb_x
    .type   isa_ldrsb_x, %function
isa_ldrsb_x:
     ldrsb x2,[x0,#7]
 str x2,[x1]
    ret

    .globl  isa_ldrsh_w
    .type   isa_ldrsh_w, %function
isa_ldrsh_w:
     ldrsh w2,[x0,#6]
 str w2,[x1]
    ret

    .globl  isa_ldrsh_x
    .type   isa_ldrsh_x, %function
isa_ldrsh_x:
     ldrsh x2,[x0,#6]
 str x2,[x1]
    ret

    .globl  isa_ldrsw_x
    .type   isa_ldrsw_x, %function
isa_ldrsw_x:
     ldrsw x2,[x0,#12]
 str x2,[x1]
    ret

    .globl  isa_ldrb_w
    .type   isa_ldrb_w, %function
isa_ldrb_w:
     ldrb w2,[x0,#9]
 str w2,[x1]
    ret

    .globl  isa_ldrh_w
    .type   isa_ldrh_w, %function
isa_ldrh_w:
     ldrh w2,[x0,#10]
 str w2,[x1]
    ret

    .globl  isa_ldr_reg_off
    .type   isa_ldr_reg_off, %function
isa_ldr_reg_off:
     mov x3,#24
 ldr x2,[x0,x3]
 str x2,[x1]
    ret

    .globl  isa_ldr_reg_off_ext
    .type   isa_ldr_reg_off_ext, %function
isa_ldr_reg_off_ext:
     mov w3,#24
 ldr x2,[x0,w3,uxtw]
 str x2,[x1]
    ret

    .globl  isa_ldr_reg_lsl
    .type   isa_ldr_reg_lsl, %function
isa_ldr_reg_lsl:
     mov w3,#3
 ldr x2,[x0,w3,sxtw #3]
 str x2,[x1]
    ret

    .globl  isa_stp_post
    .type   isa_stp_post, %function
isa_stp_post:
     ldp x2,x3,[x0]
 stp x2,x3,[x1],#16
 stp x2,x3,[x1]
    ret

    .globl  isa_strb_post
    .type   isa_strb_post, %function
isa_strb_post:
     ldrb w2,[x0]
 strb w2,[x1],#1
 ldrb w3,[x1]
 strb w3,[x1,#1]
    ret

    .globl  isa_sbfx
    .type   isa_sbfx, %function
isa_sbfx:
     ldr x2,[x0]
 sbfx x3,x2,#5,#9
 str x3,[x1]
    ret

    .globl  isa_ubfx
    .type   isa_ubfx, %function
isa_ubfx:
     ldr x2,[x0]
 ubfx x3,x2,#5,#9
 str x3,[x1]
    ret

    .globl  isa_bfi
    .type   isa_bfi, %function
isa_bfi:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 bfi x2,x3,#5,#9
 str x2,[x1]
    ret

    .globl  isa_bfxil
    .type   isa_bfxil, %function
isa_bfxil:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 bfxil x2,x3,#5,#9
 str x2,[x1]
    ret

    .globl  isa_ubfiz
    .type   isa_ubfiz, %function
isa_ubfiz:
     ldr x2,[x0]
 ubfiz x3,x2,#3,#20
 str x3,[x1]
    ret

    .globl  isa_sbfiz
    .type   isa_sbfiz, %function
isa_sbfiz:
     ldr x2,[x0]
 sbfiz x3,x2,#3,#20
 str x3,[x1]
    ret

    .globl  isa_extr
    .type   isa_extr, %function
isa_extr:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 extr x4,x2,x3,#13
 str x4,[x1]
    ret

    .globl  isa_udiv
    .type   isa_udiv, %function
isa_udiv:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 udiv x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_sdiv
    .type   isa_sdiv, %function
isa_sdiv:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 sdiv x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_madd
    .type   isa_madd, %function
isa_madd:
     ldp x2,x3,[x0]
 ldr x4,[x0,#16]
 madd x5,x2,x3,x4
 str x5,[x1]
    ret

    .globl  isa_msub
    .type   isa_msub, %function
isa_msub:
     ldp x2,x3,[x0]
 ldr x4,[x0,#16]
 msub x5,x2,x3,x4
 str x5,[x1]
    ret

    .globl  isa_smull
    .type   isa_smull, %function
isa_smull:
     ldp w2,w3,[x0]
 smull x4,w2,w3
 str x4,[x1]
    ret

    .globl  isa_umull
    .type   isa_umull, %function
isa_umull:
     ldp w2,w3,[x0]
 umull x4,w2,w3
 str x4,[x1]
    ret

    .globl  isa_smulh
    .type   isa_smulh, %function
isa_smulh:
     ldp x2,x3,[x0]
 smulh x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_umulh
    .type   isa_umulh, %function
isa_umulh:
     ldp x2,x3,[x0]
 umulh x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_smaddl
    .type   isa_smaddl, %function
isa_smaddl:
     ldp w2,w3,[x0]
 ldr x4,[x0,#16]
 smaddl x5,w2,w3,x4
 str x5,[x1]
    ret

    .globl  isa_umaddl
    .type   isa_umaddl, %function
isa_umaddl:
     ldp w2,w3,[x0]
 ldr x4,[x0,#16]
 umaddl x5,w2,w3,x4
 str x5,[x1]
    ret

    .globl  isa_smulh_neg
    .type   isa_smulh_neg, %function
isa_smulh_neg:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 smulh x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_clz
    .type   isa_clz, %function
isa_clz:
     ldr x2,[x0]
 clz x3,x2
 str x3,[x1]
    ret

    .globl  isa_rbit
    .type   isa_rbit, %function
isa_rbit:
     ldr x2,[x0]
 rbit x3,x2
 str x3,[x1]
    ret

    .globl  isa_rev16
    .type   isa_rev16, %function
isa_rev16:
     ldr x2,[x0]
 rev16 x3,x2
 str x3,[x1]
    ret

    .globl  isa_rev32
    .type   isa_rev32, %function
isa_rev32:
     ldr x2,[x0]
 rev32 x3,x2
 str x3,[x1]
    ret

    .globl  isa_rev
    .type   isa_rev, %function
isa_rev:
     ldr x2,[x0]
 rev x3,x2
 str x3,[x1]
    ret

    .globl  isa_csel
    .type   isa_csel, %function
isa_csel:
     ldp x2,x3,[x0]
 cmp x2,x3
 csel x4,x2,x3,hi
 str x4,[x1]
    ret

    .globl  isa_csinc
    .type   isa_csinc, %function
isa_csinc:
     ldp x2,x3,[x0]
 cmp x2,x3
 csinc x4,x2,x3,lt
 str x4,[x1]
    ret

    .globl  isa_csneg
    .type   isa_csneg, %function
isa_csneg:
     ldp x2,x3,[x0]
 cmp x2,x3
 csneg x4,x2,x3,eq
 str x4,[x1]
    ret

    .globl  isa_csinv
    .type   isa_csinv, %function
isa_csinv:
     ldp x2,x3,[x0]
 cmp x2,x3
 csinv x4,x2,x3,ne
 str x4,[x1]
    ret

    .globl  isa_cset
    .type   isa_cset, %function
isa_cset:
     ldr x2,[x0]
 cmp x2,#5
 cset x3,gt
 str x3,[x1]
    ret

    .globl  isa_csetm
    .type   isa_csetm, %function
isa_csetm:
     ldr x2,[x0]
 cmp x2,#5
 csetm x3,ls
 str x3,[x1]
    ret

    .globl  isa_ccmp
    .type   isa_ccmp, %function
isa_ccmp:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 cmp x2,#7
 ccmp x3,#9,#2,eq
 cset x4,le
 str x4,[x1]
    ret

    .globl  isa_ccmn
    .type   isa_ccmn, %function
isa_ccmn:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 cmp x2,#7
 ccmn x3,#9,#2,eq
 cset x4,gt
 str x4,[x1]
    ret

    .globl  isa_adc
    .type   isa_adc, %function
isa_adc:
     ldp x2,x3,[x0]
 adds x2,x2,x3
 adc x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_sbc
    .type   isa_sbc, %function
isa_sbc:
     ldp x2,x3,[x0]
 subs x2,x2,x3
 sbc x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_ngc
    .type   isa_ngc, %function
isa_ngc:
     ldr x2,[x0]
 cmp x2,#1
 ngc x3,x2
 str x3,[x1]
    ret

    .globl  isa_tst_br
    .type   isa_tst_br, %function
isa_tst_br:
     ldr x2,[x0]
 tst x2,#0xf0
 cset x3,ne
 str x3,[x1]
    ret

    .globl  isa_cmn
    .type   isa_cmn, %function
isa_cmn:
     ldr x2,[x0]
 cmn x2,#7
 cset x3,mi
 str x3,[x1]
    ret

    .globl  isa_tbz
    .type   isa_tbz, %function
isa_tbz:
     ldr x2,[x0]
 tbz w2,#3,1f
 mov x3,#1
 b 2f
1:
 mov x3,#0
2:
 str x3,[x1]
    ret

    .globl  isa_tbnz
    .type   isa_tbnz, %function
isa_tbnz:
     ldr x2,[x0]
 tbnz w2,#7,1f
 mov x3,#1
 b 2f
1:
 mov x3,#0
2:
 str x3,[x1]
    ret

    .globl  isa_madd32
    .type   isa_madd32, %function
isa_madd32:
     ldp w2,w3,[x0]
 ldr w4,[x0,#16]
 madd w5,w2,w3,w4
 str w5,[x1]
    ret

    .globl  isa_neg_lsl
    .type   isa_neg_lsl, %function
isa_neg_lsl:
     ldr x2,[x0]
 neg x3,x2, lsl #3
 str x3,[x1]
    ret

    .globl  isa_lsl_reg
    .type   isa_lsl_reg, %function
isa_lsl_reg:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 lsl x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_asr_reg
    .type   isa_asr_reg, %function
isa_asr_reg:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 asr x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_lsr_reg
    .type   isa_lsr_reg, %function
isa_lsr_reg:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 lsr x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_ror_reg
    .type   isa_ror_reg, %function
isa_ror_reg:
     ldr x2,[x0]
 ldr x3,[x0,#8]
 ror x4,x2,x3
 str x4,[x1]
    ret

    .globl  isa_movk
    .type   isa_movk, %function
isa_movk:
     movz x2,#0x1234
 movk x2,#0xabcd, lsl #16
 str x2,[x1]
    ret

    .globl  isa_movn
    .type   isa_movn, %function
isa_movn:
     movn x2,#0x1234
 str x2,[x1]
    ret

    .globl  isa_adr_adrp
    .type   isa_adr_adrp, %function
isa_adr_adrp:
     adr x2,1f
1:
 adr x3,1b
 sub x2,x2,x3
 str x2,[x1]
    ret

    .globl  isa_ands_flags
    .type   isa_ands_flags, %function
isa_ands_flags:
     ldr x2,[x0]
 ands x3,x2,#0xf0000000
 cset x4,ne
 str x4,[x1]
    ret

    .globl  isa_sxtb_sxth
    .type   isa_sxtb_sxth, %function
isa_sxtb_sxth:
     ldrb w2,[x0,#7]
 sxtb x3,w2
 sxth x4,w2
 stp x3,x4,[x1]
    ret

    .globl  isa_ubfiz_32
    .type   isa_ubfiz_32, %function
isa_ubfiz_32:
     ldr w2,[x0]
 ubfiz w3,w2,#3,#20
 str w3,[x1]
    ret

    .globl  isa_ldxrb_stxrb
    .type   isa_ldxrb_stxrb, %function
isa_ldxrb_stxrb:
     ldrb w2,[x0]
 ldxrb w3,[x0]
 stxrb w4,w2,[x1]
 stp x3,x4,[x1,#16]
    ret

    .globl  isa_swp_s
    .type   isa_swp_s, %function
isa_swp_s:
     ldr w2,[x0]
 ldaxr w3,[x0]
 stlxr w4,w2,[x1]
 str w3,[x1,#16]
 str w4,[x1,#20]
    ret

    .globl  isa_ldar_stlr
    .type   isa_ldar_stlr, %function
isa_ldar_stlr:
     ldar x2,[x0]
 stlr x2,[x1]
    ret

    .globl  isa_dmb_barriers
    .type   isa_dmb_barriers, %function
isa_dmb_barriers:
     ldr x2,[x0]
 dmb ish
 dsb sy
 isb
 str x2,[x1]
    ret

