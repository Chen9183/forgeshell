/* rt_core.c —— 运行时核心：内存工具 / 变量表与作用域 / 词展开 / 算术 / 测试
 *
 * 语义要点（与 POSIX 对齐、也是本项目“语义集中在一处”的地方）：
 *   · 词展开顺序：波浪号 → 参数展开 → 命令替换 → 算术 → 字段切分(IFS) → 路径名展开(glob) → 引号去除
 *   · 只有「未加引号的展开结果」参与字段切分；字面量与引号内内容不切分
 *   · 赋值右值（RWF_ASSIGN_RHS）不切分、不 glob
 *   · "$@" 展开为多个独立字段；$* 用 IFS 首字符连接
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <ctype.h>
#include <pwd.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "rt_internal.h"
#include "../common/parse.h"   /* parse_arith_text：运行期算术字符串求值要用 */

rt_state rt_g;

/* =====================================================================
 * 内存与字符串工具
 * ===================================================================== */
void *rt_xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "s2a: 内存不足\n"); _exit(125); }
    return p;
}

void *rt_xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "s2a: 内存不足\n"); _exit(125); }
    return q;
}

char *rt_xstrdup(const char *s)
{
    if (!s) s = "";
    size_t n = strlen(s) + 1;
    char *p = rt_xmalloc(n);
    memcpy(p, s, n);
    return p;
}

char *rt_xstrndup(const char *s, size_t n)
{
    char *p = rt_xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void rt_words_init(rc_words *w) { w->v = NULL; w->n = 0; w->cap = 0; }

void rt_words_free(rc_words *w)
{
    for (uint32_t i = 0; i < w->n; i++) free(w->v[i]);
    free(w->v);
    w->v = NULL; w->n = 0; w->cap = 0;
}

void rt_words_add(rc_words *w, char *s)
{
    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 8;
        w->v = rt_xrealloc(w->v, w->cap * sizeof(char *));
    }
    w->v[w->n++] = s ? s : rt_xstrdup("");
}

void rt_words_add_copy(rc_words *w, const char *s) { rt_words_add(w, rt_xstrdup(s)); }

char *rt_str_join(char **v, uint32_t n, const char *sep)
{
    size_t sl = strlen(sep), total = 1;
    for (uint32_t i = 0; i < n; i++) total += strlen(v[i]) + (i ? sl : 0);
    char *out = rt_xmalloc(total), *p = out;
    for (uint32_t i = 0; i < n; i++) {
        if (i) { memcpy(p, sep, sl); p += sl; }
        size_t l = strlen(v[i]);
        memcpy(p, v[i], l); p += l;
    }
    *p = 0;
    return out;
}

