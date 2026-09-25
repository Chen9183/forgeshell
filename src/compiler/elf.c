/* elf.c —— 纯静态模板 ELF 的解析与追加式链接（见 elf.h） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf.h"
#include <sys/stat.h>
#include "../common/safeperm.h"

#define PT_LOAD     1
#define PT_INTERP   3
#define SHT_SYMTAB  2
#define SHT_STRTAB  3

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const unsigned char *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
static void wr16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static void wr64(unsigned char *p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }

int elf_load(s2a_elf *e, const unsigned char *data, size_t len)
{
    memset(e, 0, sizeof *e);
    if (len < 64 || memcmp(data, "\177ELF", 4) != 0) return -1;
    if (data[4] != 2 || data[5] != 1) return -1;          /* 只支持 ELF64 LE */
    e->buf = malloc(len);
    memcpy(e->buf, data, len);
    e->len = len;

    e->e_entry = rd64(e->buf + 24);
    e->e_phoff = rd64(e->buf + 32);
    e->e_shoff = rd64(e->buf + 40);
    e->e_phentsize = rd16(e->buf + 54);
    e->e_phnum = rd16(e->buf + 56);
    e->e_shentsize = rd16(e->buf + 58);
    e->e_shnum = rd16(e->buf + 60);

    e->nph = e->e_phnum;
    e->phdrs = calloc((size_t)e->nph + 4, sizeof(s2a_phdr));
    for (int i = 0; i < e->nph; i++) {
        const unsigned char *p = e->buf + e->e_phoff + (size_t)i * e->e_phentsize;
        e->phdrs[i].type = rd32(p);
        e->phdrs[i].flags = rd32(p + 4);
        e->phdrs[i].off = rd64(p + 8);
        e->phdrs[i].vaddr = rd64(p + 16);
        e->phdrs[i].paddr = rd64(p + 24);
        e->phdrs[i].filesz = rd64(p + 32);
        e->phdrs[i].memsz = rd64(p + 40);
        e->phdrs[i].align = rd64(p + 48);
    }

    /* 符号表（模板必须保留符号表，编译器靠它拿运行时地址） */
    if (e->e_shoff && e->e_shnum) {
        size_t sym_off = 0, sym_size = 0, sym_ent = 0, str_off = 0;
        for (int i = 0; i < e->e_shnum; i++) {
            const unsigned char *p = e->buf + e->e_shoff + (size_t)i * e->e_shentsize;
            uint32_t type = rd32(p + 4);
            if (type == SHT_SYMTAB) {
                sym_off = rd64(p + 24);
                sym_size = rd64(p + 32);
                sym_ent = rd64(p + 56);
                uint32_t link = rd32(p + 40);
                const unsigned char *q = e->buf + e->e_shoff + (size_t)link * e->e_shentsize;
                str_off = rd64(q + 24);
            }
        }
        if (sym_off && sym_ent) {
            int n = (int)(sym_size / sym_ent);
            e->syms = calloc((size_t)n, sizeof(s2a_sym));
            for (int i = 0; i < n; i++) {
                const unsigned char *p = e->buf + sym_off + (size_t)i * sym_ent;
                uint32_t nameoff = rd32(p);
                uint16_t shndx = rd16(p + 6);
                uint64_t value = rd64(p + 8);
                uint64_t size = rd64(p + 16);
                if (!nameoff || shndx == 0) continue;
                const char *nm = (const char *)e->buf + str_off + nameoff;
                e->syms[e->nsyms].name = strdup(nm);
                e->syms[e->nsyms].value = value;
                e->syms[e->nsyms].size = (uint32_t)size;
                e->nsyms++;
            }
        }
    }
    return 0;
}

void elf_free(s2a_elf *e)
{
    for (int i = 0; i < e->nsyms; i++) free(e->syms[i].name);
    free(e->syms);
    free(e->phdrs);
    free(e->buf);
    memset(e, 0, sizeof *e);
}

uint64_t elf_sym(const s2a_elf *e, const char *name)
{
    for (int i = 0; i < e->nsyms; i++)
        if (strcmp(e->syms[i].name, name) == 0) return e->syms[i].value;
    return 0;
}

