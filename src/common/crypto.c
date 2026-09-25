/* crypto.c —— 见 crypto.h 说明（由 refcode 的零依赖件改造而来） */

#include <string.h>
#include "crypto.h"

#define SHA256_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA512_ROR(x, n) (((x) >> (n)) | ((x) << (64 - (n))))


/* ===== aes256.c ===== */



static const unsigned char aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static unsigned char aes_rsbox[256];
static int           aes_rsbox_ready;

static void aes_make_rsbox(void)
{
    int i;
    for (i = 0; i < 256; i++) aes_rsbox[aes_sbox[i]] = (unsigned char)i;
    aes_rsbox_ready = 1;
}

/* ---- AES-256 密钥扩展：32 字节密钥 → 60 个轮密钥字 ---- */
void aes256_init(aes256_ctx *c, const unsigned char key[32])
{
    static const unsigned char rcon[8] = { 0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40 };
    unsigned char *rk = c->rk;
    int i;

    for (i = 0; i < 32; i++) rk[i] = key[i];

    for (i = 8; i < 60; i++) {
        unsigned char t0 = rk[(i-1)*4], t1 = rk[(i-1)*4+1],
                      t2 = rk[(i-1)*4+2], t3 = rk[(i-1)*4+3];

        if (i % 8 == 0) {                       /* RotWord + SubWord + Rcon */
            unsigned char tmp = t0;
            t0 = (unsigned char)(aes_sbox[t1] ^ rcon[i / 8]);
            t1 = aes_sbox[t2];
            t2 = aes_sbox[t3];
            t3 = aes_sbox[tmp];
        } else if (i % 8 == 4) {                /* 只有 SubWord */
            t0 = aes_sbox[t0]; t1 = aes_sbox[t1];
            t2 = aes_sbox[t2]; t3 = aes_sbox[t3];
        }
        rk[i*4]   = (unsigned char)(rk[(i-8)*4]   ^ t0);
        rk[i*4+1] = (unsigned char)(rk[(i-8)*4+1] ^ t1);
        rk[i*4+2] = (unsigned char)(rk[(i-8)*4+2] ^ t2);
        rk[i*4+3] = (unsigned char)(rk[(i-8)*4+3] ^ t3);
    }
}

static unsigned char aes_xtime(unsigned char x)
{
    return (unsigned char)((x << 1) ^ ((x >> 7) * 0x1b));
}

static void aes_shift_rows(unsigned char s[16])
{
    unsigned char t;
    t = s[1];  s[1]  = s[5];  s[5]  = s[9];  s[9]  = s[13]; s[13] = t;
    t = s[2];  s[2]  = s[10]; s[10] = t;
    t = s[6];  s[6]  = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7];  s[7]  = s[3];  s[3]  = t;
}

static void aes_inv_shift_rows(unsigned char s[16])
{
    unsigned char t;
    t = s[13]; s[13] = s[9];  s[9]  = s[5];  s[5]  = s[1];  s[1]  = t;
    t = s[2];  s[2]  = s[10]; s[10] = t;
    t = s[6];  s[6]  = s[14]; s[14] = t;
    t = s[3];  s[3]  = s[7];  s[7]  = s[11]; s[11] = s[15]; s[15] = t;
}

static void aes_mix_columns(unsigned char s[16])
{
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char *c = s + i * 4;
        unsigned char a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];
        unsigned char t = (unsigned char)(a0 ^ a1 ^ a2 ^ a3);
        c[0] = (unsigned char)(c[0] ^ t ^ aes_xtime((unsigned char)(a0 ^ a1)));
        c[1] = (unsigned char)(c[1] ^ t ^ aes_xtime((unsigned char)(a1 ^ a2)));
        c[2] = (unsigned char)(c[2] ^ t ^ aes_xtime((unsigned char)(a2 ^ a3)));
        c[3] = (unsigned char)(c[3] ^ t ^ aes_xtime((unsigned char)(a3 ^ a0)));
    }
}

/* GF(2^8) 乘法（用于逆 MixColumns） */
static unsigned char aes_gmul(unsigned char a, unsigned char b)
{
    unsigned char p = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        {
            unsigned char hi = (unsigned char)(a & 0x80);
            a = (unsigned char)(a << 1);
            if (hi) a ^= 0x1b;
        }
        b = (unsigned char)(b >> 1);
    }
    return p;
}

