/* rt_exec.c —— 执行：外部程序 / 重定向 / 管道 / 后台作业 / 等待
 *
 * 语义要点：
 *   · 重定向用“保存 fd + dup2 覆盖 + 恢复”的方式实现，作用域是单条命令；
 *   · 管道每一段 fork，最后一段可以留在当前进程（这里为简单可靠，统一 fork 后等待）；
 *   · 后台作业记入作业表，$! 取最近一次；wait 无参等待全部子进程；
 *   · 退出码：正常退出取 WEXITSTATUS，被信号杀死取 128+signo（POSIX 惯例）。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>

#include "rt_internal.h"

/* ============================ 作业表 ============================ */
typedef struct rt_job {
    long pid;
    int  status;
    int  done;
    struct rt_job *next;
} rt_job;

static rt_job *jobs;

static int job_next_no = 1;

int rt_job_register(long pid)
{
    rt_job *j = rt_xmalloc(sizeof(*j));
    j->pid = pid; j->status = -1; j->done = 0;
    j->next = jobs;
    jobs = j;
    rt_g.last_bg_pid = pid;
    return job_next_no++;      /* 作业编号：`[1] 12345` 里那个 1 */
}

static void job_record(long pid, int status)
{
    for (rt_job *j = jobs; j; j = j->next)
        if (j->pid == pid) { j->status = status; j->done = 1; return; }
}

static int status_from_wait(int st)
{
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return 1;
}

long rc_wait_pid(long pid)
{
    int st = 0;
    if (waitpid((pid_t)pid, &st, 0) < 0) return 127;
    job_record(pid, st);
    rt_g.last_status = status_from_wait(st);
    return rt_g.last_status;
}

long rt_wait_children(void)
{
    int st;
    pid_t p;
    while ((p = waitpid(-1, &st, 0)) > 0) {
        job_record(p, st);
        if ((long)p == rt_g.last_bg_pid) rt_g.last_status = status_from_wait(st);
    }
    return rt_g.last_status;
}

long rc_jobs_wait_all(void) { return rt_wait_children(); }

/* ============================ 重定向 ============================ */
static struct rt_saved_fd *save_fd(int fd)
{
    struct rt_saved_fd *s = rt_xmalloc(sizeof(*s));
    s->fd = fd;
    s->saved = dup(fd);
    s->next = rt_g.saved_fds;
    rt_g.saved_fds = s;
    return s;
}