int elf_vaddr_to_off(const s2a_elf *e, uint64_t va, size_t *off)
{
    for (int i = 0; i < e->nph; i++) {
        const s2a_phdr *p = &e->phdrs[i];
        if (p->type != PT_LOAD) continue;
        if (va >= p->vaddr && va < p->vaddr + p->filesz) {
            if (off) *off = (size_t)(p->off + (va - p->vaddr));
            return 0;
        }
    }
    return -1;
}

uint64_t elf_max_vaddr_end(const s2a_elf *e)
{
    uint64_t m = 0;
    for (int i = 0; i < e->nph; i++)
        if (e->phdrs[i].type == PT_LOAD && e->phdrs[i].vaddr + e->phdrs[i].memsz > m)
            m = e->phdrs[i].vaddr + e->phdrs[i].memsz;
    return m;
}

int elf_has_interp(const s2a_elf *e)
{
    for (int i = 0; i < e->nph; i++)
        if (e->phdrs[i].type == PT_INTERP) return 1;
    return 0;
}

/* ---------------------------------------------------------------- 规划 */
#define PAGE 0x1000

static size_t alignup(size_t v, size_t a) { return (v + a - 1) / a * a; }

int s2a_plan(s2a_elf *e, uint32_t code_size, uint32_t ro_size, uint32_t rw_size,
             int ph_entries, s2a_layout *out)
{
    size_t ph_size = (size_t)ph_entries * e->e_phentsize;
    size_t seg_off = alignup(e->len, PAGE);
    size_t code_off = alignup(seg_off, 16);
    /* 程序头表必须放在某个 LOAD 段的文件范围内：AT_PHDR = 段基址 + (e_phoff - 段偏移)，
       内核与 qemu 都按这个公式算（若表在附加段里，qemu 会退化成「首个 LOAD 基址 + e_phoff」，
       AT_PHDR 落到未映射区 → musl __init_tls 扫程序头时段错误，实测踩过）。
       做法：把表写进第一个 LOAD 段文件数据之后，并把该段 filesz/memsz 扩到覆盖它。 */
    {
        int i0 = -1; uint64_t bva = 0;
        for (int i = 0; i < e->nph; i++)
            if (e->phdrs[i].type == PT_LOAD && (i0 < 0 || e->phdrs[i].vaddr < bva)) {
                i0 = i; bva = e->phdrs[i].vaddr;
            }
        if (i0 >= 0) {
            size_t off0 = (size_t)e->phdrs[i0].off;
            size_t end0 = off0 + (size_t)e->phdrs[i0].filesz;
            size_t ph = alignup(end0, 16);
            uint64_t need = (uint64_t)(ph + ph_size);          /* 需要的文件末端 */
            if (need > e->phdrs[i0].filesz + off0) {
                /* filesz 与 memsz 拉平成页对齐：qemu 明确拒绝「可执行段带 bss」，内核虽容忍但不标准 */
                uint64_t nf = alignup((size_t)(need - off0), PAGE);
                e->phdrs[i0].filesz = nf;
                e->phdrs[i0].memsz = nf;
            }
            out->ph_off = ph;
        } else {
            out->ph_off = seg_off;
        }
    }
    size_t ro_off = alignup(code_off + code_size, 16);
    size_t rw_seg_off = alignup(ro_off + ro_size, PAGE);
    size_t rw_data_off = rw_seg_off;                 /* RW 段起始即数据起点 */
    uint64_t seg_va = alignup((size_t)elf_max_vaddr_end(e), PAGE);

    out->seg_off = seg_off;
    out->seg_va = seg_va;
    out->code_off = code_off;
    out->code_va = seg_va + (code_off - seg_off);
    out->ro_off = ro_off;
    out->ro_va = seg_va + (ro_off - seg_off);
    out->rw_off = rw_seg_off;
    out->rw_va = seg_va + (rw_seg_off - seg_off);
    out->rw_data_off = rw_data_off;
    out->end_off = rw_seg_off + rw_size;
    (void)rw_data_off;
    return 0;
}

