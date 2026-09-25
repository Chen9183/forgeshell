/* shell_ast.h —— 编译器与运行时共享的 AST/词 契约
 *
 * 这是一份「同一份定义、两个世界共用」的头文件：
 *   · 编译器（host）：解析脚本产出这些结构，并原样序列化进产物只读段；
 *   · 运行时（target，静态 musl 映像）：直接按这些结构求值；
 *   · 运行时解释器（eval / 动态 source）：用同一份解析器在运行时建同样的结构。
 *
 * 序列化约定：指针字段在产物里存放**绝对虚拟地址**（同地址空间，直接可用）；
 * 所有 const char* 指向**字符串池**（可写段，静态期 AES-256 加密，启动时解密）。
 */
#ifndef RC_SHELL_AST_H
#define RC_SHELL_AST_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 词与片段 ============================ */

typedef enum {
    RW_LIT = 0,   /* 字面量文本（引号已去） */
    RW_VAR,       /* $name / ${name}                     name = 变量名 */
    RW_SPECIAL,   /* $? $# $@ $* $! $- $$ $0 $1..$9      name = 单字符/数字串 */
    RW_CMDSUB,    /* $(...) / `...`                      sub = 子节点链表 */
    RW_ARITH,     /* $(( ... ))                          arith = 算术树 */
    RW_PARAM,     /* ${name<op>word}                     op/name/word/pat/rep */
    RW_TILDE,     /* ~ 或 ~user                          name = 用户名（可为空串） */
    RW_BRACE      /* {a,b} 花括号展开（未在编译期展开时保留） */
} rc_wseg_kind;

/* 参数展开操作（RW_PARAM 的 op） */
typedef enum {
    RP_NONE = 0,
    RP_LEN,          /* ${#v} */
    RP_DEFAULT,      /* ${v-w}  ${v:-w} */
    RP_ASSIGN,       /* ${v=w}  ${v:=w} */
    RP_ERROR,        /* ${v?w}  ${v:?w} */
    RP_ALT,          /* ${v+w}  ${v:+w} */
    RP_REMOVE_PRE_S, /* ${v#p}  */
    RP_REMOVE_PRE_L, /* ${v##p} */
    RP_REMOVE_SUF_S, /* ${v%p}  */
    RP_REMOVE_SUF_L, /* ${v%%p} */
    RP_SUBST,        /* ${v/p/r} ${v//p/r} ${v/#p/r} ${v/%p/r} */
    RP_SLICE,        /* ${v:off:len} */
    RP_UPPER,        /* ${v^^} ${v^}   GNU */
    RP_LOWER,        /* ${v,,} ${v,}   GNU */
    RP_INDIRECT,     /* ${!v}          GNU */
    RP_NAMES,        /* ${!prefix*}    GNU */
    RP_ARRAY_AT,     /* ${v[@]} ${v[*]} */
    RP_ARRAY_IDX     /* ${v[idx]} */
} rc_param_op;

/* RP 修饰位（rc_wseg.op 高位） */
#define RP_F_COLON    0x0001u  /* 带 ':'：空串视为未设置 */
#define RP_F_GLOBAL   0x0002u  /* ${v//p/r} */
#define RP_F_ANCHOR_P 0x0004u  /* ${v/#p/r} */
#define RP_F_ANCHOR_S 0x0008u  /* ${v/%p/r} */
#define RP_F_SUBSTR   0x0010u  /* ${v:off:len} */
#define RP_F_HAS_LEN  0x0020u  /* 切片给了 len */
#define RP_F_ALL      0x0040u  /* ${v[@]} 展开为全部元素 */
#define RP_F_PAT_QUOTED 0x0080u/* 模式被引号引住（按字面匹配） */

/* 词标志 */
#define RWF_QUOTED     0x01u  /* 词里出现过引号 */
#define RWF_LITERAL    0x02u  /* 全字面量：编译期可折叠 */
#define RWF_ASSIGN_RHS 0x04u  /* 赋值右值：不切分、不 glob */
#define RWF_HEREDOC    0x08u  /* here-doc 正文：只做参数/命令/算术展开 */
#define RWF_PATTERN    0x10u  /* case 模式 / ${v#pat}：抑制 glob，保留元字符 */

