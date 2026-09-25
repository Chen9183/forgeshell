/* emu.c —— 内置 AArch64 用户态模拟器（s2a -r：**不真实执行**产物）
 *
 * 设计（DESIGN.md §7）：
 *   · 逐条解释产物机器码；寄存器与内存全由本模拟器掌管，产物代码不在宿主上执行；
 *   · system call 全部由模拟器代理，写操作落进**虚拟文件系统**（临时目录覆盖层），
 *     所以跑陌生脚本不会动真实磁盘；
 *   · 产物里的反调试（ptrace(PTRACE_TRACEME) 等）在模拟器内返回“未被跟踪”，
 *     这与常见做法的处理一致，也让产物在模拟器里保持可观测；
 *   · 未实现的指令**明确报错**（pc + 编码 + 最近分支），绝不静默跳过。
 *
 * ISA 覆盖：A64 通用寄存器指令集、分支/异常、各类访存、独占访存、位域、条件选择、
 *          PC 相对寻址、BTI/PAC（视作空操作）、常用 FP 传送与算术；其余在真执行到时报告。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include "s2a.h"

/* ============================ 内存模型 ============================ */
/* ---- 省内存布局：guest 只保留 512MB 虚拟地址空间，且**按需提交** ----
 * （之前用 2.25GB 虚拟 + 每次 mmap 都 memset，把宿主内存吃紧了；
 *   现在不主动触碰匿名页，虚拟映射本身不占物理内存。）*/
#define GUEST_SIZE   0x20000000ull     /* 512MB 虚拟 */
#define TLS_SCRATCH  0x01000000ull     /* 16MB 处：初始 TLS 暂存区 */
#define HEAP_BASE    0x02000000ull     /* 32MB 起：brk 堆 */
#define HEAP_LIMIT   0x04000000ull     /* 64MB 止 */
#define MMAP_BASE    0x10000000ull     /* 256MB 起：guest mmap 区 */
#define MMAP_LIMIT   0x1C000000ull     /* 448MB 止 */
#define STACK_TOP    0x1F800000ull     /* 504MB：栈顶 */
#define STACK_SIZE   0x00100000ull
/* guest 侧内存硬上限：超过就返回 ENOMEM，绝不把宿主拖进 OOM */
#define GUEST_MMAP_MAXLEN   (16ull << 20)     /* 单次 mmap 上限 16MB */
#define GUEST_MMAP_TOTAL    (64ull << 20)     /* mmap 总量上限 64MB */

typedef struct {
    uint64_t r[31];           /* x0..x30 */
    uint64_t sp;
    uint64_t pc;
    uint32_t nzcv;            /* N=8 Z=4 C=2 V=1 */
    uint64_t v[32][2];        /* 128 位浮点/SIMD 寄存器（2×u64） */
    uint64_t excl_addr;
    int      excl_size; int excl_valid;   /* 独占访存 */
    uint64_t tpidr;                       /* TPIDR_EL0：TLS 线程指针 */
    int      exited; uint64_t exit_code;
    int      trace;
    long     steps;
    char     overlay[512];    /* 虚拟文件系统覆盖层根目录 */
    int      readonly_sandbox;/* 1 = 找不到可写目录：写操作一律返回 EROFS */
    uint64_t brk_cur, brk_end;
    uint64_t mmap_cur;
    uint64_t mmap_used;       /* guest mmap 累计（省内存限流用） */
} cpu;

static uint8_t *g_base;       /* guest 内存基址：guest_va → g_base + va */
/* 内存写入监视（S2A_EMU_WATCH=起始地址[:结束地址]，只对储存在该区间的写操作报警） */
static uint64_t g_watch_lo, g_watch_hi;

/* 统一的访存范围检查：注意必须写成 GUEST_SIZE-va<n 的形式，
 * 否则 va 接近 2^64 时 va+n 回绕会绕过检查，导致宿主进程自己段错误。 */
/* 真机会映射的区域（装载与 syscall 维护）。强制模式下落在这之外 = 未映射 → 越界。
   早期是整块 512MB 平坦映射，野指针也能静静写进去，于是"模拟器跑得好好的、真机崩"。 */
static uint64_t g_img_lo, g_img_hi;
static uint64_t g_brk_lo, g_brk_hi;
static uint64_t g_mmap_lo, g_mmap_hi;
static uint64_t g_stack_lo, g_stack_hi;
static uint64_t g_tls_lo, g_tls_hi;
static int g_hole_enforce = -1;          /* 默认开启；S2A_EMU_NO_HOLECHECK=1 关闭 */

static inline int bad_range(uint64_t va, uint64_t n)
{
    if (va >= GUEST_SIZE || GUEST_SIZE - va < n) return 1;
    /* 第 0 页在真机上永远不映射（mmap_min_addr），我们的平坦映射却有 → 会让 NULL 解引用
       这类 bug 静默通过。这里按内核行为把它当未映射，好让这类 bug 早暴露。 */
    if (va < 0x10000) return 1;
    if (g_hole_enforce < 0) g_hole_enforce = getenv("S2A_EMU_NO_HOLECHECK") ? 0 : 1;
    if (!g_hole_enforce) return 0;
    if (va >= g_img_lo && va + n <= g_img_hi) return 0;
    if (va >= g_brk_lo && va + n <= g_brk_hi) return 0;
    if (va >= g_mmap_lo && va + n <= g_mmap_hi) return 0;
    if (va >= g_stack_lo && va + n <= g_stack_hi) return 0;
    if (va >= g_tls_lo && va + n <= g_tls_hi) return 0;
    return 1;                             /* 落在"洞"里 → 真机此处未映射 */
}

static int emu_read(cpu *c, uint64_t va, void *dst, size_t n)
{
    (void)c;
    if (bad_range(va, n)) return -1;
    memcpy(dst, g_base + va, n);
    return 0;
}

static int emu_write(cpu *c, uint64_t va, const void *src, size_t n)
{
    (void)c;
    if (bad_range(va, n)) return -1;
    memcpy(g_base + va, src, n);
    return 0;
}

static uint64_t emu_cstr_len(uint64_t va)
{
    uint64_t n = 0;
    while (va + n < GUEST_SIZE && g_base[va + n]) n++;
    return n;
}

static char *emu_cstr_dup(uint64_t va)
{
    uint64_t n = emu_cstr_len(va);
    char *s = malloc((size_t)n + 1);
    memcpy(s, g_base + va, (size_t)n);
    s[n] = 0;
    return s;
}

/* ============================ ELF 装载 ============================ */
typedef struct { uint64_t entry, phoff, phent, phnum, image_end, phdr_addr; } loaded;

static struct { uint64_t lo, hi; } g_noprot[16];
static int g_nnoprot;

static void seg_add(uint64_t lo, uint64_t hi, int w);
static int  guest_writable(uint64_t va, uint64_t len);
static void seg_fault(cpu *c, uint64_t va, const char *what, int nbytes);
static int  emu_simd(cpu *c, uint32_t insn, uint64_t pc);

static int load_elf_into_guest(const char *path, loaded *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "s2a 模拟器: 打不开 %s: %s\n", path, strerror(errno)); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 64) { fclose(f); return -1; }
    unsigned char *buf = malloc((size_t)sz);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); return -1; }
    fclose(f);

    if (memcmp(buf, "\177ELF", 4) != 0 || buf[4] != 2 || buf[5] != 1) {
        fprintf(stderr, "s2a 模拟器: %s 不是 ELF64 小端文件\n", path);
        free(buf);
        return -1;
    }
    out->entry = *(uint64_t *)(buf + 24);
    out->phoff = *(uint64_t *)(buf + 32);
    out->phent = *(uint16_t *)(buf + 54);
    out->phnum = *(uint16_t *)(buf + 56);

    for (uint64_t i = 0; i < out->phnum; i++) {
        const unsigned char *p = buf + out->phoff + (size_t)i * out->phent;
        uint32_t type = *(uint32_t *)p;
        if (type != 1) continue;                       /* 只看 PT_LOAD */
        uint64_t off = *(uint64_t *)(p + 8);
        uint64_t va = *(uint64_t *)(p + 16);
        uint64_t filesz = *(uint64_t *)(p + 32);
        uint64_t memsz = *(uint64_t *)(p + 40);
        if (va + memsz > GUEST_SIZE) {
            fprintf(stderr, "s2a 模拟器: 段 %#llx 超出可寻址范围\n", (unsigned long long)va);
            free(buf);
            return -1;
        }
        if (off + filesz > (uint64_t)sz) { free(buf); return -1; }
        seg_add(va, va + memsz, (*(uint32_t *)(p + 4)) & 2);   /* PT_LOAD 的 W 位 */
        memcpy(g_base + va, buf + off, (size_t)filesz);
        if (memsz > filesz) memset(g_base + va + filesz, 0, (size_t)(memsz - filesz));
        if (va + memsz > out->image_end) out->image_end = va + memsz;
    }
    out->image_end = (out->image_end + 0xFFFull) & ~0xFFFull;

    /* AT_PHDR 必须是程序头表**映射后的虚拟地址**（不是文件偏移）：
     * 找出包含 e_phoff 的那个 PT_LOAD，地址 = p_vaddr + (e_phoff - p_offset)。*/
    out->phdr_addr = 0;
    for (uint64_t i = 0; i < out->phnum; i++) {
        const unsigned char *p = buf + out->phoff + (size_t)i * out->phent;
        if (*(uint32_t *)p != 1) continue;
        uint64_t off = *(uint64_t *)(p + 8), va = *(uint64_t *)(p + 16);
        uint64_t filesz = *(uint64_t *)(p + 32);
        if (out->phoff >= off && out->phoff < off + filesz) {
            out->phdr_addr = va + (out->phoff - off);
            break;
        }
    }
    free(buf);
    return 0;
}

/* 构造入口栈（argc/argv/envp/auxv）—— musl 的 _start 依赖它。
 * 注意顺序：先放字符串与 AT_RANDOM 数据，再放参数块，**返回参数块的地址**（sp 指向 argc）。*/
static uint64_t setup_stack(cpu *c, const loaded *L, int argc, char **argv)
{
    (void)c;
    uint64_t sp = STACK_TOP - 16;
    extern char **environ;
    /* 默认用真实环境（像真 shell 一样）；S2A_EMU_ENV_STRICT=1 时用固定 3 项环境，
     * 便于与真机（gdb 会带 LINES/COLUMNS）逐条对拍时数组长度一致。 */
    static char *strict_env[] = { (char *)"PATH=/usr/bin:/bin", NULL };
    if (getenv("S2A_EMU_ENV_STRICT")) environ = strict_env;
    int envc = 0;
    while (environ[envc]) envc++;

    uint64_t strva[512];
    int nstr = 0;
    for (int i = 0; i < argc && nstr < 256; i++) {
        size_t l = strlen(argv[i]) + 1;
        sp -= l;
        memcpy(g_base + sp, argv[i], l);
        strva[nstr++] = sp;
    }
    int env_start = nstr;
    for (int i = 0; i < envc && nstr < 500; i++) {
        size_t l = strlen(environ[i]) + 1;
        sp -= l;
        memcpy(g_base + sp, environ[i], l);
        strva[nstr++] = sp;
    }

    /* AT_RANDOM（16 字节）与 AT_PLATFORM 字符串 */
    sp -= 16;
    uint64_t rand_va = sp;
    for (int i = 0; i < 16; i++) g_base[sp + i] = (uint8_t)(rand() & 0xFF);
    sp -= 8;
    uint64_t plat_va = sp;
    memcpy(g_base + sp, "aarch64", 8);

    /* 参数块：argc | argv[] | NULL | envp[] | NULL | auxv[] | AT_NULL */
    uint64_t words = 1 + (uint64_t)argc + 1 + (uint64_t)envc + 1 + 40;
    sp -= words * 8;
    sp &= ~15ull;                       /* ABI 要求 16 字节对齐 */
    uint64_t block = sp;                /* ← 这就是要返回的 sp（指向 argc） */

    uint64_t *w = (uint64_t *)(g_base + block);
    int k = 0;
    w[k++] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) w[k++] = strva[i];
    w[k++] = 0;
    for (int i = 0; i < envc; i++) w[k++] = strva[env_start + i];
    w[k++] = 0;

    struct { uint64_t k, v; } aux[] = {
        { 3, L->phdr_addr }, { 4, L->phent }, { 5, L->phnum }, { 6, 4096 },
        { 7, 0 },         { 8, 0 },        { 9, L->entry }, { 11, 0 }, { 12, 0 },
        { 13, 0 },        { 14, 0 },       { 15, plat_va }, { 16, 0 }, { 17, 100 },
        { 23, 0 },        { 25, rand_va }, { 26, 0 },       { 0, 0 },
    };
    for (size_t i = 0; i < sizeof aux / sizeof aux[0]; i++) {
        w[k++] = aux[i].k;
        w[k++] = aux[i].v;
    }
    return block;
}

/* ============================ 寄存器/标志工具 ============================ */
static inline uint64_t get_x(cpu *c, int n) { return n == 31 ? 0 : c->r[n]; }
static inline void set_x(cpu *c, int n, uint64_t v) { if (n != 31) c->r[n] = v; }
static inline uint64_t get_z(cpu *c, int n) { return n == 31 ? c->sp : c->r[n]; }
static inline void set_z(cpu *c, int n, uint64_t v) { if (n == 31) c->sp = v; else c->r[n] = v; }

#define N_BIT 8
#define Z_BIT 4
#define C_BIT 2
#define V_BIT 1
static inline void set_flags(cpu *c, int n, int z, int cc, int v)
{
    c->nzcv = (uint32_t)((n ? N_BIT : 0) | (z ? Z_BIT : 0) | (cc ? C_BIT : 0) | (v ? V_BIT : 0));
}
static inline int flag_n(cpu *c) { return (c->nzcv & N_BIT) != 0; }
static inline int flag_z(cpu *c) { return (c->nzcv & Z_BIT) != 0; }
static inline int flag_c(cpu *c) { return (c->nzcv & C_BIT) != 0; }
static inline int flag_v(cpu *c) { return (c->nzcv & V_BIT) != 0; }

static int cond_ok(cpu *c, int cond)
{
    int n = flag_n(c), z = flag_z(c), cc = flag_c(c), v = flag_v(c);
    int base = cond >> 1, inv = cond & 1;
    int r;
    switch (base) {
    case 0: r = z; break;                 /* EQ/NE */
    case 1: r = cc; break;                /* CS/CC */
    case 2: r = n; break;                 /* MI/PL */
    case 3: r = v; break;                 /* VS/VC */
    case 4: r = cc && !z; break;          /* HI/LS */
    case 5: r = (n == v); break;          /* GE/LT */
    case 6: r = (n == v) && !z; break;    /* GT/LE */
    default: r = 1; break;                /* AL */
    }
    return inv ? !r : r;
}

static uint64_t do_shift(uint64_t v, int type, int amount, int is64)
{
    if (!is64) v = (uint32_t)v;
    if (amount == 0 && type != 3) return v;
    switch (type) {
    case 0: return is64 ? (v << amount) : (uint64_t)(uint32_t)((uint32_t)v << amount);
    case 1: return is64 ? (v >> amount) : (uint64_t)((uint32_t)v >> amount);
    case 2: return is64 ? (uint64_t)((int64_t)v >> amount)
                        : (uint64_t)(uint32_t)((int32_t)(uint32_t)v >> amount);
    default:
        if (is64) return (v >> amount) | (v << ((64 - amount) & 63));
        return (uint64_t)(uint32_t)(((uint32_t)v >> amount) | ((uint32_t)v << ((32 - amount) & 31)));
    }
}

static uint64_t do_extend(uint64_t v, int option, int shift, int is64)
{
    uint64_t r;
    switch (option) {
    case 0: r = (uint64_t)(uint8_t)v; break;
    case 1: r = (uint64_t)(uint16_t)v; break;
    case 2: r = (uint64_t)(uint32_t)v; break;
    case 3: r = v; break;
    case 4: r = (uint64_t)(int64_t)(int8_t)v; break;
    case 5: r = (uint64_t)(int64_t)(int16_t)v; break;
    case 6: r = (uint64_t)(int64_t)(int32_t)v; break;
    default: r = v; break;
    }
    r <<= shift;
    return is64 ? r : (uint64_t)(uint32_t)r;
}

/* ARM ARM 的 DecodeBitMasks（用于逻辑立即数） */
static int decode_bit_masks(uint32_t immn, uint32_t imms, uint32_t immr, int is64, uint64_t *wmask)
{
    int len = 31 - __builtin_clz(((immn & 1) << 6) | ((~imms) & 0x3F));
    if (len < 1) return -1;
    uint64_t levels = (1ull << len) - 1;
    if ((!is64 && len > 31) || len > 6) return -1;
    uint64_t s = imms & levels, r = immr & levels;
    if (s == levels) return -1;
    uint64_t esize = 1ull << len;
    uint64_t welem = (s + 1 >= 64) ? ~0ull : ((1ull << (s + 1)) - 1);
    r &= (esize - 1);
    uint64_t tmp = (r == 0) ? welem : ((welem >> r) | (welem << (esize - r)));
    uint64_t m = 0;
    if (esize == 64) m = tmp;
    else { for (uint64_t i = 0; i < 64; i += esize) m |= tmp << i; }
    if (!is64) m = (uint64_t)(uint32_t)m;
    *wmask = m;
    return 0;
}

/* ============================ 目录选择（环境变量覆盖 + 回退） ============================ */
/* 覆盖层根目录的选择顺序：
 *   ① $S2A_EMU_OVERLAY（本模拟器专用，最高优先）
 *   ② $S2A_TMPDIR  ③ $TMPDIR（通用约定）
 *   ④ /tmp ⑤ /data/local/tmp（安卓宿主常见） ⑥ /sdcard ⑦ 当前目录
 * 只要有一个可写就用它；全都不可写时进入“只读沙箱”模式（写操作返回 EROFS，不崩）。*/
static const char *g_overlay_base;

static const char *pick_base_dir(void)
{
    const char *cand[8];
    int n = 0;
    const char *e;
    if ((e = getenv("S2A_EMU_OVERLAY")) && *e) cand[n++] = e;
    if ((e = getenv("S2A_TMPDIR")) && *e) cand[n++] = e;
    if ((e = getenv("TMPDIR")) && *e) cand[n++] = e;
    cand[n++] = "/tmp";
    cand[n++] = "/data/local/tmp";
    cand[n++] = "/sdcard";
    cand[n++] = ".";
    for (int i = 0; i < n; i++)
        if (access(cand[i], W_OK) == 0) return cand[i];
    return NULL;      /* 没有可写目录 */
}

/* ============================ 虚拟文件系统 ============================ */
static void vfs_resolve(cpu *c, const char *path, char *out, size_t cap, int for_write)
{
    if (path[0] != '/') { snprintf(out, cap, "%s", path); return; }
    snprintf(out, cap, "%s%s", c->overlay, path);
    if (!for_write && access(out, F_OK) != 0) snprintf(out, cap, "%s", path);
}

/* ============================ 系统调用 ============================ */
typedef struct { uint64_t dev, ino, mode, nlink, uid, gid, rdev, pad1, size, blksize, pad2, blocks,
                 atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns, unused[3]; } kstat_t;

static void fill_kstat(kstat_t *k, const struct stat *st)
{
    memset(k, 0, sizeof *k);
    k->dev = (uint64_t)st->st_dev; k->ino = (uint64_t)st->st_ino; k->mode = (uint32_t)st->st_mode;
    k->nlink = (uint64_t)st->st_nlink; k->uid = (uint64_t)st->st_uid; k->gid = (uint64_t)st->st_gid;
    k->rdev = (uint64_t)st->st_rdev; k->size = (uint64_t)st->st_size;
    k->blksize = (uint64_t)st->st_blksize; k->blocks = (uint64_t)st->st_blocks;
    k->atime = (uint64_t)st->st_atime; k->mtime = (uint64_t)st->st_mtime; k->ctime = (uint64_t)st->st_ctime;
}

