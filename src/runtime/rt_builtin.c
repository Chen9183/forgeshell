/* rt_builtin.c —— 内建命令
 *
 * 设计要点：
 *   · break/continue/return 不做栈展开，而是写“待处理标志”，由编译出来的循环/函数
 *     在每条语句后调用 rc_loop_check()/rc_pending_return() 取走（解释器路径同样处理）。
 *     —— 这样生成代码里不需要 setjmp/longjmp，语义却一致。
 *   · exit 直接走 rc_rt_exit()（进程结束，EXIT trap 在其中执行）。
 *   · 内建命令都返回 shell 状态码。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/times.h>

#include "rt_internal.h"
#include "../common/parse.h"

typedef long (*fn_t)(int argc, char **argv);

/* ---------- 退出/流程控制标志 ---------- */
long rt_pending_break_val;    /* 0 无；>0 break n；<0 continue n */
static long pending_return_flag;
static long pending_return_status;

long rc_loop_check(void)
{
    if (rt_pending_break_val == 0) return 0;
    long v = rt_pending_break_val;
    rt_pending_break_val = 0;
    return v > 0 ? 1 : 2;    /* 1 = break，2 = continue */
}

long rc_loop_depth_aware(long handled)
{
    if (!handled) return 0;
    return 0;
}

long rc_pending_return(long *status)
{
    if (!pending_return_flag) return 0;
    pending_return_flag = 0;
    if (status) *status = pending_return_status;
    return 1;
}

long rc_check_return(void)
{
    return pending_return_flag ? 1 : 0;      /* 不消费标志：状态由 rc_take_return_status 取走 */
}

long rc_take_return_status(void)
{
    if (pending_return_flag) {
        pending_return_flag = 0;
        rt_g.last_status = pending_return_status;
        return pending_return_status;
    }
    return rt_g.last_status;
}

static long b_true(int argc, char **argv)  { (void)argc; (void)argv; return 0; }
static long b_false(int argc, char **argv) { (void)argc; (void)argv; return 1; }

static long b_echo(int argc, char **argv)
{
    int nl = 1, esc = 0;
    (void)0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-') break;
        const char *p = argv[i] + 1;
        if (!*p) break;
        int ok = 1;
        for (const char *q = p; *q; q++)
            if (*q != 'n' && *q != 'e' && *q != 'E') { ok = 0; break; }
        if (!ok) break;
        for (const char *q = p; *q; q++) {
            if (*q == 'n') nl = 0;
            else if (*q == 'e') esc = 1;
            else if (*q == 'E') esc = 0;
        }
    }
    int first = 1;
    for (; i < argc; i++) {
        if (!first) putchar(' ');
        first = 0;
        if (esc) {
            for (const char *p = argv[i]; *p; p++) {
                if (*p != '\\') { putchar(*p); continue; }
                p++;
                switch (*p) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                case 'b': putchar('\b'); break;
                case 'a': putchar('\a'); break;
                case 'f': putchar('\f'); break;
                case 'v': putchar('\v'); break;
                case '\\': putchar('\\'); break;
                case 'c': nl = 0; goto done;
                case '0': {
                    int v = 0, k = 0;
                    while (k < 3 && p[1] >= '0' && p[1] <= '7') { v = v * 8 + (p[1] - '0'); p++; k++; }
                    putchar(v); break;
                }
                case 0: p--; break;
                default: putchar('\\'); putchar(*p); break;
                }
            }
        } else {
            fputs(argv[i], stdout);
        }
    }
done:
    if (nl) putchar('\n');
    if (fflush(stdout) != 0 || ferror(stdout)) {   /* 写失败（如 fd 已关闭）→ 非 0 */
        clearerr(stdout);
        return 1;
    }
    return 0;
}

