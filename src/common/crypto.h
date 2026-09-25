/* crypto.h —— 密码学原语
 *
 * 来源：/root/refcode/algo/{aes256,sha256,sha512}.c（作者 deepseek v4 flash & @Chen9183），
 *       本工程做三处改造：①去掉 static 使其可跨文件链接；②补 CTR 流模式；③统一 rc_ 包装。
 *
 * 【为什么用 CTR】字符串池长度任意、需原地加解密且要支持“二次加密”，CTR 无需填充、可随机访问。
 */
#ifndef RC_CRYPTO_H
#define RC_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* ---------- AES-256 ---------- */
#define AES256_NR     14
#define AES256_RKLEN  240
typedef struct { unsigned char rk[AES256_RKLEN]; } aes256_ctx;

void aes256_init(aes256_ctx *c, const unsigned char key[32]);
void aes256_encrypt_block(const aes256_ctx *c, const unsigned char in[16], unsigned char out[16]);
void aes256_decrypt_block(const aes256_ctx *c, const unsigned char in[16], unsigned char out[16]);
void aes256_cbc_encrypt(const aes256_ctx *c, const unsigned char iv[16], const unsigned char *in, unsigned char *out, unsigned long len);
void aes256_cbc_decrypt(const aes256_ctx *c, const unsigned char iv[16], const unsigned char *in, unsigned char *out, unsigned long len);

/* ---------- SHA-2 ---------- */
typedef struct { unsigned h[8]; unsigned long long len; unsigned char buf[64]; unsigned long buflen; } sha256_ctx;
typedef struct { unsigned long long h[8]; unsigned long long len; unsigned char buf[128]; unsigned long buflen; } sha512_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, unsigned long len);
void sha256_final(sha256_ctx *c, unsigned char *digest);
void sha256_buf(const void *data, unsigned long len, unsigned char *digest);

void sha512_init(sha512_ctx *c);
void sha512_update(sha512_ctx *c, const void *data, unsigned long len);
void sha512_final(sha512_ctx *c, unsigned char *digest);
void sha512_buf(const void *data, unsigned long len, unsigned char *digest);

/* ---------- 本工程包装 ---------- */
/* AES-256-CTR：key=32B, nonce=16B（前 12 字节随机 + 后 4 字节计数器会自增，函数不改 nonce）
 * 原地加解密（in==out 安全）；同一 (key,nonce) 二次调用等价于解密。 */
void rc_aes256_ctr(const unsigned char key[32], const unsigned char nonce[16],
                   unsigned char *buf, unsigned long len);

/* 伪随机数：仅用于非安全场合（如测试）。安全场合一律从 /dev/random 取真随机。 */
void rc_prng_seed(unsigned long long seed);
unsigned long long rc_prng_next(void);

/* ---- 强密钥派生（KDF v2）：HMAC-SHA256 / PBKDF2-HMAC-SHA256 ---- */
#ifndef S2A_KDF_ITERS
#define S2A_KDF_ITERS 5000UL   /* PBKDF2 迭代次数：实测 ~175ms（纯 C SHA-256 约 3.6MB/s），想更强可调大后重编 */
#endif
void hmac_sha256(const void *key, unsigned long klen,
                 const void *msg, unsigned long mlen, unsigned char out[32]);
void pbkdf2_sha256(const void *pw, unsigned long pwlen,
                   const void *salt, unsigned long saltlen,
                   unsigned long iters, unsigned char *out, unsigned long outlen);
void kdf_strong(const void *secret, unsigned long slen, const char *purpose,
                const void *salt, unsigned long saltlen, unsigned char out[32]);


#endif
