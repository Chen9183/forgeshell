/* rt_main.c —— 运行时总入口 / 解释器 / 函数表 / eval
 *
 * 【生成代码怎么进来】模板 ELF 的 main() 通过 .data 里的函数指针 rc_user_main 进入编译产物：
 *     long rc_user_main(long argc, char **argv)
 * 编译器把该指针 patch 成生成代码入口；生成代码第一件事就是调用 rc_rt_start()。
 *
 * 【argv 与 --password】产物支持运行期解锁：
 *     ./prog --password '我的 密码' arg1 arg2
 *   · 取出 --password 及其值后，把**其余参数原样**作为脚本参数分发（$1..$N）；
 *   · 未提供密码 / 密码错误 → 报错并退出（状态码 77），不执行任何脚本内容。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "rt_internal.h"

/* 由 rt_protect.c 提供 */
int  rc_protect_image_unlock(rc_image *img, const char *password);
void rc_protect_after_unlock(void);
long rc_lock_fail(const char *why);

/* ============================ 函数表 ============================ */
rt_func *rc_func_find(const char *name)
{
    for (rt_func *f = rt_g.funcs; f; f = f->next)
        if (!strcmp(f->name, name)) return f;
    return NULL;
}

long rc_func_register(const char *name, void *addr, const rc_node *body)
{
    rt_func *f = rc_func_find(name);
    if (!f) {
        f = rt_xmalloc(sizeof(*f));
        f->name = name;
        f->next = rt_g.funcs;
        rt_g.funcs = f;
    }
    f->addr = addr;
    f->body = body;
    return 0;
}

void rc_func_unregister(const char *name)
{
    rt_func **pp = &rt_g.funcs;
    while (*pp) {
        if (!strcmp((*pp)->name, name)) { *pp = (*pp)->next; return; }
        pp = &(*pp)->next;
    }
}

long rc_func_call(const char *name, rc_word **args, uint32_t nargs)
{
    rt_func *f = rc_func_find(name);
    if (!f) return 127;
    rc_words av;
    rt_words_init(&av);
    for (uint32_t i = 0; i < nargs; i++) rt_expand_word(args[i], &av, 0);
    char **argv = rt_xmalloc(sizeof(char *) * (av.n + 1));
    for (uint32_t i = 0; i < av.n; i++) argv[i] = av.v[i];
    argv[av.n] = NULL;

    rc_func_enter(name, (long)av.n, argv);
    long status;
    if (f->addr) {
        long (*fn)(long, char **) = (long (*)(long, char **))f->addr;
        status = fn((long)av.n, argv);
        long rs;
        if (rc_pending_return(&rs)) status = rs;
        status = rc_func_leave(status);
    } else {
        long rs;
        long saved = rt_g.last_status;
        status = rt_interp(f->body);
        if (rc_pending_return(&rs)) status = rs;
        status = rc_func_leave(status);
        (void)saved;
    }
    for (uint32_t i = 0; i < av.n; i++) free(argv[i]);
    free(argv);
    rt_g.last_status = status;
    return status;
}

/* set -e 用：条件上下文里失败不退出 */
/* 读整个文件：口令文件＝直接取内容（注意 echo 会多一个换行，建议 printf 写入） */
static char *rt_read_whole_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, len = 0;
    char *b = rt_xmalloc(cap);
    if (!b) { fclose(f); return NULL; }
    for (;;) {
        if (len + 1024 >= cap) { cap *= 2; b = rt_xrealloc(b, cap); }
        size_t got = fread(b + len, 1, 1024, f);
        len += got;
        if (got < 1024) break;              /* 读满说明还可能更多，读不满就到尾（管道也适用） */
    }
    fclose(f);
    b[len] = 0;
    return b;
}

void rc_cond_enter(void) { rt_g.in_cond++; }
void rc_cond_leave(void) { if (rt_g.in_cond > 0) rt_g.in_cond--; }

/* 位置参数保存栈（函数递归时逐层恢复） */
typedef struct pos_frame { char **pos; long npos; struct pos_frame *next; } pos_frame;
static pos_frame *pos_stack;