int rt_word_is_name(const char *s)
{
    if (!s || !*s) return 0;
    if (!(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (const char *p = s + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    return 1;
}

/* =====================================================================
 * 变量表 / 作用域 / 位置参数 / 特殊参数
 * ===================================================================== */
rt_var *rt_var_lookup(const char *name)
{
    for (rt_var *v = rt_g.vars; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}

rt_var *rt_var_define(const char *name, int level)
{
    rt_var *v = rt_xmalloc(sizeof(*v));
    v->name = rt_xstrdup(name);
    v->value = NULL;
    v->flags = 0;
    v->level = level;
    v->next = rt_g.vars;
    rt_g.vars = v;
    if (getenv("S2A_VAR_DEBUG"))
        fprintf(stderr, "[var] DEFINE %s node=%p head_before=%p level=%d\n",
                name, (void *)v, (void *)v->next, level);
    return v;
}

static void ifs_maybe_sync(const char *name);
void rt_ifs_sync(void);

void rt_var_assign(rt_var *v, const char *value)
{
    if (v->flags & RC_VF_READONLY) {        /* POSIX：给只读变量赋值是错误，非交互 shell 退出 */
        fprintf(stderr, "s2a: %s: 只读变量\n", v->name ? v->name : "");
        rc_rt_exit(1);
    }
    free(v->value);
    v->value = rt_xstrdup(value);
    if (rt_g.opt_a && v->name) {                    /* set -a：赋值即导出 */
        v->flags |= RC_VF_EXPORT;
        setenv(v->name, value ? value : "", 1);
    }
    ifs_maybe_sync(v->name);
}

char *rt_var_get_str(const char *name)
{
    rt_var *v = rt_var_lookup(name);
    return v ? v->value : NULL;
}

/* IFS 同步：IFS 被赋值/取消时更新 rt_g.ifs（用静态副本，绝不指向可能被释放的变量值）。
   早期 rt_g.ifs 只在启动时设过一次，于是 `IFS=:` / `IFS= read` 全都不生效。 */
void rt_ifs_sync(void)
{
    static char ifs_buf[64];
    const char *v = rt_var_get_str("IFS");
    if (!v) v = " \t\n";
    size_t n = strlen(v);
    if (n >= sizeof ifs_buf) n = sizeof ifs_buf - 1;
    memcpy(ifs_buf, v, n);
    ifs_buf[n] = 0;
    rt_g.ifs = ifs_buf;
}

static void ifs_maybe_sync(const char *name)
{
    if (name && strcmp(name, "IFS") == 0) rt_ifs_sync();
}

void rc_var_set(const char *name, const char *value)
{
    rt_var *v = rt_var_lookup(name);
    if (!v) v = rt_var_define(name, rt_g.level);
    rt_var_assign(v, value);
}

const char *rc_var_get(const char *name)
{
    char *v = rt_var_get_str(name);
    return v;
}

void rc_var_unset(const char *name)
{
    rt_var **pp = &rt_g.vars;
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            rt_var *v = *pp;
            if (getenv("S2A_VAR_DEBUG")) fprintf(stderr, "[var] UNSET %s node=%p\n", name, (void *)v);
            *pp = v->next;
            int was_ifs = (strcmp(v->name, "IFS") == 0);
            free(v->name); free(v->value); free(v);
            if (was_ifs) rt_ifs_sync();
            return;
        }
        pp = &(*pp)->next;
    }
}

void rc_var_export(const char *name, int on)
{
    rt_var *v = rt_var_lookup(name);
    if (!v) v = rt_var_define(name, rt_g.level);
    if (on) v->flags |= RC_VF_EXPORT; else v->flags &= (uint8_t)~RC_VF_EXPORT;
    if (v->value) setenv(name, v->value, 1);
}

void rc_var_mark_readonly(const char *name, int on)
{
    rt_var *v = rt_var_lookup(name);
    if (!v) v = rt_var_define(name, rt_g.level);
    if (on) v->flags |= RC_VF_READONLY; else v->flags &= (uint8_t)~RC_VF_READONLY;
}

void rt_var_scope_enter(void) { rt_g.level++; }

void rt_var_scope_leave(void)
{
    /* 删除本层定义的变量（局部变量随函数退出消失） */
    rt_var **pp = &rt_g.vars;
    if (getenv("S2A_VAR_DEBUG")) fprintf(stderr, "[var] SCOPE_LEAVE level=%d\n", rt_g.level);
    while (*pp) {
        if ((*pp)->level >= rt_g.level) {
            rt_var *v = *pp;
            if (getenv("S2A_VAR_DEBUG")) fprintf(stderr, "[var]   FREE %s node=%p\n", v->name, (void *)v);
            *pp = v->next;
            free(v->name); free(v->value); free(v);
        } else pp = &(*pp)->next;
    }
    if (rt_g.level > 0) rt_g.level--;
}

void rc_var_push_scope(void) { rt_var_scope_enter(); }
void rc_var_pop_scope(void) { rt_var_scope_leave(); }

void rc_var_set_local(const char *name, const char *value)
{
    rt_var *v = rt_var_lookup(name);
    if (v && v->level == rt_g.level) { rt_var_assign(v, value); return; }
    v = rt_var_define(name, rt_g.level);
    rt_var_assign(v, value);
}

/* 位置参数 */
void rc_pos_set(long argc, char **argv, const char *arg0)
{
    for (long i = 0; i < rt_g.npos; i++) free(rt_g.pos[i]);
    free(rt_g.pos);
    rt_g.pos = NULL; rt_g.npos = 0;
    if (argc > 0) {
        rt_g.pos = rt_xmalloc(sizeof(char *) * (size_t)argc);
        for (long i = 0; i < argc; i++) rt_g.pos[i] = rt_xstrdup(argv[i]);
        rt_g.npos = argc;
    }
    rt_g.arg0 = arg0 ? arg0 : "s2a";
}

long rc_pos_shift(long n)
{
    if (n < 0) n = 1;
    if (n > rt_g.npos) n = rt_g.npos;
    for (long i = 0; i < n; i++) free(rt_g.pos[i]);
    if (n > 0) {
        memmove(rt_g.pos, rt_g.pos + n, sizeof(char *) * (size_t)(rt_g.npos - n));
        rt_g.npos -= n;
    }
    return 0;
}

char **rc_pos_argv(long *argc_out, const char *arg0_out)
{
    (void)arg0_out;
    if (argc_out) *argc_out = rt_g.npos;
    return rt_g.pos;
}

long rc_special_get_long(char c)
{
    switch (c) {
    case '?': return rt_g.last_status;
    case '#': return rt_g.npos;
    case '$': return rt_g.shell_pid;
    case '!': return rt_g.last_bg_pid;
    case '-': return 0;
    case '0': return 0;
    default:
        if (c >= '1' && c <= '9') {
            long i = c - '1';
            return i < rt_g.npos ? 1 : 0;   /* 有值返回 1（真正的值见 rc_special_str） */
        }
        return 0;
    }
}

const char *rc_special_str(char c)
{
    static char buf[32];
    switch (c) {
    case '?': snprintf(buf, sizeof buf, "%ld", rt_g.last_status); return buf;
    case '#': snprintf(buf, sizeof buf, "%ld", rt_g.npos); return buf;
    case '$': snprintf(buf, sizeof buf, "%ld", rt_g.shell_pid); return buf;
    case '!': if (rt_g.last_bg_pid <= 0) return "";   /* 没有后台作业 → 空（bash 同） */
              snprintf(buf, sizeof buf, "%ld", rt_g.last_bg_pid); return buf;
    case '-': buf[0] = 0;
              if (rt_g.opt_e) strcat(buf, "e");
              if (rt_g.opt_f) strcat(buf, "f");
              if (rt_g.opt_n) strcat(buf, "n");
              if (rt_g.opt_u) strcat(buf, "u");
              if (rt_g.opt_v) strcat(buf, "v");
              if (rt_g.opt_x) strcat(buf, "x");
              return buf;
    case '0': return rt_g.arg0 ? rt_g.arg0 : "s2a";
    default:
        if (c >= '1' && c <= '9') {
            long i = c - '1';
            return i < rt_g.npos ? rt_g.pos[i] : "";
        }
        return "";
    }
}

const char *rt_special_value(const char *name)
{
    return name && name[0] ? rc_special_str(name[0]) : "";
}

/* =====================================================================
 * 词展开
 * ===================================================================== */

/* 展开一个片段为字符串（不含字段切分）；返回 malloc 的串 */
static char *expand_seg_to_string(const rc_wseg *s, int *had_quoted, int *unquoted_exp)
{
    char *out = NULL;
    switch (s->kind) {
    case RW_LIT:
        out = rt_xstrdup(s->name ? s->name : "");
        break;
    case RW_VAR: {
        const char *v = rt_var_get_str(s->name ? s->name : "");
        if (!v && rt_g.opt_u && !s->quoted) {
            fprintf(stderr, "s2a: %s: 未定义变量: %s\n", rt_g.arg0 ? rt_g.arg0 : "sh",
                    s->name ? s->name : "");
            rt_g.last_status = 1;
        }
        if (!v && rt_g.opt_u) {          /* set -u：未设置变量视为错误 */
        fprintf(stderr, "s2a: %s: 参数未设置\n", s->name ? s->name : "");
        rc_rt_exit(1);
    }
    out = rt_xstrdup(v ? v : "");
        break;
    }
    case RW_SPECIAL: {
        const char *n = s->name ? s->name : "";
        out = rt_xstrdup(rt_special_value(n));
        break;
    }
    case RW_TILDE: {
        if (s->name && s->name[0]) {
            struct passwd *pw = getpwnam(s->name);
            out = rt_xstrdup(pw ? pw->pw_dir : "");
        } else {
            const char *h = getenv("HOME");
            out = rt_xstrdup(h ? h : "");
        }
        break;
    }
    case RW_CMDSUB:
        out = rt_expand_command_subst(s->sub);
        break;
    case RW_ARITH: {
        long v = 0;
        if (rt_arith_eval(s->arith, &v) != 0) { rt_g.last_status = 1; v = 0; }
        char b[32]; snprintf(b, sizeof b, "%ld", v);
        out = rt_xstrdup(b);
        break;
    }
    case RW_PARAM: {
        const char *name = s->name ? s->name : "";
        char *val = rt_var_get_str(name);
        /* ${1}..${9}：位置参数不在变量表里，要单独取；${*} ${@} ${#} ${?} 等特殊参数同理。
           以前一律查变量表 → ${1} 恒为空、${*-x} 误判成"未设置"。 */
        static char posbuf[64];
        if (!val && name[0] && !name[1] && (name[0] == '*' || name[0] == '@')) {
            char *j = rt_str_join(rt_g.pos, (uint32_t)rt_g.npos,
                                  (rt_g.ifs && rt_g.ifs[0]) ? (char[]){rt_g.ifs[0], 0} : " ");
            snprintf(posbuf, sizeof posbuf, "%s", j ? j : "");
            free(j);
            val = posbuf;
        } else if (!val && name[0] && !name[1] && strchr("#?-$!", name[0])) {
            snprintf(posbuf, sizeof posbuf, "%s", rc_special_str(name[0]));
            val = posbuf;
        } else if (!name[0] || (name[0] >= '0' && name[0] <= '9')) {
            long idx = name[0] ? strtol(name, NULL, 10) : 0;
            if (idx <= 0) snprintf(posbuf, sizeof posbuf, "%s", rt_g.arg0 ? rt_g.arg0 : "s2a");
            else if ((idx - 1) < rt_g.npos && rt_g.pos[idx - 1]) snprintf(posbuf, sizeof posbuf, "%s", rt_g.pos[idx - 1]);
            else posbuf[0] = 0;
            if (val == NULL) val = posbuf;
        }
        int is_set = (val != NULL) && val[0] != 0;
        int unset_empty = (val == NULL) || (val[0] == 0);
        uint32_t op = s->op & 0xFF;              /* 低 8 位：操作码 */
        uint32_t pf = s->op >> 8;                /* 高 8 位：修饰标志 */
        int colon = (pf & RP_F_COLON) != 0;
        char *base = rt_xstrdup(val ? val : "");
        char *r = NULL;
        switch (op) {
        case RP_NONE:
        case RP_ARRAY_AT:
            r = base; base = NULL;
            break;
        case RP_LEN: {
            char b[32];
            if (!name || !*name) {
                snprintf(b, sizeof b, "%ld", rt_g.npos);          /* ${#} == $# */
            } else if (name[0] == '*' || name[0] == '@') {
                /* ${#*} / ${#@}：POSIX 与 dash 取的都是「$* 拼接后」的长度
                   （bash 给的是参数个数，那是它的私货） */
                char *joined = rt_str_join(rt_g.pos, (uint32_t)rt_g.npos, " ");
                snprintf(b, sizeof b, "%lu", (unsigned long)strlen(joined));
                free(joined);
            } else if (0) {
                /* 占位：保持分支结构清晰 */
            } else {
                snprintf(b, sizeof b, "%lu", (unsigned long)strlen(base));
            }
            r = rt_xstrdup(b);
            break;
        }
        case RP_DEFAULT:
            /* ${v-word}：未设置（带冒号时“未设置或为空”）→ word；否则 → v 本身 */
            if (colon ? unset_empty : !is_set) r = rc_expand_word_literal(s->word);
            else { r = base; base = NULL; }
            break;
        case RP_ASSIGN:
            if (colon ? unset_empty : !is_set) {
                r = rc_expand_word_literal(s->word);
                rc_var_set(name, r);
            } else { r = base; base = NULL; }
            break;
        case RP_ERROR:
            if (colon ? unset_empty : !is_set) {
                char *msg = rc_expand_word_literal(s->word);
                fprintf(stderr, "s2a: %s: %s: %s\n", rt_g.arg0 ? rt_g.arg0 : "sh", name,
                        (msg && msg[0]) ? msg : "参数未设置或为空");
                free(msg);
                rt_g.last_status = 1;
                r = rt_xstrdup("");
            } else { r = base; base = NULL; }
            break;
        case RP_ALT:
            /* ${v+word}：已设置 → word；未设置 → 空串 */
            if (!(colon ? unset_empty : !is_set)) r = rc_expand_word_literal(s->word);
            else r = rt_xstrdup("");
            break;
        case RP_REMOVE_PRE_S: case RP_REMOVE_PRE_L:
        case RP_REMOVE_SUF_S: case RP_REMOVE_SUF_L: {
            char *pat = rc_expand_word_literal(s->pat);
            int mode = (op == RP_REMOVE_PRE_S) ? 0 : (op == RP_REMOVE_PRE_L) ? 1
                     : (op == RP_REMOVE_SUF_S) ? 2 : 3;
            r = rt_remove_pattern(base, pat ? pat : "", mode);
            free(pat);
            break;
        }
        case RP_SUBST: {
            char *pat = rc_expand_word_literal(s->pat);
            char *rep = rc_expand_word_literal(s->rep);
            r = rt_subst_pattern(base, pat ? pat : "", rep ? rep : "", pf);
            free(pat); free(rep);
            break;
        }
        case RP_SLICE: {
            /* 约定：${v:off:len} 的 off 存 s->idx，len 存 s->namelen（见 parse.c 的写入点） */
            (void)pf;
            long off = (long)s->idx, len = (long)s->namelen;
            long slen = (long)strlen(base);
            if (off < 0) off = slen + off;
            if (off < 0) off = 0;
            if (off > slen) off = slen;
            long take = (pf & RP_F_HAS_LEN) ? len : (slen - off);
            if (take < 0) take = slen - off + take;
            if (take < 0) take = 0;
            if (off + take > slen) take = slen - off;
            r = rt_xstrndup(base + off, (size_t)take);
            break;
        }
        case RP_UPPER: {
            r = rt_xstrdup(base);
            if (pf & RP_F_ALL) {                       /* ${v^^} */
                for (char *p = r; *p; p++) *p = (char)toupper((unsigned char)*p);
            } else if (r[0]) {                         /* ${v^} 只首字符 */
                r[0] = (char)toupper((unsigned char)r[0]);
            }
            break;
        }
        case RP_LOWER: {
            r = rt_xstrdup(base);
            if (pf & RP_F_ALL) {                       /* ${v,,} */
                for (char *p = r; *p; p++) *p = (char)tolower((unsigned char)*p);
            } else if (r[0]) {                         /* ${v,} 只首字符 */
                r[0] = (char)tolower((unsigned char)r[0]);
            }
            break;
        }
        case RP_INDIRECT: {
            const char *v = rt_var_get_str(base);
            r = rt_xstrdup(v ? v : "");
            break;
        }
        case RP_ARRAY_IDX: {
            /* 未实现关联/索引数组：退化为整体值，并把该情况记录到调试输出 */
            r = base; base = NULL;
            break;
        }
        default:
            r = base; base = NULL;
            break;
        }
        free(base);
        out = r ? r : rt_xstrdup("");
        break;
    }
    default:
        out = rt_xstrdup("");
        break;
    }
    (void)had_quoted; (void)unquoted_exp;
    return out ? out : rt_xstrdup("");
}

/* 字段切分：按 IFS 规则把未加引号的展开结果切开 */
static void split_append(rc_words *out, char **cur, int *cur_glob, int glob_ok,
                         const char *text, int do_split)
{
    if (!do_split || !rt_g.ifs || !rt_g.ifs[0]) {
        size_t old = *cur ? strlen(*cur) : 0, add = strlen(text);
        *cur = rt_xrealloc(*cur, old + add + 1);
        memcpy(*cur + old, text, add + 1);
        if (glob_ok) *cur_glob = 1;
        return;
    }
    const char *ifs = rt_g.ifs;
    int ifs_ws[256] = {0};
    for (const char *p = ifs; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '\n') ifs_ws[(unsigned char)*p] = 1;
    const char *p = text;
    while (*p) {
        if (strchr(ifs, *p)) {
            int is_ws = ifs_ws[(unsigned char)*p];
            if (is_ws) {
                /* 连续的 IFS 空白合一，且会结束当前字段 */
                if (*cur) { rt_words_add(out, *cur); *cur = NULL; *cur_glob = 0; }
                while (*p && ifs_ws[(unsigned char)*p]) p++;
            } else {
                if (*cur) { rt_words_add(out, *cur); *cur = NULL; *cur_glob = 0; }
                p++;
            }
        } else {
            size_t old = *cur ? strlen(*cur) : 0;
            *cur = rt_xrealloc(*cur, old + 2);
            (*cur)[old] = *p; (*cur)[old + 1] = 0; p++;
            if (glob_ok) *cur_glob = 1;
        }
    }
}

/* 词展开主入口：out 为字段数组 */
long rt_expand_word(const rc_word *w, rc_words *out, unsigned flags)
{
    if (!w) return 0;
    int no_split = (flags & RWF_ASSIGN_RHS) || (flags & RWF_HEREDOC) || (flags & RWF_PATTERN);
    char *cur = NULL;
    int cur_glob = 0;
    /* 词级引号标记：`""` 这种空引号词没有任何片段，但按 POSIX 必须产生一个空字段 */
    int had_quoted = (w->flags & RWF_QUOTED) ? 1 : 0;
    int no_empty_field = 0;              /* "$@" 空展开 → 连空字段都不产生 */
    uint32_t n0 = out->n;                /* 本词开始前的字段数（判断"本词没产出字段"） */

    int word_no_glob = 0;      /* 词里引号部分含元字符 → 整词不做通配 */
    for (const rc_wseg *s = w->segs; s; s = s->next) {
        if (s->quoted) {
            had_quoted = 1;
            /* 引号里的东西一律不做路径名展开：
               · 引号字面量里的元字符（'*.txt'）
               · 引号里的展开结果（"$v" / "$(cmd)"）——POSIX：引号内的展开结果不参与通配。
               以前只挡了前者，于是 `f x "$v"`（v='*.txt'）会被展开成目录里的文件名。 */
            if (s->kind != RW_LIT) word_no_glob = 1;
            if (s->name) {
                for (const char *q = s->name; *q; q++)
                    if (*q == '*' || *q == '?' || *q == '[') { word_no_glob = 1; break; }
            }
        }
        char *val = expand_seg_to_string(s, &had_quoted, NULL);
        int is_expansion = (s->kind != RW_LIT);
        int do_split = !no_split && !s->quoted && is_expansion;

        if (s->kind == RW_SPECIAL && s->name && s->name[0] == '@') {
            /* "$@" 语义：每个位置参数一个字段。两侧的字面量必须粘在首/尾元素上
               （`"[a $@ b]"` 得 `[a 1 2 b]` 而不是 `[a 1 2 b ]` 那种多空格）。
               赋值右侧等不切分的场合，按空格合成一个串。 */
            if (no_split) {
                split_append(out, &cur, &cur_glob, 0, val, 0);
                free(val);
                continue;
            }
            long np = rt_g.npos;
            if (np <= 0) { no_empty_field = 1; free(val); continue; }   /* 没有位置参数 → 整段消失 */
            if (np == 1) {                                  /* 单个参数：前缀粘上去，仍是开放字段 */
                size_t old = cur ? strlen(cur) : 0, add = strlen(rt_g.pos[0]);
                cur = rt_xrealloc(cur, old + add + 1);
                memcpy(cur + old, rt_g.pos[0], add + 1);
                free(val);
                continue;
            }
            if (cur) {                                      /* 前缀 + 第 1 个元素 = 完整字段 */
                size_t old = strlen(cur), add = strlen(rt_g.pos[0]);
                cur = rt_xrealloc(cur, old + add + 1);
                memcpy(cur + old, rt_g.pos[0], add + 1);
                rt_words_add(out, cur);
                cur = NULL; cur_glob = 0;
            } else {
                rt_words_add_copy(out, rt_g.pos[0]);
            }
            for (long i = 1; i < np - 1; i++) rt_words_add_copy(out, rt_g.pos[i]);
            cur = rt_xstrdup(rt_g.pos[np - 1]);          /* 最后一个留作开放字段，接后面的字面量 */
            cur_glob = 0;
            free(val);
            continue;
        }
        if (s->kind == RW_SPECIAL && s->name && s->name[0] == '*') {
            char *joined = rt_str_join(rt_g.pos, (uint32_t)rt_g.npos,
                                       (rt_g.ifs && rt_g.ifs[0]) ? (char[]){rt_g.ifs[0], 0} : " ");
            free(val); val = joined;
            do_split = !no_split && !s->quoted;
        }
        if (s->kind == RW_VAR || s->kind == RW_PARAM || s->kind == RW_TILDE) {
            /* 未加引号且值为空 → 该片段不产生字段（POSIX：空展开不出字段） */
        }
        split_append(out, &cur, &cur_glob, !no_split && !s->quoted, val, do_split);
        free(val);
    }
    if (cur) rt_words_add(out, cur);
    else if (out->n == n0 && !no_split && had_quoted && !no_empty_field) {
        /* 整个词展开为空 → 不产生字段；但带引号的空词（""）要产生一个空字段。
           之前这里用 out->n == 0 判断，导致 `printf '<%s>' "" x` 里 "" 被吃掉。 */
        rt_words_add_copy(out, "");
    }

    /* 路径名展开（glob）：只对含未引号元字符的字段做 */
    if (!(flags & RWF_ASSIGN_RHS) && !(flags & RWF_HEREDOC) && !(flags & RWF_PATTERN) && !word_no_glob)
        rt_path_expand(out);
    return 0;
}

char *rc_expand_word_literal(const rc_word *w)
{
    if (!w) return rt_xstrdup("");
    rc_words t; rt_words_init(&t);
    /* 与 rt_expand_word 相同，但不切分不 glob：用一个临时 IFS="" 的技巧太脏，直接手写 */
    char *cur = NULL;
    for (const rc_wseg *s = w->segs; s; s = s->next) {
        char *v;
        if (s->kind == RW_SPECIAL && s->name && s->name[0] == '@') {
            v = rt_str_join(rt_g.pos, (uint32_t)rt_g.npos, " ");
        } else if (s->kind == RW_SPECIAL && s->name && s->name[0] == '*') {
            v = rt_str_join(rt_g.pos, (uint32_t)rt_g.npos, " ");
        } else {
            v = expand_seg_to_string(s, NULL, NULL);
        }
        size_t old = cur ? strlen(cur) : 0, add = strlen(v);
        cur = rt_xrealloc(cur, old + add + 1);
        memcpy(cur + old, v, add + 1);
        free(v);
    }
    rt_words_free(&t);
    return cur ? cur : rt_xstrdup("");
}

long rc_expand_words(rc_word **words, uint32_t n, rc_words *out)
{
    rt_words_init(out);
    for (uint32_t i = 0; i < n; i++) rt_expand_word(words[i], out, 0);
    return 0;
}

/* 命令替换：子进程执行，父进程读其 stdout，去掉结尾换行 */
char *rt_expand_command_subst(const rc_node *sub)
{
    int fds[2];
    if (pipe(fds) != 0) return rt_xstrdup("");
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return rt_xstrdup(""); }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        if (fds[1] != 1) close(fds[1]);
        rt_g.in_subshell++;
        rc_trap_clear_exit();          /* 父级 EXIT trap 不继承（bash 同） */
        long st = rt_interp_list(sub);
        rc_rt_exit(st);                /* 子进程自己设的 EXIT trap 在这里跑 */
    }
    close(fds[1]);
    size_t cap = 256, len = 0;
    char *buf = rt_xmalloc(cap);
    ssize_t r;
    while ((r = read(fds[0], buf + len, cap - len - 1)) > 0) {
        len += (size_t)r;
        if (cap - len < 2) { cap *= 2; buf = rt_xrealloc(buf, cap); }
    }
    close(fds[0]);
    buf[len] = 0;
    int st = 0;
    waitpid(pid, &st, 0);
    rt_g.last_status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    while (len > 0 && buf[len - 1] == '\n') buf[--len] = 0;   /* POSIX：去掉结尾所有换行 */
    return buf;
}

