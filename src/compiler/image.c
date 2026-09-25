/* image.c —— 把 AST 与字符串池组装成产物的只读段/可写段
 *
 * 只读段 ro：AST 结构（rc_node/rc_word/rc_wseg/rc_arith/rc_test/rc_assign/rc_redir/rc_case_item），
 *            指针字段写成**绝对虚拟地址**，运行时按指针直接求值；
 * 字符串池：位于可写段 rw 的最前面（偏移 0 起），AST 里的 const char* 指向池内（rw_va + 偏移），
 *            静态期整体 AES-256-CTR 加密，启动时由 rc_protect_image_unlock 解密。
 *
 * 序列化分两步：先按“符号偏移”建立对象表与字符串池，布局确定后统一回填绝对地址。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "s2a.h"
#include "image.h"

enum { OBJ_NODE, OBJ_WORD, OBJ_SEG, OBJ_ARITH, OBJ_TEST, OBJ_ASSIGN, OBJ_REDIR, OBJ_CASE };

typedef struct { int kind; const void *p; uint32_t off; } objrec;
typedef struct { uint32_t off; } strrec;
typedef struct { uint32_t at; int is_str; uint32_t val; } fixrec;

typedef struct {
    unsigned char *ro;
    uint32_t ro_len, ro_cap;
    objrec *objs; uint32_t nobjs, capobjs;
    char *pool; uint32_t pool_len, pool_cap;
    strrec *strs; uint32_t nstrs, capstrs;
    fixrec *fix; uint32_t nfix, capfix;
} builder;

static uint32_t ro_alloc(builder *B, uint32_t size)
{
    uint32_t off = (B->ro_len + 7u) & ~7u;
    if (off + size > B->ro_cap) {
        while (off + size > B->ro_cap) B->ro_cap = B->ro_cap ? B->ro_cap * 2 : 8192;
        B->ro = realloc(B->ro, B->ro_cap);
    }
    memset(B->ro + off, 0, size);
    B->ro_len = off + size;
    return off;
}

static uint32_t obj_off(builder *B, int kind, const void *p, uint32_t size)
{
    for (uint32_t i = 0; i < B->nobjs; i++)
        if (B->objs[i].p == p) return B->objs[i].off;
    if (B->nobjs == B->capobjs) {
        B->capobjs = B->capobjs ? B->capobjs * 2 : 256;
        B->objs = realloc(B->objs, sizeof(objrec) * B->capobjs);
    }
    uint32_t off = ro_alloc(B, size);
    B->objs[B->nobjs].kind = kind;
    B->objs[B->nobjs].p = p;
    B->objs[B->nobjs].off = off;
    B->nobjs++;
    return off;
}

static void fix_ptr(builder *B, uint32_t at, int is_str, uint32_t val)
{
    if (B->nfix == B->capfix) {
        B->capfix = B->capfix ? B->capfix * 2 : 256;
        B->fix = realloc(B->fix, sizeof(fixrec) * B->capfix);
    }
    B->fix[B->nfix].at = at;
    B->fix[B->nfix].is_str = is_str;
    B->fix[B->nfix].val = val;
    B->nfix++;
}

/* 字符串入池；返回“偏移 + 1”（0 表示 NULL）。比较时用池内内容，避免 realloc 后指针失效。 */
static uint32_t pool_put(builder *B, const char *s)
{
    if (!s) return 0;
    for (uint32_t i = 0; i < B->nstrs; i++)
        if (strcmp(B->pool + B->strs[i].off, s) == 0) return B->strs[i].off + 1;
    size_t l = strlen(s) + 1;
    if (B->pool_len + l > B->pool_cap) {
        while (B->pool_len + l > B->pool_cap) B->pool_cap = B->pool_cap ? B->pool_cap * 2 : 4096;
        B->pool = realloc(B->pool, B->pool_cap);
    }
    if (B->nstrs == B->capstrs) {
        B->capstrs = B->capstrs ? B->capstrs * 2 : 256;
        B->strs = realloc(B->strs, sizeof(strrec) * B->capstrs);
    }
    uint32_t off = B->pool_len;
    memcpy(B->pool + off, s, l);
    B->pool_len += (uint32_t)l;
    B->strs[B->nstrs++].off = off;
    return off + 1;
}

