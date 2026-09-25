/* protect.c —— 见 protect.h */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "protect.h"
#include "rand.h"
#include "../common/crypto.h"

int s2a_lock_debug;

/* 每产物随机盐：取池密文的前 16 字节（公开、每次构建不同；口令路径用它做 PBKDF2 的盐，
   避免同一口令在不同产物上派生出相同密钥 → 攻击者无法把一次预计算复用到多份产物）。
   ⚠️ 加密侧与解密侧必须取同一段字节：编译器取加密后的 c->pool，运行时取解密的 img->pool。 */
static unsigned char g_pw_salt[16];

/* 用密码派生密钥加密/解密任意缓冲（CTR），nonce 由 Kp 的不同片段提供 */
static void pw_crypt(const char *pw, const unsigned char *nonce_src, unsigned char *buf, uint32_t len)
{
    unsigned char kp[64];
    pbkdf2_sha256(pw, strlen(pw), g_pw_salt, 16, S2A_KDF_ITERS, kp, 64);   /* 盐=本产物的池密文前 16 字节 */   /* KDF v2：PBKDF2-HMAC-SHA256 */
    unsigned char key[32], nonce[16];
    memcpy(key, kp, 32);
    memcpy(nonce, nonce_src, 16);
    rc_aes256_ctr(key, nonce, buf, len);
    memset(kp, 0, sizeof kp);
    memset(key, 0, sizeof key);
}

/* 层-2 的两把 nonce：标记用 Kp[32..47]，seed/payload 用 Kp[48..63] */
static void pw_nonce_marker(const char *pw, unsigned char out[16])
{
    unsigned char kp[64];
    pbkdf2_sha256(pw, strlen(pw), g_pw_salt, 16, S2A_KDF_ITERS, kp, 64);   /* 盐=本产物的池密文前 16 字节 */   /* KDF v2：PBKDF2-HMAC-SHA256 */
    memcpy(out, kp + 32, 16);
    memset(kp, 0, sizeof kp);
}
static void pw_nonce_payload(const char *pw, unsigned char out[16])
{
    unsigned char kp[64];
    pbkdf2_sha256(pw, strlen(pw), g_pw_salt, 16, S2A_KDF_ITERS, kp, 64);   /* 盐=本产物的池密文前 16 字节 */   /* KDF v2：PBKDF2-HMAC-SHA256 */
    memcpy(out, kp + 48, 16);
    memset(kp, 0, sizeof kp);
}

/* ---------------------------------------------------------------
 * 栅栏密码（2 轨）+ 字节异或 —— 本项目自己的小型混淆算法
 *
 * 用途：把 16 字节校验常量打散，使产物里既没有作者名明文、也看不出是哈希值。
 * 它不是密码学原语（强度靠 AES-256 与 SHA-2 承担），只负责“别是原文”。
 * mode=0 加密：输入顺序拆成前后两轨后交错输出；mode=1 解密：逆操作。
 * 之后整段按 key 逐字节异或（key 来自每构建随机的 seed）。
 * --------------------------------------------------------------- */
static void rail_fence_xor(const unsigned char *in, unsigned char *out, int n,
                           const unsigned char *key, int klen, int mode)
{
    int half = n / 2;
    unsigned char tmp[64];
    if (n > 64) n = 64;
    for (int i = 0; i < n; i++) {
        unsigned char b = (unsigned char)(in[i] ^ key[i % klen]);
        tmp[i] = b;
    }
    if (mode == 0) {                       /* 加密：r0=前半, r1=后半，交错写出 */
        int i0 = 0, i1 = 0;
        for (int i = 0; i < n; i++) {
            if (i % 2 == 0) out[i] = tmp[i0 < half ? i0++ : 0];
            else            out[i] = tmp[half + (i1 < n - half ? i1++ : 0)];
        }
    } else {                               /* 解密：先解交错，再还原顺序 */
        int i0 = 0, i1 = 0;
        for (int i = 0; i < n; i++) {
            if (i % 2 == 0) tmp[i0 < half ? i0++ : 0] = tmp[i];
            else            tmp[half + (i1 < n - half ? i1++ : 0)] = tmp[i];
        }
        /* 上面的就地操作会互相覆盖，这里改为两段缓冲 */
        unsigned char a[32], b[32];
        int j0 = 0, j1 = 0;
        for (int i = 0; i < n; i++) {
            if (i % 2 == 0) a[j0++] = in[i] ^ key[i % klen];
            else            b[j1++] = in[i] ^ key[i % klen];
        }
        for (int i = 0; i < j0; i++) out[i] = a[i];
        for (int i = 0; i < j1; i++) out[j0 + i] = b[i];
    }
}