typedef struct rc_word  rc_word;
typedef struct rc_arith rc_arith;
typedef struct rc_node  rc_node;
typedef struct rc_test  rc_test;

/* 词片段：一个“词”= rc_wseg 单链表（顺序即拼接顺序） */
typedef struct rc_wseg {
    uint8_t     kind;    /* rc_wseg_kind */
    uint8_t     quoted;  /* 该片段处于引号内 */
    uint8_t     dquote;  /* 处于双引号内（"$@" 语义不同） */
    uint8_t     pad;
    uint32_t    op;      /* rc_param_op | RP_F_* */
    const char *name;    /* 变量名 / 字面量文本 / 波浪号用户名（NUL 结尾，位于字符串池） */
    uint32_t    namelen;
    int64_t     idx;     /* 数组下标 */
    /* 元字符模式（case / ${v#pat}）里需要保留 * ? [ ] 的原文；name 存放原文即可 */
    rc_word    *word;    /* ${v-WORD} 的 WORD */
    rc_word    *pat;     /* ${v/p/r} 的 p */
    rc_word    *rep;     /* ${v/p/r} 的 r */
    rc_node    *sub;     /* 命令替换的子节点链表 */
    rc_arith   *arith;   /* $(( )) 的算术树 */
    struct rc_wseg *next;
} rc_wseg;

struct rc_word {
    rc_wseg *segs;
    uint32_t flags;      /* RWF_* */
    uint32_t pad;
};

/* 词的展开结果 / 静态词表 */
typedef struct rc_words {
    char   **v;
    uint32_t n;
    uint32_t cap;
} rc_words;

/* ============================ 算术表达式树 ============================ */

typedef enum {
    RA_NUM = 0,   /* num */
    RA_VAR,       /* name */
    RA_UNARY,     /* op, a */
    RA_BINARY,    /* op, a, b */
    RA_TERNARY,   /* op='?', a, b, c */
    RA_ASSIGN,    /* op: '=' '+=' ... name, b */
    RA_INCDEC,    /* op: '+'/'-'，post 标记后置；name */
    RA_COMMA,     /* a, b */
    RA_CMDSUB     /* sub：算术里的 $(...) / `...`（bash：结果再当算术式求值） */
} rc_arith_kind;

/* 算术运算符编码（编译器与运行时共用，见 common/arith_op.h 的 op 表） */
enum {
    RAO_ADD = 1, RAO_SUB, RAO_MUL, RAO_DIV, RAO_MOD, RAO_POW,
    RAO_SHL, RAO_SHR, RAO_LT, RAO_LE, RAO_GT, RAO_GE, RAO_EQ, RAO_NE,
    RAO_BAND, RAO_BXOR, RAO_BOR, RAO_LAND, RAO_LOR,
    RAO_NOT, RAO_BNOT, RAO_PLUS, RAO_MINUS,
    RAO_ASG_ADD, RAO_ASG_SUB, RAO_ASG_MUL, RAO_ASG_DIV, RAO_ASG_MOD,
    RAO_ASG_SHL, RAO_ASG_SHR, RAO_ASG_BAND, RAO_ASG_BXOR, RAO_ASG_BOR,
    RAO_QUESTION, RAO_PREINC, RAO_PREDEC, RAO_POSTINC, RAO_POSTDEC, RAO_COMMAMM
};

struct rc_arith {
    uint8_t  kind;   /* rc_arith_kind */
    uint8_t  op;     /* RAO_* */
    uint8_t  post;   /* ++/-- 后缀形式 */
    uint8_t  pad;
    int64_t  num;
    const char *name;
    rc_arith *a, *b, *c;
    rc_node  *sub;   /* RA_CMDSUB：命令替换的子脚本 */
};

/* ============================ 重定向 ============================ */

typedef enum {
    RC_R_IN = 0, RC_R_OUT, RC_R_APP, RC_R_RDWR, RC_R_CLOBBER,
    RC_R_DUP_OUT, RC_R_DUP_IN, RC_R_CLOSE, RC_R_HEREDOC, RC_R_HERESTR,
    RC_R_OUT_ERR    /* &> / &>> （GNU） */
} rc_redir_op;