long rt_apply_redirs(const rc_redir *r)
{
    fflush(NULL);      /* ⚑ 换 fd 之前必须冲刷：内建命令的输出还压在 stdio 缓冲里，
                          不冲就会在 dup2 之后才写出去，落进错误的目的地（甚至丢数据） */
    for (; r; r = r->next) {
        int fd = (r->fd == 0xFF) ? -1 : (int)r->fd;
        char *path = NULL;
        int ofd = -1;

        switch (r->op) {
        case RC_R_IN:
        case RC_R_OUT:
        case RC_R_APP:
        case RC_R_RDWR:
        case RC_R_CLOBBER: {
            path = rc_expand_word_literal(r->target);
            if (fd < 0) fd = (r->op == RC_R_IN || r->op == RC_R_RDWR) ? 0 : 1;
            save_fd(fd);
            int flags;
            switch (r->op) {
            case RC_R_IN:      flags = O_RDONLY; break;
            case RC_R_OUT:
                if (rt_g.opt_C) {          /* set -C：不覆盖已存在的文件（>| 才是强制） */
                    struct stat sb;
                    if (stat(path, &sb) == 0) {
                        fprintf(stderr, "s2a: %s: 文件已存在（noclobber）\n", path);
                        free(path);
                        rt_g.last_status = 1;
                        return -1;
                    }
                }
                flags = O_WRONLY | O_CREAT | O_TRUNC; break;
            case RC_R_APP:     flags = O_WRONLY | O_CREAT | O_APPEND; break;
            case RC_R_CLOBBER: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
            default:           flags = O_RDWR | O_CREAT; break;
            }
            ofd = open(path, flags, 0666);
            if (ofd < 0) {
                fprintf(stderr, "s2a: %s: %s\n", path, strerror(errno));
                free(path);
                rt_g.last_status = 1;
                return -1;
            }
            break;
        }
        case RC_R_OUT_ERR: {   /* &> / &>> */
            path = rc_expand_word_literal(r->target);
            save_fd(1); save_fd(2);
            int flags = O_WRONLY | O_CREAT | (r->fd2 ? O_APPEND : O_TRUNC);
            ofd = open(path, flags, 0666);
            if (ofd < 0) {
                fprintf(stderr, "s2a: %s: %s\n", path, strerror(errno));
                free(path);
                rt_g.last_status = 1;
                return -1;
            }
            dup2(ofd, 1); dup2(ofd, 2);
            if (ofd > 2) close(ofd);
            free(path);
            continue;
        }
        case RC_R_HEREDOC:
        case RC_R_HERESTR: {
            if (fd < 0) fd = 0;
            save_fd(fd);
            int pfd[2];
            if (pipe(pfd) != 0) return -1;
            char *text;
            if (r->op == RC_R_HERESTR) {
                text = rc_expand_word_literal(r->target);
                size_t l = strlen(text);
                text = rt_xrealloc(text, l + 2);
                text[l] = '\n'; text[l + 1] = 0;
            } else if (r->target) {
                /* 展开型 here-doc：正文已在编译期解析成词，这里只做参数/命令/算术展开
                   （不切分、不 glob，也不做引号移除 —— POSIX here-doc 语义） */
                text = rc_expand_word_literal(r->target);
            } else if (r->hd_expand) {
                /* 老路径兜底：正文没有对应词时按字面量处理 */
                rc_word w; rc_wseg seg;
                memset(&w, 0, sizeof w); memset(&seg, 0, sizeof seg);
                seg.kind = RW_LIT; seg.name = r->hd; seg.namelen = r->hd_len;
                w.segs = &seg; w.flags = RWF_LITERAL;
                text = rc_expand_word_literal(&w);
            } else {
                text = rt_xstrndup(r->hd ? r->hd : "", r->hd_len ? r->hd_len : strlen(r->hd ? r->hd : ""));
            }
            size_t l = strlen(text), off = 0;
            while (off < l) {
                ssize_t k = write(pfd[1], text + off, l - off);
                if (k <= 0) break;
                off += (size_t)k;
            }
            free(text);
            close(pfd[1]);
            ofd = pfd[0];
            break;
        }
        case RC_R_DUP_OUT:
        case RC_R_DUP_IN: {
            if (fd < 0) fd = (r->op == RC_R_DUP_OUT) ? 1 : 0;
            int src = (int)r->fd2;
            if (r->target) {          /* 形如 >&$var 的形式 */
                char *t = rc_expand_word_literal(r->target);
                if (!strcmp(t, "-")) { save_fd(fd); close(fd); free(t); continue; }
                src = atoi(t);
                free(t);
            }
            save_fd(fd);
            if (dup2(src, fd) < 0) { rt_g.last_status = 1; return -1; }
            continue;
        }
        case RC_R_CLOSE:
            save_fd(fd < 0 ? 1 : fd);
            close(fd < 0 ? 1 : fd);
            continue;
        default:
            continue;
        }

        if (fd < 0) fd = 1;
        if (dup2(ofd, fd) < 0) {
            fprintf(stderr, "s2a: 重定向失败: %s\n", strerror(errno));
            if (ofd >= 0) close(ofd);
            free(path);
            rt_g.last_status = 1;
            return -1;
        }
        if (ofd != fd) close(ofd);
        free(path);
    }
    return 0;
}

/* ⚑ 重定向必须按「标记」回退，不能一律弹空整个栈：
   复合命令（`while ... done < f`）先压了文件重定向，循环里第一个内建命令
   执行完如果弹空整栈，外层重定向就被撤掉了 —— 现象就是只读到第一行。
   下面这个标记把回退范围限制在一次应用之内。 */
static struct rt_saved_fd *redir_mark(void) { return rt_g.saved_fds; }

static void redir_restore_to(struct rt_saved_fd *m)
{
    fflush(NULL);      /* ⚑ 还原 fd 之前也要冲刷，命令的输出才算真的写进了被重定向的文件 */
    while (rt_g.saved_fds && rt_g.saved_fds != m) {
        struct rt_saved_fd *s = rt_g.saved_fds;
        rt_g.saved_fds = s->next;
        if (s->saved >= 0) {
            dup2(s->saved, s->fd);
            close(s->saved);
        } else {
            close(s->fd);
        }
        free(s);
    }
}