static long emu_syscall(cpu *c, uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4)
{
    (void)a4;
    if (c->trace)
        fprintf(stderr, "\x1b[35m[syscall]\x1b[0m nr=%llu x0=%#llx x1=%#llx x2=%#llx (pc=%#llx)\n",
                (unsigned long long)nr, (unsigned long long)a0,
                (unsigned long long)a1, (unsigned long long)a2,
                (unsigned long long)(c->pc - 4));
    switch (nr) {
    case 63: {   /* read：长度必须夹在 guest 可寻址范围内，否则宿主会读到映射外 */
        size_t n = (size_t)a1;
        if (bad_range(a2, 1)) return -14;                 /* EFAULT */
        if (bad_range(a2, n)) n = (size_t)(GUEST_SIZE - a2);
        if (g_watch_lo && a2 < g_watch_hi && a2 + n > g_watch_lo)
            fprintf(stderr, "\x1b[33m[监视]\x1b[0m read() 写入 guest %#llx（%zu 字节，fd=%d）\n",
                    (unsigned long long)a2, n, (int)a0);
        ssize_t r = read((int)a0, g_base + a2, n);
        return r < 0 ? -errno : (long)r;
    }
    case 64: {   /* write */
        size_t n = (size_t)a2;
        if (bad_range(a1, 1)) return -14;
        if (bad_range(a1, n)) n = (size_t)(GUEST_SIZE - a1);
        ssize_t r = write((int)a0, g_base + a1, n);
        return r < 0 ? -errno : (long)r;
    }
    case 56: {   /* openat */
        char *path = emu_cstr_dup(a1);
        int flags = (int)a2, mode = (int)a3;
        char real[1200];
        int w = (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0;
        if (w && c->readonly_sandbox) { free(path); return -30; }   /* EROFS */
        vfs_resolve(c, path, real, sizeof real, w);
        int fd = open(real, flags | (w ? 0 : 0), mode ? mode : 0644);
        if (fd < 0 && !w) fd = open(real, O_RDONLY);      /* 读不到就按只读重试 */
        if (c->trace) fprintf(stderr, "\x1b[35m[open]\x1b[0m %s → %s = %d\n", path, real, fd);
        free(path);
        return fd < 0 ? -errno : fd;
    }
    case 57:  return close((int)a0) < 0 ? -errno : 0;
    case 62: { off_t r = lseek((int)a0, (off_t)a1, (int)a2); return r < 0 ? -errno : (long)r; }
    case 80: { struct stat st; if (fstat((int)a0, &st) != 0) return -errno;
               kstat_t k; fill_kstat(&k, &st); return emu_write(c, a1, &k, sizeof k) ? -14 : 0; }
    case 79: { char *path = emu_cstr_dup(a1); char real[1200];
               vfs_resolve(c, path, real, sizeof real, 0);
               struct stat st; int r = stat(real, &st); if (r) r = lstat(real, &st);
               free(path);
               if (r) return -errno;
               kstat_t k; fill_kstat(&k, &st); return emu_write(c, a2, &k, sizeof k) ? -14 : 0; }
    case 48: { char *path = emu_cstr_dup(a1); char real[1200];
               vfs_resolve(c, path, real, sizeof real, 0);
               int r = access(real, (int)a2); free(path); return r ? -errno : 0; }
    case 78: { char *path = emu_cstr_dup(a1); char real[1200];
               vfs_resolve(c, path, real, sizeof real, 0);
               ssize_t r = readlink(real, (char *)(g_base + a2), (size_t)a3);
               free(path); return r < 0 ? -errno : (long)r; }
    case 214:  /* brk：语义与内核一致——成功返回新的 break，失败返回旧的 */
        if (a0) {
            if (a0 >= c->brk_cur && a0 < MMAP_BASE) { c->brk_cur = a0; if (a0 > g_brk_hi) g_brk_hi = a0; return (long)a0; }
            return (long)c->brk_cur;
        }
        return (long)c->brk_cur;
    case 222: {  /* mmap */
        uint64_t len = (a1 + 0xFFFull) & ~0xFFFull;
        if (c->trace)
            fprintf(stderr, "\x1b[35m[mmap]\x1b[0m addr=%#llx len=%#llx prot=%#llx flags=%#llx fd=%lld\n",
                    (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)a2,
                    (unsigned long long)a3, (long long)c->r[4]);
        /* 省内存：单次与总量都设上限；且**不再 memset**（匿名映射本来就是零页，
         * 主动写反而会把物理页提交进来） */
        if (len > GUEST_MMAP_MAXLEN) return -12;                 /* ENOMEM */
        if (c->mmap_used + len > GUEST_MMAP_TOTAL) return -12;
        if (c->mmap_cur + len > MMAP_LIMIT) return -12;
        uint64_t va = c->mmap_cur;
        c->mmap_cur += len;
        c->mmap_used += len;
        if (c->mmap_cur > g_mmap_hi) g_mmap_hi = c->mmap_cur;
        if (c->trace) fprintf(stderr, "\x1b[35m[mmap]\x1b[0m → %#llx（len=%#llx）\n",
                              (unsigned long long)va, (unsigned long long)len);
        return (long)va;
    }
    case 226: {  /* mprotect：忠实记录"设成不可写"的区间（RELRO 就靠这个，真机写它会 SIGSEGV） */
        uint64_t len = (a1 + 0xFFFull) & ~0xFFFull;
        if (!(a2 & 2) && g_nnoprot < 16) {           /* 没有 PROT_WRITE */
            g_noprot[g_nnoprot].lo = a0;
            g_noprot[g_nnoprot].hi = a0 + len;
            g_nnoprot++;
        }
        return 0;
    }
    case 215: case 233: return 0;      /* munmap/madvise：忽略 */

    case 94: case 93: c->exited = 1; c->exit_code = a0 & 0xFF; return 0;
    case 172: return 4242;                       /* getpid */
    case 173: return 1;                          /* getppid */
    case 178: return 4242;                       /* gettid */
    case 174: case 175: case 176: case 177: return 0;
    case 160: {  /* uname */
        struct { char a[65], b[65], c[65], d[65], e[65], f[65]; } u;
        memset(&u, 0, sizeof u);
        strcpy(u.a, "Linux"); strcpy(u.b, "s2a-emu"); strcpy(u.c, "6.1.0-s2a");
        strcpy(u.d, "#1 SMP s2a"); strcpy(u.e, "aarch64"); strcpy(u.f, "localdomain");
        return emu_write(c, a0, &u, sizeof u) ? -14 : 0;
    }
    case 113: { struct timespec ts; clock_gettime((clockid_t)a0, &ts);
                struct { int64_t s, ns; } k = { ts.tv_sec, ts.tv_nsec };
                return emu_write(c, a1, &k, sizeof k) ? -14 : 0; }
    case 169: { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
                struct { int64_t s, us; } tv = { ts.tv_sec, ts.tv_nsec / 1000 };
                return emu_write(c, a0, &tv, sizeof tv) ? -14 : 0; }
    case 101: { struct { int64_t s, ns; } ts;
                if (emu_read(c, a0, &ts, sizeof ts) == 0) {
                    struct timespec rq = { ts.s, ts.ns };
                    nanosleep(&rq, NULL);
                }
                return 0; }
    case 117:    /* ptrace：模拟器里永远“没被跟踪”（与常见做法一致） */
        if (c->trace) fprintf(stderr, "\x1b[35m[ptrace]\x1b[0m 请求 %llu → 0\n", (unsigned long long)a0);
        return 0;
    case 134: case 135: case 139: return 0;      /* 信号：记录但不真装 */
    case 98: return 0;                           /* futex：单线程直接成功 */
    case 96: return 4242;                        /* set_tid_address */
    case 99: return 0;                           /* set_robust_list */
    case 293: return -38;                        /* rseq：ENOSYS */
    case 261: {   /* prlimit64 */
        struct { uint64_t cur, max; } rl = { 1u << 30, 1u << 30 };
        if (a3) return emu_write(c, a3, &rl, sizeof rl) ? -14 : 0;
        return 0;
    }
    case 29: {   /* ioctl */
        if (a1 == 0x5413) { struct { uint16_t r, cc, x, y; } ws = { 24, 80, 0, 0 };
                            return emu_write(c, a2, &ws, sizeof ws) ? -14 : 0; }
        return 0; }
    case 291: return -38;                        /* statx */
    case 260: return -10;                        /* wait4：无子进程（ECHILD） */
    case 221: {  /* execve：模拟器不真实执行外部程序 */
        char *p = emu_cstr_dup(a0);
        fprintf(stderr, "s2a 模拟器: 产物尝试执行外部程序 `%s`——模拟器不真实执行它"
                        "（要看真实行为请用 --iso 或直接运行产物）\n", p);
        free(p);
        return -2;
    }
    case 129: case 130: case 131: return 0;      /* kill/tkill/tgkill */
    case 318: { uint64_t n = a1;
                for (uint64_t i = 0; i < n; i++) g_base[a0 + i] = (uint8_t)(rand() & 0xFF);
                return (long)n; }
    case 66: {   /* writev：依次写各 iovec 段（逐段写，不拼大缓冲，省内存） */
        uint64_t cnt = a2;
        if (cnt > 1024) return -22;
        long total = 0;
        for (uint64_t i = 0; i < cnt; i++) {
            uint64_t base = 0, len = 0;
            if (emu_read(c, a1 + i * 16, &base, 8) || emu_read(c, a1 + i * 16 + 8, &len, 8))
                return total ? total : -14;
            if (bad_range(base, len)) return total ? total : -14;
            ssize_t r = write((int)a0, g_base + base, (size_t)len);
            if (r < 0) return total ? total : -errno;
            total += r;
            if ((uint64_t)r < len) break;
        }
        return total;
    }
    case 65: {   /* readv */
        uint64_t cnt = a2;
        if (cnt > 1024) return -22;
        long total = 0;
        for (uint64_t i = 0; i < cnt; i++) {
            uint64_t base = 0, len = 0;
            if (emu_read(c, a1 + i * 16, &base, 8) || emu_read(c, a1 + i * 16 + 8, &len, 8))
                return total ? total : -14;
            if (bad_range(base, len)) return total ? total : -14;
            ssize_t r = read((int)a0, g_base + base, (size_t)len);
            if (r < 0) return total ? total : -errno;
            total += r;
            if ((uint64_t)r < len) break;
        }
        return total;
    }
    case 17: { const char fake[] = "/"; if (a1 < sizeof fake) return -34;
               memcpy(g_base + a0, fake, sizeof fake); return (long)sizeof fake; }
    case 49: return 0;                           /* chdir */
    case 61: return 0;                           /* getdents64 */
    case 34: case 35: case 38: case 276: return 0;  /* 目录/改名类写操作：只动覆盖层 */
    case 45: case 46: return 0;                  /* truncate/ftruncate */
    default: {
        static int warned;
        if (warned++ < 3)
            fprintf(stderr, "s2a 模拟器: 未实现的系统调用 %llu（x0=%#llx x1=%#llx x2=%#llx）%s\n",
                    (unsigned long long)nr, (unsigned long long)a0,
                    (unsigned long long)a1, (unsigned long long)a2,
                    warned == 3 ? "（后续同类不再重复提示）" : "");
        return -38;
    }
    }
}


static void emu_fault(cpu *c, const char *what, uint64_t va);

/* ============================ SIMD / FP 子集 ============================
 * 只实现“产物实际会走到”的部分：立即数/逻辑/整数运算/多结构访存/常见的 FP 算术与转换。
 * 返回 0 = 已处理，-1 = 不认识（由调用方报错）。
 */
static float half_to_float(uint16_t h)          /* IEEE half → float（不依赖 libm） */
{
    uint32_t sign = (uint32_t)(h >> 15) << 31;
    uint32_t exp = (h >> 10) & 0x1F, man = h & 0x3FF;
    uint32_t f;
    if (exp == 0) {
        if (man == 0) f = sign;
        else {                                       /* 非规格化：规格化后指数为 -14 */
            int e = -1;
            do { man <<= 1; e++; } while (!(man & 0x400));
            man &= 0x3FF;
            f = sign | ((uint32_t)(127 - 15 - e) << 23) | (man << 13);
        }
    } else if (exp == 0x1F) {
        f = sign | 0x7F800000u | (man << 13);
    } else {
        f = sign | ((uint32_t)(exp - 15 + 127) << 23) | (man << 13);
    }
    float out;
    memcpy(&out, &f, 4);
    return out;
}

static uint64_t sx_lane(uint64_t v, int bits)   /* 按位宽做符号扩展 */
{
    if (bits >= 64) return v;
    uint64_t m = 1ull << (bits - 1);
    return (v ^ m) - m;
}

static uint64_t sat_s(int64_t v, int bits)       /* 有符号饱和 */
{
    if (bits >= 64) return (uint64_t)v;
    int64_t hi = (int64_t)((1ull << (bits - 1)) - 1), lo = -hi - 1;
    if (v < lo) v = lo; else if (v > hi) v = hi;
    return (uint64_t)v;
}

static uint64_t sat_u(int64_t v, int bits)       /* 无符号饱和 */
{
    uint64_t hi = (bits >= 64) ? ~0ull : ((1ull << bits) - 1);
    if (v < 0) return 0;
    return ((uint64_t)v > hi) ? hi : (uint64_t)v;
}

static uint64_t vec_lane_get(const uint64_t *V, int idx, int esz_bytes)
{
    uint64_t v = 0;
    memcpy(&v, (const uint8_t *)V + (size_t)idx * (size_t)esz_bytes, (size_t)esz_bytes);
    return v;
}

static void vec_lane_put(uint64_t *V, int idx, int esz_bytes, uint64_t val)
{
    memcpy((uint8_t *)V + (size_t)idx * (size_t)esz_bytes, &val, (size_t)esz_bytes);
}

int s2a_emu_last_reason;
static uint64_t g_callwatch_lo, g_callwatch_hi;   /* 停机原因：0=正常退出 1=缺指令 2=访存故障 3=步数上限 */

static const char *g_who = "?";
static void who_set(const char *n, uint64_t pc)
{
    g_who = n;
    if (getenv("S2A_EMU_WHO"))
        fprintf(stderr, "[谁] %#llx %s\n", (unsigned long long)pc, n);
}

static int emu_simd(cpu *c, uint32_t insn, uint64_t pc)
{
    (void)pc;
    int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
    int q = (insn >> 30) & 1;
    uint64_t *D = c->v[rd];
    const uint64_t *N = c->v[rn];

    /* --- MOVI / MVNI（向量立即数） ---
       真实 cmode 表：0000/0010=32 位车道(MSL 0/8)、0100/0110=16 位车道(MSL 0/8)、
       1000/1010=32 位车道(等价写法)、1100/1101=64 位车道、1110=8 位铺满。
       早先的实现把 0000 当逐字节填充，`movi v6.4s,#0xc` 会算成 0x0c0c0c0c…（错）。 */
    if ((insn & 0x1F800400) == 0x0F000400 && ((insn >> 19) & 0x1F) == 0) {
        who_set("movi", pc);   /* bits23-19 必须为 0；否则 shl/ushr 等移位组会被误判成 MOVI */
        int cmode = (int)((insn >> 12) & 0xF);
        int op = (int)((insn >> 29) & 1);      /* 0=MOVI 1=MVNI（判别位是 bit29，不是 bit11） */
        uint64_t imm8 = (uint64_t)(((insn >> 16) & 7) << 5 | ((insn >> 5) & 0x1F));
        uint64_t imm64 = 0;
        /* 实测 cmode 表：00/01→32 位车道；10→16 位车道；11(<1>=1)→8 位铺满；
           MSL = 8*cmode<1>（即 0x2/0x6/0xA 是左移 8 位） */
        if (cmode < 0x8) {                              /* 32 位车道 */
            uint32_t x = (uint32_t)imm8 << (((cmode >> 1) & 1) ? 8 : 0);   /* MSL 位是 cmode 的 bit1 */
            imm64 = ((uint64_t)x << 32) | (uint64_t)x;
        } else if (cmode == 0x8 || cmode == 0xA) {      /* 16 位车道 */
            uint16_t x = (uint16_t)(imm8 << (((cmode >> 1) & 1) ? 8 : 0));
            for (int i = 0; i < 4; i++) imm64 |= (uint64_t)x << (i * 16);
        } else if (cmode == 0xC || cmode == 0xD) {      /* 64 位车道：imm8 的每一位撑成一个字节 */
            int msl = (cmode == 0xD) ? 1 : 0;
            for (int i = 0; i < 8; i++)
                if ((imm8 >> i) & 1) imm64 |= (uint64_t)0xFF << (((i + msl) & 7) * 8);
        } else {                                        /* 0xE：8 位铺满 */
            for (int i = 0; i < 8; i++) imm64 |= imm8 << (i * 8);
        }
        if (op) imm64 = ~imm64;
        D[0] = imm64;
        D[1] = q ? imm64 : 0;
        return 0;
    }
    /* --- 向量逻辑运算（8/16 字节，按位） --- */
    if ((insn & 0x1F20FC00) == 0x0E201C00) {
        who_set("logical", pc);   /* 向量逻辑组（AND/BIC/ORR/ORN/EOR/EON/BSL） */
        int rm = (int)((insn >> 16) & 31);
        uint64_t a0 = N[0], a1 = N[1], b0 = c->v[rm][0], b1 = c->v[rm][1];
        /* 逻辑组真实字段（原先误把 bit30=Q 当成 opc 高位，导致 ORR 被解成 EOR）：
             bit29 = 0 → AND/BIC/ORR/ORN 族；bit29 = 1 → EOR/BSL/BIT/BIF 族
             bit23 = 0 → AND 类，1 → OR 类；bit22 = 1 → 对 M 取反（BIC/ORN） */
        int u = (insn >> 29) & 1, op = (int)((insn >> 23) & 1), inv = (int)((insn >> 22) & 1);
        uint64_t r0, r1;
        if (u == 0) {
            if (op == 0) { r0 = inv ? (a0 & ~b0) : (a0 & b0); r1 = inv ? (a1 & ~b1) : (a1 & b1); }
            else         { r0 = inv ? (a0 | ~b0) : (a0 | b0); r1 = inv ? (a1 | ~b1) : (a1 | b1); }
        } else {
            /* a=Vn, b=Vm, D=旧 Vd。实测（判别位交错输入）：
               BSL = (D & Vn) | (~D & Vm)   BIT = (Vm & Vn) | (~Vm & D)   BIF = (~Vm & Vn) | (Vm & D) */
            if (op == 0 && inv == 0)      { r0 = a0 ^ b0; r1 = a1 ^ b1; }
            else if (op == 0 && inv == 1) { r0 = (D[0] & a0) | (~D[0] & b0); r1 = (D[1] & a1) | (~D[1] & b1); }
            else if (op == 1 && inv == 0) { r0 = (b0 & a0) | (~b0 & D[0]); r1 = (b1 & a1) | (~b1 & D[1]); }
            else                          { r0 = (~b0 & a0) | (b0 & D[0]); r1 = (~b1 & a1) | (b1 & D[1]); }
        }
        D[0] = r0;
        D[1] = q ? r1 : 0;
        return 0;
    }
    /* --- 向量归约组（跨车道）：ADDV / SMAXV / UMAXV / SMINV / UMINV / SADDLV / UADDLV ---
       0 Q 0 01110 size 1 1000 opcode 10 Rn Rd */
    if ((insn & 0x9F3E0C00) == 0x0E300800) {
        who_set("reduce", pc);
        int U = (int)((insn >> 29) & 1);
        int size = (int)((insn >> 22) & 3);
        int opc = (int)((insn >> 12) & 0x1F);
        int esz = 1 << size, bits = esz * 8;
        int nl = (q ? 16 : 8) / esz;
        int wid = (opc == 0x03) ? bits * 2 : bits;      /* SADDLV/UADDLV 结果加宽 */
        uint64_t acc = 0;
        for (int l = 0; l < nl; l++) {
            int bitoff = l * bits, widx = bitoff >> 6, sh = bitoff & 63;
            uint64_t v = (N[widx] >> sh) & ((bits >= 64) ? ~0ull : ((1ull << bits) - 1));
            /* acc 可能存的是符号扩展值，先掩回元素宽度再扩展，否则比较全错 */
            int64_t sv = (int64_t)sx_lane(v & ((bits >= 64) ? ~0ull : ((1ull << bits) - 1)), bits),
                    sa = (int64_t)sx_lane(acc & ((bits >= 64) ? ~0ull : ((1ull << bits) - 1)), bits);
            switch (opc) {
            case 0x03: acc += U ? v : (uint64_t)sv; break;                            /* SADDLV/UADDLV */
            case 0x0A: acc = (l == 0) ? v : (U ? (v > acc ? v : acc)                         /* SMAXV/UMAXV */
                                               : (uint64_t)(sv > sa ? sv : sa)); break;
            case 0x1A: acc = (l == 0) ? v : (U ? (v < acc ? v : acc)                         /* SMINV/UMINV */
                                               : (uint64_t)(sv < sa ? sv : sa)); break;
            case 0x1B: acc += v; break;                                               /* ADDV */
            default: break;
            }
        }
        memset(D, 0, 16);
        memcpy(&D[0], &acc, (size_t)((wid + 7) / 8));
        return 0;
    }
    /* --- 向量双寄存器杂项组（整数 + FP 转换）：0 Q 0 01110 b23 sz 1 0000 opcode 10 Rn Rd --- */
    if ((insn & 0x9F3E0C00) == 0x0E200800) {
        who_set("two-reg", pc);
        int U = (int)((insn >> 29) & 1);
        int sz = (int)((insn >> 22) & 1);
        int opc = (int)((insn >> 12) & 0x1F);
        const uint64_t *S = N;
        uint64_t out[2] = { D[0], q ? D[1] : 0 };       /* 窄化写高半部时低半部保留 Vd */
        int isz = (insn >> 22) & 3;
        int esz = 1 << isz, bits = esz * 8;
        int nl = (q ? 16 : 8) / esz;
        uint64_t mask = (bits >= 64) ? ~0ull : ((1ull << bits) - 1);
        switch (opc) {
        case 0x00: case 0x01: {             /* REV64 / REV32(U=1) / REV16 */
            int gran = (opc == 0x01) ? 2 : (U ? 4 : 8);
            for (int i = 0; i < 16; i += gran)
                for (int j = 0; j < gran; j++)
                    ((uint8_t *)out)[i + j] = ((const uint8_t *)S)[i + gran - 1 - j];
            if (!q) out[1] = 0;
            break;
        }
        case 0x02: {                        /* SADDLP / UADDLP：成对加宽相加 */
            int onl = nl / 2, ow = bits * 2;
            uint64_t omask = (ow >= 64) ? ~0ull : ((1ull << ow) - 1);
            for (int i2 = 0; i2 < onl; i2++) {
                uint64_t x = vec_lane_get(S, 2 * i2, esz), y = vec_lane_get(S, 2 * i2 + 1, esz);
                int64_t r = U ? (int64_t)(x + y)
                              : (int64_t)(sx_lane(x, bits) + sx_lane(y, bits));
                vec_lane_put(out, i2, ow / 8, (uint64_t)r & omask);
            }
            break;
        }
        case 0x05: {                        /* CNT / NOT(MVN) / RBIT */
            if (!U) {
                for (int i = 0; i < 16; i++)
                    ((uint8_t *)out)[i] = (uint8_t)__builtin_popcount(((const uint8_t *)S)[i]);
            } else if (!sz) {                 /* b22=0 → MVN/NOT */
                out[0] = ~S[0];
                out[1] = q ? ~S[1] : 0;
            } else {                          /* b22=1（size=01）→ RBIT */
                for (int i = 0; i < 16; i++) {
                    uint8_t v = ((const uint8_t *)S)[i], r = 0;
                    for (int j = 0; j < 8; j++) if (v & (1 << j)) r |= (uint8_t)(1 << (7 - j));
                    ((uint8_t *)out)[i] = r;
                }
            }
            break;
        }
        case 0x04: {                        /* CLS(U=0) / CLZ(U=1)（opcode 是 4，不是 9） */
            for (int l = 0; l < nl; l++) {
                uint64_t v = vec_lane_get(S, l, esz) & mask;
                int r;
                if (!U) {
                    uint64_t sign = (v >> (bits - 1)) & 1;
                    r = 0;
                    for (int b = bits - 2; b >= 0; b--) { if (((v >> b) & 1) == sign) r++; else break; }
                } else {
                    r = 0;
                    for (int b = bits - 1; b >= 0; b--) { if ((v >> b) & 1) break; r++; }
                }
                vec_lane_put(out, l, esz, (uint64_t)r & mask);
            }
            break;
        }
        case 0x0B: {                        /* ABS(U=0) / NEG(U=1) */
            for (int l = 0; l < nl; l++) {
                int64_t sv = (int64_t)sx_lane(vec_lane_get(S, l, esz) & mask, bits);
                uint64_t r = U ? (uint64_t)(-sv) : (uint64_t)(sv < 0 ? -sv : sv);
                vec_lane_put(out, l, esz, r & mask);
            }
            break;
        }
        case 0x12: case 0x14: {             /* XTN/XTN2/SQXTUN(18) 与 SQXTN/UQXTN(20) */
            int eb = 1 << isz;              /* 目标元素字节数 */
            int snl = 8 / eb;               /* 每半部元素数 */
            int base_off = q ? 8 : 0;       /* Q=1（XTN2/SQXTN2…）写高 64 位 */
            int sb = 16 * eb;               /* 源元素位宽 */
            for (int l = 0; l < snl; l++) {
                uint64_t v = vec_lane_get(S, l, 2 * eb);
                uint64_t r;
                if (opc == 0x12 && !U)      r = v;                                    /* XTN */
                else if (opc == 0x12 && U)  r = sat_u((int64_t)sx_lane(v, sb), sb / 2); /* SQXTUN */
                else if (opc == 0x14 && !U) r = sat_s((int64_t)sx_lane(v, sb), sb / 2); /* SQXTN */
                else                        r = sat_u((int64_t)v, sb / 2);            /* UQXTN：按无符号饱和 */
                memcpy((uint8_t *)out + base_off + l * eb, &r, (size_t)eb);
            }
            break;
        }
        case 0x17: {                                            /* FCVTL：加宽后**铺满 128 位**（4S←4H / 2D←2S） */
            int fs = sz ? 64 : 32;
            int deb = fs / 8;                                   /* 目的元素字节数 */
            int dl = 16 / deb;                                  /* 目的车道数（总是 16 字节） */
            int seb = deb / 2;                                  /* 源元素字节数 */
            for (int i = 0; i < dl; i++) {
                uint64_t sv = vec_lane_get(S, (q ? dl : 0) + i, seb);
                uint64_t r;
                if (!sz) { float z = half_to_float((uint16_t)sv); uint32_t tz; memcpy(&tz, &z, 4); r = tz; }
                else { uint32_t t32 = (uint32_t)sv; float f; memcpy(&f, &t32, 4); double z = (double)f; memcpy(&r, &z, 8); }
                vec_lane_put(out, i, deb, r);
            }
            D[0] = out[0];                       /* 加宽结果总是 128 位 */
            D[1] = out[1];
            return 0;
        }
        case 0x0F: case 0x1B: case 0x1D: case 0x1F: {   /* FP：取绝对值/取负/开方/转换 */
            int fs = sz ? 64 : 32;
            int fnl = (q ? 16 : 8) / (fs / 8);
            for (int l = 0; l < fnl; l++) {
                uint64_t v = vec_lane_get(S, l, fs / 8);
                uint64_t r = 0;
                if (opc == 0x0F) {                                  /* FABS(U=0) / FNEG(U=1) */
                    if (!sz) r = U ? ((uint32_t)v ^ 0x80000000u) : ((uint32_t)v & 0x7FFFFFFFu);
                    else     r = U ? (v ^ (1ull << 63)) : (v & ~(1ull << 63));
                } else if (opc == 0x1F) {                           /* FSQRT */
                    if (!sz) { float x; uint32_t t = (uint32_t)v; memcpy(&x, &t, 4);
                               float z = __builtin_sqrtf(x); uint32_t tz; memcpy(&tz, &z, 4); r = tz; }
                    else { double x; memcpy(&x, &v, 8); double z = __builtin_sqrt(x); memcpy(&r, &z, 8); }
                } else if (opc == 0x1D) {                           /* SCVTF(U=0) / UCVTF(U=1)：整数→浮点 */
                    if (!sz) { float z = U ? (float)(uint32_t)v : (float)(int32_t)(uint32_t)v;
                               uint32_t tz; memcpy(&tz, &z, 4); r = tz; }
                    else { double z = U ? (double)v : (double)(int64_t)v; memcpy(&r, &z, 8); }
                } else if (opc == 0x1B) {                           /* FCVTZS(U=0) / FCVTZU(U=1)：浮点→整数（截断） */
                    if (!sz) { float x; uint32_t t = (uint32_t)v; memcpy(&x, &t, 4);
                               r = U ? (uint32_t)(x < 0 ? 0 : (x > 4294967295.0f ? 4294967295.0f : x))
                                     : (uint32_t)(int32_t)x; }
                    else { double x; memcpy(&x, &v, 8);
                           r = U ? (uint64_t)(x < 0 ? 0 : x) : (uint64_t)(int64_t)x; }
                } else {
                    r = 0;                                          /* FCVTL 已单独处理（见 opc 0x17 分支） */
                }
                vec_lane_put(out, l, fs / 8, r);
            }
            break;
        }
        default: return -1;
        }
        D[0] = out[0];
        D[1] = q ? out[1] : 0;
        return 0;
    }
    /* --- 向量三同组（整数全表 + FP）：0 Q 0 01110 size/op 1 0 1 Rm opcode 1 Rn Rd ---
       opcode = bits15-11：<23 为整数（U=bit29 选有/无符号），>=25 为 FP 三同组 */
    if ((insn & 0x9F200400) == 0x0E200400) {
        who_set("three-same", pc);
        int rm = (int)((insn >> 16) & 31);
        int U = (int)((insn >> 29) & 1);
        int size = (int)((insn >> 22) & 3);
        int opc = (int)((insn >> 11) & 0x1F);
        const uint64_t *A = N, *B = c->v[rm];
        uint64_t out[2] = { 0, 0 };
        if (opc >= 0x19) {                                  /* ---- FP 三同组 ---- */
            int isd = (insn >> 22) & 1;                     /* sz：0=单精度 1=双精度 */
            int a23 = (int)((insn >> 23) & 1);
            int nlf = q ? (isd ? 2 : 4) : (isd ? 1 : 2);
            int fbits = isd ? 64 : 32;
            uint64_t fmask = isd ? ~0ull : 0xFFFFFFFFull;
            for (int l = 0; l < nlf; l++) {
                int bitoff = l * fbits, widx = bitoff >> 6, sh = bitoff & 63;
                uint64_t av = (A[widx] >> sh) & fmask, bv = (B[widx] >> sh) & fmask;
                uint64_t dv = (D[widx] >> sh) & fmask;
                uint64_t r = 0;
                if (!isd) {
                    float x, y, d, z = 0;
                    uint32_t t32 = (uint32_t)av, u32 = (uint32_t)bv, w32 = (uint32_t)dv, rz;
                    memcpy(&x, &t32, 4); memcpy(&y, &u32, 4); memcpy(&d, &w32, 4);
                    int cmp = 0;
                    switch (opc) {
                    case 0x1A: z = a23 ? (x - y) : (x + y); break;                  /* FADD/FSUB */
                    case 0x1B: z = x * y; break;                                    /* FMUL */
                    case 0x1F: z = x / y; break;                                    /* FDIV */
                    case 0x1E:                                                       /* FMAX/FMIN（NaN 取非 NaN） */
                        if (x != x) z = y; else if (y != y) z = x;
                        else z = a23 ? (x < y ? x : y) : (x > y ? x : y);
                        break;
                    case 0x19: z = a23 ? (d - x * y) : (d + x * y); break;           /* FMLA/FMLS */
                    case 0x1C: cmp = 1; r = a23 ? ((x >= y) ? fmask : 0) : ((x == y) ? fmask : 0); break; /* FCMEQ/FCMGE */
                    case 0x1D: cmp = 1; r = a23 ? ((x < y) ? fmask : 0) : ((x > y) ? fmask : 0); break;   /* FCMGT/FCMLT */
                    default: break;
                    }
                    if (!cmp) { memcpy(&rz, &z, 4); r = rz; }
                } else {
                    double x, y, d, z = 0;
                    memcpy(&x, &av, 8); memcpy(&y, &bv, 8); memcpy(&d, &dv, 8);
                    int cmp = 0;
                    switch (opc) {
                    case 0x1A: z = a23 ? (x - y) : (x + y); break;
                    case 0x1B: z = x * y; break;
                    case 0x1F: z = x / y; break;
                    case 0x1E:
                        if (x != x) z = y; else if (y != y) z = x;
                        else z = a23 ? (x < y ? x : y) : (x > y ? x : y);
                        break;
                    case 0x19: z = a23 ? (d - x * y) : (d + x * y); break;
                    case 0x1C: cmp = 1; r = a23 ? ((x >= y) ? fmask : 0) : ((x == y) ? fmask : 0); break;
                    case 0x1D: cmp = 1; r = a23 ? ((x < y) ? fmask : 0) : ((x > y) ? fmask : 0); break;
                    default: break;
                    }
                    if (!cmp) memcpy(&r, &z, 8);
                }
                out[widx] |= (r & fmask) << sh;
            }
            D[0] = out[0];
            D[1] = q ? out[1] : 0;
            return 0;
        }
        {
            int bits = (1 << size) * 8, esz = 1 << size;
            int nl = (q ? 16 : 8) / esz;
            uint64_t mask = (bits >= 64) ? ~0ull : ((1ull << bits) - 1);
            int half = nl / 2;
            for (int l = 0; l < nl; l++) {
                int bitoff = l * bits, widx = bitoff >> 6, sh = bitoff & 63;
                uint64_t a = (A[widx] >> sh) & mask, b = (B[widx] >> sh) & mask;
                int64_t sa = (int64_t)sx_lane(a, bits), sb = (int64_t)sx_lane(b, bits);
                uint64_t r = 0;
                switch (opc) {
                case 0x00: r = U ? ((a + b) >> 1) : (uint64_t)((sa + sb) >> 1); break;              /* SHADD/UHADD */
                case 0x01: {                                                                       /* SQADD/UQADD */
                    uint64_t t = a + b, half = mask >> 1;
                    if (U) r = (bits >= 64) ? ((t < a) ? ~0ull : t)   /* 64 位：看进位 */
                                            : ((t > mask) ? mask : t);
                    else {
                        int ovf = (int)(((~(a ^ b) & (a ^ t)) >> (bits - 1)) & 1);
                        r = ovf ? (((a >> (bits - 1)) & 1) ? (half + 1) : half) : t;
                    }
                    break; }
                case 0x02: r = U ? ((a + b + 1) >> 1) : (uint64_t)((sa + sb + 1) >> 1); break;       /* SRHADD/URHADD */
                case 0x04: r = U ? ((a - b) >> 1) : (uint64_t)((sa - sb) >> 1); break;              /* SHSUB/UHSUB */
                case 0x05: {                                                                       /* SQSUB/UQSUB */
                    uint64_t t = a - b, half = mask >> 1;
                    if (U) r = (a < b) ? 0 : t;
                    else {
                        int ovf = (int)((((a ^ b) & (a ^ t)) >> (bits - 1)) & 1);
                        r = ovf ? (((a >> (bits - 1)) & 1) ? (half + 1) : half) : t;
                    }
                    break; }
                case 0x06: r = U ? (a > b ? mask : 0) : (sa > sb ? mask : 0); break;                /* CMGT/CMHI */
                case 0x07: r = U ? (a >= b ? mask : 0) : (sa >= sb ? mask : 0); break;              /* CMGE/CMHS */
                case 0x08: case 0x0A: {                                                            /* SSHL/USHL、SRSHL/URSHL */
                    int64_t amt = sb;
                    uint64_t base2 = U ? a : (uint64_t)sa;
                    if (amt < 0) {
                        int na = (int)(-amt);
                        r = (na >= bits) ? (U ? 0 : (sa < 0 ? mask : 0))
                                         : (U ? (a >> na) : (uint64_t)(sa >> na));
                        if (opc == 0x0A && na > 0) {                                                /* 舍入右移 */
                            uint64_t rb = (a >> (na - 1)) & 1;
                            uint64_t rt = r + rb;
                            if (U) r = rt; else r = (uint64_t)((((int64_t)rt) << (64 - bits)) >> (64 - bits));
                        }
                    } else {
                        r = (amt >= bits) ? 0 : (uint64_t)((base2 << amt) & mask);
                        if (opc == 0x0A && amt < bits) r = (r + (1ull << (amt ? amt - 1 : 0))) & mask;
                    }
                    break;
                }
                case 0x09: case 0x0B: {                                                            /* SQSHL/UQSHL、SQRSHL/UQRSHL */
                    int64_t amt = sb;
                    if (amt < 0) { int na = (int)(-amt);
                        r = (na >= bits) ? (sa < 0 ? mask : 0) : (uint64_t)(sa >> na);
                    } else {
                        uint64_t sh2 = (amt >= bits) ? 0 : (uint64_t)((sa << amt) & mask);
                        r = U ? sat_u((int64_t)sh2, bits) : sat_s((int64_t)sx_lane(sh2, bits), bits);
                    }
                    break;
                }
                case 0x0C: r = U ? (a > b ? a : b) : (uint64_t)(sa > sb ? sa : sb); break;          /* SMAX/UMAX */
                case 0x0D: r = U ? (a < b ? a : b) : (uint64_t)(sa < sb ? sa : sb); break;          /* SMIN/UMIN */
                case 0x0E: r = U ? (a > b ? a - b : b - a) : (uint64_t)(sa > sb ? sa - sb : sb - sa); break; /* SABD/UABD */
                case 0x0F: {                                                                       /* SABA/UABA：与 Vd 累加 */
                    uint64_t d = (D[widx] >> sh) & mask;
                    uint64_t diff = U ? (a > b ? a - b : b - a) : (uint64_t)(sa > sb ? sa - sb : sb - sa);
                    r = d + diff;
                    break;
                }
                case 0x10: r = U ? (a - b) : (a + b); break;                                       /* ADD/SUB */
                case 0x11: r = U ? (a == b ? mask : 0) : ((a & b) ? mask : 0); break;              /* CMTST/CMEQ */
                case 0x12: { uint64_t d = (D[widx] >> sh) & mask;                                   /* MLA/MLS */
                             r = (U ? d - a * b : d + a * b); break; }
                case 0x13: r = U ? (uint64_t)((a * b) & 0xFFFFu) : (a * b); break;                 /* MUL（PMUL 近似） */
                case 0x14: case 0x15: {                                                            /* SMAXP/SMINP/UMAXP/UMINP */
                    int src = (l < half) ? 0 : 1, k = (l < half) ? l : l - half;
                    const uint64_t *S = src ? B : A;
                    int b0 = (2 * k) * bits, b1 = (2 * k + 1) * bits;
                    uint64_t x = (S[b0 >> 6] >> (b0 & 63)) & mask, y = (S[b1 >> 6] >> (b1 & 63)) & mask;
                    int64_t sx = (int64_t)sx_lane(x, bits), sy = (int64_t)sx_lane(y, bits);
                    if (opc == 0x14) r = U ? (x > y ? x : y) : (uint64_t)(sx > sy ? sx : sy);
                    else             r = U ? (x < y ? x : y) : (uint64_t)(sx < sy ? sx : sy);
                    break;
                }
                case 0x17: {                                                                       /* ADDP（实测 opcode=23；两源各出一半，内部成对相加） */
                    int src = (l < half) ? 0 : 1, k = (l < half) ? l : l - half;
                    const uint64_t *S = src ? B : A;
                    int b0 = (2 * k) * bits, b1 = (2 * k + 1) * bits;
                    uint64_t x = (S[b0 >> 6] >> (b0 & 63)) & mask, y = (S[b1 >> 6] >> (b1 & 63)) & mask;
                    r = x + y;
                    break;
                }
                default: r = 0; break;
                }
                out[widx] |= (r & mask) << sh;
            }
            D[0] = out[0];
            D[1] = q ? out[1] : 0;
            return 0;
        }
    }
    /* --- 旧的两条紧匹配（作为兜底保留，正常不会走到） --- */
    if ((insn & 0x9F20FC00) == 0x0E208400 || (insn & 0x9F20FC00) == 0x0E209C00) {
        who_set("old-addsub", pc);
        int rm = (int)((insn >> 16) & 31);
        int size = (int)((insn >> 22) & 3);
        int esz = 1 << size;                              /* 元素字节数 */
        int nl = (q ? 16 : 8) / esz;                      /* 车道数（Q=0 时只有 64 位） */
        int is_mul = (insn & 0x00001800) == 0x00001800;   /* opcode 10011=MUL；ADD/SUB 的 bit15 同为 1，不能用 bit15 区分 */
        int is_sub = (insn & 0x20000000) != 0;
        const uint64_t *M = c->v[rm];
        uint64_t out[2] = { 0, 0 };
        for (int l = 0; l < nl; l++) {
            int bitoff = l * esz * 8;
            int widx = bitoff >> 6;
            int shift = bitoff & 63;
            uint64_t mask = (size == 3) ? ~0ull : ((1ull << (esz * 8)) - 1);
            uint64_t a = (N[widx] >> shift) & mask;
            uint64_t b = (M[widx] >> shift) & mask;
            uint64_t r = is_mul ? (a * b) : (is_sub ? (a - b) : (a + b));
            out[widx] |= (r & mask) << shift;
        }
        D[0] = out[0];
        D[1] = q ? out[1] : 0;
        return 0;
    }
    /* --- UZP1/UZP2/ZIP1/ZIP2/TRN1/TRN2/EXT --- */
    /* --- 多结构访存 LD2/3/4、ST2/3/4（多个结构，bit24=0）---
       布局（实测）：0 Q 0 01100 1 0 0 L Rm 0 opc(15:13) 0 size(11:10) Rn Rt
       例：ld2 {v0.16b,v1.16b},[x0] = 0x4c408000；st2 = 0x4c008000；ld3 = 0x4c404000 */
    if ((insn & 0xBF000000) == 0x0C000000 && !((insn >> 24) & 1) && !((insn >> 21) & 1)) {
        int L = (int)((insn >> 22) & 1);
        int rm = (int)((insn >> 16) & 31);
        int opc = (int)((insn >> 12) & 0xF);      /* 结构数编码在 bits15-12（实测） */
        int size = (int)((insn >> 10) & 3);
        int esz = 1 << size;                      /* 元素字节数 */
        /* 0x7→LD1(1 寄存器) 0xA→LD1(2) 0x6→LD1(3) 0x2→LD1(4)
           0x8→LD2 0x4→LD3 0x0→LD4；带 8 的高位表示"多个结构" */
        int nreg;
        switch (opc) {
        case 0x7: case 0xA: case 0x6: case 0x2:
            nreg = (opc == 0x7) ? 1 : (opc == 0xA) ? 2 : (opc == 0x6) ? 3 : 4; break;
        case 0x8: nreg = 2; break;
        case 0x4: nreg = 3; break;
        case 0x0: nreg = 4; break;
        default: nreg = 1; break;
        }
        int total = nreg * esz * (q ? 16 / esz : 8 / esz);      /* 一个结构块覆盖的字节数 */
        int rt = (int)(insn & 31);
        uint64_t base = get_z(c, rn);
        int ok = 1;
        if (!bad_range(base, (uint64_t)total)) {
            for (int r = 0; r < nreg; r++) {
                uint64_t cur[2] = { c->v[(rt + r) & 31][0], c->v[(rt + r) & 31][1] };
                if (!L) { c->v[(rt + r) & 31][0] = cur[0]; c->v[(rt + r) & 31][1] = cur[1]; }
            }
            int ld1_family = (opc == 0x7 || opc == 0xA || opc == 0x6 || opc == 0x2);
            int lanes = (q ? 16 : 8) / esz;
            for (int e = 0; e < lanes; e++) {
                for (int r = 0; r < nreg; r++) {
                    /* LD1 族是「逐寄存器连续」：寄存器 r 占一整块 lanes*esz 字节；
                       LD2/3/4 才是「按元素交错」。早期一律按交错处理，ld1 {v0,v1} 全错。 */
                    uint64_t va = ld1_family
                                ? base + (uint64_t)(r * lanes * esz + e * esz)
                                : base + (uint64_t)((e * nreg + r) * esz);
                    if (L) {
                        uint64_t v = 0;
                        memcpy(&v, g_base + va, (size_t)esz);
                        vec_lane_put(c->v[(rt + r) & 31], e, esz, v);
                    } else {
                        uint64_t v = vec_lane_get(c->v[(rt + r) & 31], e, esz);
                        memcpy(g_base + va, &v, (size_t)esz);
                        if (!guest_writable(va, (uint64_t)esz)) { seg_fault(c, va, "lane 存储", esz); ok = 0; return -1; }
                    }
                }
            }
        } else { emu_fault(c, "多结构访存", base); ok = 0; }
        if (ok && rm != 31) set_z(c, rn, base + (uint64_t)(rm ? 0 : 0));   /* 后索引由 Rm 指定时按 Xm 递增 */
        if (ok && rm != 31) set_z(c, rn, base + get_x(c, rm));
        return ok ? 0 : -1;
    }
    /* --- LD1/ST1（单车道，bit24=1，bits15-10 = selem）---
       实测 selem 规则：≤7 → 8 位元素、索引 = selem；16..23 → 16 位、索引 =(selem-16)/2；
       32/33.. → 偶数=32 位、奇数=64 位，索引 =(selem-32-奇)/4；Q=1 再偏移半个寄存器。 */
    if ((insn & 0xBF000000) == 0x0D000000 && ((insn >> 24) & 1) && ((insn >> 21) & 1) == 0) {
        int selem = (int)((insn >> 10) & 0x3F);
        if ((insn >> 12) != 0xC && (insn >> 12) != 0xE) {          /* R 形式已在上面的分支处理 */
            int esz, idx;
            if (selem <= 7) { esz = 1; idx = selem; }
            else if (selem >= 16 && selem <= 23) { esz = 2; idx = (selem - 16) / 2; }
            else { int odd = selem & 1; esz = odd ? 8 : 4; idx = (selem - 32 - odd) / 4; }
            if (idx >= 0 && idx < 16 / esz) {
                int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
                int rm = (int)((insn >> 16) & 31), L = (int)((insn >> 22) & 1);
                uint64_t base = get_z(c, rn);
                if (bad_range(base, (uint64_t)esz)) { emu_fault(c, "LD1/ST1 单车道", base); return -1; }
                if (L) {
                    uint64_t v = 0;
                    memcpy(&v, g_base + base, (size_t)esz);
                    vec_lane_put(c->v[rt], idx, esz, v);
                } else {
                    uint64_t v = vec_lane_get(c->v[rt], idx, esz);
                    memcpy(g_base + base, &v, (size_t)esz);
                }
                if (rm != 31) set_z(c, rn, base + (uint64_t)esz);
                return 0;
            }
        }
    }
    /* --- LD1R/LD2R/LD3R/LD4R：装载单个元素并复制到 N 个寄存器（bit24=1）---
       实测：bits15-12=0xC → LD1R/LD2R（bit21=0/1），0xE → LD3R/LD4R（bit21=0/1） */
    if ((insn & 0xBF000000) == 0x0D000000 && ((insn >> 24) & 1)) {
        int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
        int opc = (int)((insn >> 12) & 0xF);
        int size = (int)((insn >> 10) & 3);
        int esz = 1 << size;
        int nreg = (opc == 0xC) ? 1 : (opc == 0xE) ? 3 : 0;
        if (nreg && ((insn >> 21) & 1)) nreg++;                 /* bit21=1 → 多一个寄存器 */
        if (nreg && ((insn >> 22) & 1)) {                       /* L=1：装载并复制 */
            uint64_t base = get_z(c, rn);
            if (bad_range(base, (uint64_t)(nreg * esz))) { emu_fault(c, "LD1R", base); return -1; }
            for (int r = 0; r < nreg; r++) {
                uint64_t v = 0;
                memcpy(&v, g_base + base + r * esz, (size_t)esz);   /* 每个寄存器取下一个元素再广播 */
                uint64_t d[2] = { 0, 0 };
                for (int e = 0; e < (q ? 16 : 8) / esz; e++) vec_lane_put(d, e, esz, v);
                c->v[(rt + r) & 31][0] = d[0];
                c->v[(rt + r) & 31][1] = q ? d[1] : 0;
            }
            if (rm != 31) set_z(c, rn, base + (uint64_t)(nreg * esz));
            return 0;
        }
    }
    /* --- 多结构访存 LD1/ST1（{v?.16b}, [xn]） --- */
    if ((insn & 0xBF000000) == 0x0C000000 || (insn & 0xBF000000) == 0x0D000000) {
        int rm_addr = rn;
        int post = (int)((insn >> 23) & 1);
        int ld = (insn >> 22) & 1;
        int opcode = (int)((insn >> 12) & 0xF);
        int size = (int)((insn >> 10) & 3);
        int lanes = 16 >> size;
        uint64_t addr = get_z(c, rm_addr);
        int nstruc = 1;
        switch ((opcode >> 1) & 7) {                  /* 结构数 */
        case 0: nstruc = 1; break;
        case 1: nstruc = 4; break;
        case 2: nstruc = 3; break;
        case 4: nstruc = 2; break;
        case 6: nstruc = 1; break;
        case 7: nstruc = 1; break;
        default: nstruc = 1; break;
        }
        /* 仅支持 {vD.16b / .8b} 与 {vD.4s} 两种最常见形式，且为一寄存器 */
        int esize = 1 << size;
        int total = lanes * esize;
        if (nstruc != 1) return -1;
        if (addr + (uint64_t)total > GUEST_SIZE) { emu_fault(c, "LD1/ST1", addr); return -1; }
        if (ld) { memset(D, 0, 16); memcpy(D, g_base + addr, (size_t)total); }
        else memcpy(g_base + addr, D, (size_t)total);
        if (post) set_z(c, rm_addr, addr + (uint64_t)total);
        return 0;
    }
    /* --- FP 标量算术（D/S） --- */
    if ((insn & 0xFF20FC00) == 0x1E202800 || (insn & 0xFF20FC00) == 0x1E203800 ||
        (insn & 0xFF20FC00) == 0x1E200800 || (insn & 0xFF20FC00) == 0x1E201800) {   /* FADD/FSUB/FMUL/FDIV */
        int rm = (int)((insn >> 16) & 31);
        int is_s = (insn >> 28) & 1 ? 0 : ((insn >> 22) & 1);
        (void)is_s;
        double a, b, r;
        if (insn & 0x00040000) { float fa, fb, fr;   /* S 形式 */
            memcpy(&fa, &N[0], 4); memcpy(&fb, &c->v[rm][0], 4);
            uint32_t op = insn & 0xFF200000;
            if (op == 0x1E200000) fr = fa + fb;
            else if (op == 0x1E200000) fr = fa + fb;
            else if ((insn & 0xFF20FC00) == 0x1E203800) fr = fa - fb;
            else if ((insn & 0xFF20FC00) == 0x1E201800) fr = fa / fb;
            else fr = fa * fb;
            memset(&D[0], 0, 16);
            memcpy(&D[0], &fr, 4);
            return 0;
        }
        memcpy(&a, &N[0], 8);
        memcpy(&b, &c->v[rm][0], 8);
        if ((insn & 0xFF20FC00) == 0x1E202800) r = a + b;
        else if ((insn & 0xFF20FC00) == 0x1E203800) r = a - b;
        else if ((insn & 0xFF20FC00) == 0x1E201800) r = a / b;
        else r = a * b;
        memset(&D[0], 0, 16);
        memcpy(&D[0], &r, 8);
        return 0;
    }
    /* --- FCMP --- */
    if ((insn & 0xFF20FC1F) == 0x1E202000 || (insn & 0xFF20FC1F) == 0x1E202010) {
        int rm = (int)((insn >> 16) & 31);
        /* 单精度（bit22=0）只能读 4 字节；早期一律按 double 读，fcmp s 全错 */
        int is_single = ((insn >> 22) & 1) == 0;
        double a, b;
        if (is_single) {
            float fa, fb;
            uint32_t t;
            t = (uint32_t)N[0]; memcpy(&fa, &t, 4); a = (double)fa;
            t = (uint32_t)c->v[rm][0]; memcpy(&fb, &t, 4); b = (double)fb;
        } else {
            memcpy(&a, &N[0], 8);
            memcpy(&b, &c->v[rm][0], 8);
        }
        if (a != a || b != b) set_flags(c, 0, 0, 1, 1);        /* 无序 */
        else if (a == b) set_flags(c, 0, 1, 1, 0);             /* 相等 */
        else if (a < b) set_flags(c, 1, 0, 0, 0);              /* 小于 */
        else set_flags(c, 0, 0, 1, 0);                         /* 大于 */
        return 0;
    }
    /* --- 整数↔浮点转换（SCVTF/UCVTF/FCVTZS/FCVTZU） --- */
    if ((insn & 0xFFFFFC00) == 0x1E220000 || (insn & 0xFFFFFC00) == 0x1E230000 ||
        (insn & 0xFFFFFC00) == 0x1E380000 || (insn & 0xFFFFFC00) == 0x1E390000) {
        int sf = (insn >> 31) & 1;
        double d;
        if ((insn & 0xFFFFFC00) == 0x1E220000 || (insn & 0xFFFFFC00) == 0x1E230000) {
            int64_t v = sf ? (int64_t)get_x(c, rn) : (int64_t)(int32_t)get_x(c, rn);
            d = (double)v;
            memset(&D[0], 0, 16);
            memcpy(&D[0], &d, 8);
        } else {
            memcpy(&d, &N[0], 8);
            int64_t v = (int64_t)d;
            set_x(c, rd, sf ? (uint64_t)v : (uint64_t)(uint32_t)v);
        }
        return 0;
    }
    /* --- FMOV 标量/向量之间的常见形式 --- */
    if ((insn & 0xFFFFFC00) == 0x9E660000) { D[0] = get_x(c, rn); return 0; }
    if ((insn & 0xFFFFFC00) == 0x9E670000) { set_x(c, rd, D[0]); return 0; }
    if ((insn & 0xFFFFFC00) == 0x1E604000) { memset(D, 0, 16); D[0] = N[0]; return 0; }
    if ((insn & 0xFFFFFC00) == 0x1E204000) { memset(D, 0, 16); memcpy(&D[0], &N[0], 4); return 0; }

    /* --- SIMD 拷贝族（掩码由汇编器实测自动提取）---
         DUP 元素 : 0 Q 0 01110 000 imm5 0000 01 Rn Rd   (b29=0, bits15-11=0)
         INS 元素 : 0 Q 0 01110 000 imm5 00ii i1 10 Rn Rd (b29=1，源车道号在 bits14-11)
         INS 通用 : 0 Q 0 01110 000 imm5 0001 1 10 Rn Rd (bits15-11=00011)
         UMOV     : 0 Q 0 01110 000 imm5 0011 10 Rn Rd   (bits15-10=001111)
         SMOV     : 0 Q 0 01110 000 imm5 0010 11 Rn Rd   (bits15-10=001011) */
    if ((insn & 0xBFE0FC00) == 0x0E000400 || (insn & 0xBFE08400) == 0x2E000400 ||
        (insn & 0xBFE0FC00) == 0x0E001C00 || (insn & 0xBFE0FC00) == 0x0E003C00 ||
        (insn & 0xBFE0FC00) == 0x0E002C00) {
        who_set("copy-family", pc);
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        int imm5 = (int)((insn >> 16) & 0x1F);
        int pos = -1;
        for (int i = 0; i < 5; i++) if (imm5 & (1 << i)) { pos = i; break; }
        int eb = (pos >= 0) ? (1 << pos) : 1;                 /* 元素字节数 */
        int idx = (pos >= 0) ? (imm5 >> (pos + 1)) : 0;
        if ((insn & 0xBFE0FC00) == 0x0E000400) {              /* DUP（元素 → 所有车道） */
            uint64_t lane = vec_lane_get(N, idx, eb);   /* 源是 Vn，不是 Rd */
            uint64_t dup64 = 0;
            for (int i = 0; i < 8; i += eb) dup64 |= lane << (i * 8);
            D[0] = dup64;
            D[1] = q ? dup64 : 0;
            return 0;
        }
        if ((insn & 0xBFE08400) == 0x2E000400) {              /* INS（元素 → 元素） */
            int sidx = (int)(((insn >> 11) & 0xF) / eb);
            uint64_t lane = vec_lane_get(c->v[rn], sidx, eb);
            vec_lane_put(D, idx, eb, lane);
            return 0;
        }
        if ((insn & 0xBFE0FC00) == 0x0E001C00) {              /* INS（通用寄存器 → 车道） */
            uint64_t v = get_x(c, rn);
            vec_lane_put(D, idx, eb, v);
            return 0;
        }
        if ((insn & 0xBFE0FC00) == 0x0E003C00) {              /* UMOV（车道 → 通用，零扩展） */
            uint64_t v = vec_lane_get(N, idx, eb);
            set_x(c, rd, v);
            return 0;
        }
        {                                                     /* SMOV（车道 → 通用，符号扩展） */
            uint64_t v = vec_lane_get(N, idx, eb);
            set_x(c, rd, sx_lane(v, eb * 8));
            return 0;
        }
    }
    /* --- 向量置换组：UZP1/2、TRN1/2、ZIP1/2 ---
       编码：0 Q 0 01110 0 size 0 rm 0 opcode(14:12) 1 0 Rn Rd
       例：uzp1 v1.8h,v1.8h,v3.8h = 0x4e431821，add v0.16b,... = 0x4e208420（b21=1 属三同组） */
    if ((insn & 0xBF208C00) == 0x0E000800) {
        who_set("permute", pc);   /* bit29=0：置换组（bit29=1 归 EXT） */
        int rm = (int)((insn >> 16) & 31);
        int esz = 1 << ((insn >> 22) & 3);            /* 元素字节数 */
        int opcode = (int)((insn >> 12) & 7);
        int lanes = (q ? 16 : 8) / esz;
        int half = lanes / 2;
        const uint64_t *M = c->v[rm];
        uint64_t out[2] = { 0, 0 };
        for (int i = 0; i < lanes; i++) {
            uint64_t v = 0;
            int from_m = i & 1, k = i >> 1;
            switch (opcode) {
            case 1: v = vec_lane_get(i < half ? N : M, (i % half) * 2, esz); break;      /* UZP1：拼接后的偶数元素 */
            case 5: v = vec_lane_get(i < half ? N : M, (i % half) * 2 + 1, esz); break;  /* UZP2：拼接后的奇数元素 */
            case 3: v = vec_lane_get(from_m ? M : N, k, esz); break;            /* ZIP1 */
            case 7: v = vec_lane_get(from_m ? M : N, half + k, esz); break;     /* ZIP2 */
            case 2: v = vec_lane_get(from_m ? M : N, k * 2, esz); break;        /* TRN1 */
            case 6: v = vec_lane_get(from_m ? M : N, k * 2 + 1, esz); break;    /* TRN2 */
            default: break;
            }
            vec_lane_put(out, i, esz, v);
        }
        D[0] = out[0];
        D[1] = q ? out[1] : 0;
        return 0;
    }
    /* --- EXT：把 Vn||Vm 的 32 字节窗口按 imm4 字节右移 --- */
    if ((insn & 0xBFE00400) == 0x2E000000) {
        who_set("ext", pc);   /* bit29=1 是 EXT 的标志，必须进掩码，否则恒不成立 */
        int rm = (int)((insn >> 16) & 31);
        int imm4 = (int)((insn >> 11) & 0xF);
        uint8_t buf[32], rv[16];
        if (q) {                                     /* 16b：窗口 = v1||v2 共 32 字节 */
            memcpy(buf, N, 16);
            memcpy(buf + 16, c->v[rm], 16);
            for (int i = 0; i < 16; i++) rv[i] = buf[(i + imm4) & 31];
            memcpy(D, rv, 16);
        } else {                                     /* 8b：窗口只有 8+8 = 16 字节 */
            uint8_t b2[16];
            memcpy(b2, N, 8);
            memcpy(b2 + 8, c->v[rm], 8);
            memset(rv, 0, sizeof rv);
            for (int i = 0; i < 8; i++) rv[i] = b2[(i + imm4) & 15];
            memcpy(D, rv, 8);
            D[1] = 0;
        }
        return 0;
    }
    /* --- 向量按立即数移位组（SHL/SSHR/USHR/SRI/SSRA/USRA/SRSHR/SRSRA/SSHLL/USHLL） ---
       immh==0 的编码属于 modified-immediate（MOVI/MVNI），本组只在 immh!=0 时成立。
       例：shl v1.16b,v0.16b,#1 = 0x4f095401；shl v1.4s,v0.4s,#3 = 0x4f235401 */
    if ((insn & 0x9F800400) == 0x0F000400 && ((insn >> 19) & 0xF) != 0) {
        who_set("shift-imm", pc);
        int U = (int)((insn >> 29) & 1);
        int immh = (int)((insn >> 19) & 0xF);
        int imm7 = (immh << 3) | (int)((insn >> 16) & 7);
        int opcode = (int)((insn >> 11) & 0x1F);
        int esz = 8 << (31 - __builtin_clz((unsigned)immh));   /* 元素位宽 */
        int eb = esz / 8;
        int lanes = (q ? 16 : 8) / eb;
        uint64_t mask = (esz >= 64) ? ~0ull : ((1ull << esz) - 1);
        uint64_t out[2] = { 0, 0 };
        if (opcode == 0x14) {                            /* SSHLL / USHLL：窄元素加宽后左移
                                                           immh 给的是**源**元素位宽 esz，
                                                           目的元素 = 2*esz 且总是铺满 128 位；
                                                           Q=1 时取源寄存器的高半部（…LL2） */
            int sh = imm7 - esz;
            int deb = 2 * eb;                            /* 目的元素字节数 */
            int dl = 16 / deb;                           /* 目的车道数 */
            uint64_t dmask = (deb * 8 >= 64) ? ~0ull : ((1ull << (deb * 8)) - 1);
            int soff = q ? dl : 0;
            for (int i = 0; i < dl; i++) {
                uint64_t v = vec_lane_get(N, soff + i, eb);
                if (!U) {                                /* 符号扩展 */
                    if (eb == 1) v = (uint64_t)(int64_t)(int8_t)v;
                    else if (eb == 2) v = (uint64_t)(int64_t)(int16_t)v;
                    else v = (uint64_t)(int64_t)(int32_t)v;
                }
                v = (v << sh) & dmask;
                vec_lane_put(out, i, deb, v);
            }
            D[0] = out[0];                       /* 加宽结果总是 128 位，不能按 q 清掉高半部 */
            D[1] = out[1];
            return 0;
        } else if (opcode == 0x0A) {                     /* SHL（含 SQSHL，按普通左移处理） */
            int sh = imm7 - esz;
            for (int i = 0; i < lanes; i++) {
                uint64_t v = sh >= esz ? 0 : ((vec_lane_get(N, i, eb) << sh) & mask);
                vec_lane_put(out, i, eb, v);
            }
        } else if (opcode == 0x0E) {                     /* SQSHL / UQSHL（立即数饱和左移） */
            int sh = imm7 - esz;
            int sgn = !U;                                /* SQSHL 以有符号输入饱和，UQSHL 以无符号 */
            for (int i = 0; i < lanes; i++) {
                uint64_t v = vec_lane_get(N, i, eb) & mask;
                uint64_t r;
                if (sh >= esz) r = sgn ? (((v >> (esz - 1)) & 1) ? mask : 0) : 0;
                else if (sgn) {                       /* SQSHL：按有符号值左移后饱和 */
                    int64_t sv = (int64_t)sx_lane(v, esz);
                    r = sat_s(sv << sh, esz);
                } else {                              /* UQSHL：无符号左移，越界即饱和 */
                    uint64_t t = v << sh;
                    r = (mask != ~0ull && t > mask) ? mask : t;
                }
                vec_lane_put(out, i, eb, r & mask);
            }
        } else if (opcode == 0x00 || opcode == 0x02 || opcode == 0x04 ||
                   opcode == 0x06 || opcode == 0x08) {   /* 右移族 */
            int sh = 2 * esz - imm7;
            int sgn = !U;                                     /* SSHR/SRSHR 为 U=0；URSHR(U=1) 是无符号 */
            int round = (opcode == 0x04 || opcode == 0x06);
            for (int i = 0; i < lanes; i++) {
                uint64_t v = vec_lane_get(N, i, eb) & mask;
                uint64_t cur = vec_lane_get(D, i, eb) & mask;
                uint64_t r;
                if (sh >= esz) {
                    if (sgn) r = ((v >> (esz - 1)) & 1) ? mask : 0;
                    else r = 0;
                } else if (sh == 0) {
                    r = v;
                } else if (sgn) {
                    uint64_t sv = v;
                    if (esz < 64) sv = (uint64_t)((int64_t)(v << (64 - esz)) >> (64 - esz));
                    if (round) sv += 1ull << (sh - 1);
                    r = (uint64_t)((int64_t)sv >> sh) & mask;
                } else {
                    uint64_t sv = v;
                    if (round) sv += 1ull << (sh - 1);
                    r = (sv >> sh) & mask;
                }
                if (opcode == 0x08) {                    /* SRI：保留 dst 的高 sh 位，低位放源右移结果 */
                    uint64_t keep = (sh >= esz) ? 0 : (mask & ~((1ull << (esz - sh)) - 1));
                    r = (cur & keep) | ((sh >= esz) ? 0 : ((v >> sh) & ~keep & mask));
                } else if (opcode == 0x02 || opcode == 0x06) {  /* SSRA/USRA/SRSRA：累加 */
                    r = (cur + r) & mask;
                }
                vec_lane_put(out, i, eb, r);
            }
        } else {
            return -1;                                   /* 其余（浮点转换等）暂不实现 */
        }
        D[0] = out[0];
        D[1] = q ? out[1] : 0;
        return 0;
    }
    return -1;
}

/* ============================ 指令执行 ============================ */
static int emu_step(cpu *c);

static void emu_fault(cpu *c, const char *what, uint64_t va)
{
    s2a_emu_last_reason = 2;
    fprintf(stderr, "s2a 模拟器: 访存越界/%s（地址 %#llx，pc=%#llx）\n",
            what, (unsigned long long)va, (unsigned long long)(c->pc - 4));
    c->exited = 1;
    c->exit_code = 139;
}

static uint64_t do_load(cpu *c, uint64_t va, int size, int is_signed, int is64, int *ok)
{
    uint64_t v = 0;
    *ok = 1;
    if (bad_range(va, (uint64_t)(1 << size))) { emu_fault(c, "读", va); *ok = 0; return 0; }
    switch (size) {
    case 0: v = g_base[va]; break;
    case 1: memcpy(&v, g_base + va, 2); break;
    case 2: memcpy(&v, g_base + va, 4); break;
    case 3: memcpy(&v, g_base + va, 8); break;
    }
    if (is_signed && size < 3) {
        if (size == 0) v = (uint64_t)(int64_t)(int8_t)v;
        else if (size == 1) v = (uint64_t)(int64_t)(int16_t)v;
        else v = (uint64_t)(int64_t)(int32_t)v;
    } else if (!is64 && size < 3) {
        v = (uint32_t)v;
    }
    return v;
}

/* ---- 段权限（忠实内核：写只读段会像真机一样崩，便于抓"模拟器太宽容"的 bug）---- */
#define GUEST_MAXSEG 24
static struct { uint64_t lo, hi; int w; } g_segs[GUEST_MAXSEG];
static int g_nsegs;
static int g_seg_enforce = -1;          /* -1 未初始化；1 强制（默认）；0 = S2A_EMU_NO_SEGCHECK=1 关闭 */

static void seg_add(uint64_t lo, uint64_t hi, int w)
{
    if (g_nsegs < GUEST_MAXSEG) { g_segs[g_nsegs].lo = lo; g_segs[g_nsegs].hi = hi; g_segs[g_nsegs].w = w; g_nsegs++; }
}

static int guest_writable(uint64_t va, uint64_t len)
{
    for (int i = 0; i < g_nnoprot; i++)                 /* mprotect(PROT_READ) 过的区间 */
        if (va < g_noprot[i].hi && va + len > g_noprot[i].lo) return 0;
    if (g_seg_enforce < 0) g_seg_enforce = getenv("S2A_EMU_NO_SEGCHECK") ? 0 : 1;
    if (!g_seg_enforce) return 1;
    if (va + len < va) return 0;
    for (int i = 0; i < g_nsegs; i++)
        if (va >= g_segs[i].lo && va + len <= g_segs[i].hi) return g_segs[i].w;
    return 1;                            /* 不在已装载段内（栈/堆/mmap）：交给范围检查 */
}

static void seg_fault(cpu *c, uint64_t va, const char *what, int nbytes)
{
    fprintf(stderr,
        "\x1b[31m[s2a 模拟器]\x1b[0m 写只读内存：%s 地址 %#llx（%d 字节，pc=%#llx）\n"
        "  真机在这里会 SIGSEGV；确认产物就该这么写可用 S2A_EMU_NO_SEGCHECK=1 放行。\n",
        what, (unsigned long long)va, nbytes, (unsigned long long)(c->pc - 4));
    c->exited = 1;
    c->exit_code = 139;
}

static void do_store(cpu *c, uint64_t va, int size, uint64_t v, int *ok)
{
    if (g_watch_lo && va >= g_watch_lo && va < g_watch_hi)
        fprintf(stderr, "\x1b[33m[监视]\x1b[0m 写 %#llx ← %#llx（size=%d，pc=%#llx，sp=%#llx）\n",
                (unsigned long long)va, (unsigned long long)v, size,
                (unsigned long long)(c->pc - 4), (unsigned long long)c->sp);
    if (getenv("S2A_EMU_WATCH") && va >= 0x100000 && va < 0x121000)
        fprintf(stderr, "  [写代码段!] va=%#llx size=%d val=%#llx pc=%#llx\n",
                (unsigned long long)va, size, (unsigned long long)v,
                (unsigned long long)(c->pc - 4));
    *ok = 1;
    if (bad_range(va, (uint64_t)(1 << size))) { emu_fault(c, "写", va); *ok = 0; return; }
    if (!guest_writable(va, (uint64_t)(1 << size))) { seg_fault(c, va, "存储", 1 << size); *ok = 0; return; }
    switch (size) {
    case 0: g_base[va] = (uint8_t)v; break;
    case 1: { uint16_t t = (uint16_t)v; memcpy(g_base + va, &t, 2); break; }
    case 2: { uint32_t t = (uint32_t)v; memcpy(g_base + va, &t, 4); break; }
    case 3: memcpy(g_base + va, &v, 8); break;
    }
}

static int emu_step(cpu *c)
{
    uint32_t insn;
    uint64_t pc = c->pc;
    g_who = "?";
    if (bad_range(pc, 4)) { emu_fault(c, "取指", pc); return -1; }
    memcpy(&insn, g_base + pc, 4);
    c->pc = pc + 4;
    c->steps++;
    if (g_callwatch_lo || g_callwatch_hi) {
        uint32_t op = insn >> 26;
        if (op == 0x25 || op == 0x05) {                 /* BL / B */
            int64_t off = (int64_t)(insn & 0x3FFFFFF);
            if (off & 0x2000000) off |= ~((int64_t)0x3FFFFFF);
            uint64_t tgt = (uint64_t)((int64_t)pc + off * 4);
            if (tgt >= g_callwatch_lo && tgt <= g_callwatch_hi)
                fprintf(stderr, "\x1b[36m[调用]\x1b[0m %#llx → %#llx  x0=%#llx x1=%#llx\n",
                        (unsigned long long)pc, (unsigned long long)tgt,
                        (unsigned long long)c->r[0], (unsigned long long)c->r[1]);
        }
    }
    {
        static long from = -1, to = -1;
        if (from < 0) {
            const char *a = getenv("S2A_EMU_TRACE_FROM"), *b = getenv("S2A_EMU_TRACE_TO");
            from = a ? atol(a) : 0;
            to = b ? atol(b) : 0x7FFFFFFF;
        }
        {
            static uint64_t plo = 0, phi = 0; static int pinited;
            if (!pinited) {
                const char *r = getenv("S2A_EMU_TRACE_PC");
                if (r) sscanf(r, "%llx:%llx", (unsigned long long *)&plo, (unsigned long long *)&phi);
                pinited = 1;
            }
            if (plo && pc >= plo && pc <= phi) c->trace = 1;   /* 只对指定 pc 区间开 trace */
        }
        if (c->trace && c->steps >= from && c->steps <= to) {
            fprintf(stderr, "\x1b[90m[%#llx] %08x ", (unsigned long long)pc, insn);
            if (getenv("S2A_EMU_VREGS")) {           /* 追加向量寄存器（调试 SIMD 用，仅在 trace 窗口内输出） */
                for (int i = 0; i < 6; i++)
                    fprintf(stderr, "v%d=%016llx_%016llx ", i,
                            (unsigned long long)c->v[i][1], (unsigned long long)c->v[i][0]);
            }
            if (getenv("S2A_EMU_REGS"))
                for (int i = 0; i < 31; i++)
                    fprintf(stderr, "x%d=%#llx ", i, (unsigned long long)c->r[i]);
            else
                fprintf(stderr, "x0=%#llx x1=%#llx x2=%#llx x3=%#llx x4=%#llx ",
                        (unsigned long long)c->r[0], (unsigned long long)c->r[1],
                        (unsigned long long)c->r[2], (unsigned long long)c->r[3],
                        (unsigned long long)c->r[4]);
            fprintf(stderr, "sp=%#llx nzcv=%u\x1b[0m\n", (unsigned long long)c->sp, c->nzcv);
        }
    }

    /* ---- 空操作类：BTI / NOP / PAC / XPAC（HINT 空间整段视作空操作） ---- */
    if ((insn & 0xFFFFF000) == 0xD5032000) return 0;   /* NOP / BTI / PACIA / PACIB / AUTIA / AUTIB / XPACLRI 等 */
    if (insn == 0xD5033F9F || insn == 0xD5033FDF) return 0;   /* hint 空间其它 */
    if ((insn & 0xFFFFFF1F) == 0xD503241F || (insn & 0xFFFFFF1F) == 0xD503251F) return 0;
    /* DSB / DMB / ISB（bits 7-5 是 op2，必须参与掩码才能区分） */
    if ((insn & 0xFFFFF0FF) == 0xD503309F || (insn & 0xFFFFF0FF) == 0xD50330BF ||
        (insn & 0xFFFFF0FF) == 0xD50330DF)
        return 0;
    /* MRS/MSR（含 cntvct_el0 等）：给个稳定的假值 */
    if ((insn & 0xFFF00000) == 0xD5300000) {      /* MRS */
        int rt = (int)(insn & 31);
        uint32_t o0 = (insn >> 19) & 1, op1 = (insn >> 16) & 7, crn = (insn >> 12) & 15;
        uint32_t crm = (insn >> 8) & 15, op2 = (insn >> 5) & 7;
        uint64_t v = 0;
        if (crn == 14 && crm == 0 && op2 == 2) {              /* CNTVCT_EL0 */
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            v = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
        } else if (crn == 13 && crm == 0 && op2 == 2) {        /* TPIDR_EL0 */
            v = c->tpidr;
        } else if (crn == 13 && crm == 0 && op2 == 3) {        /* TPIDRRO_EL0 */
            v = c->tpidr;
        } else if (crn == 0 && crm == 0 && op2 == 5) {         /* MPIDR_EL1 */
            v = 0x80000000ull;
        } else if (crn == 0 && crm == 0 && op2 == 0) {         /* MIDR_EL1 */
            v = 0x410FD083ull;                                 /* 假装是 Cortex-A53 一类的核 */
        }
        (void)o0; (void)op1;
        set_x(c, rt, v);
        return 0;
    }
    if ((insn & 0xFFFFFFE0) == 0xD51BD040) {           /* MSR TPIDR_EL0, Xt（必须先判） */
        c->tpidr = get_x(c, (int)(insn & 31));
        if (c->trace) fprintf(stderr, "\x1b[35m[tls]\x1b[0m TPIDR_EL0 = %#llx\n",
                              (unsigned long long)c->tpidr);
        return 0;
    }
    if ((insn & 0xFFFFFFE0) == 0xD51BD060) return 0;   /* MSR TPIDRRO_EL0 */
    if ((insn & 0xFFF00000) == 0xD5100000) return 0;   /* 其它 MSR（DAIF/…）：忽略 */

    /* ---- 分支 ---- */
    if ((insn & 0x7C000000) == 0x14000000) {           /* B / BL */
        int64_t off = (int64_t)(insn & 0x3FFFFFF);
        if (off & (1 << 25)) off |= ~((int64_t)0x3FFFFFF);
        uint64_t target = pc + (uint64_t)(off << 2);
        if (insn & 0x80000000u) c->r[30] = c->pc;      /* BL 写 LR */
        c->pc = target;
        return 0;
    }
    if ((insn & 0xFE000000) == 0x54000000) {           /* B.cond */
        int cond = (int)(insn & 15);
        int64_t off = (int64_t)((insn >> 5) & 0x7FFFF);
        if (off & (1 << 18)) off |= ~((int64_t)0x7FFFF);
        if (cond_ok(c, cond)) c->pc = pc + (uint64_t)(off << 2);
        return 0;
    }
    if ((insn & 0x7E000000) == 0x34000000) {           /* CBZ/CBNZ */
        int64_t off = (int64_t)((insn >> 5) & 0x7FFFF);
        if (off & (1 << 18)) off |= ~((int64_t)0x7FFFF);
        uint64_t v = (insn & 0x80000000u) ? get_x(c, (int)(insn & 31))
                                          : (uint64_t)(uint32_t)get_x(c, (int)(insn & 31));
        int nz = (insn & 0x01000000) ? 1 : 0;
        if ((v == 0) != nz) c->pc = pc + (uint64_t)(off << 2);
        return 0;
    }
    if ((insn & 0x7E000000) == 0x36000000) {           /* TBZ/TBNZ */
        int rt = (int)(insn & 31);
        int bit = (int)(((insn >> 26) & 0x20) | ((insn >> 19) & 0x1F));
        int64_t off = (int64_t)((insn >> 5) & 0x3FFF);
        if (off & (1 << 13)) off |= ~((int64_t)0x3FFF);
        uint64_t v = get_x(c, rt);
        int set = (v >> bit) & 1;
        int want = (insn & 0x01000000) ? 1 : 0;
        if (set == want) c->pc = pc + (uint64_t)(off << 2);
        return 0;
    }
    if ((insn & 0xFFFFFC1F) == 0xD61F0000) { c->pc = get_x(c, (int)((insn >> 5) & 31)); return 0; }  /* BR */
    if ((insn & 0xFFFFFC1F) == 0xD63F0000) {           /* BLR */
        c->r[30] = c->pc;
        c->pc = get_x(c, (int)((insn >> 5) & 31));
        return 0;
    }
    if ((insn & 0xFFFFFC1F) == 0xD65F0000) {           /* RET */
        int rn = (int)((insn >> 5) & 31);
        uint64_t target = get_z(c, rn);
        if (target < 0x1000 || target > 0x420000) {     /* 目标不在镜像范围内：栈帧疑似错位 */
            fprintf(stderr, "\x1b[33m[ret 异常]\x1b[0m 目标 %#llx（x%d），sp=%#llx\n",
                    (unsigned long long)target, rn, (unsigned long long)c->sp);
            for (int i = -2; i < 8; i++) {
                uint64_t v = 0;
                if (emu_read(c, c->sp + i * 8, &v, 8) == 0)
                    fprintf(stderr, "    [sp%+d] = %#llx\n", i * 8, (unsigned long long)v);
            }
        }
        c->pc = target;
        return 0;
    }
    if ((insn & 0xFFE0001F) == 0xD4000001) {           /* SVC */
        uint64_t nr = c->r[8];
        long r = emu_syscall(c, nr, c->r[0], c->r[1], c->r[2], c->r[3], c->r[4]);
        c->r[0] = (uint64_t)r;
        return 0;
    }
    if ((insn & 0xFFE0001F) == 0xD4200000) {           /* BRK：模拟器里当作“调试器断点”，返回 0 */
        if (c->trace) fprintf(stderr, "\x1b[35m[brk]\x1b[0m #%u（模拟器忽略）\n", (insn >> 5) & 0xFFFF);
        return 0;
    }

    /* ---- ADR / ADRP ---- */
    if ((insn & 0x1F000000) == 0x10000000) {
        int rd = (int)(insn & 31);
        uint64_t immlo = (insn >> 29) & 3;
        uint64_t immhi = (insn >> 5) & 0x7FFFF;
        int64_t imm = (int64_t)((immhi << 2) | immlo);
        if (imm & (1 << 20)) imm |= ~((int64_t)0x1FFFFF);
        if (insn & 0x80000000u) {
            uint64_t base = pc & ~0xFFFull;
            set_x(c, rd, base + (uint64_t)(imm << 12));
        } else {
            set_x(c, rd, pc + (uint64_t)imm);
        }
        return 0;
    }
    /* ---- ADD/SUB 立即数 ---- */
    if ((insn & 0x3F800000) == 0x11000000) {   /* 掩码含 bit29，避免吞掉 SUBS/CMP */
        int is64 = (insn >> 31) & 1, sub = (insn >> 30) & 1, sf = (insn >> 29) & 1;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        uint64_t imm = (insn >> 10) & 0xFFF;
        if (sf) {
            uint64_t sh = (insn >> 22) & 3;
            if (sh == 1) imm <<= 12;
            else if (sh != 0) return -1;
        }
        /* 立即数形式的 ADD/SUB：Rn=31 表示 SP；非 S 形式时 Rd=31 也表示 SP（写回栈指针） */
        uint64_t a = get_z(c, rn);
        uint64_t res = sub ? (a - imm) : (a + imm);
        if (!is64) res = (uint32_t)res;
        if (rd == 31) c->sp = res; else c->r[rd] = res;
        return 0;
    }
    /* ---- ADD/SUB 立即数（带标志） ---- */
    if ((insn & 0x3F800000) == 0x31000000) {
        int is64 = (insn >> 31) & 1, sub = (insn >> 30) & 1;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        uint64_t imm = (insn >> 10) & 0xFFF;
        if (is64) { uint64_t sh = (insn >> 22) & 3; if (sh == 1) imm <<= 12; else if (sh) return -1; }
        uint64_t a = get_z(c, rn);            /* Rn=31 → SP */
        if (!is64) { a = (uint32_t)a; imm = (uint32_t)imm; }
        uint64_t res = sub ? a - imm : a + imm;
        uint64_t sign_a = is64 ? a : (uint32_t)a;
        uint64_t sign_r = is64 ? res : (uint32_t)res;
        int n = is64 ? (int)(sign_r >> 63) : (int)((uint32_t)sign_r >> 31);
        int z = ((is64 ? sign_r : (uint32_t)sign_r) == 0);
        int cc = sub ? (a >= imm) : (res < a);
        int v = is64 ? (int)(((sign_a ^ sign_r) & ((sign_a ^ (sub ? ~imm : imm)) )) >> 63) & 1
                     : 0;
        set_flags(c, n, z, cc, v);
        if (rd != 31) set_x(c, rd, is64 ? sign_r : (uint64_t)(uint32_t)sign_r);
        return 0;   /* SUBS/CMP：Rd=31 即 XZR，不写寄存器 */
    }
    /* ---- 逻辑立即数（必须带 bit23，否则会吞掉 MOVZ/MOVN/MOVK） ---- */
    if ((insn & 0x1F800000) == 0x12000000) {
        int is64 = (insn >> 31) & 1, opc = (insn >> 29) & 3;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        uint64_t imm = 0;
        if (decode_bit_masks((insn >> 22) & 1, (insn >> 10) & 0x3F, (insn >> 16) & 0x3F, is64, &imm) != 0)
            return -1;
        uint64_t a = get_x(c, rn);
        if (!is64) { a = (uint32_t)a; imm = (uint32_t)imm; }
        uint64_t res;
        switch (opc) {
        case 0: res = a & imm; break;                 /* AND */
        case 1: res = a | imm; break;                 /* ORR */
        case 2: res = a ^ imm; break;                 /* EOR */
        default: res = a & imm; break;                /* ANDS */
        }
        if (!is64) res = (uint32_t)res;
        if (opc == 3) {
            int n = is64 ? (int)(res >> 63) : (int)((uint32_t)res >> 31);
            set_flags(c, n, res == 0, 0, 0);
        }
        if (rd != 31) set_x(c, rd, res);
        return 0;
    }
    /* ---- MOVN/MOVZ/MOVK ---- */
    if ((insn & 0x1F800000) == 0x12800000) {
        int is64 = (insn >> 31) & 1, opc = (insn >> 29) & 3;
        int rd = (int)(insn & 31);
        uint64_t imm = (uint64_t)((insn >> 5) & 0xFFFF) << ((insn >> 21) & 3) * 16;
        uint64_t v;
        if (opc == 0) v = ~imm;
        else if (opc == 2) v = imm;
        else v = (get_x(c, rd) & ~(0xFFFFull << (((insn >> 21) & 3) * 16))) | imm;
        if (!is64) v = (uint32_t)v;
        if (getenv("S2A_EMU_DEBUG") && rd == 2)
            fprintf(stderr, "  [movz/movk] pc=%#llx insn=%08x opc=%d hw=%d imm=%#llx → x2=%#llx\n",
                    (unsigned long long)pc, insn, opc, (insn >> 21) & 3, (unsigned long long)imm,
                    (unsigned long long)v);
        set_x(c, rd, v);
        return 0;
    }
    /* ---- 带进位加减：ADC/ADCS/SBC/SBCS/NGC/NGCS（寄存器形式）----
       编码：sf(31) op(30) S(29) 11010000 Rm 000000 Rn Rd；NGC = Rm==31 且 Rn==31 */
    if ((insn & 0x1FE0FC00) == 0x1A000000) {
        int sf = (insn >> 31) & 1, op = (insn >> 30) & 1, S = (insn >> 29) & 1;
        int rm = (int)((insn >> 16) & 31), rn = (int)((insn >> 5) & 31), rd = (int)(insn & 31);
        int ds = sf ? 64 : 32;
        /* ⚑ nzcv 是 4 位布局（N=8 Z=4 C=2 V=1），别按 PSTATE 的 bit29 读——那是 0，ADC/SBC 会差 1 */
        uint64_t a = get_x(c, rn), b = get_x(c, rm), carry = (uint64_t)flag_c(c);
        if (!sf) { a = (uint32_t)a; b = (uint32_t)b; }
        uint64_t res;
        int n, z, cc, v;
        uint64_t signbit = 1ull << (ds - 1);
        if (!op) {                                   /* ADC：a + b + C */
            res = a + b + carry;
            v = (int)(((a ^ res) & (b ^ res) & signbit) != 0);
            cc = (int)(res < a) || (res == a && carry);
        } else {                                     /* SBC：a - b - 1 + C */
            uint64_t nb = ~b;
            res = a + nb + carry;
            v = (int)(((a ^ nb) & (a ^ res) & signbit) != 0);
            cc = (int)(res < a) || (res == a && carry);
        }
        n = (int)((res & signbit) != 0);
        z = (res & (sf ? ~0ull : 0xFFFFFFFFull)) == 0;
        if (S) {
            if (!sf) res = (uint32_t)res;
            set_flags(c, n, z, cc, v);
            set_x(c, rd, res);
        } else {
            set_x(c, rd, res);
        }
        return 0;
    }
    /* ---- 位域 SBFM/BFM/UBFM（LSL/LSR/ASR/SBFX/UBFIZ/BFXIL 都走这里）----
     * 语义（datasize = 32 或 64）：
     *   immr <= imms ：提取  字段 [immr, imms]   → (src >> immr) & ones(imms-immr+1)
     *   immr >  imms ：把低 (imms+1) 位抬到高位 → (src << (ds-immr)) & (ones(imms+1) << (ds-immr))
     *   SBFM 在此基础上按 imms 位做符号扩展；BFM 则是把这段插回原值。
     * 早先版本多掩了一层 ones(imms+1)，导致 `lsl x, x, #13` 丢高位（自检电池抓到）。*/
    if ((insn & 0x1F800000) == 0x13000000) {
        int is64 = (insn >> 31) & 1, opc = (insn >> 29) & 3;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        int immr = (int)((insn >> 16) & 0x3F);
        int imms = (int)((insn >> 10) & 0x3F);
        int ds = is64 ? 64 : 32;
        if (!is64) { immr &= 31; imms &= 31; }
        uint64_t src = get_x(c, rn);
        if (!is64) src = (uint32_t)src;
        uint64_t res;
        if (immr <= imms) {
            /* 提取字段 [immr, imms]（结果在低位） */
            int w = imms - immr + 1;
            uint64_t fmask = (w >= ds) ? ~0ull : ((1ull << w) - 1);
            uint64_t f = (src >> immr) & fmask;
            if (opc == 2) {                       /* UBFM：零扩展 */
                res = f;
            } else if (opc == 0) {                /* SBFM：按字段最高位符号扩展 */
                int sb = w - 1;
                res = f;
                if (sb < ds - 1 && (f & (1ull << sb))) res |= ~((1ull << (sb + 1)) - 1);
            } else {                              /* BFM：把字段插回原位置 */
                uint64_t m = fmask << immr;
                res = (get_x(c, rd) & ~m) | (f << immr);
            }
        } else {
            /* 把低 (imms+1) 位抬到高位（LSL/SBFIZ 这类形式） */
            int sh = ds - immr;
            uint64_t m = ((imms + 1 >= ds) ? ~0ull : ((1ull << (imms + 1)) - 1)) << sh;
            uint64_t f = (src << sh) & m;
            if (opc == 1) res = (get_x(c, rd) & ~m) | f;      /* BFM：插回 */
            else if (opc == 0) {                              /* SBFM：按字段最高位 imms 符号扩展 */
                uint64_t val = f;
                if ((src >> imms) & 1) {                      /* 字段为负 → 高位补 1 */
                    int top = imms + 1 + sh;
                    if (top < ds) val |= ~((1ull << top) - 1);
                }
                res = val;
            } else res = f;                                    /* UBFM：零扩展 */
        }
        if (!is64) res = (uint32_t)res;
        set_x(c, rd, res);
        return 0;
    }
    /* ---- extr ---- */
    if ((insn & 0x1F800000) == 0x13800000) {
        int is64 = (insn >> 31) & 1;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
        int lsb = (int)((insn >> 10) & (is64 ? 63 : 31));
        int width = is64 ? 64 : 32;
        uint64_t a = get_x(c, rn), b = get_x(c, rm);
        if (!is64) { a = (uint32_t)a; b = (uint32_t)b; }
        /* EXTR：把 Xn(高) 与 Xm(低) 拼成 2*width 位后右移 lsb；
           早先把 n/m 弄反（写成 a>>lsb | b<<…），实测 extr x4,x2,x3,#13 与原生不符 */
        uint64_t res = (b >> lsb) | (lsb ? (a << (width - lsb)) : 0);
        if (!is64) res = (uint32_t)res;
        set_x(c, rd, res);
        return 0;
    }
    /* ---- 数据加工（寄存器） ---- */
    if ((insn & 0x1F000000) == 0x0A000000 || (insn & 0x1F000000) == 0x0B000000) {
        int is64 = (insn >> 31) & 1;
        int op = (insn >> 24) & 0x1F;
        int shift_type = (int)((insn >> 22) & 3);
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
        int amount = (int)((insn >> 10) & 0x3F);
        if (!is64) amount &= 31;
        /* ADD/SUB 的寄存器形式分两种：
         *   移位形式（bit21=0）：Rn=31 → **XZR**
         *   扩展形式（bit21=1）：Rn=31 → **SP**（Rm=31 才是 XZR）
         * 早先把两者统一按 SP 处理，导致 `neg x1, x1` 变成 `sp - x1`，SP 值泄进数据里。*/
        uint64_t a = (op == 0x0B && ((insn >> 21) & 1)) ? get_z(c, rn) : get_x(c, rn);
        uint64_t b = get_x(c, rm);
        if (!is64) { a = (uint32_t)a; b = (uint32_t)b; }
        switch (op) {
        case 0x0B: {  /* ADD/SUB/ADDS/SUBS（移位寄存器 或 扩展寄存器两种形式） */
            int sub = (insn >> 30) & 1;
            int setf = (insn >> 29) & 1;
            uint64_t bb;
            if ((insn >> 21) & 1) {
                /* 扩展寄存器形式：add x2, x2, w1, sxtw #3 —— option/imm3 而非 shift/amount */
                int option = (int)((insn >> 13) & 7);
                int imm3 = (int)((insn >> 10) & 7);
                bb = do_extend(b, option, imm3, is64);
            } else {
                bb = do_shift(b, shift_type, amount, is64);
            }
            uint64_t res = sub ? (a - bb) : (a + bb);
            if (!is64) res = (uint32_t)res;
            if (setf) {
                uint64_t signbit = is64 ? (1ull << 63) : (1ull << 31);
                int n = (res & signbit) != 0;
                int cc = sub ? (a >= bb) : (res < a);
                int v = sub ? (int)(((a ^ bb) & (a ^ res) & signbit) != 0)
                            : (int)(((a ^ res) & (bb ^ res) & signbit) != 0);
                set_flags(c, n, res == 0, cc, v);
            }
            if (rd == 31) { if (!setf) c->sp = res; } else c->r[rd] = res;
            return 0;
        }
        case 0x0A: {  /* AND/BIC/ORR/ORN/EOR/EON/ANDS/BICS (寄存器，可带移位) */
            if (getenv("S2A_EMU_DBG") && rd == 1)
                fprintf(stderr, "  [逻辑] pc=%#llx insn=%08x opc=%d N=%d a=%#llx b=%#llx\n",
                        (unsigned long long)pc, insn, (insn >> 29) & 3, (insn >> 21) & 1,
                        (unsigned long long)get_x(c, rn), (unsigned long long)get_x(c, rm));
            int opc = (insn >> 29) & 3;
            int nn = (insn >> 21) & 1;
            int setf = (opc == 3);
            uint64_t bb = do_shift(b, shift_type, amount, is64);
            uint64_t res;
            if (nn == 0) {
                switch (opc) {
                case 0: res = a & bb; break;
                case 1: res = a | bb; break;
                case 2: res = a ^ bb; break;
                default: res = a & bb; break;
                }
            } else {
                switch (opc) {
                case 0: res = a & ~bb; break;
                case 1: res = a | ~bb; break;
                case 2: res = a ^ ~bb; break;
                default: res = a & ~bb; break;
                }
            }
            if (!is64) res = (uint32_t)res;
            if (setf) {
                int n2 = is64 ? (int)(res >> 63) : (int)((uint32_t)res >> 31);
                set_flags(c, n2, res == 0, 0, 0);
            }
            if (rd != 31) set_x(c, rd, res);
            return 0;
        }
        default: break;
        }
    }
    /* ---- MADD/MSUB/MUL 以及乘长（SMADDL/UMADDL/SMULH/UMULH） ---- */
    if ((insn & 0x7F000000) == 0x1B000000) {
        int is64 = (insn >> 31) & 1;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        int rm = (int)((insn >> 16) & 31), ra = (int)((insn >> 10) & 31);
        int o0 = (insn >> 15) & 1;
        int op31 = (insn >> 21) & 7;
        if (op31 == 1 || op31 == 5) {
            /* SMADDL/SMSUBL（op31=001）、UMADDL/UMSUBL（op31=101）：32 位操作数扩展后乘加 */
            uint64_t a = get_x(c, rn), b = get_x(c, rm);
            uint64_t acc = (ra == 31) ? 0 : get_x(c, ra);
            uint64_t prod;
            if (op31 == 1) {
                int64_t sa = (int64_t)(int32_t)a, sb = (int64_t)(int32_t)b;
                prod = (uint64_t)(sa * sb);
            } else {
                prod = (uint64_t)(uint32_t)a * (uint64_t)(uint32_t)b;
            }
            set_x(c, rd, o0 ? (acc - prod) : (acc + prod));
            return 0;
        }
        if (op31 == 2) {                     /* SMULH：有符号 64×64 的高 64 位 */
            __int128 p = (__int128)(int64_t)get_x(c, rn) * (__int128)(int64_t)get_x(c, rm);
            set_x(c, rd, (uint64_t)(p >> 64));
            return 0;
        }
        if (op31 == 6) {                     /* UMULH：无符号 64×64 的高 64 位 */
            unsigned __int128 p = (unsigned __int128)get_x(c, rn) * (unsigned __int128)get_x(c, rm);
            set_x(c, rd, (uint64_t)(p >> 64));
            return 0;
        }
        /* 普通 MADD/MSUB/MUL */
        {
            uint64_t a = get_x(c, rn), b = get_x(c, rm), acc = ra == 31 ? 0 : get_x(c, ra);
            uint64_t res = o0 ? (acc - a * b) : (acc + a * b);
            if (!is64) res = (uint32_t)res;
            set_x(c, rd, res);
            return 0;
        }
    }
    /* ---- 除法（UDIV = 0x…0800，SDIV = 0x…0C00） ---- */
    if ((insn & 0x7FE0FC00) == 0x1AC00800 || (insn & 0x7FE0FC00) == 0x1AC00C00) {   /* b30=0：别抢 1 源组的 rev/rev32 */
        int is64 = (insn >> 31) & 1, signed_op = (insn >> 10) & 1;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
        int64_t a = (int64_t)(is64 ? get_x(c, rn) : (uint64_t)(int64_t)(int32_t)get_x(c, rn));
        int64_t b = (int64_t)(is64 ? get_x(c, rm) : (uint64_t)(int64_t)(int32_t)get_x(c, rm));
        if (!is64) { a = (int32_t)a; b = (int32_t)b; }
        uint64_t res;
        if (b == 0) res = 0;
        else if (signed_op) res = (uint64_t)(a / b);
        else {
            uint64_t ua = is64 ? get_x(c, rn) : (uint32_t)get_x(c, rn);
            uint64_t ub = is64 ? get_x(c, rm) : (uint32_t)get_x(c, rm);
            res = ub ? ua / ub : 0;
        }
        if (!is64) res = (uint32_t)res;
        set_x(c, rd, res);
        return 0;
    }
    /* ---- 数据加工(2 源)：UDIV/SDIV/LSLV/LSRV/ASRV/RORV（bit30=0）----
       注意：位 30 用来区分 1 源组（RBIT/REV/CLZ），早期没分导致 RBIT 落进来被当字节交换。 */
    if ((insn & 0x7FE00000) == 0x1AC00000) {   /* b30=0：2 源 */
        int is64 = (insn >> 31) & 1;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
        int op2 = (int)((insn >> 10) & 0x3F);
        uint64_t a = get_x(c, rn), b = get_x(c, rm);
        if (!is64) { a = (uint32_t)a; b = (uint32_t)b; }
        uint64_t res = 0;
        if (op2 == 2) {                                   /* UDIV */
            if (b == 0) res = 0;
            else res = is64 ? (a / b) : (uint64_t)((uint32_t)a / (uint32_t)b);
        } else if (op2 == 3) {                            /* SDIV */
            if (b == 0) res = 0;
            else if (is64) res = (uint64_t)((int64_t)a / (int64_t)b);
            else res = (uint64_t)(uint32_t)((int32_t)a / (int32_t)b);
        }
        else if (op2 == 8) res = do_shift(a, 0, (int)(b & (is64 ? 63 : 31)), is64);       /* LSLV */
        else if (op2 == 9) res = do_shift(a, 1, (int)(b & (is64 ? 63 : 31)), is64);       /* LSRV */
        else if (op2 == 10) res = do_shift(a, 2, (int)(b & (is64 ? 63 : 31)), is64);      /* ASRV */
        else if (op2 == 11) res = do_shift(a, 3, (int)(b & (is64 ? 63 : 31)), is64);      /* RORV */
        else return -1;
        if (!is64) res = (uint32_t)res;
        set_x(c, rd, res);
        return 0;
    }
    /* ---- 数据加工(1 源)：RBIT/REV16/REV32/REV/CLZ/CLS（bit30=1）----
       ⚑ RBIT 必须按位反转：GCC 在 aarch64 上把 __builtin_ctzll 编译成 rbit+clz，
         musl 分配器用它选空闲槽；早期把 RBIT 写成 bswap 会话导致分配器重复发同一块。 */
    if ((insn & 0x7FE00000) == 0x5AC00000) {   /* b30=1：1 源（rbit/rev/clz…），sf 单独取 bit31 */
        int is64 = (insn >> 31) & 1;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        int op2 = (int)((insn >> 10) & 0x3F);
        uint64_t a = get_x(c, rn);
        int wb = is64 ? 8 : 4;
        uint64_t res;
        switch (op2) {
        case 0x00: {                                      /* RBIT：整字按位反转 */
            uint64_t v = a, r = 0;
            for (int i = 0; i < wb * 8; i++) { r = (r << 1) | (v & 1); v >>= 1; }
            res = r;
            break;
        }
        case 0x01: {                                      /* REV16：每 16 位内字节反转 */
            uint64_t r = 0;
            for (int i = 0; i < wb * 8; i += 16) {
                uint16_t h = (uint16_t)((a >> i) & 0xFFFF);
                r |= (uint64_t)(uint16_t)((h >> 8) | (h << 8)) << i;
            }
            res = r;
            break;
        }
        case 0x02: case 0x03: {                           /* op2=2 REV32（每 32 位内字节反转）；sf=1/op2=3 是 REV */
            if (is64 && op2 == 3) res = __builtin_bswap64(a);          /* REV Xd, Xn */
            else if (!is64) res = __builtin_bswap32((uint32_t)a);      /* REV Wd, Wn */
            else {                                                     /* REV32 Xd, Xn */
                uint64_t r = 0;
                for (int i = 0; i < 64; i += 32)
                    r |= (uint64_t)__builtin_bswap32((uint32_t)((a >> i) & 0xFFFFFFFF)) << i;
                res = r;
            }
            break;
        }
        case 0x04: res = is64 ? (uint64_t)__builtin_clzll(a) : (uint64_t)__builtin_clz((uint32_t)a); break; /* CLZ */
        case 0x05: {                                      /* CLS：符号位起连续相同位数（不含符号位） */
            uint64_t v = is64 ? a : (uint32_t)a;
            int n = wb * 8, r = 0;
            uint64_t sign = (v >> (n - 1)) & 1;
            for (int i = n - 2; i >= 0; i--) { if (((v >> i) & 1) == sign) r++; else break; }
            res = (uint64_t)r;
            break;
        }
        default: return -1;
        }
        if (!is64) res = (uint32_t)res;
        set_x(c, rd, res);
        return 0;
    }
    /* ---- 条件选择 / 条件比较 ---- */
    if ((insn & 0x1FE00000) == 0x1A800000) {
        int is64 = (insn >> 31) & 1;
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
        int cond = (int)((insn >> 12) & 15);
        /* ⚑ 选哪个由 (bit30, bit10) 共同决定：CSEL(0,0)/CSINC(0,1)/CSINV(1,0)/CSNEG(1,1)；
           早期只用 bits11-10 判，CSNEG 会被当成 CSINV。cset/csetm 是 Rn=Rm=31 的别名，
           汇编器已把条件取反，不需要额外处理。 */
        int op = (int)((insn >> 30) & 1), op2 = (int)((insn >> 10) & 1);
        uint64_t a = get_x(c, rn), b = get_x(c, rm);
        int ok = cond_ok(c, cond);
        uint64_t res;
        switch ((op << 1) | op2) {
        case 0: res = ok ? a : b; break;                 /* CSEL  */
        case 1: res = ok ? a : (b + 1); break;           /* CSINC */
        case 2: res = ok ? a : ~b; break;                /* CSINV */
        case 3: res = ok ? a : (uint64_t)(-(int64_t)b); break;  /* CSNEG */
        default: res = a; break;
        }
        if (!is64) res = (uint32_t)res;
        set_x(c, rd, res);
        return 0;
    }
    if ((insn & 0x1FE00800) == 0x1A400000 || (insn & 0x1FE00800) == 0x1A400800) {
        /* CCMP/CCMN（寄存器或 5 位立即数形式）：条件成立才做比较并更新标志，否则用立即数 nzcv */
        int is64 = (insn >> 31) & 1;
        int is_imm = (insn >> 11) & 1;
        int rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
        int cond = (int)((insn >> 12) & 15);
        int is_ccmp = (insn >> 30) & 1;          /* 1 = CCMP（相减比较），0 = CCMN（相加） */
        uint64_t a = get_x(c, rn);
        uint64_t b = is_imm ? (uint64_t)((insn >> 16) & 0x1F) : get_x(c, rm);
        if (!is64) { a = (uint32_t)a; b = (uint32_t)b; }
        if (cond_ok(c, cond)) {
            uint64_t res = is_ccmp ? (a - b) : (a + b);
            if (!is64) res = (uint32_t)res;
            uint64_t signbit = is64 ? (1ull << 63) : (1ull << 31);
            int n = (res & signbit) != 0;
            int cc2 = is_ccmp ? (a >= b) : (res < a);
            int v = is_ccmp ? (int)(((a ^ b) & (a ^ res) & signbit) != 0)
                            : (int)(((a ^ res) & (b ^ res) & signbit) != 0);
            set_flags(c, n, res == 0, cc2, v);
        } else {
            c->nzcv = (uint32_t)(insn & 15);
        }
        return 0;
    }
    /* ---- 条件取反/置位（CSET/CSETM/CINC/…）已由 CSEL 组覆盖 ---- */
    /* ---- 访存：无符号偏移 ---- */
    if ((insn & 0x3B000000) == 0x39000000 && !((insn >> 26) & 1)) {
        int size = (int)(insn >> 30);
        int opc = (int)((insn >> 22) & 3);
        int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        uint64_t off = (uint64_t)((insn >> 10) & 0xFFF) << size;
        uint64_t va = get_z(c, rn) + off;
        int is_load = (opc & 1) != 0;      /* 位22：1 = 装载，0 = 存储 */
        int ok = 1;
        if (rt == 31 && (opc == 2 || opc == 3)) {   /* PRFM：预取，忽略（判据必须带 Rt=31，
                                                       否则 LDRSB/LDRSH 的 opc=11 会被整条吃掉） */
            return 0;
        } else if (opc == 2) {             /* LDRSW/LDRSB/LDRSH：有符号装载到 64 位 */
            set_x(c, rt, do_load(c, va, size, 1, 1, &ok));
        } else if (opc == 3) {             /* LDRSB/LDRSH Wt：有符号装载到 32 位 */
            set_x(c, rt, (uint32_t)do_load(c, va, size, 1, 0, &ok));
        } else if (is_load) {
            set_x(c, rt, do_load(c, va, size, 0, size == 3, &ok));
        } else {
            do_store(c, va, size, get_x(c, rt), &ok);
        }
        return ok ? 0 : -1;
    }
    /* ---- 访存：非缩放/前索引/后索引 ---- */
    if ((insn & 0x3B200000) == 0x38000000 && !((insn >> 26) & 1)) {
        int size = (int)(insn >> 30);
        int opc = (int)((insn >> 22) & 3);
        int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        int mode = (int)((insn >> 10) & 3);
        int64_t off = (int64_t)((insn >> 12) & 0x1FF);
        if (off & 0x100) off |= ~((int64_t)0x1FF);
        uint64_t base = get_z(c, rn);
        /* 前索引：访问地址含偏移；后索引：访问地址是 base，写完再写回 base+off */
        uint64_t va = (mode == 1) ? base : (base + (uint64_t)off);
        int ok = 1;
        int is_load = (opc & 1) != 0;
        if (rt == 31 && (opc == 2 || opc == 3)) { /* PRFM 等 */ }
        else if (opc == 2) set_x(c, rt, do_load(c, va, size, 1, 1, &ok));
        else if (opc == 3) set_x(c, rt, (uint32_t)do_load(c, va, size, 1, 0, &ok));
        else if (is_load) set_x(c, rt, do_load(c, va, size, 0, size == 3, &ok));
        else do_store(c, va, size, get_x(c, rt), &ok);
        if (mode == 1 || mode == 3) set_z(c, rn, base + (uint64_t)off);
        return ok ? 0 : -1;
    }
    /* ---- 访存：寄存器偏移 ---- */
    if ((insn & 0x3B200C00) == 0x38200800 && !((insn >> 26) & 1)) {
        int size = (int)(insn >> 30);
        int opc = (int)((insn >> 22) & 3);
        int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rm = (int)((insn >> 16) & 31);
        int option = (int)((insn >> 13) & 7);
        int S = (int)((insn >> 12) & 1);
        uint64_t off = do_extend(get_x(c, rm), option, S ? size : 0, 1);
        uint64_t va = get_z(c, rn) + off;
        int ok = 1;
        if (rt == 31 && (opc == 2 || opc == 3)) return 0;     /* PRFM */
        else if (opc == 2) set_x(c, rt, do_load(c, va, size, 1, 1, &ok));
        else if (opc == 3) set_x(c, rt, (uint32_t)do_load(c, va, size, 1, 0, &ok));
        else if (opc & 1)  set_x(c, rt, do_load(c, va, size, 0, size == 3, &ok));
        else do_store(c, va, size, get_x(c, rt), &ok);
        return ok ? 0 : -1;
    }
    /* ---- LDP/STP ---- */
    if ((insn & 0x3A000000) == 0x28000000 && !((insn >> 26) & 1)) {
        int opc = (int)((insn >> 30) & 3);
        int v = 0;
        int mode = (int)((insn >> 23) & 3);
        int is_load = (insn >> 22) & 1;
        int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rt2 = (int)((insn >> 10) & 31);
        int64_t off = (int64_t)((insn >> 15) & 0x7F);
        if (off & 0x40) off |= ~((int64_t)0x7F);
        int scale = 2 + (opc >> 1);
        off <<= scale;
        uint64_t base = get_z(c, rn);
        /* 后索引：访问地址是 base，之后写回 base+off；前索引/偏移：访问地址含 off */
        uint64_t va = (mode == 1) ? base : (base + (uint64_t)off);
        int ok = 1;
        if (v) return -1;                 /* 向量 LDP/STP 暂不支持 */
        int sz = (opc & 2) ? 3 : 2;       /* 64 位或 32 位元素 */
        if (is_load) {
            uint64_t a = do_load(c, va, sz, 0, sz == 3, &ok);
            if (ok) {
                uint64_t b = do_load(c, va + (uint64_t)(1 << sz), sz, 0, sz == 3, &ok);
                set_x(c, rt, a);
                set_x(c, rt2, b);
            }
        } else {
            do_store(c, va, sz, get_x(c, rt), &ok);
            if (ok) do_store(c, va + (uint64_t)(1 << sz), sz, get_x(c, rt2), &ok);
        }
        if (ok && mode == 1) set_z(c, rn, base + (uint64_t)off);       /* 后索引 */
        else if (ok && mode == 3) set_z(c, rn, base + (uint64_t)off);  /* 前索引 */
        return ok ? 0 : -1;
    }
    /* ---- LDR 字面量（PC 相对） ---- */
    if ((insn & 0x3B000000) == 0x18000000) {
        int opc = (int)((insn >> 30) & 3);
        int rt = (int)(insn & 31);
        int64_t off = (int64_t)((insn >> 5) & 0x7FFFF);
        if (off & (1 << 18)) off |= ~((int64_t)0x7FFFF);
        uint64_t va = pc + (uint64_t)(off << 2);
        int ok = 1;
        int size = opc >> 1;
        if (opc == 3) { set_x(c, rt, va); return 0; }      /* PRFM */
        uint64_t v = do_load(c, va, size, (opc & 1) && size < 3, size == 3, &ok);
        set_x(c, rt, v);
        return ok ? 0 : -1;
    }
    /* ---- 独占访存 ---- */
    if ((insn & 0x3F007C00) == 0x08007C00) {   /* LDXR/LDAXR/STXR/STLXR 一类（独占访存） */
        int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rs = (int)((insn >> 16) & 31);
        int size = (int)((insn >> 30) & 3);
        /* 位22 才是装载/存储位：1 = 装载（LDXR/LDAXR），0 = 存储（STXR/STLXR）。
         * 早先用 bit15 判断，导致 STXR 被当装载 → __unlockfile 自旋不停。*/
        int is_store = ((insn >> 22) & 1) == 0;
        /* ⚑ 族的分界是 bit23：0 → 独占（LDXR/LDAXR/STXR/STLXR/LDXRB/STXRB，要监视点）；
           1 → 普通获取/释放（LDAR/STLR/LDARB/STLRB）。bit15 只是在族内区分 acquire/release，
           早先按 bit15 分，把 STLXR 当成普通存储 → 不置状态寄存器 → musl nontrivial_free 自旋。 */
        int is_plain = (int)((insn >> 23) & 1);
        uint64_t va = get_z(c, rn);
        int ok = 1;
        if (is_plain) {                            /* LDAR/STLR：不当独占处理 */
            if (!is_store) {
                uint64_t v = do_load(c, va, size, 0, size == 3, &ok);
                if (ok) set_x(c, rt, size == 3 ? v : (uint64_t)(uint32_t)v);
            } else {
                do_store(c, va, size, get_x(c, rt), &ok);
            }
            return ok ? 0 : -1;
        }
        if (!is_store) {
            c->excl_addr = va;
            c->excl_size = size;
            c->excl_valid = 1;
            uint64_t v = do_load(c, va, size, 0, size == 3, &ok);
            if (ok) set_x(c, rt, size == 3 ? v : (uint64_t)(uint32_t)v);
        } else {
            /* 独占存储必须与独占装载“同地址同尺寸”，否则失败（返回 1）且不写内存。
               实测原生 stlxr 到不同地址就是失败；早期无条件成功，与原生不符。 */
            int mon = c->excl_valid && c->excl_addr == va && c->excl_size == size;
            c->excl_valid = 0;                     /* 无论成败都清监视点 */
            if (!mon) {
                if (rs != 31) set_x(c, rs, 1);     /* 1 = 独占存储失败 */
                return 0;
            }
            do_store(c, va, size, get_x(c, rt), &ok);
            if (ok && rs != 31) set_x(c, rs, 0);   /* 0 = 独占存储成功 */
        }
        return ok ? 0 : -1;
    }
    /* ---- FP：FMOV（通用寄存器 ↔ 浮点寄存器 / 立即数） ---- */
    if ((insn & 0xFFFFFC00) == 0x9E660000) {          /* FMOV Xd, Dn */
        int rn = (int)((insn >> 5) & 31), rd = (int)(insn & 31);
        set_x(c, rd, c->v[rn][0]);
        return 0;
    }
    if ((insn & 0xFFFFFC00) == 0x1E260000) {          /* FMOV Wd, Sn */
        int rn = (int)((insn >> 5) & 31), rd = (int)(insn & 31);
        set_x(c, rd, (uint32_t)c->v[rn][0]);
        return 0;
    }
    if ((insn & 0xFFFFFC00) == 0x9E670000) {          /* FMOV Dd, Xn */
        int rn = (int)((insn >> 5) & 31), rd = (int)(insn & 31);
        c->v[rd][0] = get_x(c, rn);
        return 0;
    }
    if ((insn & 0xFFFFFC00) == 0x1E201000) {          /* FMOV Dd, imm8 */
        return 0;                                     /* 少见：当作零处理 */
    }
    if ((insn & 0xFFFFFC00) == 0x1E270000) {          /* FMOV Sd, Wn */
        int rn = (int)((insn >> 5) & 31), rd = (int)(insn & 31);
        c->v[rd][0] = (uint32_t)get_x(c, rn);
        return 0;
    }
    /* ---- SIMD/FP 单寄存器访存（V=1, bits29-27=111）----
       关键字段（已用汇编器逐条核对）：
         size = bits31-30；opc = bits23-22，其中 opc&1 = 1 表示装载、
         opc&2 = 1 表示 128 位（Q）；bit24 = 1 为无偏移形式（imm12 = bits21-10，按 16 倍数缩放），
         bit24 = 0 为前后索引：imm9 = bits20-12（**不缩放**），bits11-10: 01=后索引 11=前索引。
       旧实现把 128 位写成 opc==1，且用 `ldr q` 走成了 1 字节存取（buf 全是 0 的元凶）。 */
    if ((insn & 0x3C000000) == 0x3C000000) {
        who_set("simd-ldst", pc);   /* bits29-27=111 且 V=1（单寄存器 SIMD/FP 访存） */
        int size = (int)(insn >> 30);
        int opc = (int)((insn >> 22) & 3);
        int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        int nbytes = (opc & 2) ? 16 : (1 << size);
        int mode = (int)((insn >> 10) & 3);
        uint64_t off;
        /* ⚠ 判定顺序（三条都实测过）：
             1) bit21=1 → 寄存器偏移（`ldr q0,[x0,x2]` = 0x3ce26800，注意它的 bit24 是 0）
             2) bit24=1 → 无偏移（imm12 在 bits21-10，按元素宽度缩放）
             3) 否则   → imm9 前后索引（`str q0,[x0],#16` = 0x3c810400）
           早先按 bit24 优先判，会把寄存器偏移当成 imm9。 */
        if ((insn >> 21) & 1) {                        /* 寄存器偏移形式 */
            int rm = (int)((insn >> 16) & 31);
            int option = (int)((insn >> 13) & 7);
            int S = (int)((insn >> 12) & 1);
            off = do_extend(get_x(c, rm), option, S ? size : 0, 1);
            mode = 0;
        } else if ((insn >> 24) & 1) {                 /* 无偏移形式 */
            off = (uint64_t)((insn >> 10) & 0xFFF) * (uint64_t)nbytes;
            mode = 0;
        } else {                                       /* 前/后索引：imm9 不缩放 */
            int64_t so = (int64_t)((insn >> 12) & 0x1FF);
            if (so & 0x100) so |= ~((int64_t)0x1FF);
            off = (uint64_t)so;
        }
        uint64_t base = get_z(c, rn);
        /* 后索引（mode==1）：本次访问地址是 base，写回才是 base+off；前索引/无偏移用 base+off */
        uint64_t va = (mode == 1) ? base : base + off;
        int store = (opc & 1) == 0;                    /* 位22：1 = 装载，0 = 存储 */
        int ok = 1;
        if (!bad_range(va, (uint64_t)nbytes)) {
            if (store) {
                if (g_watch_lo && va < g_watch_hi && va + nbytes > g_watch_lo)
                    fprintf(stderr, "\x1b[33m[监视]\x1b[0m SIMD 写 %#llx（%d 字节，pc=%#llx）\n",
                            (unsigned long long)va, nbytes, (unsigned long long)(c->pc - 4));
                memcpy(g_base + va, &c->v[rt][0], (size_t)nbytes);
                if (!guest_writable(va, (uint64_t)nbytes)) { seg_fault(c, va, "向量存储", nbytes); return -1; }
            } else {
                memset(&c->v[rt][0], 0, 16);
                memcpy(&c->v[rt][0], g_base + va, (size_t)nbytes);
            }
        } else { emu_fault(c, "SIMD 访存", va); ok = 0; }
        if (ok && (mode == 1 || mode == 3)) set_z(c, rn, base + off);
        return ok ? 0 : -1;
    }
    /* ---- SIMD/FP 成对访存（V=1）：ldp/stp q/d/s ---- */
    if ((insn & 0x3A000000) == 0x28000000 && ((insn >> 26) & 1)) {
        int opc = (int)((insn >> 30) & 3);
        int mode = (int)((insn >> 23) & 3);
        int is_load = (insn >> 22) & 1;
        int rt = (int)(insn & 31), rn = (int)((insn >> 5) & 31), rt2 = (int)((insn >> 10) & 31);
        int nbytes = (opc == 2) ? 16 : (1 << (2 + (opc & 1)));
        int64_t off = (int64_t)((insn >> 15) & 0x7F);
        if (off & 0x40) off |= ~((int64_t)0x7F);
        off *= nbytes;
        uint64_t base = get_z(c, rn);
        uint64_t va = (mode == 1) ? base : base + (uint64_t)off;   /* 后索引：访问 base，写回 base+off */
        int ok = 1;
        if (!bad_range(va, (uint64_t)(2 * nbytes))) {
            if (is_load) {
                memset(&c->v[rt][0], 0, 16); memcpy(&c->v[rt][0], g_base + va, (size_t)nbytes);
                memset(&c->v[rt2][0], 0, 16); memcpy(&c->v[rt2][0], g_base + va + nbytes, (size_t)nbytes);
            } else {
                if (g_watch_lo && va < g_watch_hi && va + 2 * nbytes > g_watch_lo)
                    fprintf(stderr, "\x1b[33m[监视]\x1b[0m SIMD LDP/STP 写 %#llx（pc=%#llx）\n",
                            (unsigned long long)va, (unsigned long long)(c->pc - 4));
                memcpy(g_base + va, &c->v[rt][0], (size_t)nbytes);
                if (!guest_writable(va, (uint64_t)nbytes)) { seg_fault(c, va, "向量存储", nbytes); return -1; }
                memcpy(g_base + va + nbytes, &c->v[rt2][0], (size_t)nbytes);
            }
        } else { emu_fault(c, "SIMD LDP/STP", va); ok = 0; }
        if (ok && (mode == 1 || mode == 3)) set_z(c, rn, base + (uint64_t)off);
        return ok ? 0 : -1;
    }
    /* 常用 SIMD 工具指令：dup/ins/umov/mov（把标量放进向量寄存器）
       注意掩码必须精确到 bits15-10（0xFC00），否则会把向量三同组
       （add/sub/and/eor/mul，bits15-10 == 0b000001）一起吞掉；
       且 imm5 无低位时 size 会算成 0，旧的 `i += size` 直接死循环。 */
    if ((insn & 0x9F20FC00) == 0x0E000C00) {   /* 必须带 b21=0，否则三同组 SQADD 等会被抢 */
        who_set("dup-general(old)", pc);           /* DUP（通用寄存器 → 向量） */
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        int imm5 = (int)((insn >> 16) & 0x1F);
        int q = (int)((insn >> 30) & 1);
        int size = 1;                                  /* 兜底：imm5 非法时按字节处理，绝不让步长为 0 */
        for (int i = 0; i < 5; i++) if (imm5 & (1 << i)) { size = 1 << i; break; }
        uint64_t val = get_x(c, rn);
        uint64_t lane = 0;
        for (int i = 0; i < 8 && i < size; i++) lane |= ((val >> (8 * i)) & 0xFF) << (8 * i);
        uint64_t m = (size >= 8) ? ~0ull : ((1ull << (size * 8)) - 1);
        lane &= m;
        uint64_t dup64 = 0;
        for (int i = 0; i < 8; i += size) dup64 |= lane << (i * 8);
        c->v[rd][0] = dup64;
        c->v[rd][1] = q ? dup64 : 0;
        return 0;
    }
    if ((insn & 0xBFE0FC00) == 0x0E000400) {           /* DUP（元素 → 向量）：掩码须精确，否则会抢走 EXT */
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        int imm5 = (int)((insn >> 16) & 0x1F);
        int q = (int)((insn >> 30) & 1);
        int pos = -1;
        for (int i = 0; i < 5; i++) if (imm5 & (1 << i)) { pos = i; break; }
        if (pos < 0) { c->v[rd][0] = 0; c->v[rd][1] = 0; return 0; }
        int size = 1 << pos;                           /* 字节数：8/16/32/64 位 */
        int index = imm5 >> (pos + 1);
        uint64_t lane = 0;
        memcpy(&lane, (const uint8_t *)&c->v[rn][0] + (size_t)index * (size_t)size, (size_t)size);
        uint64_t dup64 = 0;
        for (int i = 0; i < 8; i += size) dup64 |= lane << (i * 8);
        c->v[rd][0] = dup64;
        c->v[rd][1] = q ? dup64 : 0;
        return 0;
    }
    if ((insn & 0xFFFFFC00) == 0x4E081C00) {           /* MOV Vd.D[1], Xn */
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        c->v[rd][1] = get_x(c, rn);
        return 0;
    }
    if ((insn & 0xFFFFFC00) == 0x4E083C00) {           /* MOV Xd, Vn.D[1] */
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);
        set_x(c, rd, c->v[rn][1]);
        return 0;
    }
    if ((insn & 0xFFFFFC00) == 0x0E043C00) {   /* UMOV Wd, Vn.S[imm]（低车道形式） */
        int rd = (int)(insn & 31), rn = (int)((insn >> 5) & 31);  /* UMOV Wd, Vn.S[imm] */
        int imm5 = (int)((insn >> 16) & 0x1F);
        int size = 0;
        for (int i = 0; i < 5; i++) if (imm5 & (1 << i)) { size = 1 << i; break; }
        int index = imm5 >> (size ? (size / 8 + 1) : 0);
        uint64_t val = 0;
        memcpy(&val, (uint8_t *)&c->v[rn][0] + (size_t)index * size, (size_t)size);
        set_x(c, rd, val);
        return 0;
    }
    /* --- 标量 FP 融合乘加组（FMADD/FMSUB/FNMADD/FNMSUB）：0 0 0 11111 type ... --- */
    if ((insn & 0x5F000000) == 0x1F000000) {
        int type = (int)((insn >> 22) & 3);
        if (type == 0 || type == 1) {
            int rm = (int)((insn >> 16) & 31), ra = (int)((insn >> 10) & 31);
            int rn = (int)((insn >> 5) & 31), rd = (int)(insn & 31);
            int sub = (int)((insn >> 15) & 1), neg = (int)((insn >> 21) & 1);
            if (type == 0) {
                float a, b, c2, z; uint32_t t;
                t = (uint32_t)c->v[rn][0]; memcpy(&a, &t, 4);
                t = (uint32_t)c->v[rm][0]; memcpy(&b, &t, 4);
                t = (uint32_t)c->v[ra][0]; memcpy(&c2, &t, 4);
                z = sub ? (c2 - a * b) : (a * b + c2);
                if (neg) z = -z;
                uint32_t zz; memcpy(&zz, &z, 4);
                memset(c->v[rd], 0, 16); c->v[rd][0] = zz;
            } else {
                double a, b, c2, z;
                memcpy(&a, c->v[rn], 8); memcpy(&b, c->v[rm], 8); memcpy(&c2, c->v[ra], 8);
                z = sub ? (c2 - a * b) : (a * b + c2);
                if (neg) z = -z;
                memset(c->v[rd], 0, 16); memcpy(c->v[rd], &z, 8);
            }
            return 0;
        }
    }
    /* --- 标量 FP 单/双源组：0 0 0 11110 type ... ---
       字段（实测）：2 源 opcode = bits15-10（FADD10 FSUB14 FMUL2 FDIV6 FMAX18 FMIN22
       FMAXNM26 FMINNM30 FNMUL34）；1 源 opcode = bits20-15 且 bits14-10 == 16
       （FABS1 FNEG2 FSQRT3 SCVTF4 FCVT5 UCVTF6 FRINTN8 FRINTP9 FRINTM10 FRINTZ11 FRINTA12 FRINTX14）。
       ⚠ 早先把 Rm 位混进 key，导致 fadd 报“未实现”。 */
    if ((insn & 0xFF000000) == 0x1E000000) {
        int isd = (int)((insn >> 22) & 1);
        int rn = (int)((insn >> 5) & 31), rd = (int)(insn & 31), rm = (int)((insn >> 16) & 31);
        int one_src = (((insn >> 10) & 0x1F) == 0x10);
        if (((insn >> 10) & 0x3F) == 0) {                /* 转换族：SCVTF/UCVTF/FCVTZS/FCVTZU
                                                            （bit31 = 整数位宽，bit22 = 浮点位宽） */
            int is64i = (int)((insn >> 31) & 1);
            int fsz = isd ? 64 : 32;
            int iop = (int)((insn >> 15) & 0x3F);        /* 4=SCVTF 6=UCVTF 48=FCVTZS 50=FCVTZU */
            if (iop == 4 || iop == 6) {
                uint64_t iv = get_x(c, rn);
                double dv = (iop == 4) ? (double)(is64i ? (int64_t)iv : (int64_t)(int32_t)(uint32_t)iv)
                                       : (double)(is64i ? iv : (uint64_t)(uint32_t)iv);
                memset(c->v[rd], 0, 16);
                if (fsz == 32) { float f = (float)dv; memcpy(c->v[rd], &f, 4); }
                else memcpy(c->v[rd], &dv, 8);
                return 0;
            }
            if (iop == 48 || iop == 50) {
                double dv;
                if (fsz == 32) { float f; memcpy(&f, c->v[rn], 4); dv = (double)f; }
                else memcpy(&dv, c->v[rn], 8);
                if (iop == 48) {
                    int64_t v = (dv != dv) ? 0 : ((dv >= 9.2233720368547758e18) ? INT64_MAX
                                : ((dv <= -9.2233720368547758e18) ? INT64_MIN : (int64_t)dv));
                    if (is64i) set_x(c, rd, (uint64_t)v); else set_x(c, rd, (uint32_t)(int32_t)v);
                } else {
                    uint64_t v = (dv != dv || dv <= 0) ? 0
                                : ((dv >= 1.8446744073709552e19) ? ~0ull : (uint64_t)dv);
                    if (is64i) set_x(c, rd, v); else set_x(c, rd, (uint32_t)v);
                }
                return 0;
            }
        }
        int opc = one_src ? (int)((insn >> 15) & 0x3F) : (int)((insn >> 10) & 0x3F);
        if (isd) {
            double x, y, z = 0;
            memcpy(&x, c->v[rn], 8); memcpy(&y, c->v[rm], 8);
            if (one_src) {
                switch (opc) {
                case 1:  { uint64_t t = (uint64_t)c->v[rn][0] & ~(1ull << 63); memcpy(&z, &t, 8); break; }  /* FABS */
                case 2:  { uint64_t t = (uint64_t)c->v[rn][0] ^ (1ull << 63); memcpy(&z, &t, 8); break; }   /* FNEG */
                case 3:  z = __builtin_sqrt(x); break;                       /* FSQRT */
                case 8:  z = __builtin_rint(x); break;                        /* FRINTN */
                case 9:  z = __builtin_ceil(x); break;                        /* FRINTP */
                case 10: z = __builtin_floor(x); break;                       /* FRINTM */
                case 11: z = __builtin_trunc(x); break;                       /* FRINTZ */
                case 12: z = __builtin_round(x); break;                       /* FRINTA */
                case 14: z = __builtin_rint(x); break;                        /* FRINTX */
                case 5: case 4: { float f = (float)x; memset(c->v[rd], 0, 16); memcpy(c->v[rd], &f, 4); return 0; } /* FCVT s←d */
                case 6: {                                                     /* UCVTF d←x（64 位） */
                    double d2 = (double)(uint64_t)c->v[rn][0];
                    memset(c->v[rd], 0, 16); memcpy(c->v[rd], &d2, 8); return 0;
                }
                default: return emu_simd(c, insn, pc);
                }
                memset(c->v[rd], 0, 16); memcpy(c->v[rd], &z, 8);
                return 0;
            }
            switch (opc) {
            case 10: z = x + y; break;                                        /* FADD */
            case 14: z = x - y; break;                                        /* FSUB */
            case 2:  z = x * y; break;                                        /* FMUL */
            case 6:  z = x / y; break;                                        /* FDIV */
            case 18: z = (x != x) ? y : ((y != y) ? x : (x > y ? x : y)); break;   /* FMAX */
            case 22: z = (x != x) ? y : ((y != y) ? x : (x < y ? x : y)); break;   /* FMIN */
            case 26: z = (x != x) ? y : ((y != y) ? x : (x > y ? x : y)); break;   /* FMAXNM */
            case 30: z = (x != x) ? y : ((y != y) ? x : (x < y ? x : y)); break;   /* FMINNM */
            case 34: z = -(x * y); break;                                     /* FNMUL */
            default: return emu_simd(c, insn, pc);   /* 未知 → 交给 SIMD/FP 子集 */
            }
            memset(c->v[rd], 0, 16); memcpy(c->v[rd], &z, 8);
            return 0;
        } else {
            float x, y, z = 0; uint32_t t;
            t = (uint32_t)c->v[rn][0]; memcpy(&x, &t, 4);
            t = (uint32_t)c->v[rm][0]; memcpy(&y, &t, 4);
            if (one_src) {
                switch (opc) {
                case 1:  { uint32_t t2 = (uint32_t)c->v[rn][0] & 0x7FFFFFFFu; memcpy(&z, &t2, 4); break; }
                case 2:  { uint32_t t2 = (uint32_t)c->v[rn][0] ^ 0x80000000u; memcpy(&z, &t2, 4); break; }
                case 3:  z = __builtin_sqrtf(x); break;
                case 8:  z = __builtin_rintf(x); break;
                case 9:  z = __builtin_ceilf(x); break;
                case 10: z = __builtin_floorf(x); break;
                case 11: z = __builtin_truncf(x); break;
                case 12: z = __builtin_roundf(x); break;
                case 14: z = __builtin_rintf(x); break;
                case 5:  { double d2 = (double)x; memset(c->v[rd], 0, 16); memcpy(c->v[rd], &d2, 8); return 0; } /* FCVT d←s */
                case 4: case 6: {                                             /* SCVTF/UCVTF（32 位） */
                    float f = (opc == 4) ? (float)(int32_t)(uint32_t)c->v[rn][0]
                                         : (float)(uint32_t)c->v[rn][0];
                    uint32_t tz; memcpy(&tz, &f, 4);
                    memset(c->v[rd], 0, 16); c->v[rd][0] = tz; return 0;
                }
                default: return emu_simd(c, insn, pc);
                }
                uint32_t rz; memcpy(&rz, &z, 4);
                memset(c->v[rd], 0, 16); c->v[rd][0] = rz;
                return 0;
            }
            switch (opc) {
            case 10: z = x + y; break;
            case 14: z = x - y; break;
            case 2:  z = x * y; break;
            case 6:  z = x / y; break;
            case 18: z = (x != x) ? y : ((y != y) ? x : (x > y ? x : y)); break;
            case 22: z = (x != x) ? y : ((y != y) ? x : (x < y ? x : y)); break;
            case 26: z = (x != x) ? y : ((y != y) ? x : (x > y ? x : y)); break;
            case 30: z = (x != x) ? y : ((y != y) ? x : (x < y ? x : y)); break;
            case 34: z = -(x * y); break;
            default: return emu_simd(c, insn, pc);
            }
            uint32_t rz; memcpy(&rz, &z, 4);
            memset(c->v[rd], 0, 16); c->v[rd][0] = rz;
            return 0;
        }
    }
    /* ---- 交给 SIMD/FP 子集处理器 ---- */
    if (emu_simd(c, insn, pc) == 0) return 0;
    /* ---- 未实现 ---- */
    s2a_emu_last_reason = 1;
    if (getenv("S2A_EMU_SKIP_UNIMPL")) {   /* 批量差分用：当作空操作继续跑，一次收齐全部缺失指令 */
        static uint32_t seen[256]; static int nseen, warned;
        int dup = 0;
        for (int i = 0; i < nseen; i++) if (seen[i] == insn) { dup = 1; break; }
        if (!dup) {
            if (nseen < 256) seen[nseen++] = insn;
            fprintf(stderr, "\x1b[35m[跳过未实现]\x1b[0m %08x pc=%#llx\n",
                    insn, (unsigned long long)pc);
        }
        (void)warned;
        return 0;
    }
    {
        static int reported;
        if (reported++ < 5)
            fprintf(stderr,
                    "s2a 模拟器: 未实现的指令 %#010x（pc=%#llx）\n"
                    "  提示：模拟器覆盖通用寄存器指令集；若产物走到了 SIMD/其它扩展，"
                    "请改用 `s2a --iso` 或直接运行产物。\n",
                    insn, (unsigned long long)pc);
        c->exited = 1;
        c->exit_code = 132;
        return -1;
    }
}