/* ---------- printf ---------- */
static long b_printf(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "printf: 用法: printf 格式 [参数...]\n"); return 2; }
    const char *fmt = argv[1];
    char outbuf[65536];
    size_t olen = 0;
    int argi = 2;
    int used = 0;                        /* 本轮是否消费过参数 */
    const char *p = fmt;

    for (;;) {
        if (!*p) {                       /* 走到格式串尾：还有参数就整串重来 */
            if (argi < argc && used) { p = fmt; used = 0; continue; }
            break;
        }
        /* 格式串里的反斜杠转义必须处理（POSIX）：早期逐字拷贝，`printf '%s\n'` 会真的
           打印出反斜杠 n，脚本里最常见的换行就全废了。 */
        if (*p == '\\' && p[1]) {
            p++;
            char c;
            switch (*p) {
            case 'n': c = '\n'; p++; break;
            case 't': c = '\t'; p++; break;
            case 'r': c = '\r'; p++; break;
            case 'a': c = '\a'; p++; break;
            case 'b': c = '\b'; p++; break;
            case 'f': c = '\f'; p++; break;
            case 'v': c = '\v'; p++; break;
            case '\\': c = '\\'; p++; break;
            case '"': c = '"'; p++; break;
            case '\'': c = '\''; p++; break;
            case 'x': { p++; int v = 0, k = 0;
                        while (k < 2 && isxdigit((unsigned char)*p)) {
                            v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0'
                                         : tolower((unsigned char)*p) - 'a' + 10);
                            p++; k++;
                        }
                        c = (char)v; break; }
            default:
                if (*p >= '0' && *p <= '7') { int v = 0, k = 0;
                    while (k < 3 && *p >= '0' && *p <= '7') { v = v * 8 + (*p - '0'); p++; k++; }
                    c = (char)v;
                } else { c = *p; p++; }
                break;
            }
            outbuf[olen++] = c;
            if (olen > 65000) { fwrite(outbuf, 1, olen, stdout); olen = 0; }
            continue;
        }
        if (*p != '%') { outbuf[olen++] = *p++; if (olen > 65000) { fwrite(outbuf, 1, olen, stdout); olen = 0; } continue; }
        p++;
        if (*p == '%') { outbuf[olen++] = '%'; p++; continue; }
        /* 宽度/精度 */
        int left = 0;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
            if (*p == '-') left = 1;
            p++;
        }
        int width = 0;
        if (*p == '*') { width = argi < argc ? atoi(argv[argi++]) : 0; p++; }
        else while (isdigit((unsigned char)*p)) { width = width * 10 + (*p - '0'); p++; }
        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') { prec = argi < argc ? atoi(argv[argi++]) : 0; p++; }
            else while (isdigit((unsigned char)*p)) { prec = prec * 10 + (*p - '0'); p++; }
        }
        char conv = *p;
        if (!conv) break;
        p++;
        if (argi < argc) used = 1;
        const char *a = (argi < argc) ? argv[argi++] : "";
        char tmp[8192];
        int n = 0;
        switch (conv) {
        case 's': n = snprintf(tmp, sizeof tmp, "%.*s", prec >= 0 ? prec : (int)strlen(a), a); break;
        case 'b': {
            /* %b：把参数里的反斜杠转义展开（POSIX printf 扩展） */
            size_t k = 0;
            for (const char *q = a; *q && k + 2 < sizeof tmp; q++) {
                if (*q != '\\') { tmp[k++] = *q; continue; }
                q++;
                switch (*q) {
                case 'n': tmp[k++] = '\n'; break;
                case 't': tmp[k++] = '\t'; break;
                case 'r': tmp[k++] = '\r'; break;
                case '\\': tmp[k++] = '\\'; break;
                case '0': {
                    int v = 0, c = 0;
                    while (c < 3 && q[1] >= '0' && q[1] <= '7') { v = v * 8 + (q[1] - '0'); q++; c++; }
                    tmp[k++] = (char)v; break;
                }
                case 0: q--; break;
                default: tmp[k++] = *q; break;
                }
            }
            tmp[k] = 0; n = (int)k;
            break;
        }
        case 'q': {
            size_t k = 0;
            tmp[k++] = '\'';
            for (const char *q = a; *q && k + 5 < sizeof tmp; q++) {
                if (*q == '\'') { tmp[k++] = '\''; tmp[k++] = '\\'; tmp[k++] = '\''; tmp[k++] = '\''; }
                else tmp[k++] = *q;
            }
            tmp[k++] = '\''; tmp[k] = 0; n = (int)k;
            break;
        }
        case 'c': tmp[0] = a[0] ? a[0] : 0; tmp[1] = 0; n = tmp[0] ? 1 : 0; break;
        case 'd': case 'i': n = snprintf(tmp, sizeof tmp, "%lld", (long long)(a[0] ? strtoll(a, NULL, 0) : 0)); break;
        case 'u': n = snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)(a[0] ? strtoll(a, NULL, 0) : 0)); break;
        case 'o': n = snprintf(tmp, sizeof tmp, "%llo", (unsigned long long)(a[0] ? strtoll(a, NULL, 0) : 0)); break;
        case 'x': n = snprintf(tmp, sizeof tmp, "%llx", (unsigned long long)(a[0] ? strtoll(a, NULL, 0) : 0)); break;
        case 'X': n = snprintf(tmp, sizeof tmp, "%llX", (unsigned long long)(a[0] ? strtoll(a, NULL, 0) : 0)); break;
        default: tmp[0] = conv; tmp[1] = 0; n = 1; break;
        }
        /* 宽度填充 */
        if (width > n) {
            int pad = width - n;
            if (!left) while (pad-- > 0 && olen < 65000) outbuf[olen++] = ' ';
            for (int k = 0; k < n && olen < 65000; k++) outbuf[olen++] = tmp[k];
            if (left) while (pad-- > 0 && olen < 65000) outbuf[olen++] = ' ';
        } else {
            for (int k = 0; k < n && olen < 65000; k++) outbuf[olen++] = tmp[k];
        }
        if (olen > 60000) { fwrite(outbuf, 1, olen, stdout); olen = 0; }
    }
    if (olen) fwrite(outbuf, 1, olen, stdout);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        clearerr(stdout);
        return 1;
    }
    return 0;
}

/* ---------- cd / pwd ---------- */
/* execv/execvp 要求 argv 以 NULL 结尾，而内建拿到的 argv 没有结尾（越界会多读一个参数） */
static char **argv_terminated(char **argv, int argc)
{
    char **av = rt_xmalloc(sizeof(char *) * (size_t)(argc + 1));
    for (int i = 0; i < argc; i++) av[i] = argv[i];
    av[argc] = NULL;
    return av;
}

/* 变量表优先、环境兜底：CDPATH/HOME/OLDPWD 未必被 export */
static const char *rt_var_or_env(const char *name)
{
    const char *v = rt_var_get_str(name);
    if (v && *v) return v;
    v = getenv(name);
    return (v && *v) ? v : NULL;
}