int s2a_seal(s2a_seal_ctx *c)
{
    uint64_t flags = RIF_PROTECTED | RIF_AES_POOL | RIF_SELFCHECK;

    /* ① 真随机 seed（每次编译都不同，源码里没有任何密钥） */
    if (s2a_true_random(c->seed, 64) != 0) return -1;

    /* ② 层-1：池加密 key = SHA-256(seed[0..31])，nonce = seed[32..47] */
    {
        unsigned char key[32], d[32];
        kdf_strong(c->seed, 32, "protect-pool", c->seed + 32, 16, d);   /* KDF v2：seed→池密钥 */
        memcpy(key, d, 32);
        rc_aes256_ctr(key, c->seed + 32, c->pool, c->pool_size);
        memset(key, 0, sizeof key);
        memset(d, 0, sizeof d);
    }

    /* ③ 完整性基线（明文摘要） */
    sha512_buf(c->pool, c->pool_size, c->pool_sha512);
    sha512_buf(c->code, c->code_size, c->code_sha512);

    /* ④ 层-2：密码锁（产物里不出现任何明文） */
    if (c->password && *c->password) {
        flags |= RIF_LOCKED;

        /* (a) 每构建随机的 16 字节校验料 V，只以 AES-CTR 密文形式出现在产物里 */
        unsigned char V[16];
        if (s2a_true_random(V, sizeof V) != 0) return -1;
        memcpy(c->marker, V, sizeof V);
        unsigned char mnonce[16];
        memcpy(g_pw_salt, c->pool, 16);
        pw_nonce_marker(c->password, mnonce);
        memcpy(g_pw_salt, c->pool, 16);
        pw_crypt(c->password, mnonce, c->marker, sizeof V);
        *c->marker_len = sizeof V;

        /* (b) 校验常量 = 栅栏( SHA-256(V ‖ seed)[:16], K )，K = SHA-256(seed)[:16]
         *      —— 作者名不参与写盘，只有密码正确才能算出同样结果 */
        unsigned char h[32];
        {
            sha256_ctx sc;
            sha256_init(&sc);
            sha256_update(&sc, V, sizeof V);
            sha256_update(&sc, c->seed, 64);
            sha256_final(&sc, h);
        }
        unsigned char K[32];
        kdf_strong(c->seed, 32, "protect-check", c->seed + 32, 16, K);  /* KDF v2：seed→校验密钥 */
        rail_fence_xor(h, c->author_check, 16, K, 16, 0);
        extern int s2a_lock_debug;
        if (s2a_lock_debug)
            fprintf(stderr, "  [seal] V=%02x%02x seed=%02x%02x h=%02x%02x K=%02x%02x chk=%02x%02x\n",
                    V[0],V[1], c->seed[0],c->seed[1], h[0],h[1], K[0],K[1], c->author_check[0],c->author_check[1]);
        memset(h, 0, sizeof h);
        memset(K, 0, sizeof K);
        memset(V, 0, sizeof V);

        /* (c) seed 本体也用密码包一层：没有密码连层-1 密钥都拿不到 */
        unsigned char pnonce[16];
        memcpy(g_pw_salt, c->pool, 16);
        pw_nonce_payload(c->password, pnonce);
        memcpy(g_pw_salt, c->pool, 16);
        pw_crypt(c->password, pnonce, c->seed, 64);
    } else {
        *c->marker_len = 0;
        memset(c->author_check, 0, 16);
    }

    if (c->anti_action != RC_ANTI_SILENT && c->anti_action != RC_ANTI_REPORT)
        flags |= RIF_REPORT;
    if (c->anti_action == RC_ANTI_REPORT) flags |= RIF_REPORT;

    if (c->flags_out) *c->flags_out = flags;
    c->rnd_desc = "/dev/random";
    return 0;
}

/* 编译器自检：用给定密码走一遍运行期算法，验证能对上校验常量（也验证解密路径正确） */
int s2a_unseal_check(const s2a_seal_ctx *c, const char *password)
{
    memcpy(g_pw_salt, c->pool, 16);   /* 每产物盐（校验路径同样取池密文） */
    if (!password || !*password) return -2;
    uint32_t n = *c->marker_len;
    if (n == 0 || n > 64) return -1;
    unsigned char V[80];
    memcpy(V, c->marker, n);
    unsigned char nonce[16];
    pw_nonce_marker(password, nonce);
    pw_crypt(password, nonce, V, n);

    /* 注意：此时 c->seed 已被密码层加密，先复制一份并解开用于自检 */
    unsigned char seed[64];
    memcpy(seed, c->seed, 64);
    unsigned char pnonce[16];
    pw_nonce_payload(password, pnonce);
    pw_crypt(password, pnonce, seed, 64);

    unsigned char h[32];
    sha256_ctx sc;
    sha256_init(&sc);
    sha256_update(&sc, V, n);
    sha256_update(&sc, seed, 64);
    sha256_final(&sc, h);
    unsigned char K[32], chk[16];
    kdf_strong(seed, 32, "protect-check", seed + 32, 16, K);        /* KDF v2：seed→校验密钥 */
    rail_fence_xor(h, chk, 16, K, 16, 0);
    return memcmp(chk, c->author_check, 16) == 0 ? 0 : -1;
}
