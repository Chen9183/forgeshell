/* launcher.c —— 铸壳工具链 ForgeShell 产物外壳（独立小 ELF，编译期被内嵌进编译器）
 *
 * 尾部固定 128 字节索引（放在文件末尾，ELF 加载器忽略尾部数据）：
 *   [0..8)    "S2ALAU01"
 *   [8..16)   mode：0 = easy（内嵌 ash + 脚本），1 = enc（内嵌加密 ELF）
 *   [16..24)  off1 / [24..32) len1        easy: ash blob；enc: 密文负载
 *   [32..40)  off2 / [40..48) len2        easy: 脚本正文；enc: 未用
 *   [48..112) seed[64]（enc：key=SHA256(seed[0:32])，nonce=seed[32:48]）
 *   [120..128) "S2AEND01"
 *
 * easy 模式：解出内嵌 ash 与脚本 → exec `ash <脚本> <用户参数…>`
 * enc  模式：把密文解密（AES-256-CTR）→ 匿名内存页 → fexecve，argv 原样透传
 *
 * Author: deepseek v4 flash & @Chen9183 (github)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include "../common/crypto.h"
#include "../common/safeperm.h"

#define TAIL_LEN 128
static const char *MAGIC = "S2ALAU01";
static const char *ENDM  = "S2AEND01";

static int read_tail(unsigned char *t, int *ver)
{
    int fd = open("/proc/self/exe", O_RDONLY);
    if (fd < 0) return -1;
    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < TAIL_LEN || lseek(fd, sz - TAIL_LEN, SEEK_SET) < 0 ||
        read(fd, t, TAIL_LEN) != TAIL_LEN) { close(fd); return -1; }
    if (!memcmp(t, "S2ALAU02", 8) && !memcmp(t + 120, "S2AEND02", 8)) { if (ver) *ver = 2; return fd; }
    if (!memcmp(t, "S2ALAU01", 8) && !memcmp(t + 120, "S2AEND01", 8)) { if (ver) *ver = 1; return fd; }
    close(fd);
    return -1;
}

/* 读整个文件（口令文件＝内容原样；分块读，管道也适用） */
static char *read_all(const char *path)
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
        if (got < 1024) break;
    }
    fclose(f);
    b[len] = 0;
    return b;
}

/* 找一个可写目录，把 [off,len) 原样解出来（按长度命名做缓存，原子 rename） */
static int extract(int fd, unsigned long long off, unsigned long long len,
                   const char *tag, char *out, size_t outsz)
{
    const char *dirs[5];
    int nd = 0;
    if (getenv("S2A_TOOL_DIR")) dirs[nd++] = getenv("S2A_TOOL_DIR");
    if (getenv("TMPDIR")) dirs[nd++] = getenv("TMPDIR");
    dirs[nd++] = "/data/local/tmp";
    dirs[nd++] = "/tmp";
    dirs[nd] = NULL;
    for (int i = 0; i < nd; i++) {
        snprintf(out, outsz, "%s/s2a-%s-%llu", dirs[i], tag, len);
        if (access(out, X_OK) == 0) return 0;
        char tmp[600];
        snprintf(tmp, sizeof tmp, "%s.tmp%d", out, (int)getpid());
        int of = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (of < 0) { out[0] = 0; continue; }
        if (lseek(fd, (off_t)off, SEEK_SET) != (off_t)off) { close(of); unlink(tmp); out[0] = 0; continue; }
        char buf[65536];
        unsigned long long left = len;
        int ok = 1;
        while (left) {
            size_t want = left < sizeof buf ? (size_t)left : sizeof buf;
            ssize_t r = read(fd, buf, want);
            if (r <= 0) { ok = 0; break; }
            if (write(of, buf, (size_t)r) != r) { ok = 0; break; }
            left -= (unsigned long long)r;
        }
        close(of);
        if (!ok) { unlink(tmp); out[0] = 0; continue; }
        s2a_set_perm(tmp, 0755);   /* 只对普通文件 */
        if (rename(tmp, out) != 0) { unlink(tmp); out[0] = 0; continue; }
        return 0;
    }
    return -1;
}