static long b_cd(int argc, char **argv)
{
    int i = 1, P = 0;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-P")) { P = 1; continue; }
        if (!strcmp(argv[i], "-L")) { P = 0; continue; }
        break;
    }
    const char *oldpwd = rt_var_or_env("PWD");
    const char *dir;
    if (i >= argc) {
        dir = rt_var_or_env("HOME");
        if (!dir || !*dir) { fprintf(stderr, "cd: 未设置 HOME\n"); return 1; }
    } else if (!strcmp(argv[i], "-")) {
        dir = rt_var_or_env("OLDPWD");
        if (!dir || !*dir) { fprintf(stderr, "cd: OLDPWD 未设置\n"); return 1; }
        printf("%s\n", dir);                       /* POSIX：cd - 会打印新目录 */
    } else {
        dir = argv[i];
        if (i + 1 < argc) { fprintf(stderr, "cd: 参数过多\n"); return 2; }
    }
    char full[4096];
    int ok = 0;
    /* CDPATH：相对路径（不以 . / .. 开头）先依次在 CDPATH 目录里试 */
    if (dir[0] != '/' && strcmp(dir, ".") && strcmp(dir, "..") &&
        strncmp(dir, "./", 2) && strncmp(dir, "../", 3)) {
        const char *cdp = rt_var_or_env("CDPATH");
        if (cdp && *cdp) {
            const char *p = cdp;
            while (*p && !ok) {
                const char *colon = strchr(p, ':');
                size_t n = colon ? (size_t)(colon - p) : strlen(p);
                if (n == 0) snprintf(full, sizeof full, "%s", dir);
                else snprintf(full, sizeof full, "%.*s/%s", (int)n, p, dir);
                if (chdir(full) == 0) { ok = 1; printf("%s\n", full); break; }
                if (!colon) break;
                p = colon + 1;
            }
        }
    }
    if (!ok && chdir(dir) != 0) {
        fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }
    (void)P;
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) snprintf(cwd, sizeof cwd, "%s", dir);
    if (oldpwd) { rc_var_set("OLDPWD", oldpwd); setenv("OLDPWD", oldpwd, 1); }
    rc_var_set("PWD", cwd);
    setenv("PWD", cwd, 1);
    return 0;
}

static long b_pwd(int argc, char **argv)
{
    (void)argc; (void)argv;
    const char *p = getenv("PWD");
    char cwd[4096];
    if (!p || p[0] != '/') p = getcwd(cwd, sizeof cwd) ? cwd : "/";
    puts(p);
    return 0;
}

/* ---------- export / readonly / unset / local ---------- */
static long b_export(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "-p"))) {
        for (rt_var *v = rt_g.vars; v; v = v->next)
            if (v->flags & RC_VF_EXPORT)
                printf("export %s=%s\n", v->name, v->value ? v->value : "");
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *name = rt_xstrndup(argv[i], (size_t)(eq - argv[i]));
            rc_var_set(name, eq + 1);
            rc_var_export(name, 1);
            free(name);
        } else {
            rt_var *v = rt_var_lookup(argv[i]);
            if (!v) v = rt_var_define(argv[i], rt_g.level);
            v->flags |= RC_VF_EXPORT;
            if (v->value) setenv(v->name, v->value, 1);
        }
    }
    return 0;
}

static long b_readonly(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "-p")) {
        for (rt_var *v = rt_g.vars; v; v = v->next)
            if (v->flags & RC_VF_READONLY)
                printf("readonly %s=%s\n", v->name, v->value ? v->value : "");
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *name = rt_xstrndup(argv[i], (size_t)(eq - argv[i]));
            rc_var_set(name, eq + 1);
            rc_var_mark_readonly(name, 1);
            free(name);
        } else {
            rc_var_mark_readonly(argv[i], 1);
        }
    }
    return 0;
}

static long b_unset(int argc, char **argv)
{
    int funcs = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f")) { funcs = 1; continue; }
        if (!strcmp(argv[i], "-v")) { funcs = 0; continue; }
        if (funcs) rc_func_unregister(argv[i]);
        else { rc_var_unset(argv[i]); unsetenv(argv[i]); }
    }
    return 0;
}

static long b_local(int argc, char **argv)
{
    if (rt_g.level == 0) {
        fprintf(stderr, "local: 只能在函数内使用\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char *name = rt_xstrndup(argv[i], (size_t)(eq - argv[i]));
            rc_var_set_local(name, eq + 1);
            free(name);
        } else {
            rc_var_set_local(argv[i], "");
        }
    }
    return 0;
}

static long b_shift(int argc, char **argv)
{
    long n = 1;
    if (argc > 1) n = strtol(argv[1], NULL, 10);
    if (n > rt_g.npos) return 1;
    rc_pos_shift(n);
    return 0;
}

static long b_exit(int argc, char **argv)
{
    long st = rt_g.last_status;
    if (argc > 1) st = strtol(argv[1], NULL, 10);
    rc_rt_exit(st);
    return 0;   /* 不会到达 */
}

static long b_return(int argc, char **argv)
{
    long st = rt_g.last_status;
    if (argc > 1) st = strtol(argv[1], NULL, 10);
    if (rt_g.level == 0 && !rt_g.in_source) {
        /* POSIX：return 只在函数或 source 的文件里有定义；顶层按 bash 的做法报错并给状态 2，
           不中断脚本（以前直接 rc_rt_exit，把整个脚本/命令替换都终止了）。 */
        fprintf(stderr, "s2a: return: 只能在函数或 source 的文件里使用\n");
        return 2;
    }
    pending_return_flag = 1;
    pending_return_status = st;
    rt_g.return_seen = 1;        /* 给解释执行的函数体收尾用（rc_pending_return 会被内层消费） */
    rt_g.return_seen_status = st;
    return st;
}