long rc_for_words(const rc_for *f, rc_words *out)
{
    rt_words_init(out);
    if (!f) return 0;
    if (!f->has_in) {   /* 隐含 in "$@" */
        for (long i = 0; i < rt_g.npos; i++) rt_words_add_copy(out, rt_g.pos[i]);
        return 0;
    }
    for (uint32_t i = 0; i < f->nitems; i++) {
        rc_words one;
        rt_words_init(&one);              /* 必须初始化：rt_expand_word 会往里追加 */
        rt_expand_word(f->items[i], &one, 0);
        for (uint32_t k = 0; k < one.n; k++) rt_words_add(out, one.v[k]);
        free(one.v);
    }
    return 0;
}

/* 路径名展开 */
long rt_path_expand(rc_words *w)
{
    rc_words res; rt_words_init(&res);
    if (rt_g.opt_f) {                      /* set -f：关闭通配 */
        for (uint32_t i = 0; i < w->n; i++) rt_words_add_copy(&res, w->v[i]);
        rt_words_free(w);
        *w = res;
        return 0;
    }
    for (uint32_t i = 0; i < w->n; i++) {
        const char *s = w->v[i];
        int has_meta = 0;
        for (const char *p = s; *p; p++) if (*p == '*' || *p == '?' || *p == '[') { has_meta = 1; break; }
        if (!has_meta) { rt_words_add_copy(&res, s); continue; }
        glob_t g;
        memset(&g, 0, sizeof g);
        if (glob(s, 0, NULL, &g) == 0 && g.gl_pathc > 0) {
            for (size_t k = 0; k < g.gl_pathc; k++) rt_words_add_copy(&res, g.gl_pathv[k]);
        } else {
            rt_words_add_copy(&res, s);   /* 无匹配 → 保持原文 */
        }
        globfree(&g);
    }
    rt_words_free(w);
    *w = res;
    return 0;
}