/* ---------------------------------------------------------------- 写出 */
int s2a_write(s2a_elf *e, const s2a_layout *lay,
              const unsigned char *code, uint32_t code_size,
              const unsigned char *ro, uint32_t ro_size,
              unsigned char *rw, uint32_t rw_size,
              const char *patch_sym, const char *outpath, int strip)
{
    size_t total = lay->end_off;
    unsigned char *out = calloc(1, total ? total : 1);
    if (!out) return -1;
    memcpy(out, e->buf, e->len);

    /* 数据段 */
    if (code_size) memcpy(out + lay->code_off, code, code_size);
    if (ro_size) memcpy(out + lay->ro_off, ro, ro_size);
    if (rw_size) memcpy(out + lay->rw_data_off, rw, rw_size);

    /* 新的 phdr 表：原有条目 + RX + RW */
    int nold = e->nph;
    unsigned char *ph = out + lay->ph_off;
    for (int i = 0; i < nold; i++) {
        unsigned char *p = ph + (size_t)i * e->e_phentsize;
        const s2a_phdr *s = &e->phdrs[i];
        wr32(p, s->type); wr32(p + 4, s->flags);
        wr64(p + 8, s->off); wr64(p + 16, s->vaddr); wr64(p + 24, s->paddr);
        wr64(p + 32, s->filesz); wr64(p + 40, s->memsz); wr64(p + 48, s->align);
    }
    /* RX：phdr 表 + 代码 + 只读数据（R E = 5） */
    {
        unsigned char *p = ph + (size_t)nold * e->e_phentsize;
        wr32(p, PT_LOAD); wr32(p + 4, 5);
        wr64(p + 8, lay->seg_off);
        wr64(p + 16, lay->seg_va); wr64(p + 24, lay->seg_va);
        wr64(p + 32, (uint64_t)(lay->ro_off + ro_size - lay->seg_off));
        wr64(p + 40, (uint64_t)(lay->ro_off + ro_size - lay->seg_off));
        wr64(p + 48, PAGE);
    }
    /* RW：字符串池 + 映像结构（R W = 6） */
    {
        unsigned char *p = ph + (size_t)(nold + 1) * e->e_phentsize;
        wr32(p, PT_LOAD); wr32(p + 4, 6);
        wr64(p + 8, lay->rw_data_off);
        wr64(p + 16, lay->rw_va); wr64(p + 24, lay->rw_va);
        wr64(p + 32, rw_size); wr64(p + 40, rw_size);
        wr64(p + 48, PAGE);
    }

    wr64(out + 32, (uint64_t)lay->ph_off);            /* e_phoff */
    wr16(out + 56, (uint16_t)(nold + 2));             /* e_phnum */
    if (strip) {
        wr64(out + 40, 0);                            /* e_shoff = 0 */
        wr16(out + 60, 0);                            /* e_shnum = 0 */
        wr16(out + 62, 0);                            /* e_shstrndx = 0 */
    }

    /* patch 函数指针 → 生成代码入口 */
    if (patch_sym) {
        uint64_t va = elf_sym(e, patch_sym);
        if (!va) {
            fprintf(stderr, "s2a: 模板里找不到符号 %s\n", patch_sym);
            free(out);
            return -1;
        }
        size_t off = 0;
        if (elf_vaddr_to_off(e, va, &off) != 0) {
            fprintf(stderr, "s2a: 符号 %s (%#llx) 不在任何文件映射段内，无法 patch\n",
                    patch_sym, (unsigned long long)va);
            free(out);
            return -1;
        }
        wr64(out + off, lay->code_va);
    }

    FILE *f = fopen(outpath, "wb");
    if (!f) {
        fprintf(stderr, "s2a: 无法写入 %s\n", outpath);
        free(out);
        return -1;
    }
    size_t w = fwrite(out, 1, total, f);
    fclose(f);
    free(out);
    if (w != total) return -1;
    /* 只给普通文件设权限：`-o /dev/null` 之类绝不能落到设备节点上（2026-09-19 事故） */
    if (!s2a_set_perm(outpath, 0755) && s2a_perm_special_path(outpath))
        fprintf(stderr, "s2a: 提示：输出 %s 不是普通文件，已跳过权限设置\n", outpath);
    return 0;
}