static long b_break(int argc, char **argv)
{
    long n = argc > 1 ? strtol(argv[1], NULL, 10) : 1;
    if (n < 1) n = 1;
    rt_pending_break_val = n;
    return 0;
}

static long b_continue(int argc, char **argv)
{
    long n = argc > 1 ? strtol(argv[1], NULL, 10) : 1;
    if (n < 1) n = 1;
    rt_pending_break_val = -n;
    return 0;
}

/* `set -o` 的选项名 ↔ 内部字段 */
static const struct { const char *n; int off; } set_opts[] = {
    {"allexport", 1}, {"errexit", 2}, {"ignoreeof", 3}, {"monitor", 4},
    {"noclobber", 5}, {"noexec", 6}, {"noglob", 7}, {"nolog", 8},
    {"notify", 9}, {"nounset", 10}, {"verbose", 11}, {"vi", 12}, {"xtrace", 13},
    {NULL, 0}
};

static uint8_t *set_opt_field(int id)
{
    switch (id) {
    case 1: return &rt_g.opt_a;
    case 2: return &rt_g.opt_e;
    case 3: return NULL;          /* ignoreeof：非交互无意义 */
    case 4: return &rt_g.opt_m;
    case 5: return &rt_g.opt_C;
    case 6: return &rt_g.opt_n;
    case 7: return &rt_g.opt_f;
    case 8: return NULL;          /* nolog */
    case 9: return &rt_g.opt_b;
    case 10: return &rt_g.opt_u;
    case 11: return &rt_g.opt_v;
    case 12: return NULL;         /* vi */
    case 13: return &rt_g.opt_x;
    default: return NULL;
    }
}

static int set_opt_id(const char *name)
{
    for (int i = 0; set_opts[i].n; i++) if (!strcmp(set_opts[i].n, name)) return set_opts[i].off;
    return -1;
}

static void set_opt_on(int id, int on)
{
    uint8_t *f = set_opt_field(id);
    if (f) *f = (uint8_t)on;
}

static void set_print_o(int reusable)
{
    for (int i = 0; set_opts[i].n; i++) {
        uint8_t *f = set_opt_field(set_opts[i].off);
        int on = f ? (*f != 0) : 0;
        if (reusable) printf("set %so %s\n", on ? "-" : "+", set_opts[i].n);
        else printf("%-12s %s\n", set_opts[i].n, on ? "on" : "off");
    }
}

static long b_set(int argc, char **argv)
{
    if (argc == 1) {
        for (rt_var *v = rt_g.vars; v; v = v->next)
            printf("%s=%s\n", v->name, v->value ? v->value : "");
        return 0;
    }
    int i = 1;
    int clear_rest = 0;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' && a[0] != '+') break;
        if (!a[1]) {                       /* 单独的 `-`：关掉 -v -x */
            rt_g.opt_v = rt_g.opt_x = 0;
            continue;
        }
        if (!strcmp(a, "--")) { clear_rest = 1; i++; break; }
        int on = (a[0] == '-');
        if (!strcmp(a + 1, "o") || !strcmp(a + 1, "o") ) { /* 占位，下面统一处理 */ }
        if (a[1] == 'o' && !a[2]) {        /* set -o [name] / set +o */
            if (i + 1 >= argc) {
                if (on) set_print_o(0);     /* set -o：列表 */
                else    set_print_o(1);     /* set +o：可复用形式 */
                continue;
            }
            int id = set_opt_id(argv[i + 1]);
            if (id < 0) { fprintf(stderr, "set: %s: 无效的选项名\n", argv[i + 1]); return 2; }
            set_opt_on(id, on);
            i++;
            continue;
        }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
            case 'a': rt_g.opt_a = (uint8_t)on; break;
            case 'b': rt_g.opt_b = (uint8_t)on; break;
            case 'C': rt_g.opt_C = (uint8_t)on; break;
            case 'e': rt_g.opt_e = (uint8_t)on; break;
            case 'f': rt_g.opt_f = (uint8_t)on; break;
            case 'h': break;                       /* hashall：我们不用哈希表，接受即可 */
            case 'm': rt_g.opt_m = (uint8_t)on; break;
            case 'n': rt_g.opt_n = (uint8_t)on; break;
            case 'u': rt_g.opt_u = (uint8_t)on; break;
            case 'v': rt_g.opt_v = (uint8_t)on; break;
            case 'x': rt_g.opt_x = (uint8_t)on; break;
            default:
                fprintf(stderr, "set: -%c: 无效的选项\n", *p);
                return 2;
            }
        }
    }
    /* `set --`（没有操作数）也必须清空位置参数 */
    if (i < argc || clear_rest) rc_pos_set(argc - i > 0 ? argc - i : 0, argv + i, rt_g.arg0);
    return 0;
}

static long b_times(int argc, char **argv)
{
    (void)argc; (void)argv;
    struct tms t;
    if (times(&t) == (clock_t)-1) return 1;
    long hz = 100;
    printf("%ldm%ld.%02lds %ldm%ld.%02lds\n",
           (long)(t.tms_utime / hz / 60), (long)(t.tms_utime / hz % 60), (long)(t.tms_utime % hz),
           (long)(t.tms_stime / hz / 60), (long)(t.tms_stime / hz % 60), (long)(t.tms_stime % hz));
    return 0;
}

static long b_umask(int argc, char **argv)
{
    if (argc < 2) {
        mode_t m = umask(0);
        umask(m);
        printf("%04o\n", (unsigned)m);
        return 0;
    }
    umask((mode_t)strtol(argv[1], NULL, 8));
    return 0;
}