/* 模式匹配（fnmatch 语义；pathname_mode=1 时 '*' 不跨 '/'） */
int rc_pattern_match(const char *pat, const char *s, int pathname_mode)
{
    return fnmatch(pat, s, pathname_mode ? FNM_PATHNAME : 0) == 0;
}

/* ${v#pat} 系列：在“最短/最长、前缀/后缀”四种组合里找删除量 */
char *rt_remove_pattern(const char *val, const char *pat, int mode)
{
    /* mode: 0=${v#p} 最短前缀  1=${v##p} 最长前缀  2=${v%p} 最短后缀  3=${v%%p} 最长后缀
       ⚑ 后缀两个方向以前写反了：`%` 要最短后缀（起点最大），`%%` 才要最长（起点最小）。 */
    size_t n = strlen(val);
    long best = -1;
    if (mode == 2) {                       /* %：从最短后缀开始找 */
        for (long i = (long)n; i >= 0; i--)
            if (rc_pattern_match(pat, val + i, 0)) { best = i; break; }
    } else if (mode == 3) {                /* %%：最长后缀 */
        for (size_t i = 0; i <= n; i++)
            if (rc_pattern_match(pat, val + i, 0)) { best = (long)i; break; }
    } else if (mode == 0) {                /* #：最短前缀 */
        for (size_t i = 0; i <= n; i++) {
            char *sub = rt_xstrndup(val, i);
            int m = rc_pattern_match(pat, sub, 0);
            free(sub);
            if (m) { best = (long)i; break; }
        }
    } else {                               /* ##：最长前缀 */
        for (size_t i = 0; i <= n; i++) {
            char *sub = rt_xstrndup(val, i);
            int m = rc_pattern_match(pat, sub, 0);
            free(sub);
            if (m) best = (long)i;
        }
    }
    if (best < 0) return rt_xstrdup(val);
    if (mode == 0 || mode == 1) return rt_xstrdup(val + best);
    return rt_xstrndup(val, (size_t)best);
}