typedef struct rc_redir {
    uint8_t   op;        /* rc_redir_op */
    uint8_t   fd;        /* 目标 fd；0xFF = 用该操作默认 fd */
    uint8_t   fd2;       /* >&fd2 / <&fd2 的第二个 fd */
    uint8_t   hd_expand; /* here-doc 是否做参数展开 */
    rc_word  *target;    /* 文件名词 / here-string 词 */
    const char *hd;      /* here-doc 正文 */
    uint32_t  hd_len;
    uint32_t  pad;
    struct rc_redir *next;
} rc_redir;

/* ============================ 赋值 ============================ */

typedef struct rc_assign {
    const char *name;
    uint8_t     append;    /* += */
    uint8_t     is_arith;  /* value 是算术表达式 */
    uint8_t     is_array;  /* name=(a b c) */
    uint8_t     pad;
    rc_word    *value;
    rc_arith   *arith;
    rc_word   **array_items;  /* 数组字面量词表 */
    uint32_t    narray;
    uint32_t    pad2;
    struct rc_assign *next;
} rc_assign;

/* ============================ 测试表达式（test / [ / [[ ]]） ============================ */

typedef enum {
    RT_AND = 0, RT_OR, RT_NOT, RT_GROUP,
    RT_UNARY_FILE,   /* -f -d -e -r -w -x -s -L -b -c -p -S -h -t -u -g -k -O -G -N */
    RT_UNARY_STR,    /* -z -n */
    RT_UNARY_VAR,    /* -v -R （GNU） */
    RT_CMP_NUM,      /* -eq -ne -lt -le -gt -ge */
    RT_CMP_STR,      /* = == != < > （POSIX 的 < > 需转义） */
    RT_CMP_PAT,      /* [[ x == pat ]] 模式匹配 */
    RT_CMP_REGEX,    /* [[ x =~ re ]] （GNU） */
    RT_CMP_FILES,    /* -nt -ot -ef */
    RT_ARITH         /* (( )) / -eq 之类走算术 */
} rc_test_kind;

struct rc_test {
    uint8_t  kind;    /* rc_test_kind */
    uint8_t  op;      /* RT_UNARY_FILE/RT_CMP_* 的具体操作码（见 rt_test.c 表） */
    uint8_t  negate;  /* [[ ! ]] 的取反 */
    uint8_t  pad;
    rc_word *lhs, *rhs;
    rc_arith *arith;
    rc_test *a, *b;
};

/* ============================ 语句节点 ============================ */

typedef enum {
    RN_SIMPLE = 0,  /* 简单命令：argv + assigns + redirs */
    RN_LIST,        /* a ; b            u.a 左, u.b 右 */
    RN_ANDOR,       /* a && b / a || b  sub = RC_AND / RC_OR */
    RN_PIPE,        /* a | b | c        sub 记录是否 |& */
    RN_NOT,         /* ! a */
    RN_BG,          /* a & */
    RN_IF,          /* if cond; then a; elif…; else b; fi */
    RN_WHILE,       /* while/until   sub = RC_WHILE / RC_UNTIL */
    RN_FOR,         /* for name [in words]; do body; done */
    RN_CASE,        /* case word in pat) body;; esac */
    RN_SUBSHELL,    /* ( list ) */
    RN_GROUP,       /* { list; } */
    RN_FUNC,        /* name() { body } */
    RN_ARITH_CMD,   /* (( expr ))   GNU */
    RN_TEST_CMD     /* [[ expr ]]   GNU；独立 test/[/[ 仍走 RN_SIMPLE */
} rc_node_kind;

#define RC_AND   1
#define RC_OR    2
#define RC_WHILE 1
#define RC_UNTIL 2

#define RNF_PIPE_ERR 0x01u  /* |& */
#define RNF_NEGATE   0x02u  /* case 项的 ;& / ;;& （GNU） */

typedef struct rc_func_def { const char *name; rc_node *body; } rc_func_def;
typedef struct rc_case_item rc_case_item;
typedef struct rc_case_def { rc_word *word; rc_case_item *items; } rc_case_def;

typedef struct rc_case_item {
    rc_word  **pats;
    uint32_t   npats;
    uint32_t   pad;
    uint8_t    fallthrough;  /* ;&  */
    uint8_t    retest;       /* ;;& */
    uint16_t   pad2;
    rc_node   *body;
    rc_case_item *next;
} rc_case_item;

