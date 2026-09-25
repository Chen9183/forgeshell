/* elf.h —— 纯静态模板 ELF 的解析与“追加式链接”
 *
 * 模型（架构 A 的核心）：
 *   编译器内嵌一份预链接好的静态 musl 运行时模板 ELF（带符号表），编译脚本时：
 *     ① 解析模板拿程序头/符号表 → 得到 rc_* 的绝对 vaddr（生成代码直接 bl 它们）；
 *     ② 追加两个 PT_LOAD：RX（内含 phdr 表 + 生成代码 + 只读数据）与 RW（字符串池 + 映像结构）；
 *        —— phdr 表必须落在某个 PT_LOAD 的 [p_offset, p_offset+p_filesz) 内，
 *           否则内核给出的 AT_PHDR 指向未映射页，musl 启动即崩（v1.4 原型踩过）；
 *     ③ patch 模板 .data 里的 rc_user_main 指针 → 生成代码入口；
 *     ④ 输出可剥离节头的纯静态 ELF（无 PT_INTERP、无 .dynamic）。
 */
#ifndef S2A_ELF_H
#define S2A_ELF_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t type, flags;
    uint64_t off, vaddr, paddr, filesz, memsz, align;
} s2a_phdr;

typedef struct { char *name; uint64_t value; uint32_t size; } s2a_sym;

typedef struct {
    unsigned char *buf;      /* 模板字节（可改写） */
    size_t len;
    uint64_t e_entry, e_phoff, e_shoff;
    uint16_t e_phnum, e_shnum, e_phentsize, e_shentsize;
    s2a_phdr *phdrs;
    int nph;
    s2a_sym *syms;
    int nsyms;
} s2a_elf;

/* 追加区域布局（编译器据此发射绝对地址） */
typedef struct {
    size_t   ph_off;      /* 新 phdr 表在文件中的偏移（放进第一个 LOAD 段的范围内） */
    size_t   seg_off;     /* RX 段（代码+只读数据）在文件中的偏移 */
    uint64_t seg_va;      /* RX 段虚拟地址 */
    size_t   code_off;    uint64_t code_va;
    size_t   ro_off;      uint64_t ro_va;
    size_t   rw_off;      uint64_t rw_va;    /* RW 段自身起始 */
    size_t   rw_data_off; /* RW 段内数据偏移 */
    size_t   end_off;     /* 追加区末尾（文件长度） */
} s2a_layout;

int      elf_load(s2a_elf *e, const unsigned char *data, size_t len);
void     elf_free(s2a_elf *e);
uint64_t elf_sym(const s2a_elf *e, const char *name);        /* 0 = 未找到 */
int      elf_vaddr_to_off(const s2a_elf *e, uint64_t va, size_t *off);
uint64_t elf_max_vaddr_end(const s2a_elf *e);
int      elf_has_interp(const s2a_elf *e);                   /* 应为 0（纯静态） */

/* 规划追加布局：需要在发射代码**之前**调用以取得绝对地址 */
int s2a_plan(s2a_elf *e, uint32_t code_size, uint32_t ro_size, uint32_t rw_size,
             int ph_entries, s2a_layout *out);

/* 写出产物：
 *   code/ro/rw 三个缓冲；patch_sym 指向的函数指针会被改成代码入口；strip 时清掉节头 */
int s2a_write(s2a_elf *e, const s2a_layout *lay,
              const unsigned char *code, uint32_t code_size,
              const unsigned char *ro, uint32_t ro_size,
              unsigned char *rw, uint32_t rw_size,
              const char *patch_sym, const char *outpath, int strip);

#endif /* S2A_ELF_H */