static void str_field(builder *B, uint32_t at, const char *s)
{
    if (s) fix_ptr(B, at, 1, pool_put(B, s));
}

static void *ro_at(builder *B, uint32_t off) { return B->ro + off; }

/* ------------------------------------------------------------------ 序列化 */
static void ser_word(builder *B, const rc_word *w);
static void ser_node(builder *B, const rc_node *n);
static void ser_arith(builder *B, const rc_arith *a);

static void ser_seg(builder *B, const rc_wseg *s)
{
    uint32_t off = obj_off(B, OBJ_SEG, s, (uint32_t)sizeof(rc_wseg));
    rc_wseg *d = ro_at(B, off);
    d->kind = s->kind; d->quoted = s->quoted; d->dquote = s->dquote;
    d->op = s->op; d->namelen = s->namelen; d->idx = s->idx;
    str_field(B, off + (uint32_t)offsetof(rc_wseg, name), s->name);
    if (s->word)  { fix_ptr(B, off + (uint32_t)offsetof(rc_wseg, word), 0, obj_off(B, OBJ_WORD, s->word, sizeof(rc_word))); ser_word(B, s->word); }
    if (s->pat)   { fix_ptr(B, off + (uint32_t)offsetof(rc_wseg, pat), 0, obj_off(B, OBJ_WORD, s->pat, sizeof(rc_word))); ser_word(B, s->pat); }
    if (s->rep)   { fix_ptr(B, off + (uint32_t)offsetof(rc_wseg, rep), 0, obj_off(B, OBJ_WORD, s->rep, sizeof(rc_word))); ser_word(B, s->rep); }
    if (s->sub)   { fix_ptr(B, off + (uint32_t)offsetof(rc_wseg, sub), 0, obj_off(B, OBJ_NODE, s->sub, sizeof(rc_node))); ser_node(B, s->sub); }
    if (s->arith) { fix_ptr(B, off + (uint32_t)offsetof(rc_wseg, arith), 0, obj_off(B, OBJ_ARITH, s->arith, sizeof(rc_arith))); ser_arith(B, s->arith); }
    if (s->next)  { fix_ptr(B, off + (uint32_t)offsetof(rc_wseg, next), 0, obj_off(B, OBJ_SEG, s->next, sizeof(rc_wseg))); ser_seg(B, s->next); }
}

static void ser_word(builder *B, const rc_word *w)
{
    uint32_t off = obj_off(B, OBJ_WORD, w, (uint32_t)sizeof(rc_word));
    rc_word *d = ro_at(B, off);
    d->flags = w->flags;
    if (w->segs) { fix_ptr(B, off + (uint32_t)offsetof(rc_word, segs), 0, obj_off(B, OBJ_SEG, w->segs, sizeof(rc_wseg))); ser_seg(B, w->segs); }
}

static void ser_arith(builder *B, const rc_arith *a)
{
    uint32_t off = obj_off(B, OBJ_ARITH, a, (uint32_t)sizeof(rc_arith));
    rc_arith *d = ro_at(B, off);
    d->kind = a->kind; d->op = a->op; d->post = a->post; d->num = a->num;
    str_field(B, off + (uint32_t)offsetof(rc_arith, name), a->name);
    if (a->a) { fix_ptr(B, off + (uint32_t)offsetof(rc_arith, a), 0, obj_off(B, OBJ_ARITH, a->a, sizeof(rc_arith))); ser_arith(B, a->a); }
    if (a->b) { fix_ptr(B, off + (uint32_t)offsetof(rc_arith, b), 0, obj_off(B, OBJ_ARITH, a->b, sizeof(rc_arith))); ser_arith(B, a->b); }
    if (a->c) { fix_ptr(B, off + (uint32_t)offsetof(rc_arith, c), 0, obj_off(B, OBJ_ARITH, a->c, sizeof(rc_arith))); ser_arith(B, a->c); }
    if (a->sub) { fix_ptr(B, off + (uint32_t)offsetof(rc_arith, sub), 0, obj_off(B, OBJ_NODE, a->sub, sizeof(rc_node))); ser_node(B, a->sub); }
}