/* ${v/pat/rep}：简化但正确的实现（支持全局与首/尾锚定） */
char *rt_subst_pattern(const char *val, const char *pat, const char *rep, uint32_t flags)
{
    size_t n = strlen(val), pn = strlen(pat), rn = strlen(rep);
    int global = (flags & RP_F_GLOBAL) != 0;
    int anchor_p = (flags & RP_F_ANCHOR_P) != 0;
    int anchor_s = (flags & RP_F_ANCHOR_S) != 0;
    char *out = rt_xmalloc(1);
    size_t olen = 0;
    out[0] = 0;
    int replaced = 0;

#define APPEND(p_, l_) do { \
        out = rt_xrealloc(out, olen + (l_) + 1); \
        memcpy(out + olen, (p_), (l_)); \
        olen += (l_); out[olen] = 0; \
    } while (0)

    for (size_t i = 0; i <= n; ) {
        int try_here = (global || !replaced);
        if (anchor_p && i != 0) try_here = 0;
        if (anchor_s && i != n) try_here = 0;
        if (try_here) {
            size_t best = 0;
            for (size_t l = n - i; l > 0; l--) {          /* 取最长匹配（shell 惯例） */
                char sub[4096];
                if (l >= sizeof sub) break;
                memcpy(sub, val + i, l); sub[l] = 0;
                if (rc_pattern_match(pat, sub, 0)) { best = l; break; }
            }
            if (pn == 0) {                                 /* 空模式：在位置 i 插入替换 */
                APPEND(rep, rn);
                replaced = 1;
                if (i < n) { APPEND(val + i, 1); i++; }
                else break;
                continue;
            }
            if (best > 0) {
                APPEND(rep, rn);
                i += best;
                replaced = 1;
                continue;
            }
        }
        if (i >= n) break;
        APPEND(val + i, 1);
        i++;
    }