static long b_type(int argc, char **argv)
{
    long st = 0;
    for (int i = 1; i < argc; i++) {
        if (rc_builtin_lookup(argv[i])) printf("%s 是 shell 内建命令\n", argv[i]);
        else if (rc_func_find(argv[i])) printf("%s 是函数\n", argv[i]);
        else {
            const char *path = getenv("PATH");
            char *paths = rt_xstrdup(path ? path : "/bin:/usr/bin");
            char *save = NULL, *d;
            int found = 0;
            for (d = strtok_r(paths, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
                char full[4096];
                snprintf(full, sizeof full, "%s/%s", d, argv[i]);
                if (access(full, X_OK) == 0) { printf("%s 是 %s\n", argv[i], full); found = 1; break; }
            }
            free(paths);
            if (!found) { printf("type: %s: 未找到\n", argv[i]); st = 1; }
        }
    }
    return st;
}

static long b_command(int argc, char **argv)
{
    int i = 1;
    if (i < argc && argv[i][0] == '-' && (argv[i][1] == 'v' || argv[i][1] == 'V') && !argv[i][2]) {
        int verbose = (argv[i][1] == 'V');
        i++;
        long st = 0;
        for (; i < argc; i++) {
            if (rc_func_find(argv[i])) {
                if (verbose) printf("%s: 函数\n", argv[i]); else printf("%s\n", argv[i]);
                continue;
            }
            if (rc_builtin_lookup(argv[i])) {
                if (verbose) printf("%s: 内建命令\n", argv[i]); else printf("%s\n", argv[i]);
                continue;
            }
            char *p = rt_which(argv[i]);
            if (p) { printf("%s\n", p); free(p); }
            else { st = 1; if (verbose) fprintf(stderr, "command: %s: 未找到\n", argv[i]); }
        }
        return st;
    }
    if (i < argc && !strcmp(argv[i], "-p")) i++;
    if (i >= argc) return 0;
    char **av = argv_terminated(argv + i, argc - i);
    long st = rt_run_program(av, argc - i);
    free(av);
    return st;
}

static long b_read(int argc, char **argv)
{
    int i = 1, raw = 0, nchars = -1;
    char delim = '\n';
    const char *prompt = NULL;
    const char *names[64];
    int nnames = 0;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) { raw = 1; continue; }
        if (!strcmp(argv[i], "-p") && i + 1 < argc) { prompt = argv[++i]; continue; }
        if (!strcmp(argv[i], "-n") && i + 1 < argc) { nchars = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "-d") && i + 1 < argc) { delim = argv[++i][0]; continue; }
        names[nnames++] = argv[i];
    }
    if (prompt) { fputs(prompt, stderr); fflush(stderr); }
    char buf[65536];
    size_t len = 0;
    while (nchars < 0 || (long)len < nchars) {
        char c;
        ssize_t k = read(0, &c, 1);
        if (k <= 0) break;
        if (c == delim && nchars < 0) break;
        if (!raw && c == '\\') {
            char c2;
            if (read(0, &c2, 1) <= 0) break;
            if (c2 == '\n') continue;   /* 行继续 */
            c = c2;
        }
        if (len + 1 < sizeof buf) buf[len++] = c;
    }
    buf[len] = 0;
    if (len == 0 && nchars < 0) return 1;   /* EOF */

    if (nnames == 0) { rc_var_set("REPLY", buf); return 0; }
    /* 按 IFS 切分（最后一个变量拿走剩余部分，POSIX 语义） */
    const char *ifs = rt_g.ifs ? rt_g.ifs : " \t\n";
    char *p = buf;
    for (int k = 0; k < nnames; k++) {
        while (*p && strchr(ifs, *p)) p++;
        if (k == nnames - 1) { rc_var_set(names[k], p); break; }
        char *start = p;
        while (*p && !strchr(ifs, *p)) p++;
        if (*p) *p++ = 0;
        rc_var_set(names[k], start);
    }
    return 0;
}

/* ---------- trap ---------- */
typedef struct rt_trap {
    int sig;                     /* 0 = EXIT */
    int mode;                    /* 0 = 跑动作（handler 或 action），1 = 默认，2 = 忽略 */
    void *handler;               /* 编译出来的函数指针 */
    rc_node *action;             /* 解释执行的动作（trap 'cmd' SIG） */
    char *action_src;            /* 动作原文（trap 无参数时列出来用） */
    struct rt_trap *next;
} rt_trap;

static rt_trap *traps;
static volatile sig_atomic_t trap_pending[64];

static void trap_signal(int sig)
{
    if (sig > 0 && sig < 64) trap_pending[sig] = 1;
}

/* 信号名 ↔ 编号（Linux/Android 通用编号） */
static const struct { const char *n; int v; } sig_tbl[] = {
    {"EXIT",0},{"HUP",1},{"INT",2},{"QUIT",3},{"ILL",4},{"TRAP",5},{"ABRT",6},
    {"BUS",7},{"FPE",8},{"KILL",9},{"USR1",10},{"SEGV",11},{"USR2",12},{"PIPE",13},
    {"ALRM",14},{"TERM",15},{"STKFLT",16},{"CHLD",17},{"CONT",18},{"STOP",19},
    {"TSTP",20},{"TTIN",21},{"TTOU",22},{"URG",23},{"XCPU",24},{"XFSZ",25},
    {"VTALRM",26},{"PROF",27},{"WINCH",28},{"IO",29},{"PWR",30},{"SYS",31},{NULL,0}
};

