/* test_parse.c —— 前端自检：把脚本解析成 AST 并统计 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parse.h"
static void out_line(const char *l, void *ud) { (void)ud; puts(l); }
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "-";
    FILE *f = (!strcmp(path, "-")) ? stdin : fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    if (f != stdin) fclose(f);
    rc_diag d[64];
    int nd = 0;
    rc_node *root = NULL;
    int r = rc_parse_source(buf, path, d, 64, &nd, &root);
    for (int i = 0; i < nd; i++) {
        fprintf(stderr, "%s:%d:%d: 错误: %s\n", path, d[i].line, d[i].col, d[i].msg);
        if (d[i].src_line[0]) {
            fprintf(stderr, "    %s\n", d[i].src_line);
            fprintf(stderr, "    ");
            for (int k = 1; k < d[i].col; k++) fputc(' ', stderr);
            fprintf(stderr, "^\n");
        }
    }
    rc_ast_stats st = {0};
    rc_ast_count(root, &st);
    printf("== AST ==\n");
    rc_ast_dump(root, 0, out_line, NULL);
    printf("== 统计: 节点 %u, 词 %u, 片段 %u, 字符串 %u, 诊断 %d, %s ==\n",
           st.n_nodes, st.n_words, st.n_segs, st.n_strings, nd, r ? "有错" : "无错");
    return r;
}