#undef APPEND
    return out;
}

/* =====================================================================
 * 算术求值
 * ===================================================================== */
static long arith_var_value(const char *name)
{
    const char *v = NULL;
    if (!name || !*name) return 0;
    size_t nl = strlen(name);
    int alldig = 1;
    for (size_t i = 0; i < nl; i++) if (!isdigit((unsigned char)name[i])) { alldig = 0; break; }
    if (alldig) {                        /* $1 $2 ... 位置参数（算术里也要能取值） */
        long idx = strtol(name, NULL, 10);
        if (idx == 0) v = rt_g.arg0 ? rt_g.arg0 : "s2a";
        else v = (idx - 1) < rt_g.npos ? rt_g.pos[idx - 1] : "";
    } else if (nl == 1 && strchr("#?$!-", name[0])) {
        return rc_special_get_long(name[0]);
    } else {
        v = rt_var_get_str(name);
    }
    if (!v || !*v) return 0;
    char *end = NULL;
    long r = strtol(v, &end, 10);
    if (end && *end == 0) return r;
    long nested = 0;
    if (rt_arith_str(v, &nested) == 0) return nested;
    return 0;
}

long rt_arith_eval(const rc_arith *a, long *out)
{
    if (!a) { if (out) *out = 0; return 0; }
    long x = 0, y = 0, z = 0;
    switch (a->kind) {
    case RA_NUM: if (out) *out = a->num; return 0;
    case RA_VAR: if (out) *out = arith_var_value(a->name ? a->name : ""); return 0;
    case RA_UNARY:
        if (rt_arith_eval(a->a, &x) != 0) return 1;
        switch (a->op) {
        case RAO_MINUS: x = -x; break;
        case RAO_PLUS: break;
        case RAO_NOT: x = !x; break;
        case RAO_BNOT: x = ~x; break;
        default: break;
        }
        if (out) *out = x;
        return 0;
    case RA_BINARY:
        if (rt_arith_eval(a->a, &x) != 0) return 1;
        if (rt_arith_eval(a->b, &y) != 0) return 1;
        switch (a->op) {
        case RAO_ADD: x = x + y; break;
        case RAO_SUB: x = x - y; break;
        case RAO_MUL: x = x * y; break;
        case RAO_DIV:
            if (y == 0) { fprintf(stderr, "s2a: 除零错误\n"); rt_g.last_status = 1; return 1; }
            x = x / y; break;
        case RAO_MOD:
            if (y == 0) { fprintf(stderr, "s2a: 除零错误\n"); rt_g.last_status = 1; return 1; }
            x = x % y; break;
        case RAO_POW: { long r = 1; for (long i = 0; i < y; i++) r *= x; x = r; break; }
        case RAO_SHL: x = x << y; break;
        case RAO_SHR: x = x >> y; break;
        case RAO_LT: x = x < y; break;
        case RAO_LE: x = x <= y; break;
        case RAO_GT: x = x > y; break;
        case RAO_GE: x = x >= y; break;
        case RAO_EQ: x = x == y; break;
        case RAO_NE: x = x != y; break;
        case RAO_BAND: x = x & y; break;
        case RAO_BXOR: x = x ^ y; break;
        case RAO_BOR: x = x | y; break;
        case RAO_LAND: x = (x && y); break;
        case RAO_LOR: x = (x || y); break;
        case RAO_COMMAMM: x = y; break;
        default: break;
        }
        if (out) *out = x;
        return 0;
    case RA_TERNARY:
        if (rt_arith_eval(a->a, &x) != 0) return 1;
        return rt_arith_eval(x ? a->b : a->c, out);
    case RA_ASSIGN: {
        if (rt_arith_eval(a->b, &y) != 0) return 1;
        long cur = a->op == '=' ? 0 : arith_var_value(a->name ? a->name : "");
        switch (a->op) {
        case '=': cur = y; break;
        case RAO_ASG_ADD: cur = cur + y; break;
        case RAO_ASG_SUB: cur = cur - y; break;
        case RAO_ASG_MUL: cur = cur * y; break;
        case RAO_ASG_DIV: if (!y) return 1; cur = cur / y; break;
        case RAO_ASG_MOD: if (!y) return 1; cur = cur % y; break;
        case RAO_ASG_SHL: cur = cur << y; break;
        case RAO_ASG_SHR: cur = cur >> y; break;
        case RAO_ASG_BAND: cur = cur & y; break;
        case RAO_ASG_BXOR: cur = cur ^ y; break;
        case RAO_ASG_BOR: cur = cur | y; break;
        default: break;
        }
        char b[32]; snprintf(b, sizeof b, "%ld", cur);
        rc_var_set(a->name ? a->name : "", b);
        if (out) *out = cur;
        return 0;
    }
    case RA_INCDEC: {
        long cur = arith_var_value(a->name ? a->name : "");
        long nv = (a->op == RAO_PREINC || a->op == RAO_POSTINC) ? cur + 1 : cur - 1;
        char b[32]; snprintf(b, sizeof b, "%ld", nv);
        rc_var_set(a->name ? a->name : "", b);
        if (out) *out = a->post ? cur : nv;
        return 0;
    }
    case RA_CMDSUB: {
        /* $(...) / `...`：先跑命令拿到文本，再当算术式求值（bash 语义） */
        char *txt = rt_expand_command_subst(a->sub);
        long v = 0;
        if (txt && *txt) {
            char *end = NULL;
            v = strtol(txt, &end, 0);
            while (end && (*end == ' ' || *end == '\t')) end++;
            if (!end || *end) {                 /* 不是纯数字：当表达式再求一次 */
                long n2 = 0;
                if (rt_arith_str(txt, &n2) == 0) v = n2;
                else v = 0;
            }
        }
        free(txt);
        if (out) *out = v;
        return 0;
    }
    case RA_COMMA:
        if (rt_arith_eval(a->a, &x) != 0) return 1;
        return rt_arith_eval(a->b, out);
    default:
        if (out) *out = 0;
        return 0;
    }
    (void)z;
}

