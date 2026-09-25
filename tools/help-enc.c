/* help-enc.c —— 把帮助 TXT 加密成 .enc（编译 s2a 时用；仓库里保留可读 TXT）
 * 密钥派生：k0 = SHA-256(SALT)；每个文件 nonce = SHA-256(SALT + 文件名) 前 16 字节
 * 算法：AES-256-CTR（与运行时解密同一套）
 * 用法: help-enc <输入.txt> <输出.enc> <文件名标签>
 * Author: deepseek v4 flash & @Chen9183 (github)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crypto.h"

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "用法: %s <in.txt> <out.enc> <标签>\n", argv[0]); return 2; }
    FILE *fi = fopen(argv[1], "rb");
    if (!fi) { perror("打开输入"); return 1; }
    fseek(fi, 0, SEEK_END); long n = ftell(fi); fseek(fi, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, fi) != (size_t)n) { fprintf(stderr, "读失败\n"); return 1; }
    fclose(fi);

    static const char SALT[] = "ForgeShell help text v1";
    unsigned char key[32];
    sha256_buf(SALT, sizeof SALT - 1, key);
    unsigned char nb[256], h[32], nonce[16];
    snprintf((char *)nb, sizeof nb, "%s%s", SALT, argv[3]);
    sha256_buf(nb, strlen((char *)nb), h);          /* ⚑ 先写进 32 字节缓冲，再取前 16 当 nonce
                                                       （直接写进 nonce[16] 会溢出 16 字节，把 key 踩坏） */
    memcpy(nonce, h, 16);
    rc_aes256_ctr(key, nonce, buf, (size_t)n);

    FILE *fo = fopen(argv[2], "wb");
    if (!fo) { perror("打开输出"); return 1; }
    if (fwrite(buf, 1, (size_t)n, fo) != (size_t)n) { fprintf(stderr, "写失败\n"); return 1; }
    fclose(fo);
    printf("%s → %s（%ld 字节，已加密）\n", argv[1], argv[2], n);
    return 0;
}