void rc_func_enter(const char *name, long argc, char **argv)
{
    (void)name;
    pos_frame *f = rt_xmalloc(sizeof(*f));
    /* ⚑ 必须深拷贝：rc_pos_set 会立刻 free 掉当前的位置参数表，
       只存指针的话函数返回后 rt_g.pos 就是悬垂指针，
       下一次函数调用/set -- 再 free 同一个数组 → 双重释放（崩在 musl get_meta）。 */
    f->npos = rt_g.npos;
    f->pos = NULL;
    if (rt_g.npos > 0) {
        f->pos = rt_xmalloc(sizeof(char *) * (size_t)rt_g.npos);
        for (long i = 0; i < rt_g.npos; i++) f->pos[i] = rt_xstrdup(rt_g.pos[i]);
    }
    f->next = pos_stack;
    pos_stack = f;
    rt_var_scope_enter();
    rc_pos_set(argc, argv, rt_g.arg0);
}

long rc_func_leave(long status)
{
    rt_var_scope_leave();
    if (pos_stack) {
        pos_frame *f = pos_stack;
        pos_stack = f->next;
        for (long i = 0; i < rt_g.npos; i++) free(rt_g.pos[i]);
        free(rt_g.pos);
        rt_g.pos = f->pos;        /* 接管深拷贝的所有权 */
        rt_g.npos = f->npos;
        free(f);
    }
    rt_g.last_status = status;
    return status;
}

/* ============================ 解释器 ============================ */
static long (*loop_check_fn)(void);
void rc_register_loop_check(void *fn) { loop_check_fn = (long (*)(void))fn; }

long rt_interp_list(const rc_node *n)
{
    long st = rt_g.last_status;
    for (const rc_node *p = n; p; p = p->next) {
        st = rt_interp(p);
        if (rt_g.pending_exit) return rt_g.exit_status;
        long lc = rc_loop_check();
        if (lc) { rt_g.loop_signal = (int)lc; return st; }   /* 留给外层循环消费 */
        if (rt_g.loop_signal) return st;    /* 内层列表已把标志吃掉，但信号还在 → 本层同样要早退 */
        long rs;
        if (rc_pending_return(&rs)) return rs;
        if (rt_g.return_seen) return st;   /* 内层已把 return 标志吃掉（&&/if 里 return）→ 本层也早退 */
    }
    return st;
}

/* 复合命令自带重定向的节点（`done < f`、`fi > f`、`esac 2>&1`…）：
   早期只有 GROUP 处理，于是这些重定向被整段丢掉。 */
static int node_has_own_redirs(int kind)
{
    return kind == RN_WHILE || kind == RN_FOR || kind == RN_IF ||
           kind == RN_CASE || kind == RN_SUBSHELL || kind == RN_GROUP
          ;
}