void rt_restore_redirs(void) { redir_restore_to(NULL); }

long rc_redir_push(const rc_node *n)
{
    if (!n || !n->redirs) return 0;
    /* 先在栈上放一个标记节点，rc_redir_pop 回退到它为止（fd = -1 表示标记） */
    struct rt_saved_fd *m = rt_xmalloc(sizeof(*m));
    m->fd = -1; m->saved = -1;
    m->next = rt_g.saved_fds;
    rt_g.saved_fds = m;
    return rt_apply_redirs(n->redirs);
}

long rc_redir_pop(void)
{
    fflush(NULL);
    while (rt_g.saved_fds) {
        struct rt_saved_fd *s = rt_g.saved_fds;
        rt_g.saved_fds = s->next;
        if (s->fd < 0) { free(s); break; }     /* 标记：到此为止，别碰外层 */
        if (s->saved >= 0) { dup2(s->saved, s->fd); close(s->saved); }
        else close(s->fd);
        free(s);
    }
    return 0;
}

/* ============================ 外部程序 ============================ */
/* 默认命令搜索目录（安卓宿主 + 常见 Linux 目录，用户 PATH 缺失时使用） */
static const char *const rt_default_dirs[] = {
    "/system/bin", "/system/xbin", "/vendor/bin", "/vendor/xbin", "/odm/bin",
    "/system_ext/bin", "/product/bin", "/apex/com.android.runtime/bin",
    "/data/adb/magisk", "/data/local/tmp",
    "/usr/local/bin", "/usr/bin", "/bin", "/usr/local/sbin", "/usr/sbin", "/sbin",
    NULL
};

/* 按 PATH + 默认目录找一个可执行文件，返回 malloc 的完整路径或 NULL */
char *rt_which(const char *name)
{
    if (!name || !*name) return NULL;
    if (strchr(name, '/')) return access(name, X_OK) == 0 ? rt_xstrdup(name) : NULL;
    char buf[4096];
    const char *path = getenv("PATH");
    if (path && *path) snprintf(buf, sizeof buf, "%s", path);
    else buf[0] = 0;
    for (int i = 0; rt_default_dirs[i]; i++) {
        size_t l = strlen(buf);
        if (l + strlen(rt_default_dirs[i]) + 2 < sizeof buf) {
            snprintf(buf + l, sizeof buf - l, ":%s", rt_default_dirs[i]);
        }
    }
    char *save = NULL, *d = strtok_r(buf, ":", &save);
    static char found[4096];
    for (; d; d = strtok_r(NULL, ":", &save)) {
        snprintf(found, sizeof found, "%s/%s", d, name);
        if (access(found, X_OK) == 0) return rt_xstrdup(found);
    }
    return NULL;
}

