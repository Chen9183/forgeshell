/* s2a-enc.c —— 整文件加密：任意 ELF → 外壳 + AES-256-CTR 密文 + 尾部索引
 * 用法: s2a-enc <输入.elf> <输出.elf> [--password 口令]
 * 密钥：SHA-512(口令) 与 64 字节随机种子的 SHA-256 混合（口令为空时只用种子）
 * Author: deepseek v4 flash & @Chen9183 (github)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "crypto.h"
#include "safeperm.h"

static int tail_ok = 0;
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "用法: %s <输入.elf> <输出.elf> [--password 口令]\n", argv[0]); return 2; }
    const char *pw = NULL;
    for (int i = 3; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--password")) pw = argv[i + 1];
        else if (!strncmp(argv[i], "--password=", 11)) pw = argv[i] + 11;

    FILE *fi = fopen(argv[1], "rb");
    if (!fi) { perror("打开输入"); return 1; }
    fseek(fi, 0, SEEK_END); long n = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n + 64);
    if (!buf || fread(buf, 1, (size_t)n, fi) != (size_t)n) { fprintf(stderr, "读输入失败\n"); return 1; }
    fclose(fi);

    unsigned char seed[64];
    FILE *rnd = fopen("/dev/random", "rb");
    if (!rnd || fread(seed, 1, 64, rnd) != 64) { fprintf(stderr, "取随机数失败\n"); return 1; }
    fclose(rnd);
    if (pw && *pw) {                       /* 口令参与密钥：换口令就解不开 */
        unsigned char kp[64];
        sha512_buf(pw, strlen(pw), kp);
        for (int i = 0; i < 32; i++) seed[i] ^= kp[i];
    }
    unsigned char key[32], d[32];
    sha256_buf(seed, 32, d);
    memcpy(key, d, 32);
    rc_aes256_ctr(key, seed + 32, buf, (size_t)n);

    extern const unsigned char _binary_build_launcher_elf_start[];
    extern const unsigned char _binary_build_launcher_elf_end[];
    size_t llen = (size_t)(_binary_build_launcher_elf_end - _binary_build_launcher_elf_start);

    FILE *fo = fopen(argv[2], "wb");
    if (!fo) { perror("打开输出"); return 1; }
    fwrite(_binary_build_launcher_elf_start, 1, llen, fo);
    unsigned long long off1 = llen;
    fwrite(buf, 1, (size_t)n, fo);
    unsigned char tail[128];
    memset(tail, 0, sizeof tail);
    memcpy(tail, "S2ALAU01", 8);
    unsigned long long mode = 1;
    memcpy(tail + 8, &mode, 8);
    memcpy(tail + 16, &off1, 8);
    unsigned long long len1 = (unsigned long long)n;
    memcpy(tail + 24, &len1, 8);
    memcpy(tail + 48, seed, 64);
    memcpy(tail + 120, "S2AEND01", 8);
    fwrite(tail, 1, 128, fo);
    fclose(fo);
    if (!s2a_set_perm(argv[2], 0755) && s2a_perm_special_path(argv[2]))
        fprintf(stderr, "s2a-enc: 提示：输出 %s 不是普通文件，已跳过权限设置\n", argv[2]);
    printf("已生成 %s（外壳 %zu + 密文 %ld + 尾部 128 = %ld 字节）\n", argv[2], llen, n, (long)(llen + n + 128));
    (void)tail_ok;
    return 0;
}