static long rt_interp_inner(const rc_node *n)
{
    if (!n) return 0;
    long st = 0;
    switch (n->kind) {
    case RN_SIMPLE:
        st = rt_run_simple(&n->u.simple);
        break;
    case RN_LIST:
        st = rt_interp_list(n->a);
        if (rt_g.pending_exit) return rt_g.exit_status;
        {
            long lc = rc_loop_check();
            if (lc) return st;
            long rs;
            if (rc_pending_return(&rs)) return rs;
        }
        st = rt_interp_list(n->b);
        break;
    case RN_ANDOR:
        st = rt_interp_list(n->a);
        if (n->sub == RC_AND) { if (st == 0) st = rt_interp_list(n->b); }
        else                  { if (st != 0) st = rt_interp_list(n->b); }
        break;
    case RN_PIPE:
        st = rt_run_pipeline(n);
        break;
    case RN_NOT:
        st = rt_interp_list(n->a);
        st = st ? 0 : 1;
        break;
    case RN_BG:
        st = rc_bg_launch(n->a);
        break;
    case RN_IF: {
        rc_cond_enter();
        st = rt_interp_list(n->a);
        rc_cond_leave();
        if (st == 0) st = rt_interp_list(n->b);
        else if (n->c) st = rt_interp_list(n->c);
        else st = 0;                 /* 无 else 且条件为假 → 0（POSIX） */
        break;
    }
    case RN_WHILE: {
        int guard = 0, ran = 0;
        for (;;) {
            if (++guard > 100000000) break;
            rc_cond_enter();                 /* 条件里的失败不该触发 set -e */
            st = rt_interp_list(n->a);
            rc_cond_leave();
            if (n->sub == RC_WHILE ? (st != 0) : (st == 0)) { if (!ran) st = 0; break; }
            if (n->sub == RC_WHILE ? (st != 0) : (st == 0)) break;
            st = rt_interp_list(n->b);
            ran = 1;
            long lc = rt_g.loop_signal; rt_g.loop_signal = 0;
            if (lc == 1) break;
            if (rt_g.pending_exit) return rt_g.exit_status;
            long rs;
            if (rc_pending_return(&rs)) return rs;
        }
        break;
    }
    case RN_FOR: {
        rc_words ws;
        rc_for_words(&n->u.forc, &ws);
        st = 0;
        for (uint32_t i = 0; i < ws.n; i++) {
            rc_var_set(n->u.forc.name, ws.v[i]);
            st = rt_interp_list(n->u.forc.body);
            long lc = rt_g.loop_signal; rt_g.loop_signal = 0;
            if (lc == 1) break;
            if (rt_g.pending_exit) break;
            long rs;
            if (rc_pending_return(&rs)) { st = rs; break; }
        }
        rt_words_free(&ws);
        break;
    }
    case RN_CASE: {
        char *subj = rc_expand_word_literal(n->u.casec.word);
        st = 0;
        int matched = 0;
        for (rc_case_item *it = n->u.casec.items; it; it = it->next) {
            rc_words pats;
            rt_words_init(&pats);
            for (uint32_t k = 0; k < it->npats; k++) {
                char *p = rc_expand_word_literal(it->pats[k]);
                int hit = rc_pattern_match(p, subj, 0);
                free(p);
                if (hit) {
                    matched = 1;
                    st = rt_interp(it->body);
                    if (it->retest) { matched = 0; continue; }
                    break;
                }
            }
            rt_words_free(&pats);
            if (matched) break;
        }
        free(subj);
        break;
    }
    case RN_SUBSHELL: {
        long r = rc_subshell_enter();
        if (r != 0) { st = rt_g.last_status; break; }
        st = rt_interp_list(n->a);
        rc_subshell_leave(st);
        break;
    }
    case RN_GROUP:
        st = rt_interp_list(n->a);
        break;
    case RN_FUNC:
        rc_func_register(n->u.func.name, NULL, n->u.func.body);
        st = 0;
        break;
    case RN_ARITH_CMD: {
        long v = 0;
        if (rt_arith_eval(n->u.arith, &v) != 0) st = 1;
        else st = v ? 0 : 1;
        break;
    }
    case RN_TEST_CMD:
        st = rt_test_node(n->u.test);
        break;
    default:
        st = 1;
        break;
    }
    rt_g.last_status = st;
    rc_trap_check();
    return st;
}

long rt_interp(const rc_node *n)
{
    if (!n) return 0;
    int wrap = (n->redirs && node_has_own_redirs(n->kind));
    if (wrap) rt_apply_redirs(n->redirs);
    long st = rt_interp_inner(n);
    if (wrap) rt_restore_redirs();
    return st;
}

long rc_exec(const rc_node *n)
{
    if (!n) return 0;
    if (n->kind == RN_SIMPLE) return rt_run_simple(&n->u.simple);
    if (n->kind == RN_PIPE) return rt_run_pipeline(n);
    return rt_interp(n);
}

/* eval / source 用：运行时解析再解释 */
long rc_eval_string(const char *code, const char *name)
{
    if (!code) return 0;
    char *err = NULL;
    rc_node *n = rc_parse_string(code, name ? name : "eval", &err);
    if (!n) {
        if (err) { fprintf(stderr, "s2a: %s: %s\n", name ? name : "eval", err); free(err); }
        return 2;
    }
    /* ⚑ 必须用 rt_interp_list：解析出来的根是**语句链**（next 串起来），
       用 rt_interp 只会跑第一条 —— `eval "echo A; exit 5"` 里的 exit 就这么被丢掉的。 */
    long st = rt_interp_list(n);
    rc_node_free_runtime(n);
    return st;
}

char *rc_expand_test_str(const rc_word *w) { return rc_expand_word_literal(w); }

long rc_unsupported(const char *what, uint32_t line)
{
    fprintf(stderr, "s2a: 未支持的语法/特性: %s（源行 %u）\n", what ? what : "?", line);
    return 2;
}