long rt_run_program(char **argv, int argc)
{
    if (argc <= 0 || !argv[0] || !argv[0][0]) return 0;
    (void)argc;
    fflush(NULL);   /* 关键：fork 前必须冲刷 stdio，否则子进程会重复输出缓冲内容 */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "s2a: fork 失败: %s\n", strerror(errno));
        return 127;
    }
    if (pid == 0) {
        /* POSIX：非交互 shell 自己忽略 SIGINT/SIGQUIT，但**子进程**（外部命令）
           必须拿到默认处置，否则 `sh -c 'kill -INT $$'` 这类收不到信号。
           后台任务的 & 也在这里被显式忽略（POSIX 要求）。 */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        /* 子进程：自己做 PATH 搜索，才能记住“到底是哪个路径”失败，
         * 从而在 ENOEXEC（无 shebang 脚本）时正确地 `sh <那个路径>` 兜底。 */
        char buf[4096];
        char found[512];
        char enoexec_path[512];
        int have_found = 0, have_enoexec = 0, eacces = 0;

        const char *path = getenv("PATH");
        if (!path || !*path) {
            buf[0] = 0;
            for (int i = 0; rt_default_dirs[i]; i++) {
                strncat(buf, rt_default_dirs[i], sizeof buf - strlen(buf) - 2);
                strncat(buf, ":", sizeof buf - strlen(buf) - 2);
            }
            path = buf;
        } else {
            snprintf(buf, sizeof buf, "%s", path);
            path = buf;
        }
        /* PATH 之后追加一遍默认目录（安卓上 PATH 常不含 /system/bin） */
        for (int i = 0; rt_default_dirs[i]; i++) {
            strncat(buf, rt_default_dirs[i], sizeof buf - strlen(buf) - 2);
            strncat(buf, ":", sizeof buf - strlen(buf) - 2);
        }

        if (strchr(argv[0], '/')) {              /* POSIX：含斜杠 → 直接执行，不查 PATH */
            execv(argv[0], argv);
            fprintf(stderr, "s2a: %s: %s\n", argv[0], strerror(errno));
            _exit(errno == EACCES ? 126 : 127);
        }
        char *save = NULL;
        for (char *d = strtok_r(buf, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
            snprintf(found, sizeof found, "%s/%s", d, argv[0]);
            execv(found, argv);
            if (errno == ENOEXEC) {                 /* 找到了，但不是可执行格式 */
                snprintf(enoexec_path, sizeof enoexec_path, "%s", found);
                have_enoexec = 1;
            } else if (errno == EACCES) {
                eacces = 1;
            }
            /* ⚑ 其它 errno（ENOTDIR 等）以前也被当成「无 shebang 脚本」，
               于是 `/bin/sh /path/不存在的命令`，退出码变成 sh 的而不是 127。 */
        }
        if (have_enoexec) {
            /* 无 shebang 的脚本：按 POSIX 交给 /bin/sh，且必须传**完整路径** */
            char *nargv[66];
            int n = 0;
            nargv[n++] = (char *)"/bin/sh";
            nargv[n++] = enoexec_path;
            for (int k = 1; k < argc && n < 64; k++) nargv[n++] = argv[k];
            nargv[n] = NULL;
            execv("/bin/sh", nargv);
        }
        fprintf(stderr, "s2a: %s: %s\n", argv[0],
                eacces ? "权限不足" : "未找到该命令");
        _exit(eacces ? 126 : 127);
    }
    int st = 0;
    for (;;) {
        pid_t r = waitpid(pid, &st, WUNTRACED);
        if (r == pid) {
            if (WIFSTOPPED(st)) {          /* 被停住：继续等它恢复，避免死等 */
                kill(pid, SIGCONT);
                continue;
            }
            break;
        }
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    job_record(pid, st);
    rt_g.last_status = status_from_wait(st);
    return rt_g.last_status;
}

/* ============================ 简单命令 ============================ */
static int is_assignment_word(const char *s)
{
    const char *eq = strchr(s, '=');
    if (!eq || eq == s) return 0;
    for (const char *p = s; p < eq; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    return isalpha((unsigned char)s[0]) || s[0] == '_';
}

/* 还原命令前缀赋值：变量表 + 环境一起还原 */
typedef struct { char *name; char *old; } rt_prefix_var;
static void restore_prefix_vars(rt_prefix_var *pf, int n)
{
    for (int i = 0; i < n; i++) {
        if (pf[i].old) { rc_var_set(pf[i].name, pf[i].old); setenv(pf[i].name, pf[i].old, 1); }
        else           { rc_var_unset(pf[i].name); unsetenv(pf[i].name); }
        free(pf[i].old);
        free(pf[i].name);
    }
}

long rt_run_simple(const rc_simple *sc)
{
    if (!sc) return 0;
    if (rt_g.opt_n) return 0;      /* set -n：只读不执行（POSIX noexec） */
    rc_words argv;
    rt_words_init(&argv);

    /* 词展开 */
    for (uint32_t i = 0; i < sc->argc; i++)
        rt_expand_word(sc->argv[i], &argv, 0);

    /* 前置赋值：命令前的赋值（argv 为空则永久生效，否则只对该命令的环境生效） */
    char *pending_env[128];
    int npending = 0;
    for (rc_assign *as = sc->assigns; as && npending < 127; as = as->next) {
        char *v;
        if (as->is_arith) {
            long r = 0;
            rt_arith_eval(as->arith, &r);
            char b[32]; snprintf(b, sizeof b, "%ld", r);
            v = rt_xstrdup(b);
        } else {
            v = rc_expand_word_literal(as->value);
        }
        char *kv = rt_xmalloc(strlen(as->name) + strlen(v) + 2);
        sprintf(kv, "%s=%s", as->name, v);
        pending_env[npending++] = kv;
        if (argv.n == 0) {
            rc_var_set(as->name, v);
            rt_var *vv = rt_var_lookup(as->name);
            if (vv && (vv->flags & RC_VF_EXPORT)) setenv(as->name, v, 1);
        }
        free(v);
    }
    if (argv.n == 0) {   /* 纯赋值命令 */
        for (int i = 0; i < npending; i++) free(pending_env[i]);
        rt_words_free(&argv);
        return 0;
    }

    /* 命令前缀赋值（`IFS=: read ...`、`LC_ALL=C cmd`）：对被调命令临时可见——
       既要进环境（给外部程序），也要进变量表（内建命令看得到，且 IFS 这类变量
       还要触发字段切分同步）。命令结束后逐项还原。以前只 putenv，于是
       `IFS=: read ...` 的冒号根本不生效。 */
    rt_prefix_var pf[128];
    int npf = 0;
    for (int i = 0; i < npending; i++) {
        char *eq = strchr(pending_env[i], '=');
        if (!eq) continue;
        char *nm = rt_xstrndup(pending_env[i], (size_t)(eq - pending_env[i]));
        const char *oldv = rt_var_get_str(nm);
        if (npf < 128) {
            pf[npf].name = nm;
            pf[npf].old = oldv ? rt_xstrdup(oldv) : NULL;
            npf++;
        } else {
            free(nm);
        }
        rc_var_set(nm, eq + 1);
        setenv(nm, eq + 1, 1);
    }

    struct rt_saved_fd *rmark = redir_mark();
    long status;
    if (rt_apply_redirs(sc->redirs) != 0) {
        status = 1;
        restore_prefix_vars(pf, npf);
        for (int i = 0; i < npending; i++) free(pending_env[i]);
        rt_words_free(&argv);
        return status;
    }

    if (rt_g.opt_x) {              /* set -x：跟踪到 stderr，形如 `+ cmd arg` */
        fputs("+", stderr);
        for (uint32_t k = 0; k < argv.n; k++) fprintf(stderr, " %s", argv.v[k]);
        fputc('\n', stderr);
    }
    /* 顺序：shell 函数 → 内建 → 外部命令（POSIX：函数优先于外部，内建优先于函数之外的同名命令） */
    rt_func *fn = rc_func_find(argv.v[0]);
    rc_builtin_fn bf = rc_builtin_lookup(argv.v[0]);
    if (fn) {
        if (fn->addr) {
            /* 编译出来的函数：进出作用域、位置参数保存都由它自己的序言/收尾负责 */
            long (*fp)(long, char **) = (long (*)(long, char **))fn->addr;
            status = fp((long)argv.n - 1, argv.v + 1);
        } else {
            /* 解释执行的函数（运行期 eval 定义）：这里代它进出作用域 */
            rc_func_enter(argv.v[0], (long)argv.n - 1, argv.v + 1);
            rt_g.return_seen = 0;
            status = rt_interp_list(fn->body);
            long rs;
            if (rt_g.return_seen) {              /* 函数体里 return 过（标志可能已被列表消费） */
                status = rt_g.return_seen_status;
                rt_g.return_seen = 0;
            } else if (rc_pending_return(&rs)) {
                status = rs;
            }
            status = rc_func_leave(status);
        }
    } else if (bf) {
        status = bf((int)argv.n, argv.v);
    } else {
        /* argv 需要 NULL 结尾 */
        char **av = rt_xmalloc(sizeof(char *) * (argv.n + 1));
        for (uint32_t i = 0; i < argv.n; i++) av[i] = argv.v[i];
        av[argv.n] = NULL;
        status = rt_run_program(av, (int)argv.n);
        free(av);
    }

    /* `exec` 只带重定向（没有命令）：重定向永久生效，不能撤 */
    int keep_redirs = (argv.v[0] && !strcmp(argv.v[0], "exec") && argv.n == 1);
    if (!keep_redirs) redir_restore_to(rmark);   /* 只撤掉本命令的重定向，外层保留 */
    if (rt_g.opt_e && status != 0 && rt_g.in_cond == 0) rc_rt_exit(status);   /* set -e：条件上下文外失败即退出 */
    restore_prefix_vars(pf, npf);
    rt_g.last_status = status;
    rc_trap_check();              /* ⚑ 语句之间的安全点：挂起的 trap 在这里跑（bash 同序），
                                     rc_trap_check 内部会还原 $? */
    for (int i = 0; i < npending; i++) free(pending_env[i]);
    rt_words_free(&argv);
    return status;
}

long rc_exec_simple(const rc_simple *sc) { return rt_run_simple(sc); }

/* ============================ 管道 ============================ */
long rt_run_pipeline(const rc_node *n)
{
    /* 收集管道各段 */
    const rc_node *segs[64];
    int nseg = 0;
    const rc_node *p = n;
    while (p && p->kind == RN_PIPE && nseg < 63) {
        segs[nseg++] = p->a;
        p = p->b;
    }
    if (nseg == 0) return rt_interp(n);
    segs[nseg++] = p;

    fflush(NULL);
    int in_fd = -1;
    pid_t pids[64];
    int npids = 0;
    for (int i = 0; i < nseg; i++) {
        int pfd[2] = { -1, -1 };
        if (i < nseg - 1) {
            if (pipe(pfd) != 0) { rt_g.last_status = 1; return 1; }
        }
        pid_t pid = fork();
        if (pid < 0) { rt_g.last_status = 1; return 1; }
        if (pid == 0) {
            if (in_fd >= 0) { dup2(in_fd, 0); close(in_fd); }
            if (pfd[1] >= 0) { dup2(pfd[1], 1); close(pfd[1]); }
            if (pfd[0] >= 0) close(pfd[0]);
            rc_trap_clear_exit();
            long st = rt_interp(segs[i]);
            rc_rt_exit(st);
        }
        pids[npids++] = pid;
        if (in_fd >= 0) close(in_fd);
        if (pfd[1] >= 0) close(pfd[1]);
        in_fd = pfd[0];
    }
    if (in_fd >= 0) close(in_fd);

    int status = 0;
    rt_g.n_pipe_status = 0;
    for (int i = 0; i < npids; i++) {
        int st = 0;
        while (waitpid(pids[i], &st, 0) < 0 && errno == EINTR) { }
        job_record(pids[i], st);
        int code = status_from_wait(st);
        if (rt_g.n_pipe_status < 16) rt_g.pipe_status[rt_g.n_pipe_status++] = code;
        status = code;                       /* 管道整体退出码 = 最后一段 */
    }
    rt_g.last_status = status;
    return status;
}

long rc_exec_pipeline(const rc_node *n) { return rt_run_pipeline(n); }

/* ============================ 后台作业 ============================ */
long rc_bg_launch(const rc_node *n)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { rt_g.last_status = 1; return 1; }
    if (pid == 0) {
        /* 后台作业的 stdin 指向 /dev/null（POSIX 未规定，但这是通用做法） */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, 0); if (devnull != 0) close(devnull); }
        setsid();
        rc_trap_clear_exit();
        long st = rt_interp_list(n);          /* 后台体可能是语句链 */
        rc_rt_exit(st);
    }
    int jn = rt_job_register(pid);
    fprintf(stderr, "[%d] %ld\n", jn, (long)pid);   /* bash 的作业提示走 stderr */
    fflush(stderr);
    return 0;
}

void rc_jobs_print(void)
{
    for (rt_job *j = jobs; j; j = j->next) {
        if (j->done) continue;
        printf("[%ld] 运行中\n", (long)j->pid);
    }
}

/* ============================ 子 shell ============================ */
static int subshell_depth;

long rc_subshell_enter(void)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { rt_g.last_status = 1; return -1; }
    if (pid == 0) { subshell_depth++; rc_trap_clear_exit(); return 0; }   /* 子进程：继续执行子 shell 体 */
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) { }
    rt_g.last_status = status_from_wait(st);
    return 1;                                        /* 父进程：跳过子 shell 体 */
}

long rc_subshell_leave(long status)
{
    if (subshell_depth > 0) {
        rc_rt_exit(status);   /* 冲 stdio + 跑子进程自己的 EXIT trap，再退出 */
    }
    return status;
}
