/* main.c —— s2a 命令行入口与编译驱动
 *
 * 流水线（DESIGN.md §2）：脚本 → 词法/语法 → AST → 映像组装(只读段+字符串池) → 机器码
 *                        → 布局回填(两遍) → 加壳/加密 → 追加式链接 → 输出纯静态 ELF
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
extern char **environ;
#include "s2a.h"
#include "image.h"
#include "elf.h"
#include "protect.h"
#include "../common/crypto.h"
#include "../common/safeperm.h"

/* 内嵌工具 blob（由 ld -r -b binary 提供） */
/* 口令来源：--password <值> / --password-env <变量> / --password-file <文件>（内容原样）
   都没给时看环境变量 S2A_PASSWORD。失败返回 NULL 并打印原因。 */
static char *s2a_read_whole_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, len = 0;
    char *b = malloc(cap);
    if (!b) { fclose(f); return NULL; }
    for (;;) {
        if (len + 1024 >= cap) { cap *= 2; b = realloc(b, cap); }
        size_t got = fread(b + len, 1, 1024, f);
        len += got;
        if (got < 1024) break;              /* 读满说明还可能更多，读不满就到尾（管道也适用） */
    }
    fclose(f);
    b[len] = 0;
    return b;
}

static const char *s2a_password_from_arg(const char *flag, const char *val)
{
    if (!strcmp(flag, "--password-env") || !strcmp(flag, "--password_env")) {
        const char *v = getenv(val);
        if (!v || !*v) { s2a_error("环境变量 %s 未设置或为空", val); return NULL; }
        return v;
    }
    if (!strcmp(flag, "--password-file") || !strcmp(flag, "--password_file")) {
        char *v = s2a_read_whole_file(val);
        if (!v) { s2a_error("读不到口令文件 %s", val); return NULL; }
        return v;
    }
    return val;                       /* --password <值> */
}

static int s2a_encrypt_file(const char *in, const char *out, const char *pw);
extern const unsigned char _binary_build_launcher_elf_start[];
extern const unsigned char _binary_build_launcher_elf_end[];
extern const unsigned char _binary_assets_ash_upx_start[];
extern const unsigned char _binary_assets_ash_upx_end[];
extern const unsigned char _binary_assets_qemu_aarch64_static_upx_start[];
extern const unsigned char _binary_assets_qemu_aarch64_static_upx_end[];


#define S2A_VERSION "1.0"   /* 新项目自己的编号 */
#define S2A_AUTHOR  "deepseek v4 flash & @Chen9183 (github)"

uint64_t s2a_code_va;

/* 模板符号表 */
static s2a_elf g_tpl;
static int g_tpl_loaded;

uint64_t s2a_rt_sym(const char *name) { return elf_sym(&g_tpl, name); }

