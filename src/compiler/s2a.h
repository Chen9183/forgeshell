/* s2a.h —— 编译器内部共享声明 */
#ifndef S2A_H
#define S2A_H

#include <stdint.h>
#include <stddef.h>
#include "shell_ast.h"
#include "parse.h"
#include "emit_a64.h"

/* 优化级别 / 防护级别 / 调试级别 */
typedef struct {
    const char *input;        /* 脚本路径（-e 时为 NULL） */
    const char *eval_text;    /* -e 直接编译字符串 */
    const char *output;       /* -o */
    int   opt_level;          /* 0..4 */
    int   prot_level;         /* 0 = 关；1 = 基础；2 = 强化；3 = 全力 */
    int   debug_level;        /* 0..5（-g1..-g5） */
    int   keep_intermediate;  /* -K */
    int   strip;
    int easy;         /* -easy：外壳 + 内嵌 ash + 脚本，不编译 */
    int no_tools;      /* --no-tools：产物不内嵌 qemu/ash */              /* -s */
    int   run_emu;            /* -r */
    int   run_iso;            /* --iso / -X */
    int   emu_trace;
    int   no_fallback;        /* --no-fallback / --emu-own：不回退 */
    int   emu_qemu;           /* -qemu / --emu-qemu：直接强制用 qemu 执行 */
    const char *password;     /* --password */
    int   anti_action;        /* RC_ANTI_* */
    const char *iso_args;     /* --iso-opt 透传 */
    const char *argv_rest[64];/* 运行模式下的其余参数 */
    int   argv_rest_n;
    const char *self;         /* argv[0] */
} s2a_options;

/* 全局：当前代码块虚拟地址（发射绝对调用时算偏移用） */
extern uint64_t s2a_code_va;

/* 诊断输出（带级别与颜色） */
void s2a_info(const s2a_options *o, int level, const char *fmt, ...);
void s2a_warn(const char *fmt, ...);
void s2a_error(const char *fmt, ...);

/* 模板（由 build/template_blob.o 提供） */
extern const unsigned char _binary_build_template_elf_start[];
extern const unsigned char _binary_build_template_elf_end[];

/* 阶段函数 */
/* 运行时符号地址解析（由 main.c 提供：读模板符号表） */
uint64_t s2a_rt_sym(const char *name);

int  s2a_codegen(const s2a_options *o, rc_node *root, emit_buf *code,
                 uint64_t ro_va, uint64_t rw_va, uint64_t code_va, uint64_t image_va);

int  s2a_emu_run(const s2a_options *o, const char *elf_path, int argc, char **argv);
int  s2a_iso_run(const s2a_options *o, const char *elf_path, int argc, char **argv);

#endif /* S2A_H */