/* ============================ 启动 / 收尾 ============================ */
long rc_rt_start(long argc, char **argv, const rc_image *img)
{
    memset(&rt_g, 0, sizeof rt_g);
    rt_g.ifs = " \t\n";
    rt_g.shell_pid = (long)getpid();
    /* POSIX：非交互 shell 自己忽略 SIGINT/SIGQUIT（^C 只该打断前台命令，不该打死 shell）。
       交互式（stdin 是终端）才需要默认处置。trap 会随后覆盖这里的设置。 */
    if (!isatty(0)) { signal(SIGINT, SIG_IGN); signal(SIGQUIT, SIG_IGN); }     /* $$ 保持调用者 pid（fork 后不变，POSIX） */
    rt_g.level = 0;
    rt_g.image = (rc_image *)img;

    /* 1) 命令行：取出 --password，其余原样分发给脚本 */
    char **args = rt_xmalloc(sizeof(char *) * (size_t)(argc > 0 ? argc + 1 : 1));
    long nargs = 0;
    const char *pw = NULL;
    const char *a0 = (argc > 0) ? argv[0] : "s2a";
    for (long i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--password") || !strcmp(a, "-p")) {
            if (i + 1 < argc) { pw = argv[++i]; continue; }
            fprintf(stderr, "s2a: --password 需要一个参数\n");
            return 70;
        }
        if (!strncmp(a, "--password=", 11)) { pw = a + 11; continue; }
        if (!strcmp(a, "--password-env") || !strcmp(a, "--password_env")) {   /* 从环境变量读 */
            if (i + 1 < argc) {
                const char *v = getenv(argv[++i]);
                if (!v || !*v) { fprintf(stderr, "s2a: 环境变量 %s 未设置或为空\n", argv[i]); return 70; }
                pw = v; continue;
            }
            fprintf(stderr, "s2a: --password-env 需要一个变量名\n"); return 70;
        }
        if (!strcmp(a, "--password-file") || !strcmp(a, "--password_file")) {  /* 从文件读（内容原样＝cat） */
            if (i + 1 < argc) {
                char *f = rt_read_whole_file(argv[++i]);
                if (!f) { fprintf(stderr, "s2a: 读不到口令文件 %s\n", argv[i]); return 70; }
                pw = f; continue;
            }
            fprintf(stderr, "s2a: --password-file 需要一个文件名\n"); return 70;
        }
        if (!strcmp(a, "--")) {           /* -- 之后全部是脚本参数 */
            for (long k = i + 1; k < argc; k++) args[nargs++] = argv[k];
            break;
        }
        args[nargs++] = argv[i];
    }

    /* 1b) 环境变量兜底：S2A_PASSWORD */
    if (!pw) { const char *e = getenv("S2A_PASSWORD"); if (e && *e) pw = e; }

    /* 2) 防护子系统：解密字符串池 / 校验密码 / 完整性基线 */
    if (img) {
        int r = rc_protect_image_unlock((rc_image *)img, pw);
        if (r != 0) return 70;            /* 密码缺失或错误，rc_protect 已打印原因 */
    }

    /* 3) 基本变量与位置参数 */
    rc_pos_set(nargs, args, a0);
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd)) { rc_var_set("PWD", cwd); setenv("PWD", cwd, 1); }
    char ppidb[32]; snprintf(ppidb, sizeof ppidb, "%ld", (long)getppid());
    rc_var_set("PPID", ppidb);
    rc_var_set("IFS", rt_g.ifs);
    {
        char b[32]; snprintf(b, sizeof b, "%ld", (long)getpid());
        rt_var *v = rt_var_lookup("$");
        if (!v) v = rt_var_define("$", 0);
        rt_var_assign(v, b);
    }

    /* 非交互程序：忽略“后台写终端/读终端”导致的停止信号，
     * 否则子进程（或自己）会被 SIGTTOU/SIGTTIN 停住，waitpid 永远等不到 → 卡死 */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    /* POSIX：环境变量同时也是 shell 变量（否则脚本里 $PATH/$HOME 全是空） */
    {
        extern char **environ;
        for (int i = 0; environ && environ[i]; i++) {
            const char *eq = strchr(environ[i], '=');
            if (!eq || eq == environ[i]) continue;
            size_t nl = (size_t)(eq - environ[i]);
            if (nl == 0 || nl > 128) continue;
            char name[160];
            memcpy(name, environ[i], nl);
            name[nl] = 0;
            if (!rt_word_is_name(name)) continue;
            rc_var_set(name, eq + 1);
            rt_var *v = rt_var_lookup(name);
            if (v) v->flags |= RC_VF_EXPORT;
        }
    }

    /* PATH 为空时补一个合理的默认值（安卓宿主上常为空，导致 date/tr 之类都找不到） */
    if (!getenv("PATH") || !getenv("PATH")[0]) {
        const char *def = "/system/bin:/system/xbin:/vendor/bin:/vendor/xbin:/odm/bin:"
                          "/system_ext/bin:/product/bin:/apex/com.android.runtime/bin:"
                          "/data/adb/magisk:/data/local/tmp:/usr/local/bin:/usr/bin:/bin:"
                          "/usr/local/sbin:/usr/sbin:/sbin";
        setenv("PATH", def, 1);
        rc_var_set("PATH", def);
    }

    if (img) rc_protect_after_unlock();
    return 0;
}