/* ---------------- 输出 ---------------- */
static int g_debug;
static void vlog(int level, const char *fmt, va_list ap)
{
    if (g_debug < level) return;
    fprintf(stderr, "\x1b[36m[s2a]\x1b[0m ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}
void s2a_info(const s2a_options *o, int level, const char *fmt, ...)
{
    (void)o;
    va_list ap; va_start(ap, fmt); vlog(level, fmt, ap); va_end(ap);
}
void s2a_warn(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "\x1b[33m[s2a 警告]\x1b[0m "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}
void s2a_error(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "\x1b[31m[s2a 错误]\x1b[0m "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

/* ---------------- 帮助 ---------------- */
/* ---- 帮助：四档文本以 TXT 形式经 ld -r -b binary 嵌入（不写成 C 字符串） ----
   -h          简要用法（常用命令 / 三种产物 / 口令分界 / 退出码）
   --help      常用（全部选项 / 环境变量 / 加密机制 / 语言覆盖 / 已知缺陷）
   --help-full 完整手册（字节布局、尾部字段表、架构、模拟器、加固内部、排错）
   --help-dev  构建、自检与回归、改名换文案、开发笔记 */
extern const unsigned char _binary_build_help_short_enc_start[];
extern const unsigned char _binary_build_help_short_enc_end[];
extern const unsigned char _binary_build_help_enc_start[];
extern const unsigned char _binary_build_help_enc_end[];
extern const unsigned char _binary_build_help_full_enc_start[];
extern const unsigned char _binary_build_help_full_enc_end[];
extern const unsigned char _binary_build_help_dev_enc_start[];
extern const unsigned char _binary_build_help_dev_enc_end[];

/* ---- s2a 自带 ash / qemu：`s2a ash -c '…'`、`s2a qemu -L … prog` ----
   两个工具以 blob 形式链在 s2a 里（UPX 压缩件自解压，解出来直接执行即可）。
   解出目录：$S2A_TOOL_DIR → $TMPDIR → /data/local/tmp → /tmp（按长度缓存，原子写）。 */
static int s2a_dispatch_tool(int argc, char **argv)
{
    const unsigned char *blob = NULL;
    size_t blen = 0;
    if (!strcmp(argv[1], "ash")) {
        blob = _binary_assets_ash_upx_start;
        blen = (size_t)(_binary_assets_ash_upx_end - _binary_assets_ash_upx_start);
    } else if (!strcmp(argv[1], "qemu")) {
        blob = _binary_assets_qemu_aarch64_static_upx_start;
        blen = (size_t)(_binary_assets_qemu_aarch64_static_upx_end -
                        _binary_assets_qemu_aarch64_static_upx_start);
    } else {
        return 0;
    }
    const char *dirs[5];
    int nd = 0;
    if (getenv("S2A_TOOL_DIR")) dirs[nd++] = getenv("S2A_TOOL_DIR");
    if (getenv("TMPDIR")) dirs[nd++] = getenv("TMPDIR");
    dirs[nd++] = "/data/local/tmp";
    dirs[nd++] = "/tmp";
    dirs[nd] = NULL;

    char path[512]; path[0] = 0;
    for (int i = 0; i < nd; i++) {
        snprintf(path, sizeof path, "%s/s2a-%s-%zu", dirs[i], argv[1], blen);
        if (access(path, X_OK) == 0) break;
        char tmp[600];
        snprintf(tmp, sizeof tmp, "%s.tmp%d", path, (int)getpid());
        FILE *f = fopen(tmp, "wb");
        if (!f) { path[0] = 0; continue; }
        if (fwrite(blob, 1, blen, f) != blen) { fclose(f); unlink(tmp); path[0] = 0; continue; }
        fclose(f);
        s2a_set_perm(tmp, 0755);   /* 只对普通文件（防误改设备节点） */
        if (rename(tmp, path) != 0) { unlink(tmp); path[0] = 0; continue; }
        break;
    }
    if (!path[0]) {
        fprintf(stderr, "s2a: 解不出内嵌 %s（没有可写目录，可用 S2A_TOOL_DIR 指定）\n", argv[1]);
        return 73;                        /* 73 = 写不出文件（与"命令未找到"127 区分开） */
    }
    execv(path, argv + 1);              /* argv[0] = "ash"/"qemu"，其余原样透传 */
    fprintf(stderr, "s2a: %s: %s\n", path, strerror(errno));
    return 126;                         /* 解出来了但执行不了 = 不可执行 */
}

/* ---- 许可证：s2a -license（支持 -license/-License/-LICENSE/--license 等） ---- */
static const char g_license_text[] =
"MIT License\n\n"
"Copyright (c) 2026 Chen9183\n\n"
"Permission is hereby granted, free of charge, to any person obtaining a copy\n"
"of this software and associated documentation files (the \"Software\"), to deal\n"
"in the Software without restriction, including without limitation the rights\n"
"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
"copies of the Software, and to permit persons to whom the Software is\n"
"furnished to do so, subject to the following conditions:\n\n"
"The above copyright notice and this permission notice shall be included in all\n"
"copies or substantial portions of the Software.\n\n"
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
"SOFTWARE.\n";

/* 大小写不敏感判断参数是否为 -license（-license/-License/-LICENSE/--license 均可） */
static int is_license_flag(const char *s)
{
    if (!s || s[0] != '-') return 0;
    const char *p = s + 1;
    while (*p == '-') p++;                 /* 允许 --license */
    static const char lic[7] = { 'l','i','c','e','n','s','e' };
    for (int i = 0; i < 7; i++) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != lic[i]) return 0;
    }
    return p[7] == '\0';
}

static void print_help_plain(const unsigned char *p, size_t n);

static void print_help(int detailed)
{
    const unsigned char *p;
    size_t n;
    if (detailed <= 0) {
        p = _binary_build_help_short_enc_start;
        n = (size_t)(_binary_build_help_short_enc_end - p);
    } else if (detailed == 1) {
        p = _binary_build_help_enc_start;
        n = (size_t)(_binary_build_help_enc_end - p);
    } else if (detailed == 2) {
        p = _binary_build_help_full_enc_start;
        n = (size_t)(_binary_build_help_full_enc_end - p);
    } else {
        p = _binary_build_help_dev_enc_start;
        n = (size_t)(_binary_build_help_dev_enc_end - p);
    }
    print_help_plain(p, n);      /* blob 是密文：先解密再打印 */
}

/* 帮助文本以 AES-256-CTR 密文嵌入（仓库里是 .txt，编译时由 tools/help-enc 加密）。
   密钥派生与 help-enc 一致：k0 = SHA-256(SALT)，nonce = SHA-256(SALT + 标签) 前 16 字节。 */
static void help_key(const char *tag, unsigned char key[32], unsigned char nonce[16])
{
    static const char SALT[] = "ForgeShell help text v1";
    sha256_buf(SALT, sizeof SALT - 1, key);
    char nb[256];
    snprintf(nb, sizeof nb, "%s%s", SALT, tag);
    unsigned char h[32];
    sha256_buf(nb, strlen(nb), h);
    memcpy(nonce, h, 16);
}

static void print_help_plain(const unsigned char *p, size_t n)
{
    static const unsigned char *const starts[4] = {
        _binary_build_help_short_enc_start, _binary_build_help_enc_start,
        _binary_build_help_full_enc_start, _binary_build_help_dev_enc_start };
    static const char *const tags[4] = { "short", "detail", "full", "dev" };
    const char *tag = "short";
    for (int i = 0; i < 4; i++) {   /* 用指针比对判断这是哪一档，取对应标签 */
        if (p == starts[i]) { tag = tags[i]; break; }
    }
    unsigned char *buf = malloc(n ? n : 1);
    if (!buf) return;
    memcpy(buf, p, n);
    unsigned char key[32], nonce[16];
    help_key(tag, key, nonce);
    rc_aes256_ctr(key, nonce, buf, n);
    fwrite(buf, 1, n, stdout);
    free(buf);
}

/* ---------------- 读写文件 ---------------- */
static char *read_file(const char *path, size_t *len_out)
{
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    if (!f) { s2a_error("打不开 %s: %s", path, strerror(errno)); return NULL; }
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    size_t r;
    while ((r = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += r;
        if (cap - len < 2) { cap *= 2; buf = realloc(buf, cap); }
    }
    buf[len] = 0;
    if (f != stdin) fclose(f);
    if (len_out) *len_out = len;
    return buf;
}

/* ---------------- 默认输出名 ---------------- */
static char *default_output(const char *input)
{
    const char *base = strrchr(input, '/');
    base = base ? base + 1 : input;
    const char *dot = strrchr(base, '.');
    size_t n = dot && dot > base ? (size_t)(dot - input) : strlen(input);
    char *out = malloc(n + 5);
    memcpy(out, input, n);
    strcpy(out + n, ".elf");
    return out;
}

/* ---------------- 主流程 ---------------- */
static int compile(const s2a_options *o, const char *src, const char *srcname)
{
    /* 1) 解析 */
    rc_diag diags[64];
    int nd = 0;
    rc_node *root = NULL;
    int bad = rc_parse_source(src, srcname, diags, 64, &nd, &root);
    for (int i = 0; i < nd; i++) {
        fprintf(stderr, "%s:%d:%d: \x1b[31m错误\x1b[0m: %s\n", srcname, diags[i].line, diags[i].col, diags[i].msg);
        if (diags[i].src_line[0]) {
            fprintf(stderr, "    %s\n    ", diags[i].src_line);
            for (int k = 1; k < diags[i].col; k++) fputc(' ', stderr);
            fprintf(stderr, "\x1b[32m^\x1b[0m\n");
        }
    }
    if (bad) { s2a_error("脚本有 %d 处语法错误，已中止", nd); return 1; }

    rc_ast_stats st = {0};
    rc_ast_count(root, &st);
    s2a_info(o, 1, "解析完成：节点 %u，词 %u，片段 %u（%d 条诊断）", st.n_nodes, st.n_words, st.n_segs, nd);

    if (o->debug_level >= 2) {
        struct { const s2a_options *o; } ctx = { o };
        void dump(const char *line, void *ud) { (void)ud; fprintf(stderr, "  %s\n", line); }
        fprintf(stderr, "\x1b[36m── AST ──\x1b[0m\n");
        rc_ast_dump(root, 0, dump, &ctx);
    }

    /* 2) 载入模板 */
    size_t tpl_size = (size_t)(_binary_build_template_elf_end - _binary_build_template_elf_start);
    if (!g_tpl_loaded) {
        if (elf_load(&g_tpl, _binary_build_template_elf_start, tpl_size) != 0) {
            s2a_error("内嵌模板 ELF 解析失败");
            return 1;
        }
        g_tpl_loaded = 1;
        if (elf_has_interp(&g_tpl)) s2a_warn("模板含 PT_INTERP（应为纯静态，请检查 template 构建）");
        s2a_info(o, 1, "模板：%zu 字节，符号 %d 个，程序头 %d 个（纯静态=%s）",
                 tpl_size, g_tpl.nsyms, g_tpl.nph, elf_has_interp(&g_tpl) ? "否" : "是");
    }

    /* 3) 映像/代码 两遍布局 */
    s2a_image_parts parts;
    memset(&parts, 0, sizeof parts);
    s2a_build_image(o, root, 0, 0, &parts);          /* 第一遍：只为得到尺寸 */
    uint32_t ro_size = parts.ro_len, pool_size = parts.pool_len;
    s2a_info(o, 3, "只读段 %u 字节（对象 %u 个），字符串池 %u 字节（%u 条）",
             ro_size, parts.n_objects, pool_size, parts.n_strings);

    /* 可写段布局：[池][rc_image][pool_sha512][code_sha512][marker] */
    uint32_t off_image = (pool_size + 7u) & ~7u;
    uint32_t off_psha  = (off_image + (uint32_t)sizeof(rc_image) + 7u) & ~7u;
    uint32_t off_csha  = off_psha + 64;
    uint32_t off_mark  = off_csha + 64;
    uint32_t off_check = off_mark + 64;                 /* 16 字节：打散后的校验常量 */
    uint32_t rw_size   = off_check + 16 + 16;

    s2a_layout lay;
    uint32_t code_size = 0;
    unsigned char *code = NULL, *ro = NULL;
    emit_buf eb;
    for (int pass = 0; pass < 6; pass++) {
        if (s2a_plan(&g_tpl, code_size, ro_size, rw_size, g_tpl.nph + 2, &lay) != 0) {
            s2a_error("布局规划失败");
            return 1;
        }
        s2a_build_image(o, root, lay.ro_va, lay.rw_va, &parts);   /* 用真实地址重建 */
        ro = parts.ro; ro_size = parts.ro_len;

        emit_init(&eb);
        if (s2a_codegen(o, root, &eb, lay.ro_va, lay.rw_va, lay.code_va, lay.rw_va + off_image) != 0) {
            s2a_error("代码生成失败");
            return 1;
        }
        uint32_t new_size = emit_len(&eb);
        s2a_info(o, 3, "第 %d 遍：代码 %u 字节（代码段 @ %#llx，只读段 @ %#llx，可写段 @ %#llx）",
                 pass + 1, new_size, (unsigned long long)lay.code_va,
                 (unsigned long long)lay.ro_va, (unsigned long long)lay.rw_va);
        if (new_size == code_size) { code = eb.data; break; }
        code_size = new_size;
        code = eb.data;
        if (pass == 5) { s2a_warn("布局两遍未收敛，使用最后一次结果"); }
    }
    /* 最终布局（用收敛后的代码长度重算一次） */
    s2a_plan(&g_tpl, code_size, ro_size, rw_size, g_tpl.nph + 2, &lay);

    if (o->keep_intermediate) {
        char p[512];
        snprintf(p, sizeof p, "%s.code.bin", o->output);
        FILE *f = fopen(p, "wb"); if (f) { fwrite(code, 1, code_size, f); fclose(f); }
        snprintf(p, sizeof p, "%s.rodata.bin", o->output);
        f = fopen(p, "wb"); if (f) { fwrite(ro, 1, ro_size, f); fclose(f); }
        s2a_info(o, 1, "已保留中间产物：%s.code.bin / %s.rodata.bin", o->output, o->output);
    }

    if (o->debug_level >= 4) {
        fprintf(stderr, "\x1b[36m── 机器码（%u 条指令，@ %#llx）──\x1b[0m\n", code_size / 4,
                (unsigned long long)lay.code_va);
        for (uint32_t i = 0; i + 4 <= code_size; i += 4) {
            uint32_t ins;
            memcpy(&ins, code + i, 4);
            char txt[128];
            emit_disasm_one(lay.code_va + i, ins, txt, sizeof txt);
            fprintf(stderr, "  %#010llx  %08x  %s\n",
                    (unsigned long long)(lay.code_va + i), ins, txt);
        }
    }

    /* 4) 组装可写段并加壳 */
    unsigned char *rw = calloc(1, rw_size);
    memcpy(rw, parts.pool, pool_size);

    rc_image *img = (rc_image *)(rw + off_image);
    img->magic = 0x53324132u;   /* 'S2A2' */
    img->version = 200;
    img->srcname = NULL;        /* 字符串池里的地址，下面用 s2a_str_va 填 */
    img->author = NULL;
    img->pool = (char *)(lay.rw_va + 0);
    img->pool_size = pool_size;
    img->pool_sha512 = (const unsigned char *)(lay.rw_va + off_psha);
    img->marker = (const unsigned char *)(lay.rw_va + off_mark);
    img->author_check = (const unsigned char *)(lay.rw_va + off_check);
    img->code = (const unsigned char *)(lay.code_va);
    img->code_size = code_size;
    img->code_sha512 = (const unsigned char *)(lay.rw_va + off_csha);
    img->prot_level = (uint32_t)o->prot_level;
    img->opt_level = (uint32_t)o->opt_level;
    img->anti_action = (uint32_t)o->anti_action;
    img->n_nodes = st.n_nodes;
    img->n_words = st.n_words;
    img->n_strings = st.n_strings;

    uint64_t flags = 0;
    if (o->prot_level > 0) {
        s2a_seal_ctx sc;
        memset(&sc, 0, sizeof sc);
        sc.pool = rw;                     /* 池在可写段偏移 0 */
        sc.pool_size = pool_size;
        sc.pool_sha512 = rw + off_psha;
        sc.code_sha512 = rw + off_csha;
        sc.code = code;
        sc.code_size = code_size;
        sc.seed = img->seed;
        sc.marker = rw + off_mark;
        sc.marker_len = &img->marker_len;
        sc.author_check = rw + off_check;
        sc.author = S2A_AUTHOR;
        sc.password = o->password;
        sc.prot_level = (uint32_t)o->prot_level;
        sc.anti_action = (uint32_t)o->anti_action;
        sc.flags_out = &flags;
        if (s2a_seal(&sc) != 0) { s2a_error("加壳失败（真随机数不可用？）"); return 1; }
        if (o->password && *o->password) {
            if (s2a_unseal_check(&sc, o->password) != 0) {
                s2a_error("内部自检失败：密码与标记不一致");
                return 1;
            }
            s2a_info(o, 1, "密码锁已启用：校验料密文 %u 字节 + 16 字节打散校验常量（产物内无明文）",
                     img->marker_len);
            if (o->debug_level >= 5) {
                fprintf(stderr, "  [lock调试] 编译期 seed0=%02x%02x 校验常量前4=%02x%02x%02x%02x marker前4=%02x%02x%02x%02x\n",
                        img->seed[0], img->seed[1],
                        rw[off_check], rw[off_check+1], rw[off_check+2], rw[off_check+3],
                        rw[off_mark], rw[off_mark+1], rw[off_mark+2], rw[off_mark+3]);
            }
        }
        s2a_info(o, 1, "防护：级别 %d，字符串池 %u 字节已 AES-256 加密（随机源 %s）",
                 o->prot_level, pool_size, sc.rnd_desc ? sc.rnd_desc : "/dev/random");
    } else {
        img->pool_sha512 = NULL;
        img->code_sha512 = NULL;
        img->author_check = NULL;
        img->marker_len = 0;
    }
    img->flags = flags;

    if (o->debug_level >= 5) {
        fprintf(stderr, "\x1b[36m── 防护 ──\x1b[0m\n");
        fprintf(stderr, "  flags=%#llx  anti_action=%u  池=%u 字节  代码=%u 字节\n",
                (unsigned long long)img->flags, img->anti_action, pool_size, code_size);
        fprintf(stderr, "  标记长度=%u  随机源=%s\n", img->marker_len, "/dev/random");
    }

    /* 5) 链接并写出 */
    if (s2a_write(&g_tpl, &lay, code, code_size, ro, ro_size, rw, rw_size,
                  "rc_user_main", o->output, o->strip) != 0) {
        s2a_error("写出 %s 失败", o->output);
        return 1;
    }
    s2a_info(o, 1, "已生成 %s（代码 %u 字节，只读 %u 字节，可写 %u 字节，纯静态）",
             o->output, code_size, ro_size, rw_size);

    /* 注意：产物**不内嵌** qemu/ash（那是 s2a 自己带的东西）。
       需要在本机执行 → `s2a qemu …` / 用内嵌 dash → `s2a ash …`。 */
    return 0;
}

/* 找到可用的 qemu 用户态模拟器：$S2A_QEMU → PATH 里的 qemu-aarch64-static/qemu-aarch64
   → s2a 自身所在目录 → 常见路径。找到就 execv 替换当前进程。 */
/* ---- 内嵌 qemu（upx -9）：宿主没有 qemu 时用它 ----
   落地顺序（每一级都可用环境变量覆盖）：
     ① 匿名内存页 memfd + fexecve（不落盘、不需要任何目录可执行）
     ② $S2A_QEMU_DIR / $S2A_QEMU_DIRS（冒号分隔）指定的目录
     ③ 默认目录链：/data/local/tmp → $TMPDIR → /tmp
     ④ 目录里按大小缓存解出的副本，重复运行不再写盘 */

extern const unsigned char _binary_assets_qemu_aarch64_static_upx_end[];


/* 整文件加密（v2）：外壳 + AES-256-CTR 密文 + 128 字节尾部索引。
   ★ 给了口令 → 运行期**必须**再给同一口令才能解密（不是"口令多样化"）：
       key = PBKDF2-HMAC-SHA256(口令, 随机盐 trailer.salt[16], iters)
     没给口令 → key = kdf_strong(seed[0:32], "onlyenc-seed", 盐=trailer.nonce[16])
   尾部（128 字节，魔数 S2ALAU02）：
     0 "S2ALAU02" | 8 mode | 16 off1 | 24 len1 | 32 off2 | 40 len2
     48 seed[32] | 80 nonce[16] | 96 salt[16] | 112 pw_flag(u32) | 116 iters(u32) | 120 "S2AEND02" */
static int s2a_encrypt_file(const char *in, const char *out, const char *pw)
{
    FILE *fi = fopen(in, "rb");
    if (!fi) { s2a_error("打开 %s 失败: %s", in, strerror(errno)); return 1; }
    fseek(fi, 0, SEEK_END);
    long n = ftell(fi);
    fseek(fi, 0, SEEK_SET);
    if (n <= 0) { fclose(fi); s2a_error("%s 是空文件", in); return 1; }
    unsigned char *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, fi) != (size_t)n) { fclose(fi); s2a_error("读 %s 失败", in); return 1; }
    fclose(fi);

    unsigned char rnd[64];
    FILE *r = fopen("/dev/random", "rb");
    if (!r || fread(rnd, 1, 64, r) != 64) { s2a_error("取随机数失败"); return 1; }
    fclose(r);

    unsigned char seed[32], nonce[16], salt[16], key[32];
    memcpy(seed, rnd, 32);
    memcpy(nonce, rnd + 32, 16);
    memcpy(salt, rnd + 48, 16);
    uint32_t pw_flag = (pw && *pw) ? 1u : 0u;
    uint32_t iters = (uint32_t)S2A_KDF_ITERS;
    if (pw_flag) pbkdf2_sha256(pw, strlen(pw), salt, 16, iters, key, 32);   /* 口令 + 随机盐 */
    else         kdf_strong(seed, 32, "onlyenc-seed", nonce, 16, key);
    rc_aes256_ctr(key, nonce, buf, (size_t)n);

    size_t llen = (size_t)(_binary_build_launcher_elf_end - _binary_build_launcher_elf_start);
    FILE *fo = fopen(out, "wb");
    if (!fo) { s2a_error("写出 %s 失败: %s", out, strerror(errno)); return 1; }
    fwrite(_binary_build_launcher_elf_start, 1, llen, fo);
    unsigned long long off1 = llen, len1 = (unsigned long long)n, mode = 1, off2 = 0, len2 = 0;
    fwrite(buf, 1, (size_t)n, fo);
    unsigned char tail[128];
    memset(tail, 0, sizeof tail);
    memcpy(tail, "S2ALAU02", 8);
    memcpy(tail + 8, &mode, 8);
    memcpy(tail + 16, &off1, 8); memcpy(tail + 24, &len1, 8);
    memcpy(tail + 32, &off2, 8); memcpy(tail + 40, &len2, 8);
    memcpy(tail + 48, seed, 32);
    memcpy(tail + 80, nonce, 16);
    memcpy(tail + 96, salt, 16);
    memcpy(tail + 112, &pw_flag, 4);
    memcpy(tail + 116, &iters, 4);
    memcpy(tail + 120, "S2AEND02", 8);
    fwrite(tail, 1, 128, fo);
    fclose(fo);
    if (!s2a_set_perm(out, 0755) && s2a_perm_special_path(out))
        fprintf(stderr, "s2a: 提示：输出 %s 不是普通文件，已跳过权限设置\n", out);
    free(buf);
    printf("\x1b[36m[s2a]\x1b[0m 已加密 %s → %s（外壳 %zu + 密文 %ld + 尾部 128 = %ld 字节，%s）\n",
           in, out, llen, n, (long)(llen + (size_t)n + 128),
           pw_flag ? "★运行期必须给同一口令（--password/--password-env/--password-file/S2A_PASSWORD）" : "无口令");
    return 0;
}