typedef struct rc_simple {
    rc_word  **argv;    /* argv[0] = 命令名 */
    uint32_t   argc;
    rc_assign *assigns; /* 前置/独立赋值 */
    rc_redir  *redirs;
} rc_simple;

typedef struct rc_for {
    const char *name;
    rc_word   **items;  /* NULL 且 has_in==0 → 隐含 in "$@" */
    uint32_t    nitems;
    uint8_t     has_in;
    uint8_t     pad[3];
    rc_node    *body;
} rc_for;

struct rc_node {
    uint8_t  kind;      /* rc_node_kind */
    uint8_t  flags;     /* RNF_* */
    uint16_t sub;       /* 子类型 */
    uint32_t line;      /* 源码行号 */
    rc_node *a, *b, *c; /* 通用：cond/body/else/step… */
    rc_node *next;      /* 同级链表 */
    rc_redir *redirs;   /* 复合命令自身的重定向（( ) { } if… 都可带） */
    union {
        rc_simple simple;
        rc_for    forc;
        rc_arith *arith;              /* RN_ARITH_CMD */
        rc_test  *test;               /* RN_TEST_CMD */
        rc_case_def casec;
        rc_func_def func;
    } u;
};

/* ============================ 产物映像描述 ============================ */

/* 防护动作（--anti-action） */
#define RC_ANTI_WARN     4u   /* 报告到 stderr 但继续执行（默认：便于确认检测确实生效） */
#define RC_ANTI_SILENT   0u   /* 静默退出（发行版常用，最隐蔽） */
#define RC_ANTI_REPORT   1u   /* 仅报告（用户想看“它发现了什么”） */
#define RC_ANTI_FAKE     2u   /* 假结果：继续跑但走混淆路径 */
#define RC_ANTI_DELAY    3u   /* 延迟后退出 */

typedef struct rc_image {
    uint32_t magic;        /* 'S2A2' */
    uint32_t version;
    const char *srcname;   /* 原脚本名 */
    const char *author;    /* 作者署名 @Chen9183 (github) */
    uint64_t flags;        /* RIF_* */

    /* 字面量字符串池：可写段；静态期 AES-256-CTR 加密（必要时再套密码层） */
    char    *pool;
    uint32_t pool_size;
    uint32_t pool_pad;
    const unsigned char *pool_sha512;   /* 64 字节：解密后池内容的 SHA-512（完整性基线） */

    /* 每构建随机量（从 /dev/random 读，不写进编译器源码）：
     *   seed[0..31]  = 层-1 密钥材料（key = SHA-256(seed[0..31])）
     *   seed[32..63] = 层-1 nonce
     * 有 --password 时，seed 本体也被密码派生密钥加密（无密码拿不到层-1 密钥）。 */
    unsigned char seed[64];

    /* 密码校验（产物里**没有任何明文**）：
     *   marker        = AES-256-CTR(Kp, nonce, V)   —— V 是每构建随机的 16 字节校验料
     *   author_check  = 栅栏密码 2 轨 + 字节异或( SHA-256(V ‖ seed)[:16], K )
     * 运行期：Kp= SHA-512(密码) 解密 marker 得 V'，再算同样的校验值与 author_check 比对；
     * 密码错误 → V' 是乱码 → 校验不符 → 报错退出。K = SHA-256(seed)[:16]（每构建随机）。 */
    const unsigned char *marker;      /* 密文（16/32 字节） */
    uint32_t marker_len;
    uint32_t marker_pad;
    const unsigned char *author_check; /* 16 字节：打散后的校验常量（非明文） */

    /* 生成代码范围（软件断点扫描 / 自校验用） */
    const unsigned char *code;
    uint32_t code_size;
    uint32_t code_pad;
    const unsigned char *code_sha512;

    /* 运行参数 */
    const char *workdir;
    uint32_t prot_level;
    uint32_t opt_level;
    uint32_t anti_action;
    uint32_t n_nodes;
    uint32_t n_words;
    uint32_t n_strings;
    uint32_t pad2;
} rc_image;

#define RIF_PROTECTED  0x01u
#define RIF_AES_POOL   0x02u
#define RIF_REPORT     0x04u
#define RIF_LOCKED     0x08u   /* 需要 --password 才能运行 */
#define RIF_SELFCHECK  0x10u

#ifdef __cplusplus
}
#endif
#endif /* RC_SHELL_AST_H */