static void ser_test(builder *B, const rc_test *t)
{
    uint32_t off = obj_off(B, OBJ_TEST, t, (uint32_t)sizeof(rc_test));
    rc_test *d = ro_at(B, off);
    d->kind = t->kind; d->op = t->op; d->negate = t->negate;
    if (t->lhs)   { fix_ptr(B, off + (uint32_t)offsetof(rc_test, lhs), 0, obj_off(B, OBJ_WORD, t->lhs, sizeof(rc_word))); ser_word(B, t->lhs); }
    if (t->rhs)   { fix_ptr(B, off + (uint32_t)offsetof(rc_test, rhs), 0, obj_off(B, OBJ_WORD, t->rhs, sizeof(rc_word))); ser_word(B, t->rhs); }
    if (t->arith) { fix_ptr(B, off + (uint32_t)offsetof(rc_test, arith), 0, obj_off(B, OBJ_ARITH, t->arith, sizeof(rc_arith))); ser_arith(B, t->arith); }
    if (t->a)     { fix_ptr(B, off + (uint32_t)offsetof(rc_test, a), 0, obj_off(B, OBJ_TEST, t->a, sizeof(rc_test))); ser_test(B, t->a); }
    if (t->b)     { fix_ptr(B, off + (uint32_t)offsetof(rc_test, b), 0, obj_off(B, OBJ_TEST, t->b, sizeof(rc_test))); ser_test(B, t->b); }
}

static void ser_assign(builder *B, const rc_assign *a)
{
    uint32_t off = obj_off(B, OBJ_ASSIGN, a, (uint32_t)sizeof(rc_assign));
    rc_assign *d = ro_at(B, off);
    d->append = a->append; d->is_arith = a->is_arith; d->is_array = a->is_array; d->narray = a->narray;
    str_field(B, off + (uint32_t)offsetof(rc_assign, name), a->name);
    if (a->value) { fix_ptr(B, off + (uint32_t)offsetof(rc_assign, value), 0, obj_off(B, OBJ_WORD, a->value, sizeof(rc_word))); ser_word(B, a->value); }
    if (a->arith) { fix_ptr(B, off + (uint32_t)offsetof(rc_assign, arith), 0, obj_off(B, OBJ_ARITH, a->arith, sizeof(rc_arith))); ser_arith(B, a->arith); }
    if (a->array_items && a->narray) {
        uint32_t arr = ro_alloc(B, 8u * a->narray);
        for (uint32_t i = 0; i < a->narray; i++) {
            fix_ptr(B, arr + i * 8, 0, obj_off(B, OBJ_WORD, a->array_items[i], sizeof(rc_word)));
            ser_word(B, a->array_items[i]);
        }
        fix_ptr(B, off + (uint32_t)offsetof(rc_assign, array_items), 0, arr);
    }
    if (a->next) { fix_ptr(B, off + (uint32_t)offsetof(rc_assign, next), 0, obj_off(B, OBJ_ASSIGN, a->next, sizeof(rc_assign))); ser_assign(B, a->next); }
}

static void ser_redir(builder *B, const rc_redir *r)
{
    uint32_t off = obj_off(B, OBJ_REDIR, r, (uint32_t)sizeof(rc_redir));
    rc_redir *d = ro_at(B, off);
    d->op = r->op; d->fd = r->fd; d->fd2 = r->fd2; d->hd_expand = r->hd_expand; d->hd_len = r->hd_len;
    str_field(B, off + (uint32_t)offsetof(rc_redir, hd), r->hd);
    if (r->target) { fix_ptr(B, off + (uint32_t)offsetof(rc_redir, target), 0, obj_off(B, OBJ_WORD, r->target, sizeof(rc_word))); ser_word(B, r->target); }
    if (r->next)   { fix_ptr(B, off + (uint32_t)offsetof(rc_redir, next), 0, obj_off(B, OBJ_REDIR, r->next, sizeof(rc_redir))); ser_redir(B, r->next); }
}

