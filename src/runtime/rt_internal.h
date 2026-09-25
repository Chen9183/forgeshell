/* rt_internal.h —— 运行时内部共享定义（不对外，生成代码不直接引用） */
#ifndef RT_INTERNAL_H
#define RT_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "rc_api.h"

/* ---------- 变量 ---------- */
#define RC_VF_EXPORT   0x01u
#define RC_VF_READONLY 0x02u
#define RC_VF_SPECIAL  0x04u   /* $? $# $$ $! $- 等 */

typedef struct rt_var {
    char  *name;
    char  *value;
    uint8_t flags;
    uint8_t pad[3];
    int    level;              /* 作用域层级：0 = 全局 */
    struct rt_var *next;
} rt_var;

typedef struct rt_func {
    const char *name;
    void       *addr;          /* 编译出来的函数：long f(long argc, char **argv) */
    const rc_node *body;       /* 运行期 eval 定义的函数体（解释执行） */
    struct rt_func *next;
} rt_func;

/* ---------- 全局状态 ---------- */
typedef struct rt_state {
    rt_var  *vars;
    int      level;            /* 当前作用域层级；函数进入 +1 */
    rt_func *funcs;
    char   **pos;              /* 位置参数 */
    long     npos;
    const char *arg0;
    long     last_status;      /* $? */
    long     last_bg_pid;      /* $! */
    const char *ifs;
    uint8_t  opt_e, opt_u, opt_x, opt_f, opt_n, opt_v;  /* set -euxfnv */
    uint8_t  opt_a, opt_C, opt_m, opt_b;                /* set -a(C)noclobber/-m/-b */
    long     shell_pid;        /* $$：调用本 shell 的进程号（子 shell 里也不变，POSIX） */
    long     loop_depth;       /* break/continue 目标 */
    long     pending_break;    /* >0 = break n；<0 = continue n */
    long     exit_status;      /* exit n 的记录 */
    int      in_exit_trap;
    int      loop_ran;
    int      loop_signal;
    long     return_seen_status;
    int      return_seen;      /* 本函数层发生过 return（不被 rc_pending_return 消费） */
    int      in_source;        /* 正在 source/. 一个文件（return 合法） */      /* 1=break 2=continue（给外层循环消费） */         /* 循环体是否执行过（决定空循环的状态码） */
    int      pending_exit;     /* set -e 等触发的待退出 */
    int      in_cond;          /* 正在求值条件（set -e 下不退出） */
    int      in_subshell;
    rc_image *image;
    /* 重定向保存栈 */
    struct rt_saved_fd { int fd; int saved; struct rt_saved_fd *next; } *saved_fds;
    uint32_t n_saved;
    int      pipe_status[16];
    int      n_pipe_status;
} rt_state;

extern rt_state rt_g;

/* ---------- rt_util / rt_core ---------- */
void  *rt_xmalloc(size_t n);
void  *rt_xrealloc(void *p, size_t n);
char  *rt_xstrdup(const char *s);
char  *rt_xstrndup(const char *s, size_t n);
void   rt_words_init(rc_words *w);
void   rt_words_free(rc_words *w);
void   rt_words_add(rc_words *w, char *s);      /* 接管所有权 */
void   rt_words_add_copy(rc_words *w, const char *s);
char  *rt_str_join(char **v, uint32_t n, const char *sep);
int    rt_word_is_name(const char *s);
char  *rt_status_str(long st);

/* ---------- 变量表（rt_var.c 侧） ---------- */
rt_var *rt_var_lookup(const char *name);
rt_var *rt_var_define(const char *name, int level);
void    rt_var_assign(rt_var *v, const char *value);
void    rt_var_scope_enter(void);
void    rt_var_scope_leave(void);
char   *rt_var_get_str(const char *name);          /* NULL = 未定义 */
const char *rt_special_value(const char *name);
void rt_ifs_sync(void);
char *rt_which(const char *name);
void rc_cond_enter(void);
void rc_cond_leave(void);
long rc_trap_status_set(int sig, rc_node *action, const char *src, int mode);
void rc_trap_clear_exit(void);
void rc_rt_exit(long status);
long rc_trap_foreach(void (*fn)(int, const char *, int, void *), void *ud);
void rc_node_free_runtime(rc_node *n);        /* IFS 变更后重新同步字段切分用的分隔符 */

/* ---------- 词展开 ---------- */
long rt_expand_word(const rc_word *w, rc_words *out, unsigned flags);
char *rt_expand_command_subst(const rc_node *sub);
void  rt_field_split(const char *s, rc_words *out, int do_split);
long  rt_path_expand(rc_words *w);
char *rt_remove_pattern(const char *val, const char *pat, int mode); /* mode: 0=# 1=## 2=% 3=%% */
char *rt_subst_pattern(const char *val, const char *pat, const char *rep, uint32_t flags);

/* ---------- 算术 ---------- */
long rt_arith_eval(const rc_arith *a, long *out);
long rt_arith_str(const char *expr, long *out);    /* 运行时把字符串当算术式求值（$(( )) 等） */

/* ---------- 测试 ---------- */
long rt_test_node(const rc_test *t);
long rt_test_argv(char **argv, int argc);          /* test/[ 风格 */

/* ---------- 执行 ---------- */
long rt_run_simple(const rc_simple *sc);
long rt_run_program(char **argv, int argc);
long rt_apply_redirs(const rc_redir *r);           /* 成功返回 0；失败 -1 */
void rt_restore_redirs(void);
long rt_run_pipeline(const rc_node *n);
long rt_wait_children(void);
int  rt_job_register(long pid);   /* 返回作业编号 */

/* ---------- 函数表 / 作业表 ---------- */
rt_func *rc_func_find(const char *name);
void     rc_func_unregister(const char *name);
void     rc_jobs_print(void);

/* ---------- 解释器 ---------- */
long rt_interp(const rc_node *n);
long rt_interp_list(const rc_node *n);

#endif /* RT_INTERNAL_H */
