/* rc_api.h —— 生成代码 ↔ 运行时 的 ABI，以及运行时内部模块接口
 *
 * 【A 段】生成代码 ABI：编译器按模板符号表取绝对地址，用 bl 调用；参数走 x0..x3，返回 x0。
 * 【B 段】运行时内部模块接口：运行时自己的源码之间使用（也在同一份模板映像里）。
 *
 * 约定：
 *   · 所有 rc_* 符号都在模板 ELF 的符号表里（不被 strip），编译器按名字查地址。
 *   · 状态码遵循 shell 约定：0..255，>255 由 rc_rt_exit 截断；特殊值 RC_EXIT_*。
 *   · 运行时不得调用生成代码里的“未知”符号；只能通过函数指针（trap / 回调）进入生成代码。
 */
#ifndef RC_API_H
#define RC_API_H

#include "../common/shell_ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * A 段：生成代码 ABI（编译器使用）
 * ====================================================================== */

/* 运行时启动：初始化变量表/位置参数/信号/防护子系统。
 * argc/argv = 脚本自身的参数（argv[0] 通常是产物名，与 $0 的语义见 rt_main.c）。
 * 返回 0 表示可继续执行；非 0 表示启动阶段已被防护子系统否决（此时应直接退出）。 */
long rc_rt_start(long argc, char **argv, const rc_image *img);

/* 运行时收尾并结束进程（不返回）：冲刷缓冲、等待后台作业、执行 trap EXIT。 */
void rc_rt_exit(long status) __attribute__((noreturn));

/* 执行一个节点（简单命令 / 管道 / 复合命令）。复合节点也会被解释执行，
 * 供 eval、动态 source、以及编译期未展开的路径使用。 */
long rc_exec(const rc_node *n);

/* 只执行简单命令（含前置赋值、重定向）。返回值即命令退出码。 */
long rc_exec_simple(const rc_simple *sc);

/* 执行管道 / 与或 / 列表（编译器生成的短路分支之外的运行时回退路径）。 */
long rc_exec_pipeline(const rc_node *n);

/* 词展开：把词表展开成字段数组（含 IFS 切分 / glob）。flags 见 RWF_*。 */
long rc_expand_words(rc_word **words, uint32_t n, rc_words *out);

/* 单个词展开（不切分、不 glob，用于赋值右值等场景）。 */
char *rc_expand_word_literal(const rc_word *w);

/* for 循环取词表（未给 in 列表时取 "$@"）。 */
long rc_for_words(const rc_for *f, rc_words *out);

/* 测试表达式求值：0 = 真，1 = 假，>1 为语法/求值错误。 */
long rc_test_run(const rc_test *t);

/* 算术求值：返回 0 成功并写 *out；非 0 为错误（除零/非法）。 */
long rc_arith_run(const rc_arith *a, long *out);

/* case 匹配：把 subject 展开后逐模式匹配，返回命中的项下标，未命中返回 -1。 */
long rc_case_match(const rc_word *subject, rc_word **pats, uint32_t n);

/* 变量读写（供编译期常量折叠后的朴素用法） */
void rc_var_set(const char *name, const char *value);
const char *rc_var_get(const char *name);      /* 未定义返回 NULL */

/* 函数调用：按名字查函数表并执行；不存在返回 127。 */
long rc_func_call(const char *name, rc_word **args, uint32_t nargs);

/* 注册一个 shell 函数：addr = 编译出来的函数入口（long f(long argc, char **argv)），
 * 或 addr = NULL + body = 节点树（运行期 eval 定义的函数，走解释器）。 */
long rc_func_register(const char *name, void *addr, const rc_node *body);

/* 循环 / 函数返回的“待处理标志”：编译出来的循环体与函数体在每条语句后调用。
 * rc_loop_check(): 0 = 继续，1 = break，2 = continue（取走后清零）
 * rc_pending_return(&st): 1 = 有 return（取走后清零） */
long rc_loop_check(void);
long rc_pending_return(long *status);

/* 运行期解析 + 解释（eval / 动态 source） */
long rc_eval_string(const char *code, const char *name);

/* 信号安全点：执行挂起的 trap 处理器（生成的代码在语句边界调用） */
void rc_trap_check(void);
void rc_trap_run_exit(long status);

/* jobs 内建的输出（实现在 rt_exec.c） */
void rc_jobs_print(void);

/* 特殊参数取值（$? $# $$ $! $- $0 $1..） */
const char *rc_special_str(char c);

/* 编译器生成代码用的小入口 */
long rc_last_status_get(void);
long rc_not_status(void);
long rc_arith_cmd(const rc_arith *a);
long rc_for_begin(const rc_for *f);
long rc_for_next(void);
long rc_for_end(void);
long rc_case_item_match(const rc_word *subject, const rc_case_item *it);
long rc_check_return(void);
long rc_take_return_status(void);

/* 编译出的函数体进出：保存/恢复位置参数与局部作用域。 */
void rc_func_enter(const char *name, long argc, char **argv);
long rc_func_leave(long status);

