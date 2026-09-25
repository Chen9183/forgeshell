/* codegen.c —— AST → ARM64 机器码
 *
 * 约定（DESIGN.md §3）：
 *   · 入口由模板的 rc_user_main 指针进入，参数 (argc, argv) 在 x0/x1；
 *   · 先调 rc_rt_start(argc, argv, &rc_image)：非 0 = 防护子系统否决 → 直接退出（70）；
 *   · shell 状态码由运行时统一记录（rt_g.last_status）：判定用 rc_last_status_get，
 *     取反用 rc_not_status；break/continue/return 用标志 + 语句边界检查实现（无需栈展开）；
 *   · 控制流（if/while/until/for/case/&&/||/!/子shell/命令组/函数）编译成真实 ARM64 分支；
 *   · 代码布局：[序言] → [跳过函数体] → [函数体...] → [主流程] → [退出]
 *     函数体先发射，主流程里登记函数时其标签偏移已确定，因而无需二次回填。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "s2a.h"
#include "image.h"

typedef struct {
    const s2a_options *o;
    emit_buf *code;
    uint64_t ro_va, rw_va, code_va, image_va;
    /* 函数：预扫描收集，主流程里登记 */
    struct { const char *name; uint32_t label; uint32_t off; const rc_node *body; } funcs[512];
    int nfuncs;
    /* 循环栈（break/continue 目标） */
    uint32_t lbrk[32], lcont[32];
    int loop_depth;
    int in_func;
    uint32_t lexit;      /* 当前函数体的返回标签 */
    int error;
} cg;

/* ---------- 基础 ---------- */
static uint64_t node_va(const rc_node *n)
{
    uint64_t v = s2a_obj_va(n);
    return v;
}
static uint64_t obj_va(const void *p) { return s2a_obj_va(p); }
static uint64_t str_va(const char *s) { return s2a_str_va(s); }

static void call(cg *C, const char *sym)
{
    uint64_t a = s2a_rt_sym(sym);
    if (!a) {
        fprintf(stderr, "s2a: 运行时符号缺失: %s（模板与运行时不一致）\n", sym);
        C->error = 1;
        return;
    }
    emit_b_abs(C->code, a);
}

/* 载入 64 位常量：固定 4 条指令，保证长度与地址无关（两遍发射稳定） */
static void load64(cg *C, int rd, uint64_t v)
{
    emit_movz(C->code, rd, (uint32_t)(v & 0xFFFF), 0);
    emit_movk(C->code, rd, (uint32_t)((v >> 16) & 0xFFFF), 16);
    emit_movk(C->code, rd, (uint32_t)((v >> 32) & 0xFFFF), 32);
    emit_movk(C->code, rd, (uint32_t)((v >> 48) & 0xFFFF), 48);
}

/* 读指针字段：rc_node.u 里存的是指针，load64 只给地址，必须再解引用一次。
   （早期漏了这一步，于是 [[ ]] 和 (( )) 在产物里把「指针的地址」当节点用，
     永远求值成假。） */
static void loadptr(cg *C, int rd, uint64_t addr)
{
    load64(C, rd, addr);
    emit_ldr_imm(C->code, rd, rd, 0);
}

static void gen_list(cg *C, const rc_node *n);
static void gen_stmt(cg *C, const rc_node *n);

/* 预扫描函数定义（顺序保持出现次序） */
static void scan_funcs(cg *C, const rc_node *n)
{
    for (const rc_node *p = n; p; p = p->next) {
        if (p->kind == RN_FUNC) {
            int dup = 0;
            for (int i = 0; i < C->nfuncs; i++)
                if (strcmp(C->funcs[i].name, p->u.func.name) == 0) dup = 1;
            if (!dup && C->nfuncs < 512) {
                C->funcs[C->nfuncs].name = p->u.func.name;
                C->funcs[C->nfuncs].body = p->u.func.body;
                C->funcs[C->nfuncs].label = 0;
                C->nfuncs++;
            }
        }
        if (p->kind == RN_GROUP || p->kind == RN_SUBSHELL) scan_funcs(C, p->a);
        if (p->kind == RN_FOR) scan_funcs(C, p->u.forc.body);
    }
}

