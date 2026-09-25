/* rt_parse_stub.c —— 运行时解析器的“弱”占位实现
 *
 * 正式实现由 src/common/parse.c 提供（同一份源码既编进编译器也编进运行时映像，
 * 这样 eval / 动态 source 与编译期用的是**同一套语法**）。此处给弱符号占位，
 * 保证运行时映像在没有链接前端时也能构建；链接到真实现时自动被覆盖。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt_internal.h"

__attribute__((weak))
rc_node *rc_parse_string(const char *src, const char *name, char **err_out)
{
    (void)src;
    if (err_out) {
        char *e = malloc(128);
        snprintf(e, 128, "%s: 运行时解析器尚未链接（eval/source 暂不可用）", name ? name : "eval");
        *err_out = e;
    }
    return NULL;
}

__attribute__((weak))
void rc_node_free_runtime(rc_node *n) { (void)n; }