static void aes_inv_mix_columns(unsigned char s[16])
{
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char *c = s + i * 4;
        unsigned char a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];
        c[0] = (unsigned char)(aes_gmul(a0,14) ^ aes_gmul(a1,11) ^ aes_gmul(a2,13) ^ aes_gmul(a3,9));
        c[1] = (unsigned char)(aes_gmul(a0,9)  ^ aes_gmul(a1,14) ^ aes_gmul(a2,11) ^ aes_gmul(a3,13));
        c[2] = (unsigned char)(aes_gmul(a0,13) ^ aes_gmul(a1,9)  ^ aes_gmul(a2,14) ^ aes_gmul(a3,11));
        c[3] = (unsigned char)(aes_gmul(a0,11) ^ aes_gmul(a1,13) ^ aes_gmul(a2,9)  ^ aes_gmul(a3,14));
    }
}

/* ---- 单块加解密（16 字节）---- */
void aes256_encrypt_block(const aes256_ctx *c, const unsigned char in[16],
                                 unsigned char out[16])
{
    unsigned char s[16];
    int i, r;

    for (i = 0; i < 16; i++) s[i] = (unsigned char)(in[i] ^ c->rk[i]);

    for (r = 1; r < AES256_NR; r++) {
        for (i = 0; i < 16; i++) s[i] = aes_sbox[s[i]];
        aes_shift_rows(s);
        aes_mix_columns(s);
        for (i = 0; i < 16; i++) s[i] ^= c->rk[r * 16 + i];
    }
    for (i = 0; i < 16; i++) s[i] = aes_sbox[s[i]];
    aes_shift_rows(s);
    for (i = 0; i < 16; i++) out[i] = (unsigned char)(s[i] ^ c->rk[AES256_NR * 16 + i]);
}

void aes256_decrypt_block(const aes256_ctx *c, const unsigned char in[16],
                                 unsigned char out[16])
{
    unsigned char s[16];
    int i, r;

    if (!aes_rsbox_ready) aes_make_rsbox();

    for (i = 0; i < 16; i++) s[i] = (unsigned char)(in[i] ^ c->rk[AES256_NR * 16 + i]);

    for (r = AES256_NR - 1; r >= 1; r--) {
        aes_inv_shift_rows(s);
        for (i = 0; i < 16; i++) s[i] = aes_rsbox[s[i]];
        for (i = 0; i < 16; i++) s[i] ^= c->rk[r * 16 + i];
        aes_inv_mix_columns(s);
    }
    aes_inv_shift_rows(s);
    for (i = 0; i < 16; i++) s[i] = aes_rsbox[s[i]];
    for (i = 0; i < 16; i++) out[i] = (unsigned char)(s[i] ^ c->rk[i]);
}

/* ---- CBC 模式（len 必须是 16 的倍数）---- */
void aes256_cbc_encrypt(const aes256_ctx *c, const unsigned char iv[16],
                               const unsigned char *in, unsigned char *out, unsigned long len)
{
    unsigned char prev[16], blk[16];
    unsigned long i;
    int j;

    for (j = 0; j < 16; j++) prev[j] = iv[j];

    for (i = 0; i + 16 <= len; i += 16) {
        for (j = 0; j < 16; j++) blk[j] = (unsigned char)(in[i + j] ^ prev[j]);
        aes256_encrypt_block(c, blk, out + i);
        for (j = 0; j < 16; j++) prev[j] = out[i + j];
    }
}

void aes256_cbc_decrypt(const aes256_ctx *c, const unsigned char iv[16],
                               const unsigned char *in, unsigned char *out, unsigned long len)
{
    unsigned char prev[16], cur[16];
    unsigned long i;
    int j;

    for (j = 0; j < 16; j++) prev[j] = iv[j];

    for (i = 0; i + 16 <= len; i += 16) {
        for (j = 0; j < 16; j++) cur[j] = in[i + j];
        aes256_decrypt_block(c, cur, out + i);
        for (j = 0; j < 16; j++) out[i + j] ^= prev[j];
        for (j = 0; j < 16; j++) prev[j] = cur[j];
    }
}

/* ---- PKCS#7 填充/去填充 ---- */
unsigned long aes256_pkcs7_pad(unsigned char *buf, unsigned long len)
{
    unsigned long pad = 16 - (len % 16), i;
    for (i = 0; i < pad; i++) buf[len + i] = (unsigned char)pad;
    return len + pad;
}

