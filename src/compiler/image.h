/* image.h —— 映像组装（AST 序列化 + 字符串池） */
#ifndef S2A_IMAGE_H
#define S2A_IMAGE_H

#include <stdint.h>
#include "s2a.h"

typedef struct {
    unsigned char *ro;      /* 只读段：AST 结构（绝对地址已回填） */
    uint32_t       ro_len;
    char          *pool;    /* 字符串池（明文；外部负责加密） */
    uint32_t       pool_len;
    uint32_t       n_objects;
    uint32_t       n_strings;
} s2a_image_parts;

/* 布局确定后的地址查询（codegen 用） */
uint64_t s2a_obj_va(const void *p);
uint64_t s2a_str_va(const char *s);

int s2a_build_image(const s2a_options *o, rc_node *root, uint64_t ro_va, uint64_t rw_va,
                    s2a_image_parts *out);

#endif