static int sig_by_name(const char *s)
{
    if (!s || !*s) return -1;
    if (isdigit((unsigned char)s[0])) return atoi(s);
    if (!strncasecmp(s, "SIG", 3)) s += 3;
    if (!strcasecmp(s, "EXIT")) return 0;
    for (int i = 0; sig_tbl[i].n; i++)
        if (!strcasecmp(s, sig_tbl[i].n)) return sig_tbl[i].v;
    return -1;
}

static const char *sig_name(int v)
{
    for (int i = 0; sig_tbl[i].n; i++) if (sig_tbl[i].v == v) return sig_tbl[i].n;
    return "?";
}

/* 登记一项 trap；返回该表项 */
static rt_trap *trap_slot(int sig)
{
    rt_trap **pp = &traps;
    while (*pp && (*pp)->sig != sig) pp = &(*pp)->next;
    if (!*pp) {
        rt_trap *t = rt_xmalloc(sizeof *t);
        t->sig = sig; t->mode = 1; t->handler = NULL; t->action = NULL;
        t->action_src = NULL; t->next = NULL;
        *pp = t;
    }
    return *pp;
}

static void trap_install(rt_trap *t)
{
    if (t->sig == 0) return;                       /* EXIT 不装信号 */
    if (t->mode == 2) { signal(t->sig, SIG_IGN); return; }
    if (t->mode == 1) { signal(t->sig, SIG_DFL); return; }
    if (t->handler) {                              /* 编译出来的函数 */
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = trap_signal;
        sigemptyset(&sa.sa_mask);
        if (t->sig == SIGCHLD) sa.sa_flags = SA_NOCLDSTOP;
        sigaction(t->sig, &sa, NULL);
        return;
    }
    if (t->action) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = trap_signal;
        sigemptyset(&sa.sa_mask);
        if (t->sig == SIGCHLD) sa.sa_flags = SA_NOCLDSTOP;
        sigaction(t->sig, &sa, NULL);
    }
}

/* 编译产物用的入口（trap 目标是个编译出来的函数） */
long rc_trap_set(long sig, void *handler)
{
    if (sig < 0 || sig >= 64) return 1;
    rt_trap *t = trap_slot((int)sig);
    t->handler = handler;
    t->action = NULL;
    t->mode = 0;
    trap_install(t);
    return 0;
}

long rc_trap_take_signal_num(const char *name) { return sig_by_name(name); }
long rc_trap_status_set(int sig, rc_node *action, const char *src, int mode)
{
    if (sig < 0 || sig >= 64) return 1;
    rt_trap *t = trap_slot(sig);
    t->handler = NULL;
    t->action = action;
    t->mode = mode;
    free(t->action_src);
    t->action_src = src ? rt_xstrdup(src) : NULL;
    trap_pending[sig] = 0;
    trap_install(t);
    return 0;
}

long rc_trap_default(void) { return 0; }

/* 子 shell 用：POSIX/bash 里父 shell 的 EXIT trap **不**被子进程继承
   （子进程自己设的 EXIT trap 到它退出时仍要跑）。 */
void rc_trap_clear_exit(void)
{
    rt_trap **pp = &traps;
    while (*pp) {
        if ((*pp)->sig == 0) {
            rt_trap *t = *pp;
            *pp = t->next;
            free(t->action_src);
            free(t);
        } else pp = &(*pp)->next;
    }
}

long rc_trap_foreach(void (*fn)(int sig, const char *src, int mode, void *ud), void *ud)
{
    for (rt_trap *t = traps; t; t = t->next)
        if (t->mode != 1) fn(t->sig, t->action_src, t->mode, ud);
    return 0;
}

/* 安全点：执行挂起的 trap 动作 */
void rc_trap_check(void)
{
    for (rt_trap *t = traps; t; t = t->next) {
        if (t->sig <= 0 || t->sig >= 64) continue;
        if (!trap_pending[t->sig]) continue;
        if (t->mode != 0) continue;
        trap_pending[t->sig] = 0;                 /* 先清标志，避免动作里递归触发 */
        if (t->handler) {
            long (*fn)(long, char **) = (long (*)(long, char **))t->handler;
            fn(0, NULL);
        } else if (t->action) {
            long save = rt_g.last_status;         /* POSIX：信号 trap 不改变 $? */
            rt_interp_list(t->action);            /* 动作是语句链，必须整条跑 */
            rt_g.last_status = save;
        }
    }
}

void rc_trap_run_exit(long status)
{
    if (rt_g.in_exit_trap) return;      /* trap 里再 exit：别把 EXIT trap 重跑一遍 */
    rt_g.in_exit_trap = 1;
    for (rt_trap *t = traps; t; t = t->next) {
        if (t->sig != 0 || t->mode != 0) continue;
        if (t->handler) {
            long (*fn)(long, char **) = (long (*)(long, char **))t->handler;
            fn(0, NULL);
        } else if (t->action) {
            rt_g.last_status = status;
            rt_interp_list(t->action);            /* `trap 'a; exit 9' EXIT` 里的 exit 要生效 */
        }
    }
    (void)status;
}

/* ---------- trap 内建 ---------- */
static void trap_print_one(int sig, const char *src, int mode, void *ud)
{
    (void)ud;
    const char *nm = sig_name(sig);
    char buf[32];
    if (sig > 0) { snprintf(buf, sizeof buf, "SIG%s", nm); nm = buf; }
    if (mode == 2) printf("trap -- '' %s\n", nm);
    else if (src)  printf("trap -- '%s' %s\n", src, nm);
}