/* 递归释放运行期临时构造的算术树（只释放算术节点本身；内嵌子脚本不动） */
static void free_arith_tree(rc_arith *a)
{
    if (!a) return;
    free_arith_tree(a->a);
    free_arith_tree(a->b);
    free_arith_tree(a->c);
    free(a);
}

/* 运行时把一段字符串当算术式求值。
   ★现在走真解析器★（parse.c 已链进运行时）：于是 `$(( $x ))`（x 里是 "1+1"）、
   `$(( $(echo 1+1) ))`、`eval` 出来的表达式都能算对。
   以前只会 strtol，表达式一律得 0。 */
long rt_arith_str(const char *expr, long *out)
{
    if (!expr) { if (out) *out = 0; return 0; }
    char *end = NULL;
    long v = strtol(expr, &end, 0);
    while (end && (*end == ' ' || *end == '\t')) end++;
    if (end && *end == 0) { if (out) *out = v; return 0; }
    if (rt_g.opt_u) { if (out) *out = 0; return 1; }
    rc_arith *e = parse_arith_text(expr, strlen(expr));
    if (!e) { if (out) *out = 0; return 1; }
    long r = 0;
    int rc = rt_arith_eval(e, &r);
    free_arith_tree(e);
    if (rc != 0) { if (out) *out = 0; return 1; }
    if (out) *out = r;
    return 0;
}

/* =====================================================================
 * test / [ / [[ ]] 求值
 * ===================================================================== */
static int file_test(int op, const char *path);

static long test_unary(int op, const rc_word *w)
{
    char *s = rc_expand_word_literal(w);
    long r;
    /* op 编码见 rt_test_op 表（parse.c 写入） */
    switch (op) {
    case 1: r = (s[0] != 0); break;                  /* -n */
    case 2: r = (s[0] == 0); break;                  /* -z */
    default: r = file_test(op, s); break;
    }
    free(s);
    return r;
}

static int file_test(int op, const char *path)
{
    struct stat st;
    switch (op) {
    case 10: return stat(path, &st) == 0;                       /* -e */
    case 11: return stat(path, &st) == 0 && S_ISREG(st.st_mode); /* -f */
    case 12: return stat(path, &st) == 0 && S_ISDIR(st.st_mode); /* -d */
    case 13: return access(path, R_OK) == 0;                     /* -r */
    case 14: return access(path, W_OK) == 0;                     /* -w */
    case 15: return access(path, X_OK) == 0;                     /* -x */
    case 16: return stat(path, &st) == 0 && st.st_size > 0;      /* -s */
    case 17: return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);/* -L/-h */
    case 18: return stat(path, &st) == 0 && S_ISCHR(st.st_mode); /* -c */
    case 19: return stat(path, &st) == 0 && S_ISBLK(st.st_mode); /* -b */
    case 20: return stat(path, &st) == 0 && S_ISFIFO(st.st_mode);/* -p */
    case 21: return stat(path, &st) == 0 && S_ISSOCK(st.st_mode);/* -S */
    default: return 0;
    }
}