/* 把内存里的字节写成一个可执行文件（enc 模式兜底用；优先走匿名内存页） */
static int memfd_write(const unsigned char *data, size_t len)
{
    int mfd = -1;
#ifdef SYS_memfd_create
    mfd = (int)syscall(SYS_memfd_create, "s2a-enc", 0x0001 /*MFD_CLOEXEC*/);
#endif
    if (mfd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(mfd, data + off, len - off);
        if (w <= 0) { close(mfd); return -1; }
        off += (size_t)w;
    }
    lseek(mfd, 0, SEEK_SET);
    return mfd;
}

static void die(const char *what)
{
    fprintf(stderr, "s2a-launcher: %s: %s\n", what, strerror(errno));
    _exit(126);
}

int main(int argc, char **argv)
{
    unsigned char t[TAIL_LEN];
    int ver = 0;
    int fd = read_tail(t, &ver);
    if (fd < 0) { fprintf(stderr, "s2a-launcher: 产物尾部索引损坏\n"); return 126; }
    unsigned long long mode = 0, off1 = 0, len1 = 0, off2 = 0, len2 = 0;
    memcpy(&mode, t + 8, 8);
    memcpy(&off1, t + 16, 8); memcpy(&len1, t + 24, 8);
    memcpy(&off2, t + 32, 8); memcpy(&len2, t + 40, 8);

    if (mode == 0) {                          /* ---- easy：内嵌 ash + 脚本 ---- */
        char ash[512] = {0}, script[512] = {0};
        if (extract(fd, off1, len1, "ash", ash, sizeof ash) != 0 ||
            extract(fd, off2, len2, "sh", script, sizeof script) != 0) {
            fprintf(stderr, "s2a-launcher: 解不出内嵌 ash/脚本（可用 S2A_TOOL_DIR 指定可写目录）\n");
            return 127;
        }
        close(fd);
        char **av = malloc(sizeof(char *) * (size_t)(argc + 3));
        if (!av) return 126;
        int n = 0;
        av[n++] = ash;
        av[n++] = script;
        for (int i = 1; i < argc; i++) av[n++] = argv[i];   /* 用户参数透传给脚本 */
        av[n] = NULL;
        execv(ash, av);
        die(ash);
    }

    if (mode == 1) {                          /* ---- enc：解密后执行 ---- */
        /* 1) 口令来源：--password / --password-env / --password-file / S2A_PASSWORD */
        const char *pw = NULL;
        char **keep = malloc(sizeof(char *) * (size_t)(argc + 1));   /* 去掉口令参数后的 argv */
        int nk = 0;
        int pw_taken = 0;                      /* 只吃第一个口令；后面的留给内层（嵌套时各层各有口令） */
        if (!keep) return 126;
        keep[nk++] = argv[0];
        for (int i = 1; i < argc; i++) {
            const char *a = argv[i];
            if (!pw_taken && !strcmp(a, "--password") && i + 1 < argc) { pw = argv[++i]; pw_taken = 1; continue; }
            if (!pw_taken && !strncmp(a, "--password=", 11)) { pw = a + 11; pw_taken = 1; continue; }
            if (!pw_taken && (!strcmp(a, "--password-env") || !strcmp(a, "--password_env")) && i + 1 < argc) {
                const char *v = getenv(argv[++i]);
                if (!v || !*v) { fprintf(stderr, "s2a-launcher: 环境变量 %s 未设置或为空\n", argv[i]); return 70; }
                pw = v; continue;
            }
            if (!pw_taken && (!strcmp(a, "--password-file") || !strcmp(a, "--password_file")) && i + 1 < argc) {
                char *v = read_all(argv[++i]);
                if (!v) { fprintf(stderr, "s2a-launcher: 读不到口令文件 %s\n", argv[i]); return 70; }
                pw = v; pw_taken = 1; continue;
            }
            keep[nk++] = argv[i];
        }
        if (!pw) { const char *e = getenv("S2A_PASSWORD"); if (e && *e) pw = e; }
        keep[nk] = NULL;

        /* 2) 密钥派生：v2 口令走 PBKDF2(口令, 随机盐)，无口令走 kdf_strong(seed)；
              v1（旧文件）沿用旧方案（seed 前 32 字节经口令异或后 SHA-256） */
        unsigned char key[32], nonce[16], seed[32];
        memcpy(nonce, t + 80, 16);
        if (ver == 2) {
            memcpy(seed, t + 48, 32);
            uint32_t pw_flag = 0, iters = 0;
            memcpy(&pw_flag, t + 112, 4);
            memcpy(&iters, t + 116, 4);
            if (pw_flag) {
                if (!pw) {
                    fprintf(stderr, "s2a-launcher: 该产物已加口令锁，请给 --password / --password-env / --password-file 或 S2A_PASSWORD\n");
                    return 70;
                }
                if (!iters) iters = (uint32_t)S2A_KDF_ITERS;
                pbkdf2_sha256(pw, strlen(pw), t + 96, 16, iters, key, 32);
            } else {
                kdf_strong(seed, 32, "onlyenc-seed", nonce, 16, key);
            }
        } else {                                  /* v1 兼容 */
            memcpy(seed, t + 48, 32);             /* 旧布局：seed[64] 起，nonce 在 +32 */
            memcpy(nonce, t + 48 + 32, 16);
            if (pw && *pw) {
                unsigned char kp[64];
                sha512_buf(pw, strlen(pw), kp);
                for (int i = 0; i < 32; i++) seed[i] ^= kp[i];
            }
            unsigned char d[32];
            sha256_buf(seed, 32, d);
            memcpy(key, d, 32);
        }

        /* 2b) 把口令导出到环境：嵌套加密时（内层也要口令）内层才拿得到 —— 否则外层把
               口令参数吃走，内层永远解不开（"永远无法解密"就是这个原因）。 */
        if (pw && *pw) setenv("S2A_PASSWORD", pw, 1);

        /* 3) 解密 + 校验是不是 ELF（口令错 → 这里就露馅） */
        if (len1 < 4 || len1 > (1ULL << 30)) { fprintf(stderr, "s2a-launcher: 负载长度异常\n"); return 126; }
        unsigned char *buf = malloc((size_t)len1);
        if (!buf) return 126;
        if (lseek(fd, (off_t)off1, SEEK_SET) != (off_t)off1) die("lseek");
        size_t got = 0;
        while (got < (size_t)len1) {
            ssize_t r = read(fd, buf + got, (size_t)len1 - got);
            if (r <= 0) die("read");
            got += (size_t)r;
        }
        close(fd);
        rc_aes256_ctr(key, nonce, buf, (size_t)len1);
        if (memcmp(buf, "\x7f" "ELF", 4) != 0) {
            fprintf(stderr, "s2a-launcher: 口令错误或文件被改动（解出来不是 ELF）\n");
            return 70;
        }

        /* 4) 执行：优先匿名内存页，兜底落盘 */
        int mfd = memfd_write(buf, (size_t)len1);
        if (mfd >= 0) fexecve(mfd, keep, environ);
        char p[512] = {0};
        int tf = -1;
        {   const char *dirs[4]; int nd = 0;
            if (getenv("S2A_TOOL_DIR")) dirs[nd++] = getenv("S2A_TOOL_DIR");
            if (getenv("TMPDIR")) dirs[nd++] = getenv("TMPDIR");
            dirs[nd++] = "/data/local/tmp"; dirs[nd++] = "/tmp";
            for (int i = 0; i < nd; i++) {
                snprintf(p, sizeof p, "%s/s2a-dec-%d", dirs[i], (int)getpid());
                tf = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0755);
                if (tf >= 0) break;
            }
        }
        if (tf < 0) { fprintf(stderr, "s2a-launcher: 无法落盘解密结果\n"); return 127; }
        if (write(tf, buf, (size_t)len1) != (ssize_t)len1) die("write");
        close(tf);
        s2a_set_perm(p, 0755);       /* 只对普通文件 */
        execv(p, keep);
        unlink(p);
        die(p);
    }

    fprintf(stderr, "s2a-launcher: 未知模式 %llu\n", mode);
    return 126;
}