static long b_trap(int argc, char **argv)
{
    if (argc == 1) { rc_trap_foreach(trap_print_one, NULL); return 0; }
    if (argc == 2 && (!strcmp(argv[1], "-l") || !strcmp(argv[1], "-L"))) {
        for (int i = 0; sig_tbl[i].n; i++) {
            if (sig_tbl[i].v == 0) continue;         /* bash 的 -l 不含 0/EXIT */
            printf("%2d) SIG%s\n", sig_tbl[i].v, sig_tbl[i].n);
        }
        return 0;
    }
    int i = 1;
    if (!strcmp(argv[i], "-p")) {                 /* trap -p [SIG...] */
        if (argc == 2) { rc_trap_foreach(trap_print_one, NULL); return 0; }
        i++;
        for (; i < argc; i++) {
            int sig = sig_by_name(argv[i]);
            if (sig < 0) { fprintf(stderr, "trap: %s: 无效的信号规格\n", argv[i]); return 1; }
            for (rt_trap *t = traps; t; t = t->next)
                if (t->sig == sig && t->mode != 1) trap_print_one(t->sig, t->action_src, t->mode, NULL);
        }
        return 0;
    }
    const char *action = NULL;
    int mode = 0;
    if (i < argc) {
        if (!strcmp(argv[i], "-")) { mode = 1; i++; }                 /* 恢复默认 */
        else {
            action = argv[i];
            mode = *action ? 0 : 2;                                    /* '' → 忽略 */
            i++;
        }
    }
    rc_node *node = NULL;
    if (action && mode == 0) {
        char *err = NULL;
        node = rc_parse_string(action, "trap", &err);
        if (!node) {
            if (err) { fprintf(stderr, "trap: %s\n", err); free(err); }
            return 1;
        }
    }
    int st = 0;
    for (; i < argc; i++) {
        int sig = sig_by_name(argv[i]);
        if (sig < 0) { fprintf(stderr, "trap: %s: 无效的信号规格\n", argv[i]); st = 1; continue; }
        if (sig == 9 || sig == 19) continue;       /* SIGKILL/SIGSTOP 抓不住 */
        rc_trap_status_set(sig, node, action, mode);
    }
    return st;
}

/* ---------- 其它 ---------- */
static long b_jobs(int argc, char **argv)
{
    (void)argc; (void)argv;
    rc_jobs_print();
    return 0;
}

static long b_wait(int argc, char **argv)
{
    if (argc == 1) return rt_wait_children();
    long last = 0;
    for (int i = 1; i < argc; i++) last = rc_wait_pid(strtol(argv[i], NULL, 10));
    return last;
}

static long b_kill(int argc, char **argv)
{
    int sig = SIGTERM;
    int i = 1;
    /* 支持 `kill -INT pid` / `kill -SIGINT pid` / `kill -s INT pid` / `kill -9 pid`。
       以前一律 atoi()，于是 `kill -INT $$` 变成「信号 0」→ 什么都不发。 */
    if (i < argc && !strcmp(argv[i], "-s") && i + 1 < argc) {
        int v = sig_by_name(argv[i + 1]);
        if (v >= 0) sig = v;
        i += 2;
    } else if (i < argc && argv[i][0] == '-' && argv[i][1] && !isdigit((unsigned char)argv[i][1])) {
        int v = sig_by_name(argv[i] + 1);
        if (v >= 0) sig = v; else { fprintf(stderr, "kill: %s: 无效的信号规格\n", argv[i]); return 1; }
        i++;
    } else if (i < argc && argv[i][0] == '-' && isdigit((unsigned char)argv[i][1])) {
        sig = atoi(argv[i] + 1);
        i++;
    }
    if (argc > 1 && !strcmp(argv[1], "-l")) {          /* kill -l：列信号名 */
        for (int k = 0; sig_tbl[k].n; k++)
            if (sig_tbl[k].v > 0) printf("%s ", sig_tbl[k].n);
        printf("\n");
        return 0;
    }
    long st = 0;
    for (; i < argc; i++) {
        long pid = strtol(argv[i], NULL, 10);
        if (kill((pid_t)pid, sig) != 0) { fprintf(stderr, "kill: %s: %s\n", argv[i], strerror(errno)); st = 1; }
    }
    return st;
}