long rt_test_node(const rc_test *t)
{
    if (!t) return 1;
    switch (t->kind) {
    case RT_AND: return (rt_test_node(t->a) == 0 && rt_test_node(t->b) == 0) ? 0 : 1;
    case RT_OR:  return (rt_test_node(t->a) == 0 || rt_test_node(t->b) == 0) ? 0 : 1;
    case RT_NOT: return rt_test_node(t->a) == 0 ? 1 : 0;
    case RT_GROUP: return rt_test_node(t->a);
    case RT_UNARY_FILE:
    case RT_UNARY_STR:
        return test_unary(t->op, t->lhs) ? 0 : 1;
    case RT_CMP_STR: {
        char *a = rc_expand_word_literal(t->lhs);
        char *b = rc_expand_word_literal(t->rhs);
        int eq = strcmp(a, b) == 0;
        long r;
        switch (t->op) {
        case 30: r = eq; break;      /* = / == */
        case 31: r = !eq; break;     /* != */
        case 32: r = strcmp(a, b) < 0; break;   /* < */
        case 33: r = strcmp(a, b) > 0; break;   /* > */
        default: r = 0; break;
        }
        free(a); free(b);
        return r ? 0 : 1;
    }
    case RT_CMP_NUM: {
        char *as = rc_expand_word_literal(t->lhs);
        char *bs = rc_expand_word_literal(t->rhs);
        long a = strtol(as, NULL, 10), b = strtol(bs, NULL, 10);
        long r = 0;
        switch (t->op) {
        case 40: r = a == b; break;
        case 41: r = a != b; break;
        case 42: r = a < b; break;
        case 43: r = a <= b; break;
        case 44: r = a > b; break;
        case 45: r = a >= b; break;
        default: break;
        }
        free(as); free(bs);
        return r ? 0 : 1;
    }
    case RT_CMP_PAT: {
        char *a = rc_expand_word_literal(t->lhs);
        char *p = rc_expand_word_literal(t->rhs);
        int m = rc_pattern_match(p, a, 0);
        if (t->op == 31) m = !m;
        free(a); free(p);
        return m ? 0 : 1;
    }
    case RT_CMP_REGEX: {
        char *a = rc_expand_word_literal(t->lhs);
        char *p = rc_expand_word_literal(t->rhs);
        regex_t re;
        int m = 0;
        if (regcomp(&re, p, REG_EXTENDED | REG_NOSUB) == 0) {
            m = regexec(&re, a, 0, NULL, 0) == 0;
            regfree(&re);
        }
        free(a); free(p);
        return m ? 0 : 1;
    }
    case RT_ARITH: {
        long v = 0;
        if (rt_arith_eval(t->arith, &v) != 0) return 1;
        return v ? 0 : 1;
    }
    default:
        return 1;
    }
}

long rc_test_run(const rc_test *t)
{
    long r = rt_test_node(t);
    rt_g.last_status = r;      /* test/[[ ]] 的结果也是命令状态（$?） */
    return r;
}

/* test / [ 的 argv 风格求值：POSIX 语法是递归的
     expr := and { '-o' and }
     and  := not { '-a' not }
     not  := '!' not | primary
     primary := '(' expr ')' | 一元 | 二元 | WORD
   早期这里是扁平匹配，只认 1/2/3 参数，于是 `[ ! -e f ]`、`[ a = a -a b = b ]`
   （4、5 个参数）全部返回假。 */

typedef struct { char **v; int n, i; } targv;

static int tv_or(targv *T);

static int tv_fileop(const char *op)   /* 返回 file_test 的 op 码，0 = 不是文件测试 */
{
    static const struct { const char *n; int op; } ft[] = {
        {"-e",10},{"-a",10},{"-f",11},{"-d",12},{"-r",13},{"-w",14},{"-x",15},{"-s",16},
        {"-L",17},{"-h",17},{"-c",18},{"-b",19},{"-p",20},{"-S",21},{NULL,0}
    };
    for (int i = 0; ft[i].n; i++) if (!strcmp(op, ft[i].n)) return ft[i].op;
    return 0;
}

static int tv_binop(const char *x, const char *op, const char *y)
{
    if (!strcmp(op, "=") || !strcmp(op, "==")) return strcmp(x, y) == 0 ? 0 : 1;
    if (!strcmp(op, "!=")) return strcmp(x, y) != 0 ? 0 : 1;
    if (!strcmp(op, "<"))  return strcmp(x, y) < 0 ? 0 : 1;
    if (!strcmp(op, ">"))  return strcmp(x, y) > 0 ? 0 : 1;
    long a = strtol(x, NULL, 10), b = strtol(y, NULL, 10);
    if (!strcmp(op, "-eq")) return a == b ? 0 : 1;
    if (!strcmp(op, "-ne")) return a != b ? 0 : 1;
    if (!strcmp(op, "-lt")) return a < b ? 0 : 1;
    if (!strcmp(op, "-le")) return a <= b ? 0 : 1;
    if (!strcmp(op, "-gt")) return a > b ? 0 : 1;
    if (!strcmp(op, "-ge")) return a >= b ? 0 : 1;
    return 1;
}

static int tv_is_binop(const char *op)
{
    static const char *ops[] = {"=","==","!=","<",">","-eq","-ne","-lt","-le","-gt","-ge",NULL};
    for (int i = 0; ops[i]; i++) if (!strcmp(op, ops[i])) return 1;
    return 0;
}

static int tv_primary(targv *T)
{
    if (T->i < T->n && !strcmp(T->v[T->i], "(")) {
        T->i++;
        int r = tv_or(T);
        if (T->i < T->n && !strcmp(T->v[T->i], ")")) T->i++;
        return r;
    }
    if (T->n - T->i >= 3 && tv_is_binop(T->v[T->i + 1])) {
        int r = tv_binop(T->v[T->i], T->v[T->i + 1], T->v[T->i + 2]);
        T->i += 3;
        return r;
    }
    if (T->n - T->i >= 2) {
        const char *op = T->v[T->i];
        if (!strcmp(op, "-n")) { int r = T->v[T->i + 1][0] ? 0 : 1; T->i += 2; return r; }
        if (!strcmp(op, "-z")) { int r = T->v[T->i + 1][0] ? 1 : 0; T->i += 2; return r; }
        int fc = tv_fileop(op);
        if (fc) { int r = file_test(fc, T->v[T->i + 1]) ? 0 : 1; T->i += 2; return r; }
    }
    if (T->i < T->n) { int r = T->v[T->i][0] ? 0 : 1; T->i++; return r; }
    return 1;
}

static int tv_not(targv *T)
{
    if (T->i < T->n && !strcmp(T->v[T->i], "!")) { T->i++; return tv_not(T) == 0 ? 1 : 0; }
    return tv_primary(T);
}

static int tv_and(targv *T)
{
    int r = tv_not(T);
    while (T->i < T->n && !strcmp(T->v[T->i], "-a")) {
        T->i++;
        int s = tv_not(T);
        r = (r == 0 && s == 0) ? 0 : 1;
    }
    return r;
}

static int tv_or(targv *T)
{
    int r = tv_and(T);
    while (T->i < T->n && !strcmp(T->v[T->i], "-o")) {
        T->i++;
        int s = tv_and(T);
        r = (r == 0 || s == 0) ? 0 : 1;
    }
    return r;
}

long rt_test_argv(char **argv, int argc)
{
    targv T; T.v = argv; T.n = argc; T.i = 0;
    int r = tv_or(&T);
    if (T.i < T.n) return 0;      /* 还有没吃掉的 token：语法不认识，宽容判真 */
    return r;
}