void rc_rt_exit(long status)
{
    rc_trap_run_exit(status);
    /* EXIT trap 里如果自己 exit N，最终退出码用 N（bash 同） */
    if (rt_g.pending_exit) status = rt_g.exit_status;
    fflush(NULL);
    _exit((int)(status & 0xFF));
}

const rc_image *rc_get_image(void) { return rt_g.image; }

/* ============================ 编译器用的小入口 ============================ */
/* `if 假条件; then ...; fi`（无 else）→ 状态 0（POSIX） */
long rc_status_zero(void) { rt_g.last_status = 0; return 0; }

/* return 不在函数里、也不是 source 进来的 → 报错并给状态 2（不中断脚本） */
int rc_in_function(void);

long rc_mark_loop_ran(void) { rt_g.loop_ran = 1; return 0; }

/* while/until 一次都没执行 → 状态 0（POSIX）；执行过就保留循环体状态 */
long rc_status_zero_if_loop_never_ran(void)
{
    if (rt_g.loop_ran == 0) rt_g.last_status = 0;
    rt_g.loop_ran = 0;
    return rt_g.last_status;
}

long rc_last_status_get(void)
{
    /* 编译产物的「语句之间」安全点：挂起的 trap 动作在这里跑。
       trap 动作不改本语句的 $?，所以读完再还原。 */
    long st = rt_g.last_status;
    rc_trap_check();
    rt_g.last_status = st;
    return st;
}

long rc_not_status(void)
{
    rt_g.last_status = rt_g.last_status ? 0 : 1;
    return rt_g.last_status;
}

long rc_arith_cmd(const rc_arith *a)
{
    long v = 0;
    if (rt_arith_eval(a, &v) != 0) { rt_g.last_status = 1; return 1; }
    rt_g.last_status = v ? 0 : 1;
    return rt_g.last_status;
}

long rc_check_return(void);

/* for 循环迭代：词表在运行时侧展开并压栈，生成代码用 begin/next/end 驱动 */
typedef struct { rc_words w; uint32_t i; const char *name; } forframe;
static forframe forstack[32];
static int forsp;

long rc_for_begin(const rc_for *f)
{
    if (forsp >= 32) { fprintf(stderr, "s2a: for 嵌套过深\n"); rt_g.last_status = 1; return 1; }
    forframe *fr = &forstack[forsp++];
    rt_words_init(&fr->w);
    if (f) rc_for_words(f, &fr->w);
    fr->i = 0;
    fr->name = f ? f->name : NULL;
    return 0;
}

long rc_for_next(void)
{
    if (forsp <= 0) return 0;
    forframe *fr = &forstack[forsp - 1];
    if (fr->i >= fr->w.n) return 0;
    rc_var_set(fr->name ? fr->name : "i", fr->w.v[fr->i]);
    fr->i++;
    return 1;
}

long rc_for_end(void)
{
    if (forsp > 0) { rt_words_free(&forstack[--forsp].w); }
    return 0;
}

/* case 项匹配：把 subject 与该项的每个模式依次比对（fnmatch 语义） */
long rc_case_item_match(const rc_word *subject, const rc_case_item *it)
{
    if (!it) return 0;
    char *subj = rc_expand_word_literal(subject);
    long hit = 0;
    for (uint32_t i = 0; i < it->npats; i++) {
        char *p = rc_expand_word_literal(it->pats[i]);
        if (rc_pattern_match(p, subj, 0)) { hit = 1; free(p); break; }
        free(p);
    }
    free(subj);
    return hit;
}

/* 让 rt_protect.c 也能拿到 exit 路径 */
void rc_rt_exit_public(long status) { rc_rt_exit(status); }