/* ============================ 运行入口 ============================ */
int s2a_emu_run(const s2a_options *o, const char *elf_path, int nargs, char **args)
{
    /* 1) guest 内存：一次性保留 2.25GB 虚拟地址空间（按需提交，不占物理内存） */
    g_base = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_base == MAP_FAILED) {
        fprintf(stderr, "s2a 模拟器: 保留 guest 内存失败: %s\n", strerror(errno));
        g_base = NULL;
        return 3;
    }

    /* 2) 虚拟文件系统：可写目录里开覆盖层，产物的写操作不会落到真实磁盘。
     *    目录来源见 pick_base_dir()：环境变量 S2A_EMU_OVERLAY / S2A_TMPDIR / TMPDIR 优先，
     *    然后 /tmp、/data/local/tmp、/sdcard、当前目录逐级回退。*/
    cpu c;
    memset(&c, 0, sizeof c);
    g_overlay_base = pick_base_dir();
    c.readonly_sandbox = 0;
    if (!g_overlay_base) {
        fprintf(stderr, "\x1b[33m[s2a 模拟器]\x1b[0m 找不到可写目录（/tmp 等都不存在或不可写）-> "
                        "进入只读沙箱：产物的写操作会返回 EROFS\n");
        c.readonly_sandbox = 1;
        snprintf(c.overlay, sizeof c.overlay, ".");
    } else {
        snprintf(c.overlay, sizeof c.overlay, "%s/s2a-emu-XXXXXX", g_overlay_base);
        if (!mkdtemp(c.overlay)) {
            snprintf(c.overlay, sizeof c.overlay, "%s/s2a-emu-%ld", g_overlay_base, (long)getpid());
            mkdir(c.overlay, 0700);
        }
        if (access(c.overlay, W_OK) != 0) {
            fprintf(stderr, "\x1b[33m[s2a 模拟器]\x1b[0m 覆盖层 %s 不可写 -> 只读沙箱模式\n", c.overlay);
            c.readonly_sandbox = 1;
        }
    }
    c.trace = o->emu_trace;
    {
        const char *w = getenv("S2A_EMU_WATCH");
        if (w && *w) {
            g_watch_lo = strtoull(w, NULL, 0);
            const char *colon = strchr(w, ':');
            g_watch_hi = colon ? strtoull(colon + 1, NULL, 0) : g_watch_lo + 8;
            fprintf(stderr, "\x1b[33m[监视]\x1b[0m 监视写区间 [%#llx, %#llx)\n",
                    (unsigned long long)g_watch_lo, (unsigned long long)g_watch_hi);
        }
    }
    c.tpidr = TLS_SCRATCH + 0x8000;      /* 初始线程指针：指向暂存区中部（上下都留空间） */
    c.mmap_cur = MMAP_BASE;

    /* 3) 装载产物 */
    loaded L;
    memset(&L, 0, sizeof L);
    if (load_elf_into_guest(elf_path, &L) != 0) {
        munmap(g_base, GUEST_SIZE);
        g_base = NULL;
        return 3;
    }

    c.brk_cur = L.image_end;              /* brk 起点 = 镜像末尾（与真实内核一致） */
    g_img_lo = 0; g_img_hi = L.image_end;
    g_brk_lo = L.image_end; g_brk_hi = L.image_end;
    g_mmap_lo = MMAP_BASE; g_mmap_hi = MMAP_BASE;
    g_stack_lo = STACK_TOP - 0x200000ull; g_stack_hi = STACK_TOP;
    g_tls_lo = TLS_SCRATCH; g_tls_hi = TLS_SCRATCH + 0x2000;
    c.brk_end = MMAP_BASE;

    /* 4) 入口栈与寄存器 */
    char *av[256];
    int ac = 0;
    av[ac++] = (char *)elf_path;
    for (int i = 0; i < nargs && ac < 255; i++) av[ac++] = args[i];
    uint64_t sp = setup_stack(&c, &L, ac, av);
    c.sp = sp;
    c.pc = L.entry;
    if (o->emu_trace) {
        uint64_t *w = (uint64_t *)(g_base + sp);
        fprintf(stderr, "\x1b[35m[栈]\x1b[0m sp=%#llx argc=%llu argv0=%#llx AT_PHDR=%#llx AT_PHNUM=%llu AT_ENTRY=%#llx\n",
                (unsigned long long)sp, (unsigned long long)w[0], (unsigned long long)w[1],
                (unsigned long long)L.phdr_addr, (unsigned long long)L.phnum, (unsigned long long)L.entry);
    }
    c.r[0] = 0;   /* 与真实内核一致：入口处 x0 未定义（musl 从栈上读 argc）*/
    memset(c.v, 0, sizeof c.v);

    fprintf(stderr, "\x1b[36m[s2a 模拟器]\x1b[0m 正在**解释执行** %s（不真实执行；syscall 走虚拟文件系统）\n"
                    "  入口 %#llx，栈顶 %#llx，覆盖层 %s%s\n",
            elf_path, (unsigned long long)L.entry, (unsigned long long)sp, c.overlay,
            c.readonly_sandbox ? "（只读沙箱）" : "");
    if (o->emu_trace) fprintf(stderr, "  （--emu-trace：打印指令与系统调用轨迹）\n");

    /* 5) 解释循环 */
    {
        const char *cw = getenv("S2A_EMU_CALLWATCH");
        if (cw) sscanf(cw, "%llx:%llx", (unsigned long long *)&g_callwatch_lo,
                       (unsigned long long *)&g_callwatch_hi);
    }
    const char *sl = getenv("S2A_EMU_MAX_STEPS");
    const long STEP_LIMIT = sl ? atol(sl) : 30000000L;    /* 防跑飞（默认 3000 万步，约几十秒） */
    int skip_unimpl = getenv("S2A_EMU_SKIP_UNIMPL") != NULL;
    while (!c.exited) {
        uint64_t dbg_pc = c.pc;
        int dbg_r = emu_step(&c);
        if (getenv("S2A_EMU_WHO") && g_who[0] == '?')
            fprintf(stderr, "[未标注] pc=%#llx insn=%08x\n", (unsigned long long)(dbg_pc - 4),
                    *(uint32_t *)(g_base + dbg_pc - 4));
        if (dbg_r != 0) {
            if (skip_unimpl) {          /* 批量对拍模式：遇到未实现/故障也继续跑，一次收齐全部差异 */
                c.exited = 0;
                c.exit_code = 0;
                c.steps++;
                if (c.steps > STEP_LIMIT) break;
                continue;
            }
            break;
        }
        if (c.steps > STEP_LIMIT) {
            s2a_emu_last_reason = 3;
            fprintf(stderr, "s2a 模拟器: 已达步数上限 %ld，停止（疑似死循环）\n", STEP_LIMIT);
            c.exit_code = 124;
            break;
        }
    }

    if (!c.exited)
        fprintf(stderr, "\x1b[33m[s2a 模拟器]\x1b[0m 循环因指令返回非 0 而中断（pc=%#llx，末条指令见上）\n",
                (unsigned long long)c.pc);
    long code = c.exited ? (long)c.exit_code : 0;
    long vmhwm = 0;
    {
        FILE *st = fopen("/proc/self/status", "r");
        if (st) {
            char line[256];
            while (fgets(line, sizeof line, st))
                if (strncmp(line, "VmHWM:", 6) == 0) { vmhwm = atol(line + 6); break; }
            fclose(st);
        }
    }
    fprintf(stderr, "\x1b[36m[s2a 模拟器]\x1b[0m 结束：退出码 %ld，共解释 %ld 条指令，"
                    "guest 映射 %llu MB，本进程峰值内存 %ld MB\n"
                    "  覆盖层保留在 %s（可自行检查产物写了什么）\n",
            code, c.steps, (unsigned long long)(c.mmap_used >> 20), vmhwm / 1024, c.overlay);
    fflush(stdout);
    fflush(stderr);
    return (int)code;
}