/* ---------- 语句 ---------- */
static void gen_stmt(cg *C, const rc_node *n)
{
    /* 复合命令自带的重定向（`done < f` 等）：只 GROUP 在下面自己处理，其余在这里包一层 */
    int wrap = (n->redirs && (n->kind == RN_WHILE || n->kind == RN_FOR || n->kind == RN_IF ||
                              n->kind == RN_CASE || n->kind == RN_SUBSHELL));
    if (wrap) {
        load64(C, X0, node_va(n));
        call(C, "rc_redir_push");
    }
    if (!n || C->error) return;
    switch (n->kind) {
    case RN_SIMPLE:
        load64(C, X0, node_va(n) + (uint64_t)offsetof(rc_node, u));
        call(C, "rc_exec_simple");
        break;
    case RN_PIPE:
        load64(C, X0, node_va(n));
        call(C, "rc_exec_pipeline");
        break;
    case RN_ANDOR: {
        uint32_t skip = emit_new_label(C->code);
        gen_stmt(C, n->a);
        call(C, "rc_last_status_get");
        if (n->sub == RC_AND) emit_cbnz_w(C->code, X0, skip);
        else                  emit_cbz_w(C->code, X0, skip);
        gen_stmt(C, n->b);
        emit_bind(C->code, skip);
        break;
    }
    case RN_NOT:
        gen_stmt(C, n->a);
        call(C, "rc_not_status");
        break;
    case RN_BG:
        load64(C, X0, node_va(n->a));
        call(C, "rc_bg_launch");
        break;
    case RN_LIST:
        gen_stmt(C, n->a);
        gen_stmt(C, n->b);
        break;
    case RN_IF: {
        uint32_t lelse = emit_new_label(C->code), lend = emit_new_label(C->code);
        call(C, "rc_cond_enter");
        gen_list(C, n->a);
        call(C, "rc_cond_leave");
        call(C, "rc_last_status_get");
        emit_cbnz_w(C->code, X0, lelse);
        gen_list(C, n->b);
        emit_b(C->code, lend);
        emit_bind(C->code, lelse);
        if (n->c) gen_list(C, n->c);
        else call(C, "rc_status_zero");      /* 无 else 且条件为假 → 状态 0（POSIX） */
        emit_bind(C->code, lend);
        break;
    }
    case RN_WHILE: {
        uint32_t ltop = emit_new_label(C->code), lend = emit_new_label(C->code);
        emit_bind(C->code, ltop);
        call(C, "rc_cond_enter");
        gen_list(C, n->a);
        call(C, "rc_cond_leave");
        call(C, "rc_last_status_get");
        if (n->sub == RC_WHILE) emit_cbnz_w(C->code, X0, lend);
        else                    emit_cbz_w(C->code, X0, lend);
        C->lbrk[C->loop_depth] = lend;
        C->lcont[C->loop_depth] = ltop;
        C->loop_depth++;
        gen_list(C, n->b);
        C->loop_depth--;
        call(C, "rc_mark_loop_ran");
        emit_b(C->code, ltop);
        emit_bind(C->code, lend);
        call(C, "rc_status_zero_if_loop_never_ran");
        break;
    }
    case RN_FOR: {
        uint32_t ltop = emit_new_label(C->code), lend = emit_new_label(C->code), lcont = emit_new_label(C->code);
        load64(C, X0, node_va(n) + (uint64_t)offsetof(rc_node, u));
        call(C, "rc_for_begin");
        emit_bind(C->code, ltop);
        call(C, "rc_for_next");
        emit_cbz_w(C->code, X0, lend);
        C->lbrk[C->loop_depth] = lend;
        C->lcont[C->loop_depth] = lcont;
        C->loop_depth++;
        gen_list(C, n->u.forc.body);
        C->loop_depth--;
        call(C, "rc_mark_loop_ran");
        emit_bind(C->code, lcont);
        emit_b(C->code, ltop);
        emit_bind(C->code, lend);
        call(C, "rc_status_zero_if_loop_never_ran");
        call(C, "rc_for_end");
        break;
    }
    case RN_CASE: {
        int nitems = 0;
        for (rc_case_item *it = n->u.casec.items; it; it = it->next) nitems++;
        uint32_t *btest = calloc((size_t)nitems + 1, sizeof(uint32_t));
        uint32_t *bbody = calloc((size_t)nitems + 1, sizeof(uint32_t));
        uint32_t lend = emit_new_label(C->code);
        for (int i = 0; i < nitems; i++) { btest[i] = emit_new_label(C->code); bbody[i] = emit_new_label(C->code); }
        /* 逐项测试 */
        for (int i = 0; i < nitems; i++) {
            emit_bind(C->code, btest[i]);
            load64(C, X0, obj_va(n->u.casec.word));
            rc_case_item *it = n->u.casec.items;
            for (int k = 0; k < i; k++) it = it->next;
            load64(C, X1, obj_va(it));
            call(C, "rc_case_item_match");
            emit_cbnz_w(C->code, X0, bbody[i]);
        }
        emit_b(C->code, lend);
        /* 各项体 */
        for (int i = 0; i < nitems; i++) {
            emit_bind(C->code, bbody[i]);
            rc_case_item *it = n->u.casec.items;
            for (int k = 0; k < i; k++) it = it->next;
            gen_list(C, it->body);
            if (it->retest && i + 1 < nitems)      emit_b(C->code, btest[i + 1]);   /* ;;& 继续测试 */
            else if (it->fallthrough && i + 1 < nitems) emit_b(C->code, bbody[i + 1]); /* ;& 落到下一体 */
            else                                   emit_b(C->code, lend);
        }
        emit_bind(C->code, lend);
        free(btest); free(bbody);
        break;
    }
    case RN_SUBSHELL: {
        uint32_t lskip = emit_new_label(C->code);
        call(C, "rc_subshell_enter");
        emit_cbnz_w(C->code, X0, lskip);
        gen_list(C, n->a);
        call(C, "rc_last_status_get");
        call(C, "rc_subshell_leave");
        emit_bind(C->code, lskip);
        break;
    }
    case RN_GROUP:
        if (n->redirs) {
            load64(C, X0, node_va(n));
            call(C, "rc_redir_push");
        }
        gen_list(C, n->a);
        if (n->redirs) call(C, "rc_redir_pop");
        break;
    case RN_FUNC: {
        /* 主流程里登记：rc_func_register(name, code_va + 函数体偏移, NULL) */
        int idx = -1;
        for (int i = 0; i < C->nfuncs; i++)
            if (strcmp(C->funcs[i].name, n->u.func.name) == 0) idx = i;
        load64(C, X0, str_va(n->u.func.name));
        load64(C, X1, idx >= 0 ? (C->code_va + C->funcs[idx].off) : 0);
        emit_mov_imm64(C->code, X2, 0);
        call(C, "rc_func_register");
        break;
    }
    case RN_TEST_CMD:
        loadptr(C, X0, node_va(n) + (uint64_t)offsetof(rc_node, u));
        call(C, "rc_test_run");
        break;
    case RN_ARITH_CMD:
        loadptr(C, X0, node_va(n) + (uint64_t)offsetof(rc_node, u));
        call(C, "rc_arith_cmd");
        break;
    default:
        load64(C, X0, node_va(n));
        call(C, "rc_exec");
        break;
    }
    if (wrap) call(C, "rc_redir_pop");
}