/* 子 shell（( ) ）与命令组（{ } ） */
long rc_subshell_enter(void);
long rc_subshell_leave(long status);

/* 复合节点自带的重定向：压栈/出栈 */
long rc_redir_push(const rc_node *n);
long rc_redir_pop(void);

/* 后台作业（&）：返回 0；作业表记录 PID。 */
long rc_bg_launch(const rc_node *n);

/* trap：把信号处理绑定到**编译出来的函数指针**（handler 为 NULL 表示忽略/恢复默认）。 */
long rc_trap_set(long sig, void *handler);

/* [[ ]] / test 的词展开辅助（把词展开为单字段字符串） */
char *rc_expand_test_str(const rc_word *w);

/* 配置访问（调试/报告模式） */
const rc_image *rc_get_image(void);

/* 未定义/未实现路径：打印位置并返回错误码（编译器对不支持语法会直接报错，不会走到这）。 */
long rc_unsupported(const char *what, uint32_t line);

/* ======================================================================
 * B 段：运行时内部模块接口
 * ====================================================================== */

/* ---- 内存与字符串工具（rt_util.c） ---- */
void *rc_xmalloc(size_t n);
void *rc_xrealloc(void *p, size_t n);
char *rc_xstrdup(const char *s);
char *rc_xstrndup(const char *s, size_t n);
size_t rc_strlen(const char *s);

/* ---- 变量表 / 作用域（rt_var.c） ---- */
typedef struct rc_var rc_var;
rc_var *rc_var_find(const char *name);
void    rc_var_export(const char *name, int on);
void    rc_var_set_local(const char *name, const char *value);
void    rc_var_unset(const char *name);
void    rc_var_mark_readonly(const char *name, int on);
void    rc_var_push_scope(void);
void    rc_var_pop_scope(void);
void    rc_pos_set(long argc, char **argv, const char *arg0);
long    rc_pos_shift(long n);
char  **rc_pos_argv(long *argc_out, const char *arg0_out);
const char *rc_special_get(char c);        /* $? $# $$ $! $- 的取值 */

/* ---- 词展开（rt_expand.c） ---- */
char *rc_expand_to_string(const rc_word *w, unsigned flags);   /* 无切分/无 glob */
long  rc_split_fields(char *s, rc_words *out, int ifs_split);
long  rc_glob_expand(rc_words *inout);
int   rc_pattern_match(const char *pat, const char *s, int pathname_mode);

/* ---- 算术（rt_arith.c） ---- */
void rc_arith_init(void);

/* ---- 执行（rt_exec.c） ---- */
long rc_run_external(char **argv, int argc);
long rc_wait_pid(long pid);
long rc_jobs_wait_all(void);

/* ---- 内建命令（rt_builtin.c） ---- */
typedef long (*rc_builtin_fn)(int argc, char **argv);
rc_builtin_fn rc_builtin_lookup(const char *name);
int rc_builtin_count(void);

/* ---- 解释器（rt_interp.c）：eval / 动态 source / 运行时构造的节点 ---- */
long rc_interp_node(const rc_node *n);

/* ---- 解析器（与编译器共享的 parser，运行时用于 eval / source） ---- */
rc_node *rc_parse_string(const char *src, const char *name, char **err_out);
void     rc_node_free_runtime(rc_node *n);

/* ---- 防护 / 反调试（rt_protect.c） ---- */
typedef struct rc_detect_result {
    uint32_t checks_run;
    uint32_t hits;
    uint64_t bitmap;      /* 每一项检测的命中位（见 rt_protect.h 的 AD_* 定义） */
    long     tracer_pid;
    char     tracer_comm[64];
    char     detail[256];
} rc_detect_result;

void rc_protect_init(rc_image *img);          /* 字符串池解密 + 完整性基线 + 首次检测 */
void rc_protect_pool_decrypt(void);
int  rc_protect_self_check(void);             /* SHA-512 完整性抽查：0 = 通过 */
int  rc_antidebug_probe(rc_detect_result *out);/* 全量反调试检测，返回命中数 */
void rc_antidebug_report_print(const rc_detect_result *r);
void rc_antidebug_act(const rc_detect_result *r); /* 按策略处置 */
void rc_protect_tick(void);                   /* 运行中偶发抽查（由生成代码在安全点调用） */

/* 密码解锁：pw == NULL 表示未提供。返回 0 = 可以继续；非 0 = 拒绝运行（已打印原因）。
 * 语义：①无锁产物直接返回 0；②有锁但没给密码 → 报错；③密码派生密钥解不开“作者名标记”
 * → 判定密码错误 → 报错；④解开后再解层-1（每构建随机密钥）并做 SHA-512 完整性校验。 */
int  rc_protect_image_unlock(rc_image *img, const char *password);
void rc_protect_after_unlock(void);

/* 编译器在产物里写入的元信息（由 image.c 组装） */
extern const rc_image rc_build_image;

#ifdef __cplusplus
}
#endif
#endif /* RC_API_H */
