/* parse.h —— shell 前端（词法 + 语法）对外接口
 *
 * 同一份实现既编进编译器（host），也编进运行时映像（target），
 * 因此 eval / 动态 source 与编译期用的是**完全相同的语法与 AST**。
 */
#ifndef RC_PARSE_H
#define RC_PARSE_H

#include "shell_ast.h"

/* 诊断：一条错误 */
typedef struct rc_diag {
    int  line, col;
    char msg[256];
    char src_line[256];
} rc_diag;

/* 解析结果 */
typedef struct rc_parse_result {
    rc_node *root;      /* 语句链表（用 next 串联） */
    rc_diag *diags;
    int      ndiags;
} rc_parse_result;

/* 解析一段脚本源码。返回 0 成功；失败时 root 可能仍非 NULL（尽力恢复）。
 * name 用于错误信息（脚本名/<eval>）。diags/diags_cap 由调用方提供。 */
int rc_parse_source(const char *src, const char *name,
                    rc_diag *diags, int diags_cap, int *ndiags_out,
                    rc_node **root_out);

/* 运行期入口（运行时映像里用）：malloc 分配，错误信息 malloc 返回 */
rc_node *rc_parse_string(const char *src, const char *name, char **err_out);
/* 把一段文本当算术式解析（运行时 eval/算术字符串求值共用） */
rc_arith *parse_arith_text(const char *s, size_t len);

/* 释放运行期（非 arena）解析结果 */
void rc_node_free_runtime(rc_node *n);

/* 调试：把 AST 打印成树（-g2 用） */
void rc_ast_dump(const rc_node *n, int indent, void (*out)(const char *line, void *ud), void *ud);

/* 统计（-g1 用） */
typedef struct { uint32_t n_nodes, n_words, n_segs, n_strings; } rc_ast_stats;
void rc_ast_count(const rc_node *n, rc_ast_stats *st);

#endif /* RC_PARSE_H */
