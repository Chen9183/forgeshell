/* 由 tests/gen_isa_bulk.py 生成：批量指令差分测试驱动（勿手改） */
#include <unistd.h>
#include <string.h>

typedef void (*fn_t)(unsigned char *, unsigned char *);

struct ent { const char *name; fn_t fn; };
extern void isa_add_16b(unsigned char *, unsigned char *);
extern void isa_add_8h(unsigned char *, unsigned char *);
extern void isa_add_4s(unsigned char *, unsigned char *);
extern void isa_add_2d(unsigned char *, unsigned char *);
extern void isa_sub_16b(unsigned char *, unsigned char *);
extern void isa_sub_8h(unsigned char *, unsigned char *);
extern void isa_sub_4s(unsigned char *, unsigned char *);
extern void isa_sub_2d(unsigned char *, unsigned char *);
extern void isa_mul_16b(unsigned char *, unsigned char *);
extern void isa_mul_8h(unsigned char *, unsigned char *);
extern void isa_mul_4s(unsigned char *, unsigned char *);
extern void isa_smax_16b(unsigned char *, unsigned char *);
extern void isa_smax_8h(unsigned char *, unsigned char *);
extern void isa_smax_4s(unsigned char *, unsigned char *);
extern void isa_smin_16b(unsigned char *, unsigned char *);
extern void isa_smin_8h(unsigned char *, unsigned char *);
extern void isa_smin_4s(unsigned char *, unsigned char *);
extern void isa_umax_16b(unsigned char *, unsigned char *);
extern void isa_umax_8h(unsigned char *, unsigned char *);
extern void isa_umax_4s(unsigned char *, unsigned char *);
extern void isa_umin_16b(unsigned char *, unsigned char *);
extern void isa_umin_8h(unsigned char *, unsigned char *);
extern void isa_umin_4s(unsigned char *, unsigned char *);
extern void isa_sabd_16b(unsigned char *, unsigned char *);
extern void isa_sabd_8h(unsigned char *, unsigned char *);
extern void isa_sabd_4s(unsigned char *, unsigned char *);
extern void isa_uabd_16b(unsigned char *, unsigned char *);
extern void isa_uabd_8h(unsigned char *, unsigned char *);
extern void isa_uabd_4s(unsigned char *, unsigned char *);
extern void isa_cmgt_16b(unsigned char *, unsigned char *);
extern void isa_cmgt_8h(unsigned char *, unsigned char *);
extern void isa_cmgt_4s(unsigned char *, unsigned char *);
extern void isa_cmgt_2d(unsigned char *, unsigned char *);
extern void isa_cmge_16b(unsigned char *, unsigned char *);
extern void isa_cmge_8h(unsigned char *, unsigned char *);
extern void isa_cmge_4s(unsigned char *, unsigned char *);
extern void isa_cmge_2d(unsigned char *, unsigned char *);
extern void isa_cmeq_16b(unsigned char *, unsigned char *);
extern void isa_cmeq_8h(unsigned char *, unsigned char *);
extern void isa_cmeq_4s(unsigned char *, unsigned char *);
extern void isa_cmeq_2d(unsigned char *, unsigned char *);
extern void isa_cmhi_16b(unsigned char *, unsigned char *);
extern void isa_cmhi_8h(unsigned char *, unsigned char *);
extern void isa_cmhi_4s(unsigned char *, unsigned char *);
extern void isa_cmhi_2d(unsigned char *, unsigned char *);
extern void isa_cmhs_16b(unsigned char *, unsigned char *);
extern void isa_cmhs_8h(unsigned char *, unsigned char *);
extern void isa_cmhs_4s(unsigned char *, unsigned char *);
extern void isa_cmhs_2d(unsigned char *, unsigned char *);
extern void isa_sqadd_16b(unsigned char *, unsigned char *);
extern void isa_sqadd_8h(unsigned char *, unsigned char *);
extern void isa_sqadd_4s(unsigned char *, unsigned char *);
extern void isa_sqadd_2d(unsigned char *, unsigned char *);
extern void isa_uqadd_16b(unsigned char *, unsigned char *);
extern void isa_uqadd_8h(unsigned char *, unsigned char *);
extern void isa_uqadd_4s(unsigned char *, unsigned char *);
extern void isa_uqadd_2d(unsigned char *, unsigned char *);
extern void isa_sqsub_16b(unsigned char *, unsigned char *);
extern void isa_sqsub_8h(unsigned char *, unsigned char *);
extern void isa_sqsub_4s(unsigned char *, unsigned char *);
extern void isa_sqsub_2d(unsigned char *, unsigned char *);
extern void isa_uqsub_16b(unsigned char *, unsigned char *);
extern void isa_uqsub_8h(unsigned char *, unsigned char *);
extern void isa_uqsub_4s(unsigned char *, unsigned char *);
extern void isa_uqsub_2d(unsigned char *, unsigned char *);
extern void isa_shadd_16b(unsigned char *, unsigned char *);
extern void isa_shadd_8h(unsigned char *, unsigned char *);
extern void isa_shadd_4s(unsigned char *, unsigned char *);
extern void isa_uhadd_16b(unsigned char *, unsigned char *);
extern void isa_uhadd_8h(unsigned char *, unsigned char *);
extern void isa_uhadd_4s(unsigned char *, unsigned char *);
extern void isa_mla_16b(unsigned char *, unsigned char *);
extern void isa_mla_8h(unsigned char *, unsigned char *);
extern void isa_mla_4s(unsigned char *, unsigned char *);
extern void isa_mls_16b(unsigned char *, unsigned char *);
extern void isa_mls_8h(unsigned char *, unsigned char *);
extern void isa_mls_4s(unsigned char *, unsigned char *);
extern void isa_smaxp_16b(unsigned char *, unsigned char *);
extern void isa_smaxp_8h(unsigned char *, unsigned char *);
extern void isa_smaxp_4s(unsigned char *, unsigned char *);
extern void isa_addp_16b(unsigned char *, unsigned char *);
extern void isa_addp_8h(unsigned char *, unsigned char *);
extern void isa_addp_4s(unsigned char *, unsigned char *);
extern void isa_addp_2d(unsigned char *, unsigned char *);
extern void isa_and_16b(unsigned char *, unsigned char *);
extern void isa_bic_16b(unsigned char *, unsigned char *);
extern void isa_orr_16b(unsigned char *, unsigned char *);
extern void isa_orn_16b(unsigned char *, unsigned char *);
extern void isa_eor_16b(unsigned char *, unsigned char *);
extern void isa_bsl_16b(unsigned char *, unsigned char *);
extern void isa_bit_16b(unsigned char *, unsigned char *);
extern void isa_bif_16b(unsigned char *, unsigned char *);
extern void isa_add_8b(unsigned char *, unsigned char *);
extern void isa_add_4h(unsigned char *, unsigned char *);
extern void isa_add_2s(unsigned char *, unsigned char *);
extern void isa_sub_8b(unsigned char *, unsigned char *);
extern void isa_sub_4h(unsigned char *, unsigned char *);
extern void isa_sub_2s(unsigned char *, unsigned char *);
extern void isa_mul_8b(unsigned char *, unsigned char *);
extern void isa_mul_4h(unsigned char *, unsigned char *);
extern void isa_mul_2s(unsigned char *, unsigned char *);
extern void isa_cmeq_8b(unsigned char *, unsigned char *);
extern void isa_cmeq_4h(unsigned char *, unsigned char *);
extern void isa_cmeq_2s(unsigned char *, unsigned char *);
extern void isa_cmgt_8b(unsigned char *, unsigned char *);
extern void isa_cmgt_4h(unsigned char *, unsigned char *);
extern void isa_cmgt_2s(unsigned char *, unsigned char *);
extern void isa_and_8b(unsigned char *, unsigned char *);
extern void isa_orr_8b(unsigned char *, unsigned char *);
extern void isa_eor_8b(unsigned char *, unsigned char *);
extern void isa_uzp1_16b(unsigned char *, unsigned char *);
extern void isa_uzp1_8h(unsigned char *, unsigned char *);
extern void isa_uzp1_4s(unsigned char *, unsigned char *);
extern void isa_uzp1_2d(unsigned char *, unsigned char *);
extern void isa_uzp1_8b(unsigned char *, unsigned char *);
extern void isa_uzp1_4h(unsigned char *, unsigned char *);
extern void isa_uzp2_16b(unsigned char *, unsigned char *);
extern void isa_uzp2_8h(unsigned char *, unsigned char *);
extern void isa_uzp2_4s(unsigned char *, unsigned char *);
extern void isa_uzp2_2d(unsigned char *, unsigned char *);
extern void isa_uzp2_8b(unsigned char *, unsigned char *);
extern void isa_uzp2_4h(unsigned char *, unsigned char *);
extern void isa_zip1_16b(unsigned char *, unsigned char *);
extern void isa_zip1_8h(unsigned char *, unsigned char *);
extern void isa_zip1_4s(unsigned char *, unsigned char *);
extern void isa_zip1_2d(unsigned char *, unsigned char *);
extern void isa_zip1_8b(unsigned char *, unsigned char *);
extern void isa_zip1_4h(unsigned char *, unsigned char *);
extern void isa_zip2_16b(unsigned char *, unsigned char *);
extern void isa_zip2_8h(unsigned char *, unsigned char *);
extern void isa_zip2_4s(unsigned char *, unsigned char *);
extern void isa_zip2_2d(unsigned char *, unsigned char *);
extern void isa_zip2_8b(unsigned char *, unsigned char *);
extern void isa_zip2_4h(unsigned char *, unsigned char *);
extern void isa_trn1_16b(unsigned char *, unsigned char *);
extern void isa_trn1_8h(unsigned char *, unsigned char *);
extern void isa_trn1_4s(unsigned char *, unsigned char *);
extern void isa_trn1_2d(unsigned char *, unsigned char *);
extern void isa_trn1_8b(unsigned char *, unsigned char *);
extern void isa_trn1_4h(unsigned char *, unsigned char *);
extern void isa_trn2_16b(unsigned char *, unsigned char *);
extern void isa_trn2_8h(unsigned char *, unsigned char *);
extern void isa_trn2_4s(unsigned char *, unsigned char *);
extern void isa_trn2_2d(unsigned char *, unsigned char *);
extern void isa_trn2_8b(unsigned char *, unsigned char *);
extern void isa_trn2_4h(unsigned char *, unsigned char *);
extern void isa_ext16b_0(unsigned char *, unsigned char *);
extern void isa_ext16b_1(unsigned char *, unsigned char *);
extern void isa_ext16b_3(unsigned char *, unsigned char *);
extern void isa_ext16b_7(unsigned char *, unsigned char *);
extern void isa_ext16b_8(unsigned char *, unsigned char *);
extern void isa_ext16b_15(unsigned char *, unsigned char *);
extern void isa_ext8b_2(unsigned char *, unsigned char *);
extern void isa_shl_16b_1(unsigned char *, unsigned char *);
extern void isa_ushr_16b_1(unsigned char *, unsigned char *);
extern void isa_sshr_16b_1(unsigned char *, unsigned char *);
extern void isa_sri_16b_1(unsigned char *, unsigned char *);
extern void isa_shl_16b_3(unsigned char *, unsigned char *);
extern void isa_ushr_16b_3(unsigned char *, unsigned char *);
extern void isa_sshr_16b_3(unsigned char *, unsigned char *);
extern void isa_sri_16b_3(unsigned char *, unsigned char *);
extern void isa_shl_16b_7(unsigned char *, unsigned char *);
extern void isa_ushr_16b_7(unsigned char *, unsigned char *);
extern void isa_sshr_16b_7(unsigned char *, unsigned char *);
extern void isa_sri_16b_7(unsigned char *, unsigned char *);
extern void isa_shl_8h_1(unsigned char *, unsigned char *);
extern void isa_sshr_8h_1(unsigned char *, unsigned char *);
extern void isa_ushr_8h_1(unsigned char *, unsigned char *);
extern void isa_shl_8h_5(unsigned char *, unsigned char *);
extern void isa_sshr_8h_5(unsigned char *, unsigned char *);
extern void isa_ushr_8h_5(unsigned char *, unsigned char *);
extern void isa_shl_8h_11(unsigned char *, unsigned char *);
extern void isa_sshr_8h_11(unsigned char *, unsigned char *);
extern void isa_ushr_8h_11(unsigned char *, unsigned char *);
extern void isa_shl_4s_1(unsigned char *, unsigned char *);
extern void isa_sshr_4s_1(unsigned char *, unsigned char *);
extern void isa_shl_4s_10(unsigned char *, unsigned char *);
extern void isa_sshr_4s_10(unsigned char *, unsigned char *);
extern void isa_shl_2d_3(unsigned char *, unsigned char *);
extern void isa_sshr_2d_5(unsigned char *, unsigned char *);
extern void isa_usra_16b_2(unsigned char *, unsigned char *);
extern void isa_ssra_16b_2(unsigned char *, unsigned char *);
extern void isa_urshr_16b_2(unsigned char *, unsigned char *);
extern void isa_srsra_16b_2(unsigned char *, unsigned char *);
extern void isa_sqshl_16b_1(unsigned char *, unsigned char *);
extern void isa_ushll_8h_1(unsigned char *, unsigned char *);
extern void isa_sshll_8h_1(unsigned char *, unsigned char *);
extern void isa_ushll_4s_2(unsigned char *, unsigned char *);
extern void isa_sshll_4s_2(unsigned char *, unsigned char *);
extern void isa_xtn_8b(unsigned char *, unsigned char *);
extern void isa_xtn2_16b(unsigned char *, unsigned char *);
extern void isa_sqxtn_8b(unsigned char *, unsigned char *);
extern void isa_uqxtn_8b(unsigned char *, unsigned char *);
extern void isa_sqxtun_8b(unsigned char *, unsigned char *);
extern void isa_movi_16b_3f(unsigned char *, unsigned char *);
extern void isa_movi_8b_1(unsigned char *, unsigned char *);
extern void isa_movi_4s_12(unsigned char *, unsigned char *);
extern void isa_movi_2s_1(unsigned char *, unsigned char *);
extern void isa_movi_4h_5(unsigned char *, unsigned char *);
extern void isa_movi_8h_5(unsigned char *, unsigned char *);
extern void isa_mvni_4s_1(unsigned char *, unsigned char *);
extern void isa_movi_4s_lsl8(unsigned char *, unsigned char *);
extern void isa_dup_16b_w(unsigned char *, unsigned char *);
extern void isa_dup_8h_w(unsigned char *, unsigned char *);
extern void isa_dup_4s_w(unsigned char *, unsigned char *);
extern void isa_dup_2d_x(unsigned char *, unsigned char *);
extern void isa_dup_elem_b(unsigned char *, unsigned char *);
extern void isa_dup_elem_h(unsigned char *, unsigned char *);
extern void isa_dup_elem_s(unsigned char *, unsigned char *);
extern void isa_ins_elem(unsigned char *, unsigned char *);
extern void isa_umov_b(unsigned char *, unsigned char *);
extern void isa_umov_h(unsigned char *, unsigned char *);
extern void isa_umov_s(unsigned char *, unsigned char *);
extern void isa_umov_d(unsigned char *, unsigned char *);
extern void isa_smov_b(unsigned char *, unsigned char *);
extern void isa_ins_gen_b(unsigned char *, unsigned char *);
extern void isa_ins_gen_s(unsigned char *, unsigned char *);
extern void isa_mov_d1_x(unsigned char *, unsigned char *);
extern void isa_mov_x_d1(unsigned char *, unsigned char *);
extern void isa_addv_16b(unsigned char *, unsigned char *);
extern void isa_addv_8h(unsigned char *, unsigned char *);
extern void isa_addv_4s(unsigned char *, unsigned char *);
extern void isa_saddlv_16b(unsigned char *, unsigned char *);
extern void isa_uaddlv_16b(unsigned char *, unsigned char *);
extern void isa_smaxv_16b(unsigned char *, unsigned char *);
extern void isa_umaxv_16b(unsigned char *, unsigned char *);
extern void isa_sminv_4s(unsigned char *, unsigned char *);
extern void isa_cnt_16b(unsigned char *, unsigned char *);
extern void isa_rbit_16b(unsigned char *, unsigned char *);
extern void isa_rev16_16b(unsigned char *, unsigned char *);
extern void isa_rev32_16b(unsigned char *, unsigned char *);
extern void isa_rev64_16b(unsigned char *, unsigned char *);
extern void isa_abs_16b(unsigned char *, unsigned char *);
extern void isa_neg_16b(unsigned char *, unsigned char *);
extern void isa_mvn_16b(unsigned char *, unsigned char *);
extern void isa_not_16b(unsigned char *, unsigned char *);
extern void isa_cls_16b(unsigned char *, unsigned char *);
extern void isa_clz_16b(unsigned char *, unsigned char *);
extern void isa_saddlp_16b(unsigned char *, unsigned char *);
extern void isa_uaddlp_16b(unsigned char *, unsigned char *);
extern void isa_xtl_8h(unsigned char *, unsigned char *);
extern void isa_fadd_4s(unsigned char *, unsigned char *);
extern void isa_fsub_4s(unsigned char *, unsigned char *);
extern void isa_fmul_4s(unsigned char *, unsigned char *);
extern void isa_fdiv_4s(unsigned char *, unsigned char *);
extern void isa_fadd_2d(unsigned char *, unsigned char *);
extern void isa_fmul_2d(unsigned char *, unsigned char *);
extern void isa_fmax_4s(unsigned char *, unsigned char *);
extern void isa_fmin_4s(unsigned char *, unsigned char *);
extern void isa_fmla_4s(unsigned char *, unsigned char *);
extern void isa_fabs_4s(unsigned char *, unsigned char *);
extern void isa_fneg_4s(unsigned char *, unsigned char *);
extern void isa_fsqrt_4s(unsigned char *, unsigned char *);
extern void isa_fcmeq_4s(unsigned char *, unsigned char *);
extern void isa_fcvtl_4s(unsigned char *, unsigned char *);
extern void isa_scvtf_4s(unsigned char *, unsigned char *);
extern void isa_ucvtf_4s(unsigned char *, unsigned char *);
extern void isa_fcvtzs_4s(unsigned char *, unsigned char *);
extern void isa_fadd_s(unsigned char *, unsigned char *);
extern void isa_fsub_s(unsigned char *, unsigned char *);
extern void isa_fmul_s(unsigned char *, unsigned char *);
extern void isa_fdiv_s(unsigned char *, unsigned char *);
extern void isa_fadd_d(unsigned char *, unsigned char *);
extern void isa_fmul_d(unsigned char *, unsigned char *);
extern void isa_fabs_s(unsigned char *, unsigned char *);
extern void isa_fneg_s(unsigned char *, unsigned char *);
extern void isa_fsqrt_s(unsigned char *, unsigned char *);
extern void isa_fmin_s(unsigned char *, unsigned char *);
extern void isa_fmax_s(unsigned char *, unsigned char *);
extern void isa_frintm_s(unsigned char *, unsigned char *);
extern void isa_frintn_s(unsigned char *, unsigned char *);
extern void isa_fcvtzs_s_w(unsigned char *, unsigned char *);
extern void isa_fcvtzu_s_w(unsigned char *, unsigned char *);
extern void isa_scvtf_w_s(unsigned char *, unsigned char *);
extern void isa_ucvtf_w_s(unsigned char *, unsigned char *);
extern void isa_fcvt_d_s(unsigned char *, unsigned char *);
extern void isa_fcvt_s_d(unsigned char *, unsigned char *);
extern void isa_fmov_w_s(unsigned char *, unsigned char *);
extern void isa_fmov_s_w(unsigned char *, unsigned char *);
extern void isa_fmov_x_d(unsigned char *, unsigned char *);
extern void isa_fmov_d_x(unsigned char *, unsigned char *);
extern void isa_fcmp_s(unsigned char *, unsigned char *);
extern void isa_fmadd_s(unsigned char *, unsigned char *);
extern void isa_fmsub_s(unsigned char *, unsigned char *);
extern void isa_ldr_q_off(unsigned char *, unsigned char *);
extern void isa_ldr_q_post(unsigned char *, unsigned char *);
extern void isa_ldr_q_pre(unsigned char *, unsigned char *);
extern void isa_str_q_post(unsigned char *, unsigned char *);
extern void isa_str_q_pre(unsigned char *, unsigned char *);
extern void isa_ldr_d_off(unsigned char *, unsigned char *);
extern void isa_ldr_s_off(unsigned char *, unsigned char *);
extern void isa_ldr_h_off(unsigned char *, unsigned char *);
extern void isa_ldr_b_off(unsigned char *, unsigned char *);
extern void isa_ldr_q_reg(unsigned char *, unsigned char *);
extern void isa_str_q_reg(unsigned char *, unsigned char *);
extern void isa_ldp_q(unsigned char *, unsigned char *);
extern void isa_stp_q(unsigned char *, unsigned char *);
extern void isa_stp_q_post(unsigned char *, unsigned char *);
extern void isa_ldp_d(unsigned char *, unsigned char *);
extern void isa_stp_s(unsigned char *, unsigned char *);
extern void isa_ld1_16b(unsigned char *, unsigned char *);
extern void isa_st1_16b(unsigned char *, unsigned char *);
extern void isa_ld1_multi(unsigned char *, unsigned char *);
extern void isa_ld2_16b(unsigned char *, unsigned char *);
extern void isa_st2_16b(unsigned char *, unsigned char *);
extern void isa_ld3_16b(unsigned char *, unsigned char *);
extern void isa_ld4_16b(unsigned char *, unsigned char *);
extern void isa_ld1r_16b(unsigned char *, unsigned char *);
extern void isa_ld2r_8h(unsigned char *, unsigned char *);
extern void isa_ld1_single(unsigned char *, unsigned char *);
extern void isa_st1_single(unsigned char *, unsigned char *);
extern void isa_ldr_x_off(unsigned char *, unsigned char *);
extern void isa_ldr_w_off(unsigned char *, unsigned char *);
extern void isa_ldr_x_post(unsigned char *, unsigned char *);
extern void isa_ldr_x_pre(unsigned char *, unsigned char *);
extern void isa_ldur_x(unsigned char *, unsigned char *);
extern void isa_ldrsb_w(unsigned char *, unsigned char *);
extern void isa_ldrsb_x(unsigned char *, unsigned char *);
extern void isa_ldrsh_w(unsigned char *, unsigned char *);
extern void isa_ldrsh_x(unsigned char *, unsigned char *);
extern void isa_ldrsw_x(unsigned char *, unsigned char *);
extern void isa_ldrb_w(unsigned char *, unsigned char *);
extern void isa_ldrh_w(unsigned char *, unsigned char *);
extern void isa_ldr_reg_off(unsigned char *, unsigned char *);
extern void isa_ldr_reg_off_ext(unsigned char *, unsigned char *);
extern void isa_ldr_reg_lsl(unsigned char *, unsigned char *);
extern void isa_stp_post(unsigned char *, unsigned char *);
extern void isa_strb_post(unsigned char *, unsigned char *);
extern void isa_sbfx(unsigned char *, unsigned char *);
extern void isa_ubfx(unsigned char *, unsigned char *);
extern void isa_bfi(unsigned char *, unsigned char *);
extern void isa_bfxil(unsigned char *, unsigned char *);
extern void isa_ubfiz(unsigned char *, unsigned char *);
extern void isa_sbfiz(unsigned char *, unsigned char *);
extern void isa_extr(unsigned char *, unsigned char *);
extern void isa_udiv(unsigned char *, unsigned char *);
extern void isa_sdiv(unsigned char *, unsigned char *);
extern void isa_madd(unsigned char *, unsigned char *);
extern void isa_msub(unsigned char *, unsigned char *);
extern void isa_smull(unsigned char *, unsigned char *);
extern void isa_umull(unsigned char *, unsigned char *);
extern void isa_smulh(unsigned char *, unsigned char *);
extern void isa_umulh(unsigned char *, unsigned char *);
extern void isa_smaddl(unsigned char *, unsigned char *);
extern void isa_umaddl(unsigned char *, unsigned char *);
extern void isa_smulh_neg(unsigned char *, unsigned char *);
extern void isa_clz(unsigned char *, unsigned char *);
extern void isa_rbit(unsigned char *, unsigned char *);
extern void isa_rev16(unsigned char *, unsigned char *);
extern void isa_rev32(unsigned char *, unsigned char *);
extern void isa_rev(unsigned char *, unsigned char *);
extern void isa_csel(unsigned char *, unsigned char *);
extern void isa_csinc(unsigned char *, unsigned char *);
extern void isa_csneg(unsigned char *, unsigned char *);
extern void isa_csinv(unsigned char *, unsigned char *);
extern void isa_cset(unsigned char *, unsigned char *);
extern void isa_csetm(unsigned char *, unsigned char *);
extern void isa_ccmp(unsigned char *, unsigned char *);
extern void isa_ccmn(unsigned char *, unsigned char *);
extern void isa_adc(unsigned char *, unsigned char *);
extern void isa_sbc(unsigned char *, unsigned char *);
extern void isa_ngc(unsigned char *, unsigned char *);
extern void isa_tst_br(unsigned char *, unsigned char *);
extern void isa_cmn(unsigned char *, unsigned char *);
extern void isa_tbz(unsigned char *, unsigned char *);
extern void isa_tbnz(unsigned char *, unsigned char *);
extern void isa_madd32(unsigned char *, unsigned char *);
extern void isa_neg_lsl(unsigned char *, unsigned char *);
extern void isa_lsl_reg(unsigned char *, unsigned char *);
extern void isa_asr_reg(unsigned char *, unsigned char *);
extern void isa_lsr_reg(unsigned char *, unsigned char *);
extern void isa_ror_reg(unsigned char *, unsigned char *);
extern void isa_movk(unsigned char *, unsigned char *);
extern void isa_movn(unsigned char *, unsigned char *);
extern void isa_adr_adrp(unsigned char *, unsigned char *);
extern void isa_ands_flags(unsigned char *, unsigned char *);
extern void isa_sxtb_sxth(unsigned char *, unsigned char *);
extern void isa_ubfiz_32(unsigned char *, unsigned char *);
extern void isa_ldxrb_stxrb(unsigned char *, unsigned char *);
extern void isa_swp_s(unsigned char *, unsigned char *);
extern void isa_ldar_stlr(unsigned char *, unsigned char *);
extern void isa_dmb_barriers(unsigned char *, unsigned char *);