/* ---------- getopts（POSIX） ---------- */
static long b_getopts(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "getopts: 用法: getopts optstring name [arg...]\n"); return 2; }
    const char *optstr = argv[1];
    const char *name = argv[2];
    int silent = (*optstr == ':');
    if (silent) optstr++;

    char **av; long ac;
    if (argc > 3) { av = argv + 3; ac = argc - 3; }
    else { av = rt_g.pos; ac = rt_g.npos; }

    static long optpos = 1;
    long optind = 1;
    const char *oi = rt_var_get_str("OPTIND");
    if (oi && *oi) {
        long v = strtol(oi, NULL, 10);
        if (v >= 1) { if (v != optind) optpos = 1; optind = v; }
    }
    char num[32];

    while (optind <= ac) {
        const char *cur = av[optind - 1];
        if (!cur || cur[0] != '-' || !cur[1]) {
            snprintf(num, sizeof num, "%ld", optind);
            rc_var_set("OPTIND", num);
            optpos = 1;
            return 1;
        }
        if (!strcmp(cur, "--")) {
            optind++;
            snprintf(num, sizeof num, "%ld", optind);
            rc_var_set("OPTIND", num);
            optpos = 1;
            return 1;
        }
        if (optpos < 1) optpos = 1;
        if (!cur[optpos]) { optind++; optpos = 1; continue; }
        break;
    }
    if (optind > ac) {
        snprintf(num, sizeof num, "%ld", optind);
        rc_var_set("OPTIND", num);
        return 1;
    }
    const char *cur = av[optind - 1];
    char c = cur[optpos];
    const char *found = strchr(optstr, c);
    char one[2] = { c, 0 };
    if (!found || c == ':') {                     /* 非法选项 */
        optpos++;
        if (!cur[optpos]) { optind++; optpos = 1; }
        snprintf(num, sizeof num, "%ld", optind);
        rc_var_set("OPTIND", num);
        if (silent) { rc_var_set("OPTARG", one); rc_var_set(name, ":"); }
        else { fprintf(stderr, "getopts: 非法选项 -- %c\n", c); rc_var_set(name, "?"); }
        return 0;
    }
    if (found[1] == ':') {                        /* 该选项要参数 */
        const char *arg = NULL;
        if (cur[optpos + 1]) { arg = cur + optpos + 1; optind++; optpos = 1; }
        else if (optind < ac) { arg = av[optind]; optind += 2; optpos = 1; }
        if (!arg) {
            optind++; optpos = 1;
            snprintf(num, sizeof num, "%ld", optind);
            rc_var_set("OPTIND", num);
            if (silent) { rc_var_set("OPTARG", one); rc_var_set(name, ":"); }
            else { fprintf(stderr, "getopts: 选项需要参数 -- %c\n", c); rc_var_set(name, "?"); }
            return 0;
        }
        rc_var_set("OPTARG", arg);
    } else {
        optpos++;
        if (!cur[optpos]) { optind++; optpos = 1; }
    }
    snprintf(num, sizeof num, "%ld", optind);
    rc_var_set("OPTIND", num);
    rc_var_set(name, one);
    return 0;
}

static long b_eval(int argc, char **argv)
{
    if (argc < 2) return 0;
    char *joined = rt_str_join(argv + 1, (uint32_t)(argc - 1), " ");
    long st = rc_eval_string(joined, "eval");
    free(joined);
    return st;
}

static long b_source(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "source: 缺少文件名\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "source: %s: %s\n", argv[1], strerror(errno)); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = rt_xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    /* `. file a b`：为被包含文件临时设置位置参数（POSIX） */
    long saved_n = rt_g.npos;
    char **saved = NULL;
    if (argc > 2) {
        saved = rt_xmalloc(sizeof(char *) * (size_t)(saved_n > 0 ? saved_n : 1));
        for (long k = 0; k < saved_n; k++) saved[k] = rt_xstrdup(rt_g.pos[k]);
        rc_pos_set(argc - 2, argv + 2, rt_g.arg0);
    }
    rt_g.in_source++;
    long st = rc_eval_string(buf, argv[1]);
    rt_g.in_source--;
    if (argc > 2) {
        rc_pos_set(saved_n, saved, rt_g.arg0);
        for (long k = 0; k < saved_n; k++) free(saved[k]);
        free(saved);
    }
    free(buf);
    return st;
}

static long b_exec(int argc, char **argv)
{
    if (argc < 2) return 0;      /* 只带重定向：重定向已由调用方永久生效 */
    char **av = argv_terminated(argv + 1, argc - 1);
    execvp(argv[1], av);
    fprintf(stderr, "exec: %s: %s\n", argv[1], strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
}

static long b_test(int argc, char **argv)
{
    int n = argc - 1;
    char **a = argv + 1;
    /* `[` 形式要求以 `]` 结尾：剥掉它再按 test 的参数规则求值 */
    if (argc > 1 && !strcmp(argv[0], "[") && n > 0 && !strcmp(a[n - 1], "]")) n--;
    long r = rt_test_argv(a, n);
    rt_g.last_status = r;
    return r;
}


/* ---------- 注册表 ---------- */
typedef struct { const char *name; fn_t fn; } bi_t;

static const bi_t builtins[] = {
    { ":",         b_true },
    { "true",      b_true },
    { "false",     b_false },
    { "echo",      b_echo },
    { "printf",    b_printf },
    { "cd",        b_cd },
    { "pwd",       b_pwd },
    { "export",    b_export },
    { "readonly",  b_readonly },
    { "unset",     b_unset },
    { "local",     b_local },
    { "shift",     b_shift },
    { "exit",      b_exit },
    { "return",    b_return },
    { "break",     b_break },
    { "continue",  b_continue },
    { "set",       b_set },
    { "times",     b_times },
    { "umask",     b_umask },
    { "type",      b_type },
    { "command",   b_command },
    { "read",      b_read },
    { "trap",      b_trap },
    { "getopts",   b_getopts },
    { "eval",      b_eval },
    { "source",    b_source },
    { ".",         b_source },
    { "exec",      b_exec },
    { "test",      b_test },
    { "[",         b_test },
    { "jobs",      b_jobs },
    { "wait",      b_wait },
    { "kill",      b_kill },
    { NULL,        NULL }
};

rc_builtin_fn rc_builtin_lookup(const char *name)
{
    for (int i = 0; builtins[i].name; i++)
        if (!strcmp(builtins[i].name, name)) return builtins[i].fn;
    return NULL;
}

int rc_builtin_count(void)
{
    int n = 0;
    while (builtins[n].name) n++;
    return n;
}