static void ser_case(builder *B, const rc_case_item *it)
{
    uint32_t off = obj_off(B, OBJ_CASE, it, (uint32_t)sizeof(rc_case_item));
    rc_case_item *d = ro_at(B, off);
    d->npats = it->npats; d->fallthrough = it->fallthrough; d->retest = it->retest;
    if (it->pats && it->npats) {
        uint32_t arr = ro_alloc(B, 8u * it->npats);
        for (uint32_t i = 0; i < it->npats; i++) {
            fix_ptr(B, arr + i * 8, 0, obj_off(B, OBJ_WORD, it->pats[i], sizeof(rc_word)));
            ser_word(B, it->pats[i]);
        }
        fix_ptr(B, off + (uint32_t)offsetof(rc_case_item, pats), 0, arr);
    }
    if (it->body) { fix_ptr(B, off + (uint32_t)offsetof(rc_case_item, body), 0, obj_off(B, OBJ_NODE, it->body, sizeof(rc_node))); ser_node(B, it->body); }
    if (it->next) { fix_ptr(B, off + (uint32_t)offsetof(rc_case_item, next), 0, obj_off(B, OBJ_CASE, it->next, sizeof(rc_case_item))); ser_case(B, it->next); }
}

static void ser_node(builder *B, const rc_node *n)
{
    if (!n) return;
    uint32_t off = obj_off(B, OBJ_NODE, n, (uint32_t)sizeof(rc_node));
    rc_node *d = ro_at(B, off);
    d->kind = n->kind; d->flags = n->flags; d->sub = n->sub; d->line = n->line;
    if (n->a)      { fix_ptr(B, off + (uint32_t)offsetof(rc_node, a), 0, obj_off(B, OBJ_NODE, n->a, sizeof(rc_node))); ser_node(B, n->a); }
    if (n->b)      { fix_ptr(B, off + (uint32_t)offsetof(rc_node, b), 0, obj_off(B, OBJ_NODE, n->b, sizeof(rc_node))); ser_node(B, n->b); }
    if (n->c)      { fix_ptr(B, off + (uint32_t)offsetof(rc_node, c), 0, obj_off(B, OBJ_NODE, n->c, sizeof(rc_node))); ser_node(B, n->c); }
    if (n->next)   { fix_ptr(B, off + (uint32_t)offsetof(rc_node, next), 0, obj_off(B, OBJ_NODE, n->next, sizeof(rc_node))); ser_node(B, n->next); }
    if (n->redirs) { fix_ptr(B, off + (uint32_t)offsetof(rc_node, redirs), 0, obj_off(B, OBJ_REDIR, n->redirs, sizeof(rc_redir))); ser_redir(B, n->redirs); }

    uint32_t u = off + (uint32_t)offsetof(rc_node, u);
    switch (n->kind) {
    case RN_SIMPLE: {
        rc_simple *ds = ro_at(B, u);
        ds->argc = n->u.simple.argc;
        if (n->u.simple.argv && n->u.simple.argc) {
            uint32_t arr = ro_alloc(B, 8u * n->u.simple.argc);
            for (uint32_t i = 0; i < n->u.simple.argc; i++) {
                fix_ptr(B, arr + i * 8, 0, obj_off(B, OBJ_WORD, n->u.simple.argv[i], sizeof(rc_word)));
                ser_word(B, n->u.simple.argv[i]);
            }
            fix_ptr(B, u + (uint32_t)offsetof(rc_simple, argv), 0, arr);
        }
        if (n->u.simple.assigns) {
            fix_ptr(B, u + (uint32_t)offsetof(rc_simple, assigns), 0, obj_off(B, OBJ_ASSIGN, n->u.simple.assigns, sizeof(rc_assign)));
            ser_assign(B, n->u.simple.assigns);
        }
        /* rc_simple.redirs 也要落进镜像（运行期读它；OBJ_REDIR 已在节点级建好） */
        if (n->redirs)
            fix_ptr(B, u + (uint32_t)offsetof(rc_simple, redirs), 0, obj_off(B, OBJ_REDIR, n->redirs, sizeof(rc_redir)));
        break;
    }
    case RN_FOR: {
        rc_for *df = ro_at(B, u);
        df->nitems = n->u.forc.nitems;
        df->has_in = n->u.forc.has_in;
        str_field(B, u + (uint32_t)offsetof(rc_for, name), n->u.forc.name);
        if (n->u.forc.items && n->u.forc.nitems) {
            uint32_t arr = ro_alloc(B, 8u * n->u.forc.nitems);
            for (uint32_t i = 0; i < n->u.forc.nitems; i++) {
                fix_ptr(B, arr + i * 8, 0, obj_off(B, OBJ_WORD, n->u.forc.items[i], sizeof(rc_word)));
                ser_word(B, n->u.forc.items[i]);
            }
            fix_ptr(B, u + (uint32_t)offsetof(rc_for, items), 0, arr);
        }
        if (n->u.forc.body) { fix_ptr(B, u + (uint32_t)offsetof(rc_for, body), 0, obj_off(B, OBJ_NODE, n->u.forc.body, sizeof(rc_node))); ser_node(B, n->u.forc.body); }
        break;
    }
    case RN_CASE: {
        if (n->u.casec.word) {
            fix_ptr(B, u + (uint32_t)offsetof(rc_case_def, word), 0, obj_off(B, OBJ_WORD, n->u.casec.word, sizeof(rc_word)));
            ser_word(B, n->u.casec.word);
        }
        if (n->u.casec.items) {
            fix_ptr(B, u + (uint32_t)offsetof(rc_case_def, items), 0, obj_off(B, OBJ_CASE, n->u.casec.items, sizeof(rc_case_item)));
            ser_case(B, n->u.casec.items);
        }
        break;
    }
    case RN_FUNC: {
        str_field(B, u + (uint32_t)offsetof(rc_func_def, name), n->u.func.name);
        if (n->u.func.body) { fix_ptr(B, u + (uint32_t)offsetof(rc_func_def, body), 0, obj_off(B, OBJ_NODE, n->u.func.body, sizeof(rc_node))); ser_node(B, n->u.func.body); }
        break;
    }
    case RN_ARITH_CMD:
        if (n->u.arith) { fix_ptr(B, u, 0, obj_off(B, OBJ_ARITH, n->u.arith, sizeof(rc_arith))); ser_arith(B, n->u.arith); }
        break;
    case RN_TEST_CMD:
        if (n->u.test) { fix_ptr(B, u, 0, obj_off(B, OBJ_TEST, n->u.test, sizeof(rc_test))); ser_test(B, n->u.test); }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ 入口 */
/* 供 codegen 查询：AST 对象 / 字符串在产物里的绝对地址（布局确定后可用） */
static objrec *g_objs; static uint32_t g_nobjs; static uint64_t g_ro_va;
static strrec *g_strs; static uint32_t g_nstrs; static uint64_t g_rw_va;
static char *g_pool_base;

uint64_t s2a_obj_va(const void *p)
{
    for (uint32_t i = 0; i < g_nobjs; i++)
        if (g_objs[i].p == p) return g_ro_va + g_objs[i].off;
    return 0;
}

uint64_t s2a_str_va(const char *s)
{
    if (!s) return 0;
    for (uint32_t i = 0; i < g_nstrs; i++)
        if (strcmp(g_pool_base + g_strs[i].off, s) == 0) return g_rw_va + g_strs[i].off;
    return 0;
}

int s2a_build_image(const s2a_options *o, rc_node *root, uint64_t ro_va, uint64_t rw_va,
                    s2a_image_parts *out)
{
    (void)o;
    builder B;
    memset(&B, 0, sizeof B);
    ser_node(&B, root);

    for (uint32_t i = 0; i < B.nfix; i++) {
        uint64_t v;
        if (B.fix[i].is_str)
            v = B.fix[i].val ? (rw_va + (uint64_t)(B.fix[i].val - 1)) : 0;
        else
            v = ro_va + B.fix[i].val;
        memcpy(B.ro + B.fix[i].at, &v, 8);
    }

    out->ro = B.ro;
    out->ro_len = B.ro_len;
    out->pool = B.pool;
    out->pool_len = B.pool_len;
    out->n_objects = B.nobjs;
    out->n_strings = B.nstrs;

    /* 保留查询表（codegen 需要按 AST 指针取绝对地址） */
    g_objs = B.objs; g_nobjs = B.nobjs; g_ro_va = ro_va;
    g_strs = B.strs; g_nstrs = B.nstrs; g_rw_va = rw_va;
    g_pool_base = B.pool;
    free(B.fix);
    return 0;
}