static struct ent tbl[] = {
    { "add_16b", isa_add_16b },
    { "add_8h", isa_add_8h },
    { "add_4s", isa_add_4s },
    { "add_2d", isa_add_2d },
    { "sub_16b", isa_sub_16b },
    { "sub_8h", isa_sub_8h },
    { "sub_4s", isa_sub_4s },
    { "sub_2d", isa_sub_2d },
    { "mul_16b", isa_mul_16b },
    { "mul_8h", isa_mul_8h },
    { "mul_4s", isa_mul_4s },
    { "smax_16b", isa_smax_16b },
    { "smax_8h", isa_smax_8h },
    { "smax_4s", isa_smax_4s },
    { "smin_16b", isa_smin_16b },
    { "smin_8h", isa_smin_8h },
    { "smin_4s", isa_smin_4s },
    { "umax_16b", isa_umax_16b },
    { "umax_8h", isa_umax_8h },
    { "umax_4s", isa_umax_4s },
    { "umin_16b", isa_umin_16b },
    { "umin_8h", isa_umin_8h },
    { "umin_4s", isa_umin_4s },
    { "sabd_16b", isa_sabd_16b },
    { "sabd_8h", isa_sabd_8h },
    { "sabd_4s", isa_sabd_4s },
    { "uabd_16b", isa_uabd_16b },
    { "uabd_8h", isa_uabd_8h },
    { "uabd_4s", isa_uabd_4s },
    { "cmgt_16b", isa_cmgt_16b },
    { "cmgt_8h", isa_cmgt_8h },
    { "cmgt_4s", isa_cmgt_4s },
    { "cmgt_2d", isa_cmgt_2d },
    { "cmge_16b", isa_cmge_16b },
    { "cmge_8h", isa_cmge_8h },
    { "cmge_4s", isa_cmge_4s },
    { "cmge_2d", isa_cmge_2d },
    { "cmeq_16b", isa_cmeq_16b },
    { "cmeq_8h", isa_cmeq_8h },
    { "cmeq_4s", isa_cmeq_4s },
    { "cmeq_2d", isa_cmeq_2d },
    { "cmhi_16b", isa_cmhi_16b },
    { "cmhi_8h", isa_cmhi_8h },
    { "cmhi_4s", isa_cmhi_4s },
    { "cmhi_2d", isa_cmhi_2d },
    { "cmhs_16b", isa_cmhs_16b },
    { "cmhs_8h", isa_cmhs_8h },
    { "cmhs_4s", isa_cmhs_4s },
    { "cmhs_2d", isa_cmhs_2d },
    { "sqadd_16b", isa_sqadd_16b },
    { "sqadd_8h", isa_sqadd_8h },
    { "sqadd_4s", isa_sqadd_4s },
    { "sqadd_2d", isa_sqadd_2d },
    { "uqadd_16b", isa_uqadd_16b },
    { "uqadd_8h", isa_uqadd_8h },
    { "uqadd_4s", isa_uqadd_4s },
    { "uqadd_2d", isa_uqadd_2d },
    { "sqsub_16b", isa_sqsub_16b },
    { "sqsub_8h", isa_sqsub_8h },
    { "sqsub_4s", isa_sqsub_4s },
    { "sqsub_2d", isa_sqsub_2d },
    { "uqsub_16b", isa_uqsub_16b },
    { "uqsub_8h", isa_uqsub_8h },
    { "uqsub_4s", isa_uqsub_4s },
    { "uqsub_2d", isa_uqsub_2d },
    { "shadd_16b", isa_shadd_16b },
    { "shadd_8h", isa_shadd_8h },
    { "shadd_4s", isa_shadd_4s },
    { "uhadd_16b", isa_uhadd_16b },
    { "uhadd_8h", isa_uhadd_8h },
    { "uhadd_4s", isa_uhadd_4s },
    { "mla_16b", isa_mla_16b },
    { "mla_8h", isa_mla_8h },
    { "mla_4s", isa_mla_4s },
    { "mls_16b", isa_mls_16b },
    { "mls_8h", isa_mls_8h },
    { "mls_4s", isa_mls_4s },
    { "smaxp_16b", isa_smaxp_16b },
    { "smaxp_8h", isa_smaxp_8h },
    { "smaxp_4s", isa_smaxp_4s },
    { "addp_16b", isa_addp_16b },
    { "addp_8h", isa_addp_8h },
    { "addp_4s", isa_addp_4s },
    { "addp_2d", isa_addp_2d },
    { "and_16b", isa_and_16b },
    { "bic_16b", isa_bic_16b },
    { "orr_16b", isa_orr_16b },
    { "orn_16b", isa_orn_16b },
    { "eor_16b", isa_eor_16b },
    { "bsl_16b", isa_bsl_16b },
    { "bit_16b", isa_bit_16b },
    { "bif_16b", isa_bif_16b },
    { "add_8b", isa_add_8b },
    { "add_4h", isa_add_4h },
    { "add_2s", isa_add_2s },
    { "sub_8b", isa_sub_8b },
    { "sub_4h", isa_sub_4h },
    { "sub_2s", isa_sub_2s },
    { "mul_8b", isa_mul_8b },
    { "mul_4h", isa_mul_4h },
    { "mul_2s", isa_mul_2s },
    { "cmeq_8b", isa_cmeq_8b },
    { "cmeq_4h", isa_cmeq_4h },
    { "cmeq_2s", isa_cmeq_2s },
    { "cmgt_8b", isa_cmgt_8b },
    { "cmgt_4h", isa_cmgt_4h },
    { "cmgt_2s", isa_cmgt_2s },
    { "and_8b", isa_and_8b },
    { "orr_8b", isa_orr_8b },
    { "eor_8b", isa_eor_8b },
    { "uzp1_16b", isa_uzp1_16b },
    { "uzp1_8h", isa_uzp1_8h },
    { "uzp1_4s", isa_uzp1_4s },
    { "uzp1_2d", isa_uzp1_2d },
    { "uzp1_8b", isa_uzp1_8b },
    { "uzp1_4h", isa_uzp1_4h },
    { "uzp2_16b", isa_uzp2_16b },
    { "uzp2_8h", isa_uzp2_8h },
    { "uzp2_4s", isa_uzp2_4s },
    { "uzp2_2d", isa_uzp2_2d },
    { "uzp2_8b", isa_uzp2_8b },
    { "uzp2_4h", isa_uzp2_4h },
    { "zip1_16b", isa_zip1_16b },
    { "zip1_8h", isa_zip1_8h },
    { "zip1_4s", isa_zip1_4s },
    { "zip1_2d", isa_zip1_2d },
    { "zip1_8b", isa_zip1_8b },
    { "zip1_4h", isa_zip1_4h },
    { "zip2_16b", isa_zip2_16b },
    { "zip2_8h", isa_zip2_8h },
    { "zip2_4s", isa_zip2_4s },
    { "zip2_2d", isa_zip2_2d },
    { "zip2_8b", isa_zip2_8b },
    { "zip2_4h", isa_zip2_4h },
    { "trn1_16b", isa_trn1_16b },
    { "trn1_8h", isa_trn1_8h },
    { "trn1_4s", isa_trn1_4s },
    { "trn1_2d", isa_trn1_2d },
    { "trn1_8b", isa_trn1_8b },
    { "trn1_4h", isa_trn1_4h },
    { "trn2_16b", isa_trn2_16b },
    { "trn2_8h", isa_trn2_8h },
    { "trn2_4s", isa_trn2_4s },
    { "trn2_2d", isa_trn2_2d },
    { "trn2_8b", isa_trn2_8b },
    { "trn2_4h", isa_trn2_4h },
    { "ext16b_0", isa_ext16b_0 },
    { "ext16b_1", isa_ext16b_1 },
    { "ext16b_3", isa_ext16b_3 },
    { "ext16b_7", isa_ext16b_7 },
    { "ext16b_8", isa_ext16b_8 },
    { "ext16b_15", isa_ext16b_15 },
    { "ext8b_2", isa_ext8b_2 },
    { "shl_16b_1", isa_shl_16b_1 },
    { "ushr_16b_1", isa_ushr_16b_1 },
    { "sshr_16b_1", isa_sshr_16b_1 },
    { "sri_16b_1", isa_sri_16b_1 },
    { "shl_16b_3", isa_shl_16b_3 },
    { "ushr_16b_3", isa_ushr_16b_3 },
    { "sshr_16b_3", isa_sshr_16b_3 },
    { "sri_16b_3", isa_sri_16b_3 },
    { "shl_16b_7", isa_shl_16b_7 },
    { "ushr_16b_7", isa_ushr_16b_7 },
    { "sshr_16b_7", isa_sshr_16b_7 },
    { "sri_16b_7", isa_sri_16b_7 },
    { "shl_8h_1", isa_shl_8h_1 },
    { "sshr_8h_1", isa_sshr_8h_1 },
    { "ushr_8h_1", isa_ushr_8h_1 },
    { "shl_8h_5", isa_shl_8h_5 },
    { "sshr_8h_5", isa_sshr_8h_5 },
    { "ushr_8h_5", isa_ushr_8h_5 },
    { "shl_8h_11", isa_shl_8h_11 },
    { "sshr_8h_11", isa_sshr_8h_11 },
    { "ushr_8h_11", isa_ushr_8h_11 },
    { "shl_4s_1", isa_shl_4s_1 },
    { "sshr_4s_1", isa_sshr_4s_1 },
    { "shl_4s_10", isa_shl_4s_10 },
    { "sshr_4s_10", isa_sshr_4s_10 },
    { "shl_2d_3", isa_shl_2d_3 },
    { "sshr_2d_5", isa_sshr_2d_5 },
    { "usra_16b_2", isa_usra_16b_2 },
    { "ssra_16b_2", isa_ssra_16b_2 },
    { "urshr_16b_2", isa_urshr_16b_2 },
    { "srsra_16b_2", isa_srsra_16b_2 },
    { "sqshl_16b_1", isa_sqshl_16b_1 },
    { "ushll_8h_1", isa_ushll_8h_1 },
    { "sshll_8h_1", isa_sshll_8h_1 },
    { "ushll_4s_2", isa_ushll_4s_2 },
    { "sshll_4s_2", isa_sshll_4s_2 },
    { "xtn_8b", isa_xtn_8b },
    { "xtn2_16b", isa_xtn2_16b },
    { "sqxtn_8b", isa_sqxtn_8b },
    { "uqxtn_8b", isa_uqxtn_8b },
    { "sqxtun_8b", isa_sqxtun_8b },
    { "movi_16b_3f", isa_movi_16b_3f },
    { "movi_8b_1", isa_movi_8b_1 },
    { "movi_4s_12", isa_movi_4s_12 },
    { "movi_2s_1", isa_movi_2s_1 },
    { "movi_4h_5", isa_movi_4h_5 },
    { "movi_8h_5", isa_movi_8h_5 },
    { "mvni_4s_1", isa_mvni_4s_1 },
    { "movi_4s_lsl8", isa_movi_4s_lsl8 },
    { "dup_16b_w", isa_dup_16b_w },
    { "dup_8h_w", isa_dup_8h_w },
    { "dup_4s_w", isa_dup_4s_w },
    { "dup_2d_x", isa_dup_2d_x },
    { "dup_elem_b", isa_dup_elem_b },
    { "dup_elem_h", isa_dup_elem_h },
    { "dup_elem_s", isa_dup_elem_s },
    { "ins_elem", isa_ins_elem },
    { "umov_b", isa_umov_b },
    { "umov_h", isa_umov_h },
    { "umov_s", isa_umov_s },
    { "umov_d", isa_umov_d },
    { "smov_b", isa_smov_b },
    { "ins_gen_b", isa_ins_gen_b },
    { "ins_gen_s", isa_ins_gen_s },
    { "mov_d1_x", isa_mov_d1_x },
    { "mov_x_d1", isa_mov_x_d1 },
    { "addv_16b", isa_addv_16b },
    { "addv_8h", isa_addv_8h },
    { "addv_4s", isa_addv_4s },
    { "saddlv_16b", isa_saddlv_16b },
    { "uaddlv_16b", isa_uaddlv_16b },
    { "smaxv_16b", isa_smaxv_16b },
    { "umaxv_16b", isa_umaxv_16b },
    { "sminv_4s", isa_sminv_4s },
    { "cnt_16b", isa_cnt_16b },
    { "rbit_16b", isa_rbit_16b },
    { "rev16_16b", isa_rev16_16b },
    { "rev32_16b", isa_rev32_16b },
    { "rev64_16b", isa_rev64_16b },
    { "abs_16b", isa_abs_16b },
    { "neg_16b", isa_neg_16b },
    { "mvn_16b", isa_mvn_16b },
    { "not_16b", isa_not_16b },
    { "cls_16b", isa_cls_16b },
    { "clz_16b", isa_clz_16b },
    { "saddlp_16b", isa_saddlp_16b },
    { "uaddlp_16b", isa_uaddlp_16b },
    { "xtl_8h", isa_xtl_8h },
    { "fadd_4s", isa_fadd_4s },
    { "fsub_4s", isa_fsub_4s },
    { "fmul_4s", isa_fmul_4s },
    { "fdiv_4s", isa_fdiv_4s },
    { "fadd_2d", isa_fadd_2d },
    { "fmul_2d", isa_fmul_2d },
    { "fmax_4s", isa_fmax_4s },
    { "fmin_4s", isa_fmin_4s },
    { "fmla_4s", isa_fmla_4s },
    { "fabs_4s", isa_fabs_4s },
    { "fneg_4s", isa_fneg_4s },
    { "fsqrt_4s", isa_fsqrt_4s },
    { "fcmeq_4s", isa_fcmeq_4s },
    { "fcvtl_4s", isa_fcvtl_4s },
    { "scvtf_4s", isa_scvtf_4s },
    { "ucvtf_4s", isa_ucvtf_4s },
    { "fcvtzs_4s", isa_fcvtzs_4s },
    { "fadd_s", isa_fadd_s },
    { "fsub_s", isa_fsub_s },
    { "fmul_s", isa_fmul_s },
    { "fdiv_s", isa_fdiv_s },
    { "fadd_d", isa_fadd_d },
    { "fmul_d", isa_fmul_d },
    { "fabs_s", isa_fabs_s },
    { "fneg_s", isa_fneg_s },
    { "fsqrt_s", isa_fsqrt_s },
    { "fmin_s", isa_fmin_s },
    { "fmax_s", isa_fmax_s },
    { "frintm_s", isa_frintm_s },
    { "frintn_s", isa_frintn_s },
    { "fcvtzs_s_w", isa_fcvtzs_s_w },
    { "fcvtzu_s_w", isa_fcvtzu_s_w },
    { "scvtf_w_s", isa_scvtf_w_s },
    { "ucvtf_w_s", isa_ucvtf_w_s },
    { "fcvt_d_s", isa_fcvt_d_s },
    { "fcvt_s_d", isa_fcvt_s_d },
    { "fmov_w_s", isa_fmov_w_s },
    { "fmov_s_w", isa_fmov_s_w },
    { "fmov_x_d", isa_fmov_x_d },
    { "fmov_d_x", isa_fmov_d_x },
    { "fcmp_s", isa_fcmp_s },
    { "fmadd_s", isa_fmadd_s },
    { "fmsub_s", isa_fmsub_s },
    { "ldr_q_off", isa_ldr_q_off },
    { "ldr_q_post", isa_ldr_q_post },
    { "ldr_q_pre", isa_ldr_q_pre },
    { "str_q_post", isa_str_q_post },
    { "str_q_pre", isa_str_q_pre },
    { "ldr_d_off", isa_ldr_d_off },
    { "ldr_s_off", isa_ldr_s_off },
    { "ldr_h_off", isa_ldr_h_off },
    { "ldr_b_off", isa_ldr_b_off },
    { "ldr_q_reg", isa_ldr_q_reg },
    { "str_q_reg", isa_str_q_reg },
    { "ldp_q", isa_ldp_q },
    { "stp_q", isa_stp_q },
    { "stp_q_post", isa_stp_q_post },
    { "ldp_d", isa_ldp_d },
    { "stp_s", isa_stp_s },
    { "ld1_16b", isa_ld1_16b },
    { "st1_16b", isa_st1_16b },
    { "ld1_multi", isa_ld1_multi },
    { "ld2_16b", isa_ld2_16b },
    { "st2_16b", isa_st2_16b },
    { "ld3_16b", isa_ld3_16b },
    { "ld4_16b", isa_ld4_16b },
    { "ld1r_16b", isa_ld1r_16b },
    { "ld2r_8h", isa_ld2r_8h },
    { "ld1_single", isa_ld1_single },
    { "st1_single", isa_st1_single },
    { "ldr_x_off", isa_ldr_x_off },
    { "ldr_w_off", isa_ldr_w_off },
    { "ldr_x_post", isa_ldr_x_post },
    { "ldr_x_pre", isa_ldr_x_pre },
    { "ldur_x", isa_ldur_x },
    { "ldrsb_w", isa_ldrsb_w },
    { "ldrsb_x", isa_ldrsb_x },
    { "ldrsh_w", isa_ldrsh_w },
    { "ldrsh_x", isa_ldrsh_x },
    { "ldrsw_x", isa_ldrsw_x },
    { "ldrb_w", isa_ldrb_w },
    { "ldrh_w", isa_ldrh_w },
    { "ldr_reg_off", isa_ldr_reg_off },
    { "ldr_reg_off_ext", isa_ldr_reg_off_ext },
    { "ldr_reg_lsl", isa_ldr_reg_lsl },
    { "stp_post", isa_stp_post },
    { "strb_post", isa_strb_post },
    { "sbfx", isa_sbfx },
    { "ubfx", isa_ubfx },
    { "bfi", isa_bfi },
    { "bfxil", isa_bfxil },
    { "ubfiz", isa_ubfiz },
    { "sbfiz", isa_sbfiz },
    { "extr", isa_extr },
    { "udiv", isa_udiv },
    { "sdiv", isa_sdiv },
    { "madd", isa_madd },
    { "msub", isa_msub },
    { "smull", isa_smull },
    { "umull", isa_umull },
    { "smulh", isa_smulh },
    { "umulh", isa_umulh },
    { "smaddl", isa_smaddl },
    { "umaddl", isa_umaddl },
    { "smulh_neg", isa_smulh_neg },
    { "clz", isa_clz },
    { "rbit", isa_rbit },
    { "rev16", isa_rev16 },
    { "rev32", isa_rev32 },
    { "rev", isa_rev },
    { "csel", isa_csel },
    { "csinc", isa_csinc },
    { "csneg", isa_csneg },
    { "csinv", isa_csinv },
    { "cset", isa_cset },
    { "csetm", isa_csetm },
    { "ccmp", isa_ccmp },
    { "ccmn", isa_ccmn },
    { "adc", isa_adc },
    { "sbc", isa_sbc },
    { "ngc", isa_ngc },
    { "tst_br", isa_tst_br },
    { "cmn", isa_cmn },
    { "tbz", isa_tbz },
    { "tbnz", isa_tbnz },
    { "madd32", isa_madd32 },
    { "neg_lsl", isa_neg_lsl },
    { "lsl_reg", isa_lsl_reg },
    { "asr_reg", isa_asr_reg },
    { "lsr_reg", isa_lsr_reg },
    { "ror_reg", isa_ror_reg },
    { "movk", isa_movk },
    { "movn", isa_movn },
    { "adr_adrp", isa_adr_adrp },
    { "ands_flags", isa_ands_flags },
    { "sxtb_sxth", isa_sxtb_sxth },
    { "ubfiz_32", isa_ubfiz_32 },
    { "ldxrb_stxrb", isa_ldxrb_stxrb },
    { "swp_s", isa_swp_s },
    { "ldar_stlr", isa_ldar_stlr },
    { "dmb_barriers", isa_dmb_barriers },
};

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
        put("\n");
        ssize_t w = write(1, out, (size_t)olen);   /* 每条立刻输出：崩在哪条一眼可见 */
        (void)w;
    }
    return 0;
}