static size_t s2a_qemu_blob_len(void)
{
    return (size_t)(_binary_assets_qemu_aarch64_static_upx_end -
                    _binary_assets_qemu_aarch64_static_upx_start);
}

/* ① 匿名内存页 + exec：最干净，不留任何文件 */
static int s2a_qemu_from_memfd(char **av)
{
    size_t left = s2a_qemu_blob_len();
    if (!left) return -1;
    const unsigned char *p = _binary_assets_qemu_aarch64_static_upx_start;
    int fd = memfd_create("s2a-qemu", 0x0004 /* MFD_EXEC（新内核） */);
    if (fd < 0) fd = memfd_create("s2a-qemu", 0);        /* 老内核不认 MFD_EXEC */
    if (fd < 0) return -1;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w <= 0) { close(fd); return -1; }
        p += w; left -= (size_t)w;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) { close(fd); return -1; }
    fprintf(stderr, "\x1b[36m[s2a]\x1b[0m 宿主没找到 qemu，改用内嵌副本（匿名内存页 %zu 字节）+ exec\n",
            s2a_qemu_blob_len());
    fexecve(fd, av, environ);                            /* 成功则不返回 */
    close(fd);
    return -1;
}

/* ③④ 兜底：解到目录（默认 /data/local/tmp → $TMPDIR → /tmp），按大小缓存 */
static const char *s2a_qemu_from_dir(char **av)
{
    static char path[1024];
    size_t len = s2a_qemu_blob_len();
    if (!len) return NULL;
    const char *dirs[12];
    int n = 0;
    if (getenv("S2A_QEMU_DIR") && *getenv("S2A_QEMU_DIR")) dirs[n++] = getenv("S2A_QEMU_DIR");
    static char listbuf[1024];
    const char *list = getenv("S2A_QEMU_DIRS");
    if (list && *list) {
        snprintf(listbuf, sizeof listbuf, "%s", list);
        for (char *tok = strtok(listbuf, ":"); tok && n < 10; tok = strtok(NULL, ":")) dirs[n++] = tok;
    }
    dirs[n++] = "/data/local/tmp";
    if (getenv("TMPDIR") && *getenv("TMPDIR")) dirs[n++] = getenv("TMPDIR");
    dirs[n++] = "/tmp";
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof path, "%s/s2a-qemu-aarch64-%zx", dirs[i], len);
        struct stat st;
        if (stat(path, &st) == 0 && (size_t)st.st_size == len && access(path, X_OK) == 0)
            goto run;                                    /* 已缓存 */
        char tmp[1200];
        snprintf(tmp, sizeof tmp, "%s.tmp%d", path, (int)getpid());
        int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (fd < 0) continue;                            /* 这个目录不可写，换下一个 */
        const unsigned char *p = _binary_assets_qemu_aarch64_static_upx_start;
        size_t left = len;
        int bad = 0;
        while (left) {
            ssize_t w = write(fd, p, left);
            if (w <= 0) { bad = 1; break; }
            p += w; left -= (size_t)w;
        }
        close(fd);
        if (bad) { unlink(tmp); continue; }
        s2a_set_perm(tmp, 0755);   /* 只对普通文件（防误改设备节点） */
        if (rename(tmp, path) != 0) { unlink(tmp); continue; }
        fprintf(stderr, "\x1b[36m[s2a]\x1b[0m 宿主没找到 qemu，已从内嵌副本解出 %zu 字节到 %s\n",
                len, path);
    run:
        return path;
    }
    return NULL;
}

