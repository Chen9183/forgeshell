/* protect.h —— 编译期防护（产物加壳/加密/完整性基线）
 *
 * 分层（见 DESIGN.md §6）：
 *   层-1（总是启用，密钥不落源码）：seed 来自 /dev/random，池密钥 = SHA-256(seed[0..31])
 *   层-2（--password 时启用）：Kp = SHA-512(密码)，用它加密 seed 本体 + 写“作者名标记”
 *   完整性：池明文 SHA-512、代码 SHA-512 作为运行期自校验基线
 */
#ifndef S2A_PROTECT_H
#define S2A_PROTECT_H

#include <stdint.h>
#include "../common/shell_ast.h"

typedef struct {
    unsigned char *pool;        /* 字符串池（原地加密） */
    uint32_t       pool_size;
    unsigned char *pool_sha512; /* 输出：64 字节（池明文摘要） */
    unsigned char *code_sha512; /* 输出：64 字节（生成代码摘要） */
    const unsigned char *code;
    uint32_t       code_size;
    unsigned char *seed;        /* 输出：64 字节 */
    unsigned char *marker;      /* 输出：校验料密文（AES-CTR，>=64 字节缓冲） */
    uint32_t      *marker_len;  /* 输出：密文长度 */
    unsigned char *author_check;/* 输出：16 字节打散后的校验常量（非明文） */
    const char    *author;      /* 参与派生，但**不写进产物** */
    const char    *password;    /* NULL = 不加密码锁 */
    uint32_t       prot_level;
    uint32_t       anti_action;
    uint64_t      *flags_out;   /* 输出：RIF_* */
    const char    *rnd_desc;    /* 调试：随机源描述 */
} s2a_seal_ctx;

int s2a_seal(s2a_seal_ctx *c);
int s2a_unseal_check(const s2a_seal_ctx *c, const char *password);   /* 自检：能否用该密码解出标记 */

#endif