long aes256_pkcs7_unpad(const unsigned char *buf, unsigned long len)
{
    unsigned char pad;
    unsigned long i;

    if (len == 0 || (len % 16) != 0) return -1;
    pad = buf[len - 1];
    if (pad == 0 || pad > 16 || pad > len) return -1;
    for (i = 0; i < pad; i++)
        if (buf[len - 1 - i] != pad) return -1;
    return (long)(len - pad);
}

/* ---- 一步到位：自动填充的 CBC 整段加解密 ---- */
unsigned long aes256_cbc_encrypt_pad(const aes256_ctx *c, const unsigned char iv[16],
                                            unsigned char *buf, unsigned long len)
{
    unsigned long n = aes256_pkcs7_pad(buf, len);
    aes256_cbc_encrypt(c, iv, buf, buf, n);      /* 原地加密 */
    return n;
}

long aes256_cbc_decrypt_unpad(const aes256_ctx *c, const unsigned char iv[16],
                                     unsigned char *buf, unsigned long len)
{
    aes256_cbc_decrypt(c, iv, buf, buf, len);    /* 原地解密 */
    return aes256_pkcs7_unpad(buf, len);
}


/* ===== sha256.c ===== */


static const unsigned sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};


static void sha256_copy(unsigned char *d, const unsigned char *s, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
    unsigned w[64], a, b, cc, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = (unsigned)p[i*4] << 24 | (unsigned)p[i*4+1] << 16
             | (unsigned)p[i*4+2] << 8 | (unsigned)p[i*4+3];
    for (; i < 64; i++) {
        unsigned s0 = SHA256_ROR(w[i-15], 7) ^ SHA256_ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        unsigned s1 = SHA256_ROR(w[i-2], 17) ^ SHA256_ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g  = c->h[6]; h  = c->h[7];

    for (i = 0; i < 64; i++) {
        unsigned s1 = SHA256_ROR(e, 6) ^ SHA256_ROR(e, 11) ^ SHA256_ROR(e, 25);
        unsigned ch = (e & f) ^ (~e & g);
        unsigned t1 = h + s1 + ch + sha256_k[i] + w[i];
        unsigned s0 = SHA256_ROR(a, 2) ^ SHA256_ROR(a, 13) ^ SHA256_ROR(a, 22);
        unsigned mj = (a & b) ^ (a & cc) ^ (b & cc);
        unsigned t2 = s0 + mj;

        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g;  c->h[7] += h;
}

void sha256_init(sha256_ctx *c)
{
    static const unsigned iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    int i;
    for (i = 0; i < 8; i++) c->h[i] = iv[i];
    c->len = 0;
    c->buflen = 0;
}

void sha256_update(sha256_ctx *c, const void *data, unsigned long len)
{
    const unsigned char *p = data;

    c->len += len;
    while (len) {
        unsigned long take = 64 - c->buflen;
        if (take > len) take = len;
        sha256_copy(c->buf + c->buflen, p, take);
        c->buflen += take;
        p += take;
        len -= take;
        if (c->buflen == 64) {
            sha256_block(c, c->buf);
            c->buflen = 0;
        }
    }
}

void sha256_final(sha256_ctx *c, unsigned char *digest)
{
    unsigned long long bits = c->len * 8;   /* 先存位长，后面 update 会改 c->len */
    unsigned char b = 0x80, z = 0;
    int i;

    sha256_update(c, &b, 1);
    while (c->buflen != 56) sha256_update(c, &z, 1);
    for (i = 7; i >= 0; i--) {
        b = (unsigned char)(bits >> (i * 8));
        sha256_update(c, &b, 1);
    }

    for (i = 0; i < 8; i++) {
        digest[i*4]   = (unsigned char)(c->h[i] >> 24);
        digest[i*4+1] = (unsigned char)(c->h[i] >> 16);
        digest[i*4+2] = (unsigned char)(c->h[i] >> 8);
        digest[i*4+3] = (unsigned char)c->h[i];
    }
}

void sha256_buf(const void *data, unsigned long len, unsigned char *digest)
{
    sha256_ctx c;

    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_final(&c, digest);
}


/* ===== sha512.c ===== */


static const unsigned long long sha512_k[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};


static void sha512_copy(unsigned char *d, const unsigned char *s, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static void sha512_block(sha512_ctx *c, const unsigned char *p)
{
    unsigned long long w[80], a, b, cc, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = (unsigned long long)p[i*8]   << 56 | (unsigned long long)p[i*8+1] << 48
             | (unsigned long long)p[i*8+2] << 40 | (unsigned long long)p[i*8+3] << 32
             | (unsigned long long)p[i*8+4] << 24 | (unsigned long long)p[i*8+5] << 16
             | (unsigned long long)p[i*8+6] << 8  | (unsigned long long)p[i*8+7];
    for (; i < 80; i++) {
        unsigned long long s0 = SHA512_ROR(w[i-15], 1) ^ SHA512_ROR(w[i-15], 8) ^ (w[i-15] >> 7);
        unsigned long long s1 = SHA512_ROR(w[i-2], 19) ^ SHA512_ROR(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g  = c->h[6]; h  = c->h[7];

    for (i = 0; i < 80; i++) {
        unsigned long long s1 = SHA512_ROR(e, 14) ^ SHA512_ROR(e, 18) ^ SHA512_ROR(e, 41);
        unsigned long long ch = (e & f) ^ (~e & g);
        unsigned long long t1 = h + s1 + ch + sha512_k[i] + w[i];
        unsigned long long s0 = SHA512_ROR(a, 28) ^ SHA512_ROR(a, 34) ^ SHA512_ROR(a, 39);
        unsigned long long mj = (a & b) ^ (a & cc) ^ (b & cc);
        unsigned long long t2 = s0 + mj;

        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g;  c->h[7] += h;
}

void sha512_init(sha512_ctx *c)
{
    static const unsigned long long iv[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };
    int i;
    for (i = 0; i < 8; i++) c->h[i] = iv[i];
    c->len = 0;
    c->buflen = 0;
}

void sha512_update(sha512_ctx *c, const void *data, unsigned long len)
{
    const unsigned char *p = data;

    c->len += len;
    while (len) {
        unsigned long take = 128 - c->buflen;
        if (take > len) take = len;
        sha512_copy(c->buf + c->buflen, p, take);
        c->buflen += take;
        p += take;
        len -= take;
        if (c->buflen == 128) {
            sha512_block(c, c->buf);
            c->buflen = 0;
        }
    }
}

void sha512_final(sha512_ctx *c, unsigned char *digest)
{
    unsigned long long bits = c->len * 8;
    unsigned char b = 0x80, z = 0;
    int i;

    sha512_update(c, &b, 1);
    while (c->buflen != 112) sha512_update(c, &z, 1);   /* 补到 128-16 */
    for (i = 0; i < 8; i++) sha512_update(c, &z, 1);    /* 长度高 64 位（够用，恒为 0） */
    for (i = 7; i >= 0; i--) {                          /* 长度低 64 位，大端 */
        b = (unsigned char)(bits >> (i * 8));
        sha512_update(c, &b, 1);
    }

    for (i = 0; i < 8; i++) {
        digest[i*8]   = (unsigned char)(c->h[i] >> 56);
        digest[i*8+1] = (unsigned char)(c->h[i] >> 48);
        digest[i*8+2] = (unsigned char)(c->h[i] >> 40);
        digest[i*8+3] = (unsigned char)(c->h[i] >> 32);
        digest[i*8+4] = (unsigned char)(c->h[i] >> 24);
        digest[i*8+5] = (unsigned char)(c->h[i] >> 16);
        digest[i*8+6] = (unsigned char)(c->h[i] >> 8);
        digest[i*8+7] = (unsigned char)c->h[i];
    }
}

void sha512_buf(const void *data, unsigned long len, unsigned char *digest)
{
    sha512_ctx c;

    sha512_init(&c);
    sha512_update(&c, data, len);
    sha512_final(&c, digest);
}


/* ===== 本工程包装 ===== */
void rc_aes256_ctr(const unsigned char key[32], const unsigned char nonce[16],
                   unsigned char *buf, unsigned long len)
{
    aes256_ctx c;
    unsigned char ctr[16], ks[16];
    unsigned long off = 0;
    aes256_init(&c, key);
    for (int i = 0; i < 12; i++) ctr[i] = nonce[i];
    unsigned int blk = ((unsigned int)nonce[12] << 24) | ((unsigned int)nonce[13] << 16)
                     | ((unsigned int)nonce[14] << 8) | (unsigned int)nonce[15];
    while (off < len) {
        ctr[12] = (unsigned char)(blk >> 24); ctr[13] = (unsigned char)(blk >> 16);
        ctr[14] = (unsigned char)(blk >> 8);  ctr[15] = (unsigned char)blk;
        aes256_encrypt_block(&c, ctr, ks);
        for (int i = 0; i < 16 && off < len; i++, off++) buf[off] ^= ks[i];
        blk++;
    }
}

static unsigned long long prng_state = 0x9E3779B97F4A7C15ull;
void rc_prng_seed(unsigned long long s) { prng_state = s ? s : 1; }
unsigned long long rc_prng_next(void) {
    unsigned long long x = prng_state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return (prng_state = x);
}

/* ============================================================================
 * 强密钥派生：HMAC-SHA256 与 PBKDF2-HMAC-SHA256（KDF v2）
 * 用途：口令/种子到 AES 密钥的派生。旧版是「一次 SHA-512」，现在改成
 * PBKDF2 迭代（次数见 S2A_KDF_ITERS），暴力破解成本提高几个数量级。
 * ========================================================================== */

void hmac_sha256(const void *key, unsigned long klen,
                 const void *msg, unsigned long mlen, unsigned char out[32])
{
    unsigned char k[64], ipad[64], opad[64], ih[32];
    sha256_ctx c;
    memset(k, 0, sizeof k);
    if (klen > 64) sha256_buf(key, klen, k);      /* 超块长先哈希 */
    else memcpy(k, key, klen);
    for (int i = 0; i < 64; i++) { ipad[i] = (unsigned char)(k[i] ^ 0x36); opad[i] = (unsigned char)(k[i] ^ 0x5c); }
    sha256_init(&c); sha256_update(&c, ipad, 64); sha256_update(&c, msg, mlen); sha256_final(&c, ih);
    sha256_init(&c); sha256_update(&c, opad, 64); sha256_update(&c, ih, 32);   sha256_final(&c, out);
    memset(k, 0, sizeof k); memset(ipad, 0, sizeof ipad);
    memset(opad, 0, sizeof opad); memset(ih, 0, sizeof ih);
}

void pbkdf2_sha256(const void *pw, unsigned long pwlen,
                   const void *salt, unsigned long saltlen,
                   unsigned long iters, unsigned char *out, unsigned long outlen)
{
    if (iters == 0) iters = 1;
    if (saltlen > 256) saltlen = 256;
    unsigned long blocks = (outlen + 31) / 32;
    for (unsigned long b = 1; b <= blocks; b++) {
        unsigned char sb[260], u[32], t[32], ib[4];
        ib[0] = (unsigned char)(b >> 24); ib[1] = (unsigned char)(b >> 16);
        ib[2] = (unsigned char)(b >> 8);  ib[3] = (unsigned char)b;
        memcpy(sb, salt, saltlen);
        memcpy(sb + saltlen, ib, 4);
        hmac_sha256(pw, pwlen, sb, saltlen + 4, u);
        memcpy(t, u, 32);
        for (unsigned long i = 1; i < iters; i++) {
            unsigned char prev[32];
            memcpy(prev, u, 32);
            hmac_sha256(pw, pwlen, prev, 32, u);   /* in-place 安全：先拷再算 */
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }
        unsigned long n = outlen - (b - 1) * 32;
        if (n > 32) n = 32;
        memcpy(out + (b - 1) * 32, t, n);
        memset(sb, 0, sizeof sb); memset(u, 0, sizeof u); memset(t, 0, sizeof t);
    }
}

void kdf_strong(const void *secret, unsigned long slen, const char *purpose,
                const void *salt, unsigned long saltlen, unsigned char out[32])
{
    unsigned char sb[320];
    unsigned long pl = purpose ? (unsigned long)strlen(purpose) : 0;
    if (pl > 48) pl = 48;
    if (saltlen > 256) saltlen = 256;
    if (purpose) memcpy(sb, purpose, pl);
    if (saltlen) memcpy(sb + pl, salt, saltlen);
    pbkdf2_sha256(secret, slen, sb, pl + saltlen, S2A_KDF_ITERS, out, 32);
    memset(sb, 0, sizeof sb);
}