int s2a_run_qemu(const char *self, const char *target, int nargs, char **args)
{
    const char *cand[8];
    int n = 0;
    const char *env = getenv("S2A_QEMU");
    if (env && *env) cand[n++] = env;
    /* 先在本进程所在目录找 */
    static char beside[1024];
    const char *slash = self ? strrchr(self, '/') : NULL;
    if (slash && (size_t)(slash - self) < sizeof beside - 32) {
        size_t dl = (size_t)(slash - self) + 1;
        memcpy(beside, self, dl);
        snprintf(beside + dl, sizeof beside - dl, "qemu-aarch64-static");
        cand[n++] = beside;
    }
    cand[n++] = "/root/qemu-aarch64-static";
    cand[n++] = "/usr/bin/qemu-aarch64-static";
    cand[n++] = "/usr/local/bin/qemu-aarch64-static";
    cand[n++] = "/usr/bin/qemu-aarch64";
    const char *q = NULL;
    if (!getenv("S2A_QEMU_FORCE_EMBEDDED"))
        for (int i = 0; i < n; i++) if (access(cand[i], X_OK) == 0) { q = cand[i]; break; }
    if (!q && !getenv("S2A_QEMU_FORCE_EMBEDDED")) {
        const char *path = getenv("PATH");
        static char buf[1024];
        while (path && *path) {
            const char *c = strchr(path, ':');
            size_t l = c ? (size_t)(c - path) : strlen(path);
            if (l + 32 < sizeof buf) {
                snprintf(buf, sizeof buf, "%.*s/qemu-aarch64-static", (int)l, path);
                if (access(buf, X_OK) == 0) { q = buf; break; }
            }
            if (!c) break;
            path = c + 1;
        }
    }
    char **av = calloc((size_t)nargs + 3, sizeof(char *));
    if (!av) return 127;
    av[0] = (char *)q;                                /* 外部 qemu 时先填，内嵌时会被覆盖 */
    av[1] = (char *)target;
    for (int i = 0; i < nargs; i++) av[i + 2] = args[i];
    av[nargs + 2] = NULL;
    if (!q) {
        /* 内嵌副本：先试匿名内存页 + exec（不落盘） */
        if (!getenv("S2A_NO_MEMFD") && s2a_qemu_blob_len()) {
            av[0] = (char *)"qemu-aarch64";
            s2a_qemu_from_memfd(av);                      /* 成功即已被替换 */
        }
        q = s2a_qemu_from_dir(av);                        /* 兜底：解到目录 */
        if (q) av[0] = (char *)q;
        else {
            fprintf(stderr, "s2a: 未找到可用的 qemu-aarch64（可用 S2A_QEMU=<路径> 指定，"
                            "或 S2A_QEMU_DIR / S2A_QEMU_DIRS 指定解出目录，"
                            "或加 --emu-own 只用内置解释器）\n");
            return 127;
        }
    }
    fprintf(stderr, "\x1b[36m[s2a]\x1b[0m 用 %s 执行 %s%s\n", q, target,
            nargs ? "（透传剩余参数）" : "");
    execv(q, av);
    fprintf(stderr, "s2a: execv(%s) 失败: %s\n", q, strerror(errno));
    return 127;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && (!strcmp(argv[1], "ash") || !strcmp(argv[1], "qemu"))) {
        int r = s2a_dispatch_tool(argc, argv);     /* 成功则不会返回 */
        if (r) return r;
    }
    if (argc >= 2 && !strcmp(argv[1], "--only-enc")) {     /* 任意 ELF → 自解密外壳 */
        const char *pw = NULL;
        for (int i = argc - 1; i >= 4; i--) {
            if (!strncmp(argv[i - 1], "--password=", 11)) { pw = argv[i - 1] + 11; break; }
            if (!strcmp(argv[i - 1], "--password") || !strcmp(argv[i - 1], "--password-env") ||
                !strcmp(argv[i - 1], "--password_env") || !strcmp(argv[i - 1], "--password-file") ||
                !strcmp(argv[i - 1], "--password_file")) {
                pw = s2a_password_from_arg(argv[i - 1], argv[i]);
                if (!pw) return 70;
                break;
            }
        }
        if (!pw) { const char *e = getenv("S2A_PASSWORD"); if (e && *e) pw = e; }
        if (argc < 4) {
            fprintf(stderr, "用法: %s --only-enc <输入.elf> <输出.elf> [--password 口令]\n", argv[0]);
            return 2;
        }
        return s2a_encrypt_file(argv[2], argv[3], pw);
    }
    s2a_options o;
    memset(&o, 0, sizeof o);
    o.self = argv[0];
    o.opt_level = 2;
    o.anti_action = RC_ANTI_WARN;
    { extern int s2a_lock_debug; s2a_lock_debug = (getenv("S2A_LOCK_DEBUG") != NULL); }

    const char *run_target = NULL;
    int run_mode = 0;      /* 1 = 模拟器，2 = iso */
    const char *input = NULL;

    int rest_only = 0;          /* 见到 `--` 之后：所有参数原样交给产物 */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        /* `--` 是分隔符：后面的东西（哪怕以 - 开头，例如产物自己的 --password）
           全部当作**产物的参数**，s2a 不再解析。
           例：s2a -qemu posix_test.elf -- --password 'test1234' arg1 arg2 */
        if (!rest_only && !strcmp(a, "--")) { rest_only = 1; continue; }
        if (rest_only) {
            if (!input) input = a;                          /* 还没给产物时，第一个当产物 */
            else if (run_mode && o.argv_rest_n < 63) o.argv_rest[o.argv_rest_n++] = a;
            else { s2a_error("多余的参数: %s", a); return 2; }
            continue;
        }
        if (is_license_flag(a)) { fputs(g_license_text, stdout); return 0; }
        if (!strcmp(a, "-h")) { print_help(0); return 0; }
        else if (!strcmp(a, "--help")) { print_help(1); return 0; }
        else if (!strcmp(a, "--help-full")) { print_help(2); return 0; }
        else if (!strcmp(a, "--help-dev")) { print_help(3); return 0; }
        else if (!strcmp(a, "-v") || !strcmp(a, "--version")) {
            printf("s2a (铸壳工具链 ForgeShell) v%s\nAuthor: %s\n", S2A_VERSION, S2A_AUTHOR);
            return 0;
        }
        else if (!strcmp(a, "-o")) { if (++i >= argc) { s2a_error("-o 需要参数"); return 2; } o.output = argv[i]; }
        else if (!strcmp(a, "-e")) { if (++i >= argc) { s2a_error("-e 需要参数"); return 2; } o.eval_text = argv[i]; }
        else if (!strcmp(a, "-P")) { o.prot_level = 2; }
        else if (!strcmp(a, "-F")) { o.prot_level = 3; o.opt_level = 4; }
        else if (!strcmp(a, "-K")) { o.keep_intermediate = 1; }
        else if (!strcmp(a, "-s")) { o.strip = 1; }
        else if (!strcmp(a, "--no-tools")) { o.no_tools = 1; }   /* 产物不内嵌工具（默认内嵌） */
        else if (!strcmp(a, "-easy")) { o.easy = 1; }            /* 最简模式：不编译，外壳 + 内嵌 ash + 脚本 */
        else if (!strcmp(a, "-r")) { run_mode = 1; }
        else if (!strcmp(a, "--iso") || !strcmp(a, "-X")) { run_mode = 2; }
        else if (!strcmp(a, "--no-fallback") || !strcmp(a, "--emu-own")) { o.no_fallback = 1; }
        else if (!strcmp(a, "-qemu") || !strcmp(a, "--emu-qemu")) { run_mode = 1; o.emu_qemu = 1; }
        else if (!strcmp(a, "--emu-trace")) { o.emu_trace = 1; }
        else if (!strcmp(a, "--password")) { if (++i >= argc) { s2a_error("--password 需要参数"); return 2; } o.password = argv[i]; }
        else if (!strncmp(a, "--password=", 11)) { o.password = a + 11; }
        else if (!strcmp(a, "--password-env") || !strcmp(a, "--password_env") ||
                 !strcmp(a, "--password-file") || !strcmp(a, "--password_file")) {
            if (++i >= argc) { s2a_error("%s 需要参数", a); return 2; }
            const char *v = s2a_password_from_arg(a, argv[i]);   /* 环境变量 / 文件内容 / 原值 */
            if (!v) return 70;
            o.password = v;
        }
        else if (!strcmp(a, "--anti-action")) {
            if (++i >= argc) { s2a_error("--anti-action 需要参数"); return 2; }
            if (!strcmp(argv[i], "warn")) o.anti_action = RC_ANTI_WARN;
            else if (!strcmp(argv[i], "report")) o.anti_action = RC_ANTI_REPORT;
            else if (!strcmp(argv[i], "fake")) o.anti_action = RC_ANTI_FAKE;
            else if (!strcmp(argv[i], "delay")) o.anti_action = RC_ANTI_DELAY;
            else o.anti_action = RC_ANTI_SILENT;
        }
        else if (!strcmp(a, "-g")) { o.debug_level = 2; g_debug = o.debug_level; }
        else if (!strncmp(a, "-g", 2) && a[2] >= '1' && a[2] <= '5') { o.debug_level = a[2] - '0'; g_debug = o.debug_level; }
        else if (!strncmp(a, "-O", 2)) { o.opt_level = atoi(a + 2); }
        else if (a[0] == '-' && a[1]) { s2a_error("未知选项: %s（-h 看帮助）", a); return 2; }
        else if (!input) input = a;
        else if (run_mode && o.argv_rest_n < 63) o.argv_rest[o.argv_rest_n++] = a;
        else { s2a_error("多余的参数: %s", a); return 2; }
    }

    if (run_mode) {
        run_target = input;              /* 运行模式下第一个非选项参数就是产物路径 */
        if (!run_target) { s2a_error("运行模式缺少产物路径（例如 s2a -r ./a.elf）"); return 2; }
        if (run_mode == 1) {
            if (o.emu_qemu) return s2a_run_qemu(o.self, run_target, o.argv_rest_n, (char **)o.argv_rest);
            int rc = s2a_emu_run(&o, run_target, o.argv_rest_n, (char **)o.argv_rest);
            extern int s2a_emu_last_reason;
            if (!o.no_fallback && s2a_emu_last_reason != 0) {
                const char *why = (s2a_emu_last_reason == 1) ? "内置解释器缺少该指令"
                                : (s2a_emu_last_reason == 2) ? "内置解释器访存故障"
                                : "内置解释器步数上限";
                fprintf(stderr,
                    "\x1b[33m[s2a]\x1b[0m %s，自动回退到 qemu 重新执行整个产物"
                    "（可用 --emu-own 关闭回退，或 -qemu 直接指定 qemu）\n", why);
                return s2a_run_qemu(o.self, run_target, o.argv_rest_n, (char **)o.argv_rest);
            }
            return rc;
        }
        return s2a_iso_run(&o, run_target, o.argv_rest_n, (char **)o.argv_rest);
    }

    /* 给了密码就必须加壳：否则密码形同虚设 */
    if (o.password && *o.password && o.prot_level == 0) {
        o.prot_level = 1;
        s2a_info(&o, 1, "检测到 --password：自动启用基础防护（字符串加密 + 完整性 + 密码锁）");
    }

    if (!input && !o.eval_text) {
        s2a_error("没有输入脚本（-h 看帮助）");
        return 2;
    }
    if (!o.output) o.output = o.eval_text ? strdup("a.elf") : default_output(input);

    char *src = o.eval_text ? strdup(o.eval_text) : read_file(input, NULL);

    /* -easy：不做词法/语法/代码生成，直接产出「外壳 + 内嵌 ash + 脚本（正文）」
       —— 于是 dash 能吃、s2a 不一定能编译的脚本也能出产物；加密交给 --only-enc。 */
    if (o.easy) {
        if (!src) { s2a_error("没有输入脚本"); return 1; }
        size_t llen = (size_t)(_binary_build_launcher_elf_end - _binary_build_launcher_elf_start);
        size_t alen = (size_t)(_binary_assets_ash_upx_end - _binary_assets_ash_upx_start);
        size_t slen = strlen(src);
        FILE *f = fopen(o.output, "wb");
        if (!f) { s2a_error("写出 %s 失败: %s", o.output, strerror(errno)); return 1; }
        unsigned long long off1 = llen, len1 = alen, off2 = llen + alen, len2 = slen, mode = 0;
        fwrite(_binary_build_launcher_elf_start, 1, llen, f);
        fwrite(_binary_assets_ash_upx_start, 1, alen, f);
        fwrite(src, 1, slen, f);
        unsigned char tail[128];
        memset(tail, 0, sizeof tail);
        memcpy(tail, "S2ALAU01", 8);
        memcpy(tail + 8, &mode, 8);
        memcpy(tail + 16, &off1, 8); memcpy(tail + 24, &len1, 8);
        memcpy(tail + 32, &off2, 8); memcpy(tail + 40, &len2, 8);
        memcpy(tail + 120, "S2AEND01", 8);
        fwrite(tail, 1, 128, f);
        fclose(f);
        if (!s2a_set_perm(o.output, 0755) && s2a_perm_special_path(o.output))
            fprintf(stderr, "s2a: 提示：输出 %s 不是普通文件，已跳过权限设置\n", o.output);
        s2a_info(&o, 1, "-easy：已生成 %s（外壳 %zu + ash %zu + 脚本 %zu + 尾部 128 = %zu 字节）",
                 o.output, llen, alen, slen, llen + alen + slen + 128);
        return 0;
    }
    if (!src) return 1;
    const char *name = o.eval_text ? "<-e>" : input;
    return compile(&o, src, name);
}