static void gen_list(cg *C, const rc_node *n)
{
    for (const rc_node *p = n; p && !C->error; p = p->next) {
        gen_stmt(C, p);
        if (C->loop_depth > 0) {
            uint32_t lnext = emit_new_label(C->code), lcont = emit_new_label(C->code);
            call(C, "rc_loop_check");            /* 0 继续 / 1 break / 2 continue */
            emit_cbz_w(C->code, X0, lnext);
            emit_cmp_imm(C->code, X0, 1);
            emit_b_cond(C->code, COND_EQ, C->lbrk[C->loop_depth - 1]);
            emit_b(C->code, C->lcont[C->loop_depth - 1]);   /* continue */
            emit_bind(C->code, lnext);
            (void)lcont;
        }
        if (C->in_func) {
            call(C, "rc_check_return");
            emit_cbnz_w(C->code, X0, C->lexit ? C->lexit : 0);
        }
    }
}

/* ---------- 入口 ---------- */
int s2a_codegen(const s2a_options *o, rc_node *root, emit_buf *code,
                uint64_t ro_va, uint64_t rw_va, uint64_t code_va, uint64_t image_va)
{
    cg C;
    memset(&C, 0, sizeof C);
    C.o = o; C.code = code;
    C.ro_va = ro_va; C.rw_va = rw_va; C.code_va = code_va; C.image_va = image_va;
    s2a_code_va = code_va;

    scan_funcs(&C, root);

    /* 序言 */
    emit_stp_pre(code, X29, X30, SP, -48);
    emit_str_imm(code, X19, SP, 16);
    emit_str_imm(code, X20, SP, 24);
    emit_mov_reg(code, X19, X0);
    emit_mov_reg(code, X20, X1);
    emit_mov_reg(code, X0, X19);
    emit_mov_reg(code, X1, X20);
    load64(&C, X2, image_va);
    call(&C, "rc_rt_start");
    uint32_t lfail = emit_new_label(code);
    emit_cbnz_w(code, X0, lfail);

    uint32_t lmain = emit_new_label(code), lfuncs = emit_new_label(code);
    emit_b(code, lfuncs);

    /* 函数体（先发射，主流程登记时偏移已知） */
    for (int i = 0; i < C.nfuncs; i++) {
        C.funcs[i].label = emit_new_label(code);
        C.funcs[i].off = emit_pos(code);
        emit_bind(code, C.funcs[i].label);
        C.funcs[i].off = emit_pos(code);            /* 绑定后即为真实偏移 */
        emit_stp_pre(code, X29, X30, SP, -48);
        emit_str_imm(code, X19, SP, 16);
        emit_str_imm(code, X20, SP, 24);
        emit_mov_reg(code, X19, X0);
        emit_mov_reg(code, X20, X1);
        load64(&C, X0, str_va(C.funcs[i].name));
        emit_mov_reg(code, X1, X19);
        emit_mov_reg(code, X2, X20);
        call(&C, "rc_func_enter");
        uint32_t lret = emit_new_label(code);
        C.lexit = lret;
        C.in_func = 1;
        for (const rc_node *p = C.funcs[i].body; p; p = p->next) {
            gen_stmt(&C, p);
            call(&C, "rc_check_return");
            emit_cbnz_w(code, X0, lret);
        }
        C.in_func = 0;
        C.lexit = 0;
        emit_bind(code, lret);
        call(&C, "rc_take_return_status");
        call(&C, "rc_func_leave");
        emit_ldr_imm(code, X19, SP, 16);
        emit_ldr_imm(code, X20, SP, 24);
        emit_ldp_post(code, X29, X30, SP, 48);
        emit_ret(code);
    }

    emit_bind(code, lfuncs);
    emit_bind(code, lmain);
    gen_list(&C, root);

    call(&C, "rc_last_status_get");
    call(&C, "rc_rt_exit");

    emit_bind(code, lfail);
    emit_mov_imm32(code, X0, 70);
    emit_ldr_imm(code, X19, SP, 16);
    emit_ldr_imm(code, X20, SP, 24);
    emit_ldp_post(code, X29, X30, SP, 48);
    emit_ret(code);

    emit_fixup_all(code);
    if (code->error || C.error) return -1;

    return 0;
}
