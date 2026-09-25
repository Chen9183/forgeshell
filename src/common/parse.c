/* parse.c —— shell 前端：词法分析 + 递归下降语法分析（POSIX sh 文法 + 常用 GNU 扩展）
 *
 * 这一份代码被编译两次：一次进编译器（host），一次进运行时映像（target，用于 eval/source），
 * 所以「编译期语法」与「运行期语法」永远一致。
 *
 * 覆盖：列表/与或/管道/取反/后台；简单命令（前置赋值、重定向、词表）；
 *       if-elif-else / while / until / for-in / case（| 多模式、;; ;& ;;&）；
 *       子 shell ( ) / 命令组 { } / 函数定义；( ( ) ) 算术命令 / [[ ]] 测试命令；
 *       引用：'...' "..." \ $'...' $"..."；展开：$var ${...} $(...) `...` $((...)) ~；
 *       重定向全套 + here-doc(<< <<-) + here-string(<<<)。
 *
 * 已知取舍（会打印诊断而不是静默出错）：${v:off:len} 的 off/len 必须是常量或简单 $var；
 *       数组（v[i] / v[@]）保留语法但语义退化为整体值；$'...' 只处理常用转义。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "parse.h"

/* ============================ 基础工具 ============================ */
static void *xcalloc(size_t n, size_t sz)
{
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) { fprintf(stderr, "s2a: 内存不足\n"); exit(125); }
    return p;
}

static char *dupn(const char *s, size_t n)
{
    char *p = xcalloc(n + 1, 1);
    memcpy(p, s, n);
    return p;
}

/* ============================ 词法 ============================ */
enum {
    TK_EOF = 0, TK_NEWLINE, TK_WORD,
    TK_SEMI, TK_AMP, TK_ANDAND, TK_OROR, TK_PIPE, TK_PIPEBOTH,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_DSEMI, TK_SEMIAMP, TK_DSEMIAMP,
    TK_LT, TK_GT, TK_DGT, TK_DLESS, TK_DLESSDASH, TK_TLESS,
    TK_LESSAND, TK_GREATAND, TK_LESSGREAT, TK_CLOBBER, TK_ANDGT, TK_ANDGTGT,
    TK_BANG
};

typedef struct {
    int kind;
    rc_word *word;
    char *text;          /* 词的纯文本（用于关键字判定/函数名） */
    int line, col;
    int heredoc_index;   /* >> / << 对应的 here-doc 槽位，-1 = 无 */
} tok;

typedef struct {
    const char *src;
    size_t len, pos;
    int line, col;
    const char *name;
    rc_diag *diags;
    int diags_cap, ndiags;
    int had_error;
} lexer;

static void diag(lexer *L, int line, int col, const char *fmt, ...)
{
    if (L->ndiags >= L->diags_cap) return;
    rc_diag *d = &L->diags[L->ndiags];
    memset(d, 0, sizeof *d);
    d->line = line; d->col = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->msg, sizeof d->msg, fmt, ap);
    va_end(ap);
    /* 取出错行源码 */
    int ln = 1;
    const char *start = L->src;
    for (const char *p = L->src; *p; p++) {
        if (ln == line) { start = p; break; }
        if (*p == '\n') { ln++; start = p + 1; }
    }
    if (ln == line) {
        size_t k = 0;
        for (const char *p = start; *p && *p != '\n' && k + 1 < sizeof d->src_line; p++) d->src_line[k++] = *p;
        d->src_line[k] = 0;
    }
    L->ndiags++;
    L->had_error = 1;
}

static int peek(lexer *L) { return L->pos < L->len ? (unsigned char)L->src[L->pos] : -1; }
static int peek2(lexer *L) { return L->pos + 1 < L->len ? (unsigned char)L->src[L->pos + 1] : -1; }
static int peekn(lexer *L, size_t k) { return L->pos + k < L->len ? (unsigned char)L->src[L->pos + k] : -1; }
static int next_ch(lexer *L)
{
    if (L->pos >= L->len) return -1;
    int c = (unsigned char)L->src[L->pos++];
    if (c == '\n') { L->line++; L->col = 1; } else L->col++;
    return c;
}

/* ---- 片段构造 ---- */
static rc_wseg *seg_new(int kind)
{
    rc_wseg *s = xcalloc(1, sizeof *s);
    s->kind = (uint8_t)kind;
    return s;
}

static void word_push(rc_word *w, rc_wseg **tail, rc_wseg *s)
{
    s->next = NULL;
    if (*tail) (*tail)->next = s;
    else w->segs = s;
    *tail = s;
}

static void word_push_lit(rc_word *w, rc_wseg **tail, const char *text, size_t n, int quoted, int dquote)
{
    if (n == 0) return;
    /* 与上一个同引号状态的字面量合并，减少片段数 */
    if (*tail && (*tail)->kind == RW_LIT && (*tail)->quoted == quoted && (*tail)->dquote == dquote) {
        size_t old = (*tail)->namelen;
        char *merged = xcalloc(old + n + 1, 1);
        memcpy(merged, (*tail)->name, old);
        memcpy(merged + old, text, n);
        free((char *)(*tail)->name);
        (*tail)->name = merged;
        (*tail)->namelen = (uint32_t)(old + n);
        return;
    }
    rc_wseg *s = seg_new(RW_LIT);
    s->quoted = (uint8_t)quoted;
    s->dquote = (uint8_t)dquote;
    s->name = dupn(text, n);
    s->namelen = (uint32_t)n;
    word_push(w, tail, s);
}

/* ---- 算术表达式解析（$(( )) 与 (( ))）---- */
typedef struct { const char *s; size_t pos, len; int failed; } aparser;

static void askip(aparser *A) { while (A->pos < A->len && isspace((unsigned char)A->s[A->pos])) A->pos++; }
static int apeek(aparser *A) { return A->pos < A->len ? (unsigned char)A->s[A->pos] : -1; }
static int apeekn(aparser *A, size_t k) { return A->pos + k < A->len ? (unsigned char)A->s[A->pos + k] : -1; }

static rc_arith *a_node(int kind) { rc_arith *a = xcalloc(1, sizeof *a); a->kind = (uint8_t)kind; return a; }

static rc_arith *a_assign(aparser *A);
static rc_arith *a_ternary(aparser *A);
static rc_node *parse_script_text(const char *txt);
static rc_node *parse_list_inner(lexer *L);

/* 把一段源码文本解析成语句链（供算术里的 $(...) 使用） */
static rc_node *parse_script_text(const char *txt)
{
    if (!txt) return NULL;
    lexer sub;
    memset(&sub, 0, sizeof sub);
    sub.src = txt;
    sub.len = strlen(txt);
    sub.pos = 0;
    sub.line = 1;
    sub.col = 1;
    sub.name = "<arith-subst>";
    return parse_list_inner(&sub);
}

static rc_arith *a_primary(aparser *A)
{
    askip(A);
    int c = apeek(A);
    if (c == '(') {
        A->pos++;
        rc_arith *e = a_assign(A);
        askip(A);
        if (apeek(A) == ')') A->pos++;
        return e;
    }
    if (c == '`') {                     /* `...` 命令替换 */
        A->pos++;
        size_t st = A->pos;
        while (A->pos < A->len) {
            if (A->s[A->pos] == '\\') { A->pos += 2; continue; }
            if (A->s[A->pos] == '`') break;
            A->pos++;
        }
        char *bt = dupn(A->s + st, A->pos > st ? A->pos - st : 0);
        if (A->pos < A->len && A->s[A->pos] == '`') A->pos++;
        rc_arith *bn = a_node(RA_CMDSUB);
        bn->sub = parse_script_text(bt);
        free(bt);
        return bn;
    }
    if (c == '$' && apeekn(A, 1) == '(') {   /* $(...) 命令替换 */
        A->pos += 2;
        size_t st = A->pos;
        int depth = 1;
        while (A->pos < A->len) {
            char ch = A->s[A->pos];
            if (ch == '\\') { A->pos += 2; continue; }
            if (ch == '\'') { A->pos++; while (A->pos < A->len && A->s[A->pos] != '\'') A->pos++; A->pos++; continue; }
            if (ch == '(') depth++;
            else if (ch == ')') { depth--; if (depth == 0) break; }
            A->pos++;
        }
        char *txt = dupn(A->s + st, A->pos > st ? A->pos - st : 0);
        if (A->pos < A->len && A->s[A->pos] == ')') A->pos++;
        rc_arith *n = a_node(RA_CMDSUB);
        n->sub = parse_script_text(txt);
        free(txt);
        return n;
    }
    if (c == '$') {                     /* $var */
        A->pos++;
        if (apeek(A) == '{') {
            A->pos++;
            size_t st = A->pos;
            while (A->pos < A->len && A->s[A->pos] != '}') A->pos++;
            char *nm = dupn(A->s + st, A->pos - st);
            if (apeek(A) == '}') A->pos++;
            rc_arith *n = a_node(RA_VAR);
            n->name = nm;
            return n;
        }
        size_t st = A->pos;
        while (A->pos < A->len && (isalnum((unsigned char)A->s[A->pos]) || A->s[A->pos] == '_'
                                   || strchr("?#@*!$-", A->s[A->pos]))) A->pos++;
        char *nm = dupn(A->s + st, A->pos - st);
        rc_arith *n = a_node(RA_VAR);
        n->name = nm;
        return n;
    }
    if (c == '\'' || c == '"') {         /* 引号里的数字 */
        int q = c;
        A->pos++;
        size_t st = A->pos;
        while (A->pos < A->len && A->s[A->pos] != q) A->pos++;
        char *txt = dupn(A->s + st, A->pos - st);
        if (apeek(A) == q) A->pos++;
        rc_arith *n = a_node(RA_NUM);
        n->num = strtoll(txt, NULL, 0);
        free(txt);
        return n;
    }
    if (c == '#' ) { /* 少见：进制前缀已在 strtoll 里处理 */ }
    if (isdigit(c)) {
        size_t st = A->pos;
        /* 支持 16#ff 形式 */
        while (A->pos < A->len && (isalnum((unsigned char)A->s[A->pos]) || A->s[A->pos] == '#')) A->pos++;
        char *txt = dupn(A->s + st, A->pos - st);
        long long v = 0;
        char *hash = strchr(txt, '#');
        if (hash) {
            int base = atoi(txt);
            v = strtoll(hash + 1, NULL, base ? base : 10);
        } else {
            v = strtoll(txt, NULL, 0);
        }
        free(txt);
        rc_arith *n = a_node(RA_NUM);
        n->num = v;
        return n;
    }
    if (isalpha(c) || c == '_') {        /* 变量名（无 $，算术里允许） */
        size_t st = A->pos;
        while (A->pos < A->len && (isalnum((unsigned char)A->s[A->pos]) || A->s[A->pos] == '_')) A->pos++;
        char *nm = dupn(A->s + st, A->pos - st);
        rc_arith *n = a_node(RA_VAR);
        n->name = nm;
        return n;
    }
    A->failed = 1;
    return a_node(RA_NUM);
}

static rc_arith *a_postfix(aparser *A)
{
    rc_arith *e = a_primary(A);
    for (;;) {
        askip(A);
        if (A->pos + 1 < A->len && A->s[A->pos] == '+' && A->s[A->pos + 1] == '+' && e->kind == RA_VAR) {
            A->pos += 2;
            rc_arith *n = a_node(RA_INCDEC);
            n->op = RAO_POSTINC; n->post = 1; n->name = e->name;
            e = n;
            continue;
        }
        if (A->pos + 1 < A->len && A->s[A->pos] == '-' && A->s[A->pos + 1] == '-' && e->kind == RA_VAR) {
            A->pos += 2;
            rc_arith *n = a_node(RA_INCDEC);
            n->op = RAO_POSTDEC; n->post = 1; n->name = e->name;
            e = n;
            continue;
        }
        break;
    }
    return e;
}

static rc_arith *a_unary(aparser *A)
{
    askip(A);
    int c = apeek(A);
    if (c == '!') { A->pos++; rc_arith *n = a_node(RA_UNARY); n->op = RAO_NOT; n->a = a_unary(A); return n; }
    if (c == '~') { A->pos++; rc_arith *n = a_node(RA_UNARY); n->op = RAO_BNOT; n->a = a_unary(A); return n; }
    if (c == '+' && apeekn(A, 1) == '+') {
        A->pos += 2;
        rc_arith *v = a_unary(A);
        rc_arith *n = a_node(RA_INCDEC);
        n->op = RAO_PREINC; n->name = v->name;
        return n;
    }
    if (c == '+') { A->pos++; rc_arith *n = a_node(RA_UNARY); n->op = RAO_PLUS; n->a = a_unary(A); return n; }
    if (c == '-') { A->pos++; rc_arith *n = a_node(RA_UNARY); n->op = RAO_MINUS; n->a = a_unary(A); return n; }

    if (c == '-' && apeekn(A, 1) == '-') {
        A->pos += 2;
        rc_arith *v = a_unary(A);
        rc_arith *n = a_node(RA_INCDEC);
        n->op = RAO_PREDEC; n->name = v->name;
        return n;
    }
    return a_postfix(A);
}

static rc_arith *a_binary(aparser *A, rc_arith *(*sub)(aparser *), const char *const *ops, const uint8_t *codes, int nops)
{
    rc_arith *l = sub(A);
    for (;;) {
        askip(A);
        int matched = 0;
        for (int i = 0; i < nops; i++) {
            size_t l2 = strlen(ops[i]);
            if (A->pos + l2 <= A->len && strncmp(A->s + A->pos, ops[i], l2) == 0) {
                /* 单字符运算符不能是更长运算符的前缀：`&&` 被 `&` 吃掉会让
                   `$(( 1 && 2 ))` 变成 `1 & (&2)` → 0 */
                if (l2 == 1) {
                    char nx = A->s[A->pos + 1];
                    /* 同字符（`&` vs `&&`）或跟着 '='（`+` vs `+=`、`*` vs `*=`）都不算本层运算符，
                       必须让上层 a_assign 去处理，否则 `m += 4` 被 a_add 吃成 `m + (= 4)`。 */
                    if (nx == ops[i][0] || nx == '=') continue;
                }
                A->pos += l2;
                rc_arith *n = a_node(RA_BINARY);
                n->op = codes[i];
                n->a = l;
                n->b = sub(A);
                l = n;
                matched = 1;
                break;
            }
        }
        if (!matched) return l;
    }
}

/* ** 幂：bash 里右结合，且优先级高于乘除（`2**3**2` = 512） */
static rc_arith *a_pow(aparser *A)
{
    rc_arith *l = a_unary(A);
    askip(A);
    if (A->pos + 1 < A->len && A->s[A->pos] == '*' && A->s[A->pos + 1] == '*') {
        A->pos += 2;
        rc_arith *n = a_node(RA_BINARY);
        n->op = RAO_POW;
        n->a = l;
        n->b = a_pow(A);          /* 右结合 */
        return n;
    }
    return l;
}

static rc_arith *a_mul(aparser *A)   { static const char *o[] = {"*","/","%"}; static const uint8_t c[] = {RAO_MUL,RAO_DIV,RAO_MOD}; return a_binary(A, a_pow, o, c, 3); }
static rc_arith *a_add(aparser *A)   { static const char *o[] = {"+","-"}; static const uint8_t c[] = {RAO_ADD,RAO_SUB}; return a_binary(A, a_mul, o, c, 2); }
static rc_arith *a_shift(aparser *A) { static const char *o[] = {"<<",">>"}; static const uint8_t c[] = {RAO_SHL,RAO_SHR}; return a_binary(A, a_add, o, c, 2); }
static rc_arith *a_rel(aparser *A)   { static const char *o[] = {"<=",">=","<",">"}; static const uint8_t c[] = {RAO_LE,RAO_GE,RAO_LT,RAO_GT}; return a_binary(A, a_shift, o, c, 4); }
static rc_arith *a_eq(aparser *A)    { static const char *o[] = {"==","!="}; static const uint8_t c[] = {RAO_EQ,RAO_NE}; return a_binary(A, a_rel, o, c, 2); }
static rc_arith *a_band(aparser *A)  { static const char *o[] = {"&"}; static const uint8_t c[] = {RAO_BAND}; return a_binary(A, a_eq, o, c, 1); }
static rc_arith *a_bxor(aparser *A)  { static const char *o[] = {"^"}; static const uint8_t c[] = {RAO_BXOR}; return a_binary(A, a_band, o, c, 1); }
static rc_arith *a_bor(aparser *A)   { static const char *o[] = {"|"}; static const uint8_t c[] = {RAO_BOR}; return a_binary(A, a_bxor, o, c, 1); }
static rc_arith *a_land(aparser *A)  { static const char *o[] = {"&&"}; static const uint8_t c[] = {RAO_LAND}; return a_binary(A, a_bor, o, c, 1); }
static rc_arith *a_lor(aparser *A)   { static const char *o[] = {"||"}; static const uint8_t c[] = {RAO_LOR}; return a_binary(A, a_land, o, c, 1); }

static rc_arith *a_ternary(aparser *A)
{
    rc_arith *c = a_lor(A);
    askip(A);
    if (apeek(A) == '?') {
        A->pos++;
        rc_arith *n = a_node(RA_TERNARY);
        n->op = RAO_QUESTION;
        n->a = c;
        n->b = a_assign(A);
        askip(A);
        if (apeek(A) == ':') A->pos++;
        n->c = a_assign(A);
        return n;
    }
    return c;
}

static rc_arith *a_assign(aparser *A)
{
    rc_arith *l = a_ternary(A);
    askip(A);
    static const struct { const char *op; uint8_t code; } tbl[] = {
        { "<<=", RAO_ASG_SHL }, { ">>=", RAO_ASG_SHR },
        { "+=", RAO_ASG_ADD }, { "-=", RAO_ASG_SUB }, { "*=", RAO_ASG_MUL },
        { "/=", RAO_ASG_DIV }, { "%=", RAO_ASG_MOD }, { "&=", RAO_ASG_BAND },
        { "^=", RAO_ASG_BXOR }, { "|=", RAO_ASG_BOR }, { "=", '=' },
    };
    for (size_t i = 0; i < sizeof tbl / sizeof tbl[0]; i++) {
        size_t l2 = strlen(tbl[i].op);
        if (A->pos + l2 <= A->len && strncmp(A->s + A->pos, tbl[i].op, l2) == 0) {
            if (tbl[i].code == '=' && A->pos + 1 < A->len && A->s[A->pos + 1] == '=') continue;
            if (l->kind != RA_VAR) return l;
            A->pos += l2;
            rc_arith *n = a_node(RA_ASSIGN);
            n->op = tbl[i].code;
            n->name = l->name;
            n->b = a_assign(A);
            return n;
        }
    }
    return l;
}

static rc_arith *a_comma(aparser *A)
{
    rc_arith *l = a_assign(A);
    askip(A);
    while (apeek(A) == ',') {
        A->pos++;
        rc_arith *n = a_node(RA_COMMA);
        n->a = l;
        n->b = a_assign(A);
        l = n;
        askip(A);
    }
    return l;
}

rc_arith *parse_arith_text(const char *s, size_t len)   /* 运行时也要用（rt_arith_str） */
{
    aparser A = { s, 0, len, 0 };
    rc_arith *e = a_comma(&A);
    return e;
}

/* ---- 递归扫描文本：找到 $(...) / ${...} / $((...)) / `...` 的边界 ---- */
/* 返回结束位置（指向闭合符之后），并把内容拷出 */
/* 有些扫描为了省事直接改 L->pos（scan_balanced / $((...)) 等），事后再把 line/col
   按 pos 重算 —— 不然之后的 token 行列号会错位，而 (( )) 的原文切片依赖列号，
   切片一错，里面的命令替换就解析成空。 */
static void lex_resync(lexer *L)
{
    if (!L || !L->src) return;
    int ln = 1, col = 1;
    for (size_t i = 0; i < L->pos && i < L->len; i++) {
        if (L->src[i] == '\n') { ln++; col = 1; } else col++;
    }
    L->line = ln; L->col = col;
}

static size_t scan_balanced(lexer *L, char open, char close, char **out)
{
    size_t st = L->pos;
    int depth = 1;
    int case_depth = 0;      /* `case x in a) ... esac` 里的 `)` 不是闭合括号 */
    while (L->pos < L->len) {
        int c = (unsigned char)L->src[L->pos];
        if (c == '\\' && L->pos + 1 < L->len) { L->pos += 2; continue; }
        if (c == '\'') {                       /* 单引号内不解析 */
            L->pos++;
            while (L->pos < L->len && L->src[L->pos] != '\'') L->pos++;
            if (L->pos < L->len) L->pos++;
            continue;
        }
        if (c == '"') {
            L->pos++;
            while (L->pos < L->len && L->src[L->pos] != '"') {
                if (L->src[L->pos] == '\\' && L->pos + 1 < L->len) L->pos++;
                L->pos++;
            }
            if (L->pos < L->len) L->pos++;
            continue;
        }
        if (isalpha(c) || c == '_') {           /* 跟踪 case/esac 关键词 */
            size_t w0 = L->pos;
            while (L->pos < L->len && (isalnum((unsigned char)L->src[L->pos]) || L->src[L->pos] == '_')) L->pos++;
            size_t wl = L->pos - w0;
            if (wl == 4 && strncmp(L->src + w0, "case", 4) == 0) case_depth++;
            else if (wl == 4 && strncmp(L->src + w0, "esac", 4) == 0 && case_depth > 0) case_depth--;
            continue;
        }
        if (c == open) depth++;
        else if (c == close) {
            if (case_depth > 0 && depth == 1) { L->pos++; continue; }   /* 模式收尾的 ')' */
            depth--;
            if (depth == 0) {
                *out = dupn(L->src + st, L->pos - st);
                L->pos++;
                lex_resync(L);
                return L->pos;
            }
        }
        L->pos++;
    }
    lex_resync(L);
    return 0;
}

/* 反引号命令替换 */
static char *scan_backtick(lexer *L)
{
    size_t st = ++L->pos;   /* 跳过 ` */
    while (L->pos < L->len) {
        if (L->src[L->pos] == '\\' && L->pos + 1 < L->len) { L->pos += 2; continue; }
        if (L->src[L->pos] == '`') {
            char *out = dupn(L->src + st, L->pos - st);
            L->pos++;
            return out;
        }
        L->pos++;
    }
    return dupn(L->src + st, L->pos - st);
}

/* 语法分析的解析器状态（token 流游标） */
typedef struct { lexer *L; tok *t; int n, pos, in_func; } parser;

static rc_node *parse_list_inner(lexer *L);
static rc_node *parse_list_tokens(parser *P);
static rc_node *parse_list_tokens_until(parser *P, const char *stop);
static rc_node *parse_and_or_c(parser *P);
static rc_node *parse_list_tokens_until_multi(parser *P, const char **stops);
static rc_node *parse_pipe_c(parser *P);
static int lex_tokenize(lexer *L, tok **out_toks, int *out_n);

/* 解析 ${...} 内容为一个片段 */
static rc_wseg *parse_brace_expansion(lexer *L, int quoted, int dquote)
{
    /* 进入时 L->pos 指向 '{' 之后 */
    rc_wseg *s = seg_new(RW_PARAM);
    s->quoted = (uint8_t)quoted;
    s->dquote = (uint8_t)dquote;

    int indirect = 0, len_op = 0;
    if (peek(L) == '#') { next_ch(L); len_op = 1; }
    else if (peek(L) == '!') { next_ch(L); indirect = 1; }

    /* 变量名 / 特殊参数 */
    char nb[128];
    size_t nl = 0;
    if (peek(L) == '@' || peek(L) == '*' || peek(L) == '?' || peek(L) == '-' ||
        peek(L) == '$' || peek(L) == '!' || peek(L) == '#') {
        nb[nl++] = (char)next_ch(L);
    } else {
        while (nl + 1 < sizeof nb && (isalnum(peek(L)) || peek(L) == '_')) nb[nl++] = (char)next_ch(L);
        while (nl + 1 < sizeof nb && isdigit(peek(L)) && nl == 0) nb[nl++] = (char)next_ch(L);
    }
    nb[nl] = 0;
    s->name = dupn(nb, nl);
    s->namelen = (uint32_t)nl;

    /* 数组下标 */
    if (peek(L) == '[') {
        next_ch(L);
        char ib[64]; size_t il = 0;
        while (il + 1 < sizeof ib && peek(L) != ']' && peek(L) > 0) ib[il++] = (char)next_ch(L);
        ib[il] = 0;
        if (peek(L) == ']') next_ch(L);
        if (!strcmp(ib, "@") || !strcmp(ib, "*")) { s->op = RP_ARRAY_AT | (RP_F_ALL << 8); }
        else { s->op = RP_ARRAY_IDX; s->idx = strtoll(ib, NULL, 10); }
        if (peek(L) == '}') { next_ch(L); return s; }
    }

    if (len_op) {
        s->op = RP_LEN;
        /* ${#*} / ${#@}：把名字记下来，运行期才知道要按「$* 拼接后的长度」算（POSIX/dash） */
        if (peek(L) == '*' || peek(L) == '@') {
            char nb[2] = { (char)next_ch(L), 0 };
            s->name = dupn(nb, 1);
        }
        if (peek(L) == '}') next_ch(L);
        return s;
    }
    if (indirect) {
        if (peek(L) == '}') { next_ch(L); s->op = RP_INDIRECT; return s; }
        /* ${!prefix*} / ${!prefix@} */
        s->op = RP_NAMES;
        while (peek(L) > 0 && peek(L) != '}') next_ch(L);
        if (peek(L) == '}') next_ch(L);
        return s;
    }

    int colon = 0;
    if (peek(L) == ':') {
        /* 可能是 :-, :=, :?, :+, :off:len */
        int c2 = peek2(L);
        if (c2 == '-' || c2 == '=' || c2 == '?' || c2 == '+') {
            next_ch(L); colon = 1;
        } else {
            /* 切片 ${v:off[:len]} —— off/len 支持常量或 $var（取整数值） */
            next_ch(L);
            s->op = RP_SLICE;          /* ⚑ 修饰位必须放高位（运行期读 op>>8），
                                          以前写成低字节 → op 值对不上 → 切片原样返回 */
            long vals[2] = { 0, 0 };
            for (int k = 0; k < 2; k++) {
                int neg = 0;
                while (peek(L) == ' ') next_ch(L);
                if (peek(L) == '-') { neg = 1; next_ch(L); }
                if (peek(L) == '$') {
                    /* $var / ${var}：留待运行期（这里只支持 $var 的整数值，否则记 0） */
                    next_ch(L);
                    if (peek(L) == '{') { while (peek(L) > 0 && peek(L) != '}') next_ch(L); if (peek(L)=='}') next_ch(L); }
                    else while (isalnum(peek(L)) || peek(L) == '_') next_ch(L);
                    vals[k] = 0;
                } else {
                    long v = 0;
                    while (isdigit(peek(L))) v = v * 10 + (next_ch(L) - '0');
                    vals[k] = neg ? -v : v;
                }
                if (k == 0 && peek(L) == ':') { next_ch(L); s->op |= (RP_F_HAS_LEN << 8); continue; }
                break;
            }
            s->idx = vals[0];
            s->namelen = (uint32_t)vals[1];
            if (peek(L) == '}') next_ch(L);
            return s;
        }
    }

    int c = peek(L);
    switch (c) {
    case '}':
        next_ch(L);
        s->op = RP_NONE;
        return s;
    case '-': case '=': case '?': case '+':
        next_ch(L);
        s->op = (c == '-') ? RP_DEFAULT : (c == '=') ? RP_ASSIGN : (c == '?') ? RP_ERROR : RP_ALT;
        if (colon) s->op |= (RP_F_COLON << 8);
        break;
    case '#':
        next_ch(L);
        if (peek(L) == '#') { next_ch(L); s->op = RP_REMOVE_PRE_L; }
        else s->op = RP_REMOVE_PRE_S;
        break;
    case '%':
        next_ch(L);
        if (peek(L) == '%') { next_ch(L); s->op = RP_REMOVE_SUF_L; }
        else s->op = RP_REMOVE_SUF_S;
        break;
    case '/': {
        next_ch(L);
        s->op = RP_SUBST;
        if (peek(L) == '/') { next_ch(L); s->op |= (RP_F_GLOBAL << 8); }
        else if (peek(L) == '#') { next_ch(L); s->op |= (RP_F_ANCHOR_P << 8); }
        else if (peek(L) == '%') { next_ch(L); s->op |= (RP_F_ANCHOR_S << 8); }
        /* pat 到下一个未转义的 '/' 或 '}' */
        {
            char pb[1024]; size_t pl = 0;
            while (peek(L) > 0 && peek(L) != '/' && peek(L) != '}') {
                if (peek(L) == '\\' && peek2(L) > 0) { if (pl + 1 < sizeof pb) pb[pl++] = (char)next_ch(L); }
                if (pl + 1 < sizeof pb) pb[pl++] = (char)next_ch(L); else next_ch(L);
            }
            pb[pl] = 0;
            rc_word *pw = xcalloc(1, sizeof *pw);
            rc_wseg *ps = seg_new(RW_LIT), *pt = NULL;
            ps->name = dupn(pb, pl);
            ps->namelen = (uint32_t)pl;
            pw->segs = ps; pw->flags = RWF_PATTERN;
            (void)pt;
            s->pat = pw;
            if (peek(L) == '/') {
                next_ch(L);
                char rb[1024]; size_t rl = 0;
                while (peek(L) > 0 && peek(L) != '}') {
                    if (peek(L) == '\\' && peek2(L) > 0) { if (rl + 1 < sizeof rb) rb[rl++] = (char)next_ch(L); }
                    if (rl + 1 < sizeof rb) rb[rl++] = (char)next_ch(L); else next_ch(L);
                }
                rb[rl] = 0;
                rc_word *rw2 = xcalloc(1, sizeof *rw2);
                rc_wseg *rs = seg_new(RW_LIT);
                rs->name = dupn(rb, rl);
                rs->namelen = (uint32_t)rl;
                rw2->segs = rs; rw2->flags = RWF_LITERAL;
                s->rep = rw2;
            }
        }
        if (peek(L) == '}') next_ch(L);
        return s;
    }
    case '^': {
        next_ch(L);
        s->op = RP_UPPER;
        if (peek(L) == '^') { next_ch(L); s->op |= (RP_F_ALL << 8); }   /* ^^ 全转 */
        if (peek(L) == '}') next_ch(L);
        return s;
    }
    case ',': {
        next_ch(L);
        s->op = RP_LOWER;
        if (peek(L) == ',') { next_ch(L); s->op |= (RP_F_ALL << 8); }
        if (peek(L) == '}') next_ch(L);
        return s;
    }
    default: {
        /* ${v:...} 之外的不认识形式：跳过后按 RP_NONE 处理 */
        while (peek(L) > 0 && peek(L) != '}') next_ch(L);
        if (peek(L) == '}') next_ch(L);
        s->op = RP_NONE;
        return s;
    }
    }

    /* 带默认值/替换值的操作：剩下的内容是一个“词” */
    rc_word *sub = xcalloc(1, sizeof *sub);
    rc_wseg *tail = NULL;
    char buf[2048];
    size_t bl = 0;
    int depth = 1;
    while (peek(L) > 0) {
        int ch2 = peek(L);
        if (ch2 == '{') depth++;
        if (ch2 == '}') {
            depth--;
            if (depth == 0) { next_ch(L); break; }
        }
        if (ch2 == '\'' ) {         /* 单引号内原样 */
            if (bl) { word_push_lit(sub, &tail, buf, bl, 0, 0); bl = 0; }
            next_ch(L);
            char qb[2048]; size_t ql = 0;
            while (peek(L) > 0 && peek(L) != '\'') qb[ql++] = (char)next_ch(L);
            if (peek(L) == '\'') next_ch(L);
            word_push_lit(sub, &tail, qb, ql, 1, 0);
            continue;
        }
        if (ch2 == '\\' && peek2(L) > 0) {
            if (bl) { word_push_lit(sub, &tail, buf, bl, 0, 0); bl = 0; }
            char esc[2];
            esc[0] = (char)next_ch(L);
            esc[1] = (char)next_ch(L);
            word_push_lit(sub, &tail, esc, 2, 1, 0);
            continue;
        }
        if (ch2 == '$') {
            if (bl) { word_push_lit(sub, &tail, buf, bl, 0, 0); bl = 0; }
            int c2 = peek2(L);
            next_ch(L);
            if (c2 == '{') {
                next_ch(L);
                rc_wseg *inner = parse_brace_expansion(L, 0, 0);
                word_push(sub, &tail, inner);
            } else if (c2 == '(') {
                next_ch(L);
                if (peek(L) == '(') {
                    next_ch(L);
                    char *txt = NULL;
                    size_t end = 0;
                    /* 扫描到匹配的 )) */
                    size_t st = L->pos;
                    int d = 1;
                    while (L->pos < L->len) {
                        if (L->src[L->pos] == '(') d++;
                        else if (L->src[L->pos] == ')') { d--; if (d == 0) { end = L->pos; L->pos += 2; break; } }
                        L->pos++;
                    }
                    lex_resync(L);
                    txt = dupn(L->src + st, end - st);
                    rc_wseg *ar = seg_new(RW_ARITH);
                    ar->arith = parse_arith_text(txt, strlen(txt));
                    free(txt);
                    word_push(sub, &tail, ar);
                } else {
                    char *txt = NULL;
                    scan_balanced(L, '(', ')', &txt);
                    rc_node *body = NULL;
                    if (txt) {
                        lexer sub_lex = *L;
                        sub_lex.src = txt; sub_lex.len = strlen(txt); sub_lex.pos = 0;
                        sub_lex.line = 1; sub_lex.col = 1;   /* ⚑ 子源文本坐标从 1 起，(( )) 的原文切片要用 */
                        sub_lex.line = 1; sub_lex.col = 1;   /* 子源文本从 1 开始（src_off 依赖） */
                        sub_lex.ndiags = L->ndiags; sub_lex.diags = L->diags;
                        body = parse_list_inner(&sub_lex);
                        L->ndiags = sub_lex.ndiags;
                        free(txt);
                    }
                    rc_wseg *cs = seg_new(RW_CMDSUB);
                    cs->sub = body;
                    word_push(sub, &tail, cs);
                }
            } else if (isalpha(c2) || c2 == '_') {
                char nm[128]; size_t k = 0;
                nm[k++] = (char)next_ch(L);
                while (isalnum(peek(L)) || peek(L) == '_') { if (k + 1 < sizeof nm) nm[k++] = (char)next_ch(L); else next_ch(L); }
                nm[k] = 0;
                rc_wseg *v = seg_new(RW_VAR);
                v->name = dupn(nm, k);
                word_push(sub, &tail, v);
            } else {
                char sc[2]; sc[0] = (char)(c2 > 0 ? c2 : '?'); sc[1] = 0;
                if (c2 > 0) next_ch(L);
                rc_wseg *v = seg_new(RW_SPECIAL);
                v->name = dupn(sc, 1);
                word_push(sub, &tail, v);
            }
            continue;
        }
        if (bl + 1 < sizeof buf) buf[bl++] = (char)next_ch(L); else next_ch(L);
    }
    if (bl) word_push_lit(sub, &tail, buf, bl, 0, 0);
    /* ${v#p} ${v##p} ${v%p} ${v%%p} 的右边是「模式」，运行期从 s->pat 取；
       以前统一塞进 s->word，于是修剪模式永远是空串 → 原样返回。 */
    if ((s->op & 0xFF) == RP_REMOVE_PRE_S || (s->op & 0xFF) == RP_REMOVE_PRE_L ||
        (s->op & 0xFF) == RP_REMOVE_SUF_S || (s->op & 0xFF) == RP_REMOVE_SUF_L)
        s->pat = sub;
    else
        s->word = sub;
    return s;
}

/* 扫描一个词（进入时已确认首字符属于词） */
static rc_word *scan_word(lexer *L, char **plain_out)
{
    rc_word *w = xcalloc(1, sizeof *w);
    rc_wseg *tail = NULL;
    char buf[4096];
    size_t bl = 0;
    char plain[8192];
    size_t pl = 0;
    int saw_quote = 0;

#define FLUSH_LIT(q, dq) do { if (bl) { word_push_lit(w, &tail, buf, bl, (q), (dq)); bl = 0; } } while (0)
#define PLAIN_ADD(c) do { if (pl + 1 < sizeof plain) plain[pl++] = (char)(c); } while (0)

    for (;;) {
        int c = peek(L);
        if (c < 0) break;
        if (c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' ||
            c == '<' || c == '>' || c == '(' || c == ')')
            break;
        if (c == '\\') {
            /* 反斜杠换行 = 续行（不产生字符） */
            if (peek2(L) == '\n') { next_ch(L); next_ch(L); continue; }
            FLUSH_LIT(0, 0);
            next_ch(L);
            int c2 = next_ch(L);
            if (c2 < 0) break;
            buf[bl++] = (char)c2;
            PLAIN_ADD(c2);
            FLUSH_LIT(1, 0);
            saw_quote = 1;
            continue;
        }
        if (c == '\'') {
            FLUSH_LIT(0, 0);
            next_ch(L);
            while (peek(L) > 0 && peek(L) != '\'') { char ch = (char)next_ch(L); buf[bl++] = ch; PLAIN_ADD(ch); }
            if (peek(L) == '\'') next_ch(L);
            FLUSH_LIT(1, 0);
            saw_quote = 1;
            continue;
        }
        if (c == '"') {
            FLUSH_LIT(0, 0);
            next_ch(L);
            for (;;) {
                int d = peek(L);
                if (d < 0 || d == '"') { if (d == '"') next_ch(L); break; }
                if (d == '\\') {
                    int e = peek2(L);
                    if (e == '$' || e == '`' || e == '"' || e == '\\') {
                        FLUSH_LIT(1, 1);
                        next_ch(L);
                        char ch = (char)next_ch(L);
                        buf[bl++] = ch; PLAIN_ADD(ch);
                        FLUSH_LIT(1, 1);
                    } else if (e == '\n') { next_ch(L); next_ch(L); }
                    else { next_ch(L); buf[bl++] = '\\'; PLAIN_ADD('\\'); }
                    continue;
                }
                if (d == '$') {
                    FLUSH_LIT(1, 1);
                    next_ch(L);
                    int c2 = peek(L);
                    if (c2 == '{') {
                        next_ch(L);
                        rc_wseg *s = parse_brace_expansion(L, 1, 1);
                        word_push(w, &tail, s);
                        if (s->name) { for (const char *q = s->name; *q; q++) PLAIN_ADD(*q); }
                    } else if (c2 == '(') {
                        next_ch(L);
                        if (peek(L) == '(') {
                            next_ch(L);
                            size_t st = L->pos; int dd = 1; size_t end = st;
                            while (L->pos < L->len) {
                                if (L->src[L->pos] == '(') dd++;
                                else if (L->src[L->pos] == ')') { dd--; if (dd == 0) { end = L->pos; L->pos += 2; break; } }
                                L->pos++;
                            }
                            char *txt = dupn(L->src + st, end - st);
                            rc_wseg *ar = seg_new(RW_ARITH);
                            ar->quoted = 1; ar->dquote = 1;
                            ar->arith = parse_arith_text(txt, strlen(txt));
                            free(txt);
                            word_push(w, &tail, ar);
                            PLAIN_ADD('0');
                        } else {
                            char *txt = NULL;
                            scan_balanced(L, '(', ')', &txt);
                            rc_node *body = NULL;
                            if (txt) {
                                lexer sub_lex = *L;
                                sub_lex.src = txt; sub_lex.len = strlen(txt); sub_lex.pos = 0;
                                sub_lex.line = 1; sub_lex.col = 1;   /* ⚑ 子源文本坐标从 1 起，(( )) 的原文切片要用 */
                                body = parse_list_inner(&sub_lex);
                                L->ndiags = sub_lex.ndiags;
                                free(txt);
                            }
                            rc_wseg *cs = seg_new(RW_CMDSUB);
                            cs->quoted = 1; cs->dquote = 1;
                            cs->sub = body;
                            word_push(w, &tail, cs);
                            PLAIN_ADD('X');
                        }
                    } else if (isalpha(c2) || c2 == '_') {
                        char nm[128]; size_t k = 0;
                        while (isalnum(peek(L)) || peek(L) == '_') { if (k + 1 < sizeof nm) nm[k++] = (char)next_ch(L); else next_ch(L); }
                        nm[k] = 0;
                        rc_wseg *v = seg_new(RW_VAR);
                        v->quoted = 1; v->dquote = 1;
                        v->name = dupn(nm, k);
                        word_push(w, &tail, v);
                    } else if (c2 == '?' || c2 == '#' || c2 == '@' || c2 == '*' || c2 == '!' ||
                               c2 == '$' || c2 == '-' || (c2 >= '0' && c2 <= '9')) {
                        char sc[2] = { (char)next_ch(L), 0 };
                        rc_wseg *v = seg_new(RW_SPECIAL);
                        v->quoted = 1; v->dquote = 1;
                        v->name = dupn(sc, 1);
                        word_push(w, &tail, v);
                    } else {
                        /* 引号内 `$` 后面不是合法参数名（如 `"$="`、词尾）→ 字面量 $，
                           否则那个字符会被当成「名字为它的特殊参数」吞掉。 */
                        buf[bl++] = '$';
                    }
                    continue;
                }
                if (d == '`') {
                    FLUSH_LIT(1, 1);
                    char *txt = scan_backtick(L);
                    rc_wseg *cs = seg_new(RW_CMDSUB);
                    cs->quoted = 1; cs->dquote = 1;
                    if (txt) {
                        lexer sub_lex = *L;
                        sub_lex.src = txt; sub_lex.len = strlen(txt); sub_lex.pos = 0;
                        sub_lex.line = 1; sub_lex.col = 1;   /* ⚑ 子源文本坐标从 1 起，(( )) 的原文切片要用 */
                        cs->sub = parse_list_inner(&sub_lex);
                        L->ndiags = sub_lex.ndiags;
                        free(txt);
                    }
                    word_push(w, &tail, cs);
                    saw_quote = 1;
                    continue;
                }
                { char ch = (char)next_ch(L); buf[bl++] = ch; PLAIN_ADD(ch); }
            }
            FLUSH_LIT(1, 1);
            saw_quote = 1;
            continue;
        }
        if (c == '$') {
            FLUSH_LIT(0, 0);
            next_ch(L);
            int c2 = peek(L);
            if (c2 == '\'') {
                /* $'...'：ANSI-C 转义 */
                next_ch(L);
                while (peek(L) > 0 && peek(L) != '\'') {
                    int ch = next_ch(L);
                    if (ch == '\\') {
                        int e = next_ch(L);
                        switch (e) {
                        case 'n': ch = '\n'; break;
                        case 't': ch = '\t'; break;
                        case 'r': ch = '\r'; break;
                        case '\\': ch = '\\'; break;
                        case '\'': ch = '\''; break;
                        case 'a': ch = '\a'; break;
                        case 'b': ch = '\b'; break;
                        case 'f': ch = '\f'; break;
                        case 'v': ch = '\v'; break;
                        case '0': { int v = 0, k = 0; while (k < 3 && peek(L) >= '0' && peek(L) <= '7') { v = v * 8 + (next_ch(L) - '0'); k++; } ch = v; break; }
                        case 'x': { int v = 0, k = 0; while (k < 2 && isxdigit(peek(L))) { int d2 = next_ch(L); v = v * 16 + (isdigit(d2) ? d2 - '0' : tolower(d2) - 'a' + 10); k++; } ch = v; break; }
                        default: break;
                        }
                    }
                    buf[bl++] = (char)ch;
                    PLAIN_ADD(ch);
                }
                if (peek(L) == '\'') next_ch(L);
                FLUSH_LIT(1, 0);
                saw_quote = 1;
                continue;
            }
            if (c2 == '"') {
                /* $"..."：按普通双引号处理（不做本地化） */
                next_ch(L);
                for (;;) {
                    int d = peek(L);
                    if (d < 0 || d == '"') { if (d == '"') next_ch(L); break; }
                    if (d == '\\') { next_ch(L); }
                    char ch = (char)next_ch(L);
                    buf[bl++] = ch; PLAIN_ADD(ch);
                }
                FLUSH_LIT(1, 1);
                saw_quote = 1;
                continue;
            }
            if (c2 == '{') {
                next_ch(L);
                rc_wseg *s = parse_brace_expansion(L, 0, 0);
                word_push(w, &tail, s);
                if (s->name) { for (const char *q = s->name; *q; q++) PLAIN_ADD(*q); }
                else PLAIN_ADD('X');
                continue;
            }
            if (c2 == '(') {
                next_ch(L);
                if (peek(L) == '(') {
                    next_ch(L);
                    size_t st = L->pos; int dd = 1; size_t end = st;
                    while (L->pos < L->len) {
                        if (L->src[L->pos] == '(') dd++;
                        else if (L->src[L->pos] == ')') { dd--; if (dd == 0) { end = L->pos; L->pos += 2; break; } }
                        L->pos++;
                    }
                    lex_resync(L);
                    char *txt = dupn(L->src + st, end - st);
                    rc_wseg *ar = seg_new(RW_ARITH);
                    ar->arith = parse_arith_text(txt, strlen(txt));
                    free(txt);
                    word_push(w, &tail, ar);
                    PLAIN_ADD('0');
                    continue;
                }
                char *txt = NULL;
                scan_balanced(L, '(', ')', &txt);
                rc_wseg *cs = seg_new(RW_CMDSUB);
                if (txt) {
                    lexer sub_lex = *L;
                    sub_lex.src = txt; sub_lex.len = strlen(txt); sub_lex.pos = 0;
                    sub_lex.line = 1; sub_lex.col = 1;   /* ⚑ 子源文本坐标从 1 起，(( )) 的原文切片要用 */
                    cs->sub = parse_list_inner(&sub_lex);
                    L->ndiags = sub_lex.ndiags;
                    free(txt);
                }
                word_push(w, &tail, cs);
                PLAIN_ADD('X');
                continue;
            }
            if (c2 > 0 && (isalpha(c2) || c2 == '_')) {
                char nm[128]; size_t k = 0;
                while (isalnum(peek(L)) || peek(L) == '_') { if (k + 1 < sizeof nm) nm[k++] = (char)next_ch(L); else next_ch(L); }
                nm[k] = 0;
                rc_wseg *v = seg_new(RW_VAR);
                v->name = dupn(nm, k);
                word_push(w, &tail, v);
                for (size_t i = 0; i < k; i++) PLAIN_ADD(nm[i]);
                continue;
            }
            /* 只有这些字符才是「特殊参数」：? # @ * ! $ - 和 0-9。
               ⚑ 以前这里是「$ 后面跟任何字符都算特殊参数」，于是 `$=`、`$]`、行尾的 `$`
               全被当成名字为那个字符的参数展开成空串 → 字符凭空消失
               （`echo "[\$$=$$]"` 少了 `=$$` 就是这么来的）。其余情况 $ 是字面量。 */
            if (c2 == '?' || c2 == '#' || c2 == '@' || c2 == '*' || c2 == '!' ||
                c2 == '$' || c2 == '-' || (c2 >= '0' && c2 <= '9')) {
                char sc[2] = { (char)next_ch(L), 0 };
                rc_wseg *v = seg_new(RW_SPECIAL);
                v->name = dupn(sc, 1);
                word_push(w, &tail, v);
                PLAIN_ADD('X');
                continue;
            }
            /* 单独的 $（后面不是合法参数名，如 `$=` 或词尾）→ 字面量 $ */
            buf[bl++] = '$'; PLAIN_ADD('$');
            continue;
        }
        if (c == '`') {
            FLUSH_LIT(0, 0);
            char *txt = scan_backtick(L);
            rc_wseg *cs = seg_new(RW_CMDSUB);
            if (txt) {
                lexer sub_lex = *L;
                sub_lex.src = txt; sub_lex.len = strlen(txt); sub_lex.pos = 0;
                sub_lex.line = 1; sub_lex.col = 1;   /* ⚑ 子源文本坐标从 1 起，(( )) 的原文切片要用 */
                cs->sub = parse_list_inner(&sub_lex);
                L->ndiags = sub_lex.ndiags;
                free(txt);
            }
            word_push(w, &tail, cs);
            PLAIN_ADD('X');
            continue;
        }
        if (c == '~' && w->segs == NULL && bl == 0) {
            /* 词首波浪号 */
            next_ch(L);
            char ub[128]; size_t k = 0;
            while (k + 1 < sizeof ub && (isalnum(peek(L)) || peek(L) == '_' || peek(L) == '-'))
                ub[k++] = (char)next_ch(L);
            ub[k] = 0;
            rc_wseg *s = seg_new(RW_TILDE);
            s->name = dupn(ub, k);
            word_push(w, &tail, s);
            PLAIN_ADD('~');
            for (size_t i = 0; i < k; i++) PLAIN_ADD(ub[i]);
            continue;
        }
        {
            char ch = (char)next_ch(L);
            buf[bl++] = ch;
            PLAIN_ADD(ch);
        }
    }
    FLUSH_LIT(0, 0);
    if (saw_quote) w->flags |= RWF_QUOTED;
    plain[pl] = 0;
    if (plain_out) *plain_out = dupn(plain, pl);
    /* 全字面量标记 */
    {
        int lit = 1;
        for (rc_wseg *s = w->segs; s; s = s->next) if (s->kind != RW_LIT) { lit = 0; break; }
        if (lit) w->flags |= RWF_LITERAL;
    }
    return w;
#undef FLUSH_LIT
#undef PLAIN_ADD
}

/* ---- here-doc 收集 ---- */
typedef struct {
    char *delim;
    int   quoted;      /* 定界符被引号引住 → 不展开 */
    int   strip_tabs;
    int   token_index; /* 对应的 << token 下标 */
    char *body;
    uint32_t body_len;
} heredoc;

static heredoc lx_heredocs[64];
static int lx_nheredocs;      /* 已登记总数：单调递增，★不复用槽位★（两个 here-doc 复用
                                 同一个槽位会让前者拿到后者的正文） */
static int lx_hd_pending;     /* 还没收正文的个数 */

static void read_heredocs(lexer *L)
{
    for (int i = lx_nheredocs - lx_hd_pending; i < lx_nheredocs; i++) {
        heredoc *h = &lx_heredocs[i];
        size_t cap = 256, len = 0;
        char *body = xcalloc(cap, 1);
        for (;;) {
            if (L->pos >= L->len) break;
            size_t st = L->pos;
            while (L->pos < L->len && L->src[L->pos] != '\n') L->pos++;
            size_t linelen = L->pos - st;
            const char *line = L->src + st;
            size_t cmp_off = 0;
            if (h->strip_tabs) while (cmp_off < linelen && line[cmp_off] == '\t') cmp_off++;
            size_t dl = strlen(h->delim);
            int is_end = (linelen - cmp_off == dl) && strncmp(line + cmp_off, h->delim, dl) == 0;
            if (L->pos < L->len) L->pos++;   /* 吃掉换行 */
            if (is_end) break;
            if (cmp_off > 0) { line += cmp_off; linelen -= cmp_off; }
            if (len + linelen + 2 > cap) { while (len + linelen + 2 > cap) cap *= 2; body = realloc(body, cap); }
            memcpy(body + len, line, linelen);
            len += linelen;
            body[len++] = '\n';
        }
        body[len] = 0;
        h->body = body;
        h->body_len = (uint32_t)len;
    }
    lx_hd_pending = 0;
}

/* ---- 分词主循环 ---- */
static int lex_tokenize(lexer *L, tok **out_toks, int *out_n)
{
    int cap = 64, n = 0;
    tok *t = xcalloc(cap, sizeof(tok));
    lx_nheredocs = 0;
#define PUSH() do { if (n == cap) { cap *= 2; t = realloc(t, sizeof(tok) * cap); } } while (0)

    for (;;) {
        int c = peek(L);
        if (c < 0) break;
        if (c == ' ' || c == '\t') { next_ch(L); continue; }
        if (c == '\\' && peek2(L) == '\n') { next_ch(L); next_ch(L); continue; }
        if (c == '#') {                       /* 注释（词首才算） */
            while (peek(L) > 0 && peek(L) != '\n') next_ch(L);
            continue;
        }
        int line = L->line, col = L->col;
        if (c == '\n') {
            next_ch(L);
            PUSH(); t[n].kind = TK_NEWLINE; t[n].line = line; t[n].col = col; n++;
            if (lx_hd_pending) read_heredocs(L);
            continue;
        }
        /* 操作符 */
        {
            int c2 = peek2(L), c3 = peekn(L, 2);
            int kind = -1, adv = 1;
            if (c == ';' && c2 == ';' && c3 == '&') { kind = TK_DSEMIAMP; adv = 3; }
            else if (c == ';' && c2 == ';') { kind = TK_DSEMI; adv = 2; }
            else if (c == ';' && c2 == '&') { kind = TK_SEMIAMP; adv = 2; }
            else if (c == ';') kind = TK_SEMI;
            else if (c == '&' && c2 == '&') { kind = TK_ANDAND; adv = 2; }
            else if (c == '&' && c2 == '>') {
                if (c3 == '>') { kind = TK_ANDGTGT; adv = 3; } else { kind = TK_ANDGT; adv = 2; }
            }
            else if (c == '&') kind = TK_AMP;
            else if (c == '|' && c2 == '|') { kind = TK_OROR; adv = 2; }
            else if (c == '|' && c2 == '&') { kind = TK_PIPEBOTH; adv = 2; }
            else if (c == '|') kind = TK_PIPE;
            else if (c == '(' ) kind = TK_LPAREN;
            else if (c == ')' ) kind = TK_RPAREN;
            else if (c == '{' ) kind = TK_LBRACE;
            else if (c == '}' ) kind = TK_RBRACE;
            else if (c == '<' && c2 == '<' && c3 == '-') { kind = TK_DLESSDASH; adv = 3; }
            else if (c == '<' && c2 == '<' && c3 == '<') { kind = TK_TLESS; adv = 3; }
            else if (c == '<' && c2 == '<') { kind = TK_DLESS; adv = 2; }
            else if (c == '<' && c2 == '&') { kind = TK_LESSAND; adv = 2; }
            else if (c == '<' && c2 == '>') { kind = TK_LESSGREAT; adv = 2; }
            else if (c == '<') kind = TK_LT;
            else if (c == '>' && c2 == '>') { kind = TK_DGT; adv = 2; }
            else if (c == '>' && c2 == '&') { kind = TK_GREATAND; adv = 2; }
            else if (c == '>' && c2 == '|') { kind = TK_CLOBBER; adv = 2; }
            else if (c == '>') kind = TK_GT;
            if (kind >= 0) {
                for (int k = 0; k < adv; k++) next_ch(L);
                PUSH(); t[n].kind = kind; t[n].line = line; t[n].col = col; t[n].heredoc_index = -1;
                /* here-doc：紧随其后的词是定界符 */
                if (kind == TK_DLESS || kind == TK_DLESSDASH) {
                    while (peek(L) == ' ' || peek(L) == '\t') next_ch(L);
                    char *plain = NULL;
                    rc_word *w = scan_word(L, &plain);
                    /* 定界符是否被引号引住（<<'EOF' 不展开正文）。
                       ⚑ plain 是「纯文本」，里面的引号已经被剥掉，靠它判断永远得 0，
                       必须看词的 RWF_QUOTED 标志。 */
                    int quoted = (w && (w->flags & RWF_QUOTED)) ? 1 : 0;
                    if (!quoted && plain) {
                        for (const char *q = plain; *q; q++) if (*q == '\\' || *q == '\'' || *q == '"') quoted = 1;
                    }
                    if (lx_nheredocs < 64 && plain) {
                        heredoc *h = &lx_heredocs[lx_nheredocs];
                        memset(h, 0, sizeof *h);
                        /* 去引号 */
                        char db[256]; size_t dl2 = 0;
                        for (const char *q = plain; *q && dl2 + 1 < sizeof db; q++) {
                            if (*q == '\\' || *q == '\'' || *q == '"') continue;
                            db[dl2++] = *q;
                        }
                        db[dl2] = 0;
                        h->delim = dupn(db, dl2);
                        h->quoted = quoted;
                        h->strip_tabs = (kind == TK_DLESSDASH);
                        h->token_index = n;
                        t[n].heredoc_index = lx_nheredocs;
                        lx_nheredocs++;
                        lx_hd_pending++;
                    }
                    if (w) { /* 定界符词本身不再作为普通词 */ }
                    n++;
                    continue;
                }
                n++;
                continue;
            }
            /* `!` 只有独立成词时才是取反关键字；`!=` 必须整体当普通词，
               否则 `[ a != b ]` 会被切成 `!` + `=` 两个词（POSIX 里 != 是运算符） */
            if (c == '!' && (c2 == ' ' || c2 == '\t' || c2 == '\n' || c2 == ';' || c2 < 0)) {
                next_ch(L);
                PUSH(); t[n].kind = TK_BANG; t[n].line = line; t[n].col = col; n++;
                continue;
            }
        }
        /* 普通词 */
        {
            char *plain = NULL;
            rc_word *w = scan_word(L, &plain);
            PUSH();
            t[n].kind = TK_WORD; t[n].word = w; t[n].text = plain;
            t[n].line = line; t[n].col = col; t[n].heredoc_index = -1;
            n++;
        }
    }
    PUSH();
    t[n].kind = TK_EOF; t[n].line = L->line; t[n].col = L->col; n++;
    *out_toks = t;
    *out_n = n;
    return 0;
#undef PUSH
}


/* ============================ 语法 ============================ */
static tok *cur(parser *P) { return &P->t[P->pos]; }
static tok *adv(parser *P) { if (P->pos < P->n - 1) P->pos++; return &P->t[P->pos]; }
static rc_node *parse_if_after_kw(parser *P, const tok *t);

static int is_kw(tok *t, const char *kw) { return t->kind == TK_WORD && t->text && strcmp(t->text, kw) == 0; }
static int at_kw(parser *P, const char *kw) { return is_kw(cur(P), kw); }

static rc_node *node_new(int kind, int line)
{
    rc_node *n = xcalloc(1, sizeof *n);
    n->kind = (uint8_t)kind;
    n->line = (uint32_t)line;
    return n;
}

static rc_node *parse_command(parser *P);

/* 跳过行分隔符 */
static void skip_newlines(parser *P)
{
    while (cur(P)->kind == TK_NEWLINE) adv(P);
}

/* 跳过语句分隔符（; 与换行）—— 列表解析必须用它，否则会在 ';' 上原地打转 */
static void skip_seps(parser *P)
{
    while (cur(P)->kind == TK_NEWLINE || cur(P)->kind == TK_SEMI) adv(P);
}

/* 把 here-doc 正文按「双引号内」的规则扫描成一个词：这样 $A、$(cmd)、$((..))、
   反斜杠转义都在编译期解析好，运行期只要 rc_expand_word_literal 展开即可。
   早期运行期把正文包成 RW_LIT 字面量，于是正文里的 $A 一辈子不展开。 */
static rc_word *scan_heredoc_body_word(const char *body, size_t len)
{
    if (!body) return NULL;
    char *tmp = xcalloc(len + 3, 1);
    tmp[0] = '"';
    memcpy(tmp + 1, body, len);
    tmp[len + 1] = '"';
    lexer sub;
    memset(&sub, 0, sizeof sub);
    sub.src = tmp; sub.len = len + 2; sub.pos = 0; sub.line = 1; sub.col = 1;
    sub.name = "<heredoc>";
    char *plain = NULL;
    rc_word *w = scan_word(&sub, &plain);
    free(plain);
    free(tmp);
    return w;
}

/* 收集重定向 */
static rc_redir *parse_redirs(parser *P, int *consumed)
{
    rc_redir *head = NULL, *tail = NULL;
    for (;;) {
        tok *t = cur(P);
        int fd = 0xFF;
        int have_fd = 0;
        if (t->kind == TK_WORD && t->text && isdigit((unsigned char)t->text[0])) {
            /* 可能是 2>file 形式：数字词紧随重定向符 */
            int k = 1;
            while (t->text[k] && isdigit((unsigned char)t->text[k])) k++;
            if (!t->text[k]) {
                tok *nx = &P->t[P->pos + 1];
                if (nx->kind >= TK_LT && nx->kind <= TK_ANDGTGT) {
                    fd = atoi(t->text);
                    have_fd = 1;
                    adv(P);
                    t = cur(P);
                }
            }
        }
        if (t->kind < TK_LT || t->kind > TK_ANDGTGT) break;
        rc_redir *r = xcalloc(1, sizeof *r);
        r->fd = have_fd ? (uint8_t)fd : 0xFF;
        int kind = t->kind;
        int hd_idx = t->heredoc_index;
        adv(P);
        switch (kind) {
        case TK_LT:       r->op = RC_R_IN; break;
        case TK_GT:       r->op = RC_R_OUT; break;
        case TK_DGT:      r->op = RC_R_APP; break;
        case TK_LESSGREAT: r->op = RC_R_RDWR; break;
        case TK_CLOBBER:  r->op = RC_R_CLOBBER; break;
        case TK_ANDGT:    r->op = RC_R_OUT_ERR; break;
        case TK_ANDGTGT:  r->op = RC_R_OUT_ERR; r->fd2 = 1; break;
        case TK_LESSAND:  r->op = RC_R_DUP_IN; break;
        case TK_GREATAND: r->op = RC_R_DUP_OUT; break;
        case TK_TLESS:    r->op = RC_R_HERESTR; break;
        case TK_DLESS:
        case TK_DLESSDASH:
            r->op = RC_R_HEREDOC;
            /* 判据是「该槽位已收到正文」：lex 期间计数器会变，别拿它当上界。 */
            if (hd_idx >= 0 && hd_idx < 64 && lx_heredocs[hd_idx].body) {
                r->hd = lx_heredocs[hd_idx].body;
                r->hd_len = lx_heredocs[hd_idx].body_len;
                r->hd_expand = lx_heredocs[hd_idx].quoted ? 0 : 1;
                if (r->hd_expand && r->hd)      /* 展开型正文：编译期就解析成词 */
                    r->target = scan_heredoc_body_word(r->hd, r->hd_len);
            }
            break;
        default: r->op = RC_R_IN; break;
        }
        if (r->op != RC_R_HEREDOC) {
            tok *wt = cur(P);
            if (wt->kind != TK_WORD) {
                diag(P->L, wt->line, wt->col, "重定向缺少目标");
                free(r);
                break;
            }
            r->target = wt->word;
            /* >&2 / <&0 这类数字目标 */
            if ((r->op == RC_R_DUP_OUT || r->op == RC_R_DUP_IN) && wt->text) {
                if (!strcmp(wt->text, "-")) r->op = RC_R_CLOSE;
                else if (isdigit((unsigned char)wt->text[0])) {
                    r->fd2 = (uint8_t)atoi(wt->text);
                    r->target = NULL;
                }
            }
            adv(P);
        }
        if (tail) tail->next = r; else head = r;
        tail = r;
        if (consumed) (*consumed)++;
    }
    return head;
}

/* 解析一个“词”为赋值（name=value）*/
static rc_assign *try_parse_assign(tok *t)
{
    if (t->kind != TK_WORD || !t->word || !t->word->segs) return NULL;
    rc_wseg *s = t->word->segs;
    if (s->kind != RW_LIT || !s->name) return NULL;
    const char *eq = strchr(s->name, '=');
    if (!eq || eq == s->name) return NULL;
    int plus = (eq > s->name && eq[-1] == '+');
    for (const char *p = s->name; p < eq - (plus ? 1 : 0); p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return NULL;
    if (!(isalpha((unsigned char)s->name[0]) || s->name[0] == '_')) return NULL;

    rc_assign *a = xcalloc(1, sizeof *a);
    a->name = dupn(s->name, (size_t)(eq - s->name) - (plus ? 1 : 0));
    a->append = (uint8_t)plus;
    /* 值 = 该字面量 = 之后的文本 + 后续片段 */
    rc_word *v = xcalloc(1, sizeof *v);
    rc_wseg *tail = NULL;
    const char *vp = eq + 1;
    if (*vp) {
        rc_wseg *vs = seg_new(RW_LIT);
        vs->name = dupn(vp, strlen(vp));
        vs->namelen = (uint32_t)strlen(vp);
        v->segs = vs;
        tail = vs;
    }
    for (rc_wseg *seg = s->next; seg; seg = seg->next) {
        rc_wseg *cp = xcalloc(1, sizeof *cp);
        *cp = *seg;
        cp->next = NULL;
        if (tail) tail->next = cp; else v->segs = cp;
        tail = cp;
    }
    v->flags = RWF_ASSIGN_RHS;
    a->value = v;
    return a;
}

/* 简单命令 */
static rc_node *parse_simple(parser *P)
{
    rc_node *n = node_new(RN_SIMPLE, cur(P)->line);
    rc_word **argv = NULL;
    uint32_t argc = 0, cap = 0;
    rc_assign *atail = NULL;

    for (;;) {
        tok *t = cur(P);
        if (t->kind == TK_WORD) {
            /* 纯数字词紧跟重定向符 = fd 前缀（`2>file`、`3>&1`）。
               以前它先被当成普通参数收下，于是 `echo err 2>/dev/null` 会
               打印出 "err 2" 且重定向落到默认 fd 上。 */
            if (t->text && t->text[0] && isdigit((unsigned char)t->text[0])) {
                int allnum = 1;
                for (const char *q = t->text; *q; q++) if (!isdigit((unsigned char)*q)) { allnum = 0; break; }
                tok *nx = &P->t[P->pos + 1];
                if (allnum && nx->kind >= TK_LT && nx->kind <= TK_ANDGTGT) {
                    rc_redir *rs = parse_redirs(P, NULL);
                    if (rs) {
                        if (n->redirs) { rc_redir *tl = n->redirs; while (tl->next) tl = tl->next; tl->next = rs; }
                        else n->redirs = rs;
                    }
                    continue;
                }
            }
            /* 赋值？只有在本命令还没有命令名时才可能是赋值 */
            rc_assign *a = (argc == 0) ? try_parse_assign(t) : NULL;
            if (a) {
                if (atail) atail->next = a; else n->u.simple.assigns = a;
                atail = a;
                adv(P);
                continue;
            }
            if (argc == cap) { cap = cap ? cap * 2 : 8; argv = realloc(argv, sizeof(rc_word *) * cap); }
            argv[argc++] = t->word;
            adv(P);
            continue;
        }
        if (t->kind >= TK_LT && t->kind <= TK_ANDGTGT) {
            n->redirs = parse_redirs(P, NULL);
            continue;
        }
        /* 命令中间出现的 `!` 是普通词：POSIX 的取反只认命令首位的 `!`
           （parse_pipe_c 已消费首位那种）。`[ ! -e f ]` 的 `!` 走这里。 */
        if (t->kind == TK_BANG && argc > 0) {
            rc_word *bang = xcalloc(1, sizeof *bang);
            rc_wseg *bt = NULL;
            word_push_lit(bang, &bt, "!", 1, 0, 0);
            if (argc == cap) { cap = cap ? cap * 2 : 8; argv = realloc(argv, sizeof(rc_word *) * cap); }
            argv[argc++] = bang;
            adv(P);
            continue;
        }
        break;
    }
    (void)parse_redirs;
    n->u.simple.argv = argv;
    n->u.simple.argc = argc;
    /* ⚑ 重定向必须同时挂到 rc_simple 上：运行期 rc_exec_simple/rt_run_simple 读的是
       sc->redirs（rc_simple 内的字段），只写节点级 n->redirs 的话编译产物和解释器
       都会静默丢掉简单命令的所有重定向（`> file`、`2>&1`、here-doc 全哑）。 */
    n->u.simple.redirs = n->redirs;
    return n;
}

/* [[ ]] 测试表达式 */
static rc_test *parse_test_expr(parser *P);

static rc_test *test_new(int kind) { rc_test *t = xcalloc(1, sizeof *t); t->kind = (uint8_t)kind; return t; }

static rc_test *parse_test_primary(parser *P)
{
    tok *t = cur(P);
    if (t->kind == TK_LPAREN) {
        adv(P);
        rc_test *e = parse_test_expr(P);
        if (cur(P)->kind == TK_RPAREN) adv(P);
        rc_test *g = test_new(RT_GROUP);
        g->a = e;
        return g;
    }
    if (t->kind == TK_BANG) {
        adv(P);
        rc_test *n = test_new(RT_NOT);
        n->a = parse_test_primary(P);
        return n;
    }
    if (t->kind != TK_WORD) {
        diag(P->L, t->line, t->col, "[[ ]] 内期望表达式");
        adv(P);
        return test_new(RT_GROUP);
    }
    /* 一元测试 */
    if (t->text && t->text[0] == '-' && t->text[1] && !t->text[2]) {
        char c = t->text[1];
        static const struct { char c; int op; int kind; } un[] = {
            {'n',1,RT_UNARY_STR},{'z',2,RT_UNARY_STR},
            {'e',10,RT_UNARY_FILE},{'f',11,RT_UNARY_FILE},{'d',12,RT_UNARY_FILE},
            {'r',13,RT_UNARY_FILE},{'w',14,RT_UNARY_FILE},{'x',15,RT_UNARY_FILE},
            {'s',16,RT_UNARY_FILE},{'L',17,RT_UNARY_FILE},{'h',17,RT_UNARY_FILE},
            {'c',18,RT_UNARY_FILE},{'b',19,RT_UNARY_FILE},{'p',20,RT_UNARY_FILE},{'S',21,RT_UNARY_FILE},
        };
        for (size_t i = 0; i < sizeof un / sizeof un[0]; i++) {
            if (un[i].c == c) {
                adv(P);
                rc_test *n = test_new(un[i].kind);
                n->op = (uint8_t)un[i].op;
                tok *w = cur(P);
                if (w->kind == TK_WORD) { n->lhs = w->word; adv(P); }
                else diag(P->L, w->line, w->col, "一元测试缺少操作数");
                return n;
            }
        }
    }
    /* 二元测试 */
    {
        rc_word *lhs = t->word;
        adv(P);
        tok *op = cur(P);
        if (op->kind == TK_WORD && op->text) {
            const char *o = op->text;
            int kind = -1, code = 0;
            /* `=`/`==`/`!=` 的右边**没加引号**时是通配模式（bash `[[ ]]` 语义）；
               加了引号才是字面比较。以前一律走字符串比较，于是
               `[[ abc == a* ]]` 判成假。 */
            if (!strcmp(o, "=") || !strcmp(o, "==")) { kind = RT_CMP_STR; code = 30; }
            else if (!strcmp(o, "!=")) { kind = RT_CMP_STR; code = 31; }
            else if (!strcmp(o, "=~")) { kind = RT_CMP_REGEX; code = 0; }
            else if (!strcmp(o, "-eq")) { kind = RT_CMP_NUM; code = 40; }
            else if (!strcmp(o, "-ne")) { kind = RT_CMP_NUM; code = 41; }
            else if (!strcmp(o, "-lt")) { kind = RT_CMP_NUM; code = 42; }
            else if (!strcmp(o, "-le")) { kind = RT_CMP_NUM; code = 43; }
            else if (!strcmp(o, "-gt")) { kind = RT_CMP_NUM; code = 44; }
            else if (!strcmp(o, "-ge")) { kind = RT_CMP_NUM; code = 45; }
            if (kind >= 0) {
                adv(P);
                tok *rhs = cur(P);
                if (kind == RT_CMP_STR && rhs->kind == TK_WORD && rhs->word &&
                    !(rhs->word->flags & RWF_QUOTED))
                    kind = RT_CMP_PAT;          /* 未加引号的右侧 = 通配模式 */
                rc_test *n = test_new(kind);
                n->op = (uint8_t)code;
                n->lhs = lhs;
                if (rhs->kind == TK_WORD) { n->rhs = rhs->word; adv(P); }
                else diag(P->L, rhs->line, rhs->col, "二元测试缺少右操作数");
                return n;
            }
            if (!strcmp(o, "&&") || !strcmp(o, "||")) { /* 交给上层 */ }
        }
        /* 单个词：非空测试 */
        rc_test *n = test_new(RT_UNARY_STR);
        n->op = 1;   /* -n */
        n->lhs = lhs;
        return n;
    }
}

static rc_test *parse_test_expr(parser *P)
{
    rc_test *l = parse_test_primary(P);
    for (;;) {
        if (at_kw(P, "&&")) {
            adv(P);
            rc_test *n = test_new(RT_AND);
            n->a = l;
            n->b = parse_test_primary(P);
            l = n;
            continue;
        }
        if (at_kw(P, "||")) {
            adv(P);
            rc_test *n = test_new(RT_OR);
            n->a = l;
            n->b = parse_test_primary(P);
            l = n;
            continue;
        }
        break;
    }
    return l;
}


/* 由 (line,col) 反查源码绝对偏移：(( )) 命令需要把算术原文切出来喂给算术解析器 */
static size_t src_off(const lexer *L, int line, int col)
{
    if (!L || !L->src) return 0;
    const char *p = L->src;
    size_t off = 0;
    for (int ln = 1; ln < line && *p && off < L->len; ) { if (*p == '\n') ln++; p++; off++; }
    off += (size_t)(col > 0 ? col - 1 : 0);
    return off > L->len ? L->len : off;      /* 夹住，别切到源文本之外 */
}

static rc_node *parse_command(parser *P)
{
    tok *t = cur(P);
    /* 函数定义： name ( ) 复合命令 */
    if (t->kind == TK_WORD && t->text && P->t[P->pos + 1].kind == TK_LPAREN &&
        P->t[P->pos + 2].kind == TK_RPAREN) {
        char *name = dupn(t->text, strlen(t->text));
        adv(P); adv(P); adv(P);
        skip_newlines(P);
        P->in_func++;
        rc_node *body = parse_command(P);
        P->in_func--;
        rc_node *n = node_new(RN_FUNC, t->line);
        n->u.func.name = name;
        n->u.func.body = body;
        return n;
    }
    if (t->kind == TK_LPAREN && P->t[P->pos + 1].kind == TK_LPAREN) {
        /* (( 算术命令 ))（bash/GNU 扩展）。算术解析器吃字符串，所以按源码偏移
           把 (( 和 )) 之间的原文切出来；括号里可以再嵌括号，按深度配对。 */
        size_t a = src_off(P->L, P->t[P->pos + 1].line, P->t[P->pos + 1].col) + 1;
        adv(P); adv(P);
        int depth = 0;
        size_t b = a;
        while (cur(P)->kind != TK_EOF) {
            if (cur(P)->kind == TK_LPAREN) depth++;
            else if (cur(P)->kind == TK_RPAREN) {
                if (depth == 0) {
                    b = src_off(P->L, cur(P)->line, cur(P)->col);
                    adv(P);
                    if (cur(P)->kind == TK_RPAREN) adv(P);
                    break;
                }
                depth--;
            }
            adv(P);
        }
        size_t alen = (b > a) ? b - a : 0;
        char *atxt = (P->L && P->L->src) ? dupn(P->L->src + a, alen) : dupn("", 0);
        aparser A; A.s = atxt; A.pos = 0; A.len = strlen(atxt); A.failed = 0;
        rc_node *n = node_new(RN_ARITH_CMD, t->line);
        n->u.arith = a_assign(&A);
        if (A.failed) diag(P->L, t->line, t->col, "(( )) 里的算术表达式无法解析");
        free(atxt);
        n->redirs = parse_redirs(P, NULL);
        return n;
    }
    if (t->kind == TK_LPAREN) {
        adv(P);
        rc_node *n = node_new(RN_SUBSHELL, t->line);
        n->a = parse_list_tokens(P);
        if (cur(P)->kind == TK_RPAREN) adv(P);
        else diag(P->L, cur(P)->line, cur(P)->col, "缺少 ')'");
        n->redirs = parse_redirs(P, NULL);
        return n;
    }
    if (t->kind == TK_LBRACE) {
        adv(P);
        rc_node *n = node_new(RN_GROUP, t->line);
        n->a = parse_list_tokens(P);
        if (cur(P)->kind == TK_RBRACE) adv(P);
        else diag(P->L, cur(P)->line, cur(P)->col, "缺少 '}'");
        n->redirs = parse_redirs(P, NULL);
        return n;
    }
    if (at_kw(P, "if")) {
        adv(P);
        return parse_if_after_kw(P, t);
    }
    if (at_kw(P, "while") || at_kw(P, "until")) {
        int is_while = at_kw(P, "while");
        adv(P);
        rc_node *n = node_new(RN_WHILE, t->line);
        n->sub = is_while ? RC_WHILE : RC_UNTIL;
        n->a = parse_list_tokens_until(P, "do");
        skip_newlines(P);
        if (at_kw(P, "do")) adv(P);
        else diag(P->L, cur(P)->line, cur(P)->col, "while 缺少 do");
        n->b = parse_list_tokens_until(P, "done");
        skip_newlines(P);
        if (at_kw(P, "done")) adv(P);
        else diag(P->L, cur(P)->line, cur(P)->col, "while 缺少 done");
        n->redirs = parse_redirs(P, NULL);
        return n;
    }
    if (at_kw(P, "for")) {
        adv(P);
        rc_node *n = node_new(RN_FOR, t->line);
        tok *vt = cur(P);
        if (vt->kind == TK_WORD) { n->u.forc.name = dupn(vt->text, strlen(vt->text)); adv(P); }
        else diag(P->L, vt->line, vt->col, "for 缺少循环变量");
        skip_newlines(P);
        if (at_kw(P, "in")) {
            adv(P);
            rc_word **items = NULL; uint32_t ni = 0, capi = 0;
            while (cur(P)->kind == TK_WORD) {
                if (ni == capi) { capi = capi ? capi * 2 : 8; items = realloc(items, sizeof(rc_word *) * capi); }
                items[ni++] = cur(P)->word;
                adv(P);
            }
            n->u.forc.items = items;
            n->u.forc.nitems = ni;
            n->u.forc.has_in = 1;
        }
        /* 可选的 ; 或换行 */
        while (cur(P)->kind == TK_SEMI || cur(P)->kind == TK_NEWLINE) adv(P);
        if (at_kw(P, "do")) adv(P);
        else diag(P->L, cur(P)->line, cur(P)->col, "for 缺少 do");
        n->u.forc.body = parse_list_tokens_until(P, "done");
        skip_newlines(P);
        if (at_kw(P, "done")) adv(P);
        else diag(P->L, cur(P)->line, cur(P)->col, "for 缺少 done");
        n->redirs = parse_redirs(P, NULL);
        return n;
    }
    if (at_kw(P, "case")) {
        adv(P);
        rc_node *n = node_new(RN_CASE, t->line);
        tok *wt = cur(P);
        if (wt->kind == TK_WORD) { n->u.casec.word = wt->word; adv(P); }
        skip_newlines(P);
        if (at_kw(P, "in")) adv(P);
        else diag(P->L, cur(P)->line, cur(P)->col, "case 缺少 in");
        rc_case_item *ihead = NULL, *itail = NULL;
        for (;;) {
            skip_newlines(P);
            if (at_kw(P, "esac") || cur(P)->kind == TK_EOF) break;
            if (cur(P)->kind == TK_LPAREN) adv(P);
            rc_case_item *it = xcalloc(1, sizeof *it);
            rc_word **pats = NULL; uint32_t np = 0, cp = 0;
            while (cur(P)->kind == TK_WORD) {
                if (np == cp) { cp = cp ? cp * 2 : 4; pats = realloc(pats, sizeof(rc_word *) * cp); }
                pats[np++] = cur(P)->word;
                adv(P);
                if (cur(P)->kind == TK_PIPE) { adv(P); continue; }
                break;
            }
            it->pats = pats;
            it->npats = np;
            if (cur(P)->kind == TK_RPAREN) adv(P);
            it->body = parse_list_tokens_until(P, "esac");
            if (cur(P)->kind == TK_DSEMI) { adv(P); }
            else if (cur(P)->kind == TK_SEMIAMP) { it->fallthrough = 1; adv(P); }
            else if (cur(P)->kind == TK_DSEMIAMP) { it->retest = 1; adv(P); }
            if (itail) itail->next = it; else ihead = it;
            itail = it;
        }
        if (at_kw(P, "esac")) adv(P);
        else diag(P->L, cur(P)->line, cur(P)->col, "case 缺少 esac");
        n->u.casec.items = ihead;
        n->redirs = parse_redirs(P, NULL);
        return n;
    }
    if (t->kind == TK_WORD && t->word && t->word->segs && t->word->segs->kind == RW_LIT &&
        t->word->segs->name && strcmp(t->word->segs->name, "[[") == 0) {
        adv(P);
        rc_node *n = node_new(RN_TEST_CMD, t->line);
        n->u.test = parse_test_expr(P);
        /* 跳过 ]] */
        while (cur(P)->kind != TK_WORD || !cur(P)->text || strcmp(cur(P)->text, "]]")) {
            if (cur(P)->kind == TK_EOF || cur(P)->kind == TK_NEWLINE) break;
            adv(P);
        }
        if (cur(P)->kind == TK_WORD) adv(P);
        return n;
    }
    /* 其余：简单命令 */
    return parse_simple(P);
}

/* 用 token 流解析一个语句序列，直到遇见 stop 关键字或 EOF */
static rc_node *parse_list_tokens_until(parser *P, const char *stop)
{
    rc_node *head = NULL, *tail = NULL;
    for (;;) {
        int pos_before = P->pos;
        while (cur(P)->kind == TK_NEWLINE || cur(P)->kind == TK_SEMI) adv(P);
        if (cur(P)->kind == TK_EOF) break;
        if (cur(P)->kind == TK_DSEMI || cur(P)->kind == TK_SEMIAMP ||
            cur(P)->kind == TK_DSEMIAMP) break;          /* case 项终止符 */
        if (stop && at_kw(P, stop)) break;
        if (!stop && (cur(P)->kind == TK_RPAREN || cur(P)->kind == TK_RBRACE)) break;
        rc_node *n = parse_and_or_c(P);
        if (!n) break;
        if (tail) tail->next = n; else head = n;
        tail = n;
        while (tail->next) tail = tail->next;
        if (cur(P)->kind == TK_AMP) {
            adv(P);
            rc_node *bg = node_new(RN_BG, n->line);
            bg->a = n;
            if (head == n) head = bg;
            else { /* 重新挂到链尾 */ }
            tail = bg;
            if (tail->next) { }
            /* 简化：把整条链视为该后台节点 */
            head = bg;
            n->next = NULL;
            tail = bg;
        }
        if (cur(P)->kind == TK_NEWLINE || cur(P)->kind == TK_SEMI) { skip_seps(P); continue; }
        if (stop && at_kw(P, stop)) break;
        if (cur(P)->kind == TK_DSEMI || cur(P)->kind == TK_SEMIAMP || cur(P)->kind == TK_DSEMIAMP) break;
        if (cur(P)->kind == TK_RPAREN || cur(P)->kind == TK_RBRACE || cur(P)->kind == TK_EOF) break;
        if (P->pos == pos_before) break;                  /* 无进展：避免死循环 */
    }
    return head;
}

static rc_node *parse_list_tokens(parser *P) { return parse_list_tokens_until(P, NULL); }

/* 支持多个终止关键字（如 then/else/fi 三种都可能结束 then 体） */
static rc_node *parse_list_tokens_until_multi(parser *P, const char **stops)
{
    rc_node *head = NULL, *tail = NULL;
    for (;;) {
        int pos_before = P->pos;
        skip_seps(P);
        if (cur(P)->kind == TK_EOF) break;
        if (cur(P)->kind == TK_DSEMI || cur(P)->kind == TK_SEMIAMP || cur(P)->kind == TK_DSEMIAMP) break;
        int stop = 0;
        for (int i = 0; stops[i]; i++) if (at_kw(P, stops[i])) { stop = 1; break; }
        if (stop) break;
        rc_node *n = parse_and_or_c(P);
        if (!n) break;
        if (tail) tail->next = n; else head = n;
        tail = n;
        while (tail->next) tail = tail->next;
        if (cur(P)->kind == TK_AMP) {
            adv(P);
            rc_node *bg = node_new(RN_BG, n->line);
            bg->a = n;
            if (head == n) head = bg;
            else { rc_node *q = head; while (q->next && q->next != n) q = q->next; if (q->next == n) q->next = bg; }
            tail = bg;
        }
        if (cur(P)->kind == TK_NEWLINE || cur(P)->kind == TK_SEMI) { skip_seps(P); continue; }
        if (cur(P)->kind == TK_EOF || cur(P)->kind == TK_RPAREN || cur(P)->kind == TK_RBRACE) break;
        int stop2 = 0;
        for (int i = 0; stops[i]; i++) if (at_kw(P, stops[i])) { stop2 = 1; break; }
        if (stop2) break;
        if (P->pos == pos_before) adv(P);
    }
    return head;
}

static rc_node *parse_and_or_c(parser *P)
{
    rc_node *l = parse_pipe_c(P);
    for (;;) {
        if (cur(P)->kind == TK_ANDAND || cur(P)->kind == TK_OROR) {
            int is_and = (cur(P)->kind == TK_ANDAND);
            int line = cur(P)->line;
            adv(P);
            skip_newlines(P);
            rc_node *r = parse_pipe_c(P);
            rc_node *n = node_new(RN_ANDOR, line);
            n->sub = is_and ? RC_AND : RC_OR;
            n->a = l;
            n->b = r;
            l = n;
            continue;
        }
        break;
    }
    return l;
}

static rc_node *parse_pipe_c(parser *P)
{
    int line = cur(P)->line;
    int bang = 0;
    if (cur(P)->kind == TK_BANG || at_kw(P, "!")) { bang = 1; adv(P); }
    rc_node *l = parse_command(P);
    while (cur(P)->kind == TK_PIPE || cur(P)->kind == TK_PIPEBOTH) {
        int both = (cur(P)->kind == TK_PIPEBOTH);
        adv(P);
        skip_newlines(P);
        rc_node *r = parse_command(P);
        rc_node *n = node_new(RN_PIPE, line);
        if (both) n->flags |= RNF_PIPE_ERR;
        n->a = l;
        n->b = r;
        l = n;
    }
    if (bang) {
        rc_node *n = node_new(RN_NOT, line);
        n->a = l;
        l = n;
    }
    return l;
}

/* ---- 完整解析（token 流） ---- */
static rc_node *parse_program(parser *P)
{
    rc_node *head = NULL, *tail = NULL;
    skip_newlines(P);
    while (cur(P)->kind != TK_EOF) {
        int pos_before = P->pos;
        rc_node *n = parse_and_or_c(P);
        if (!n) break;
        if (tail) tail->next = n; else head = n;
        tail = n;
        while (tail->next) tail = tail->next;
        if (cur(P)->kind == TK_AMP) {
            adv(P);
            rc_node *bg = node_new(RN_BG, n->line);
            bg->a = n;
            if (head == n) head = bg;
            else {
                /* 找到上一个节点的末尾并改接 */
                rc_node *q = head;
                while (q->next && q->next != n) q = q->next;
                if (q->next == n) q->next = bg;
            }
            tail = bg;
        }
        if (cur(P)->kind == TK_NEWLINE || cur(P)->kind == TK_SEMI) { skip_seps(P); continue; }
        if (cur(P)->kind == TK_EOF) break;
        if (cur(P)->kind == TK_RPAREN || cur(P)->kind == TK_RBRACE) break;
        if (P->pos == pos_before) { adv(P); }             /* 无进展：跳过该 token，避免死循环 */
    }
    return head;
}

/* 供 $( ) 内部使用的“文本 → 节点”入口 */
static rc_node *parse_list_inner(lexer *L)
{
    tok *toks = NULL;
    int ntok = 0;
    lex_tokenize(L, &toks, &ntok);
    parser P;
    memset(&P, 0, sizeof P);
    P.L = L; P.t = toks; P.n = ntok; P.pos = 0;
    rc_node *root = parse_program(&P);
    /* tokens 里的词已被 AST 引用，这里不释放 */
    return root;
}

/* ---- 对外入口 ---- */
int rc_parse_source(const char *src, const char *name,
                    rc_diag *diags, int diags_cap, int *ndiags_out,
                    rc_node **root_out)
{
    lexer L;
    memset(&L, 0, sizeof L);
    L.src = src;
    L.len = strlen(src);
    L.pos = 0;
    L.line = 1;
    L.col = 1;
    L.name = name;
    L.diags = diags;
    L.diags_cap = diags_cap;
    L.ndiags = 0;
    *root_out = parse_list_inner(&L);
    if (ndiags_out) *ndiags_out = L.ndiags;
    return L.had_error ? 1 : 0;
}

rc_node *rc_parse_string(const char *src, const char *name, char **err_out)
{
    rc_diag d[8];
    int nd = 0;
    rc_node *root = NULL;
    int r = rc_parse_source(src, name, d, 8, &nd, &root);
    if (r != 0 && nd > 0 && err_out) {
        char *e = xcalloc(512, 1);
        snprintf(e, 512, "%.100s:%d:%d: %.200s", name ? name : "eval", d[0].line, d[0].col, d[0].msg);
        *err_out = e;
    }
    return root;
}

/* ============================ AST 释放 / 统计 / 转储 ============================ */
void rc_node_free_runtime(rc_node *n)
{
    /* 简化实现：运行时 eval 产生的树整体丢弃（进程级 shell 用不了多少） */
    (void)n;
}

static void count_word(const rc_word *w, rc_ast_stats *st)
{
    for (const rc_wseg *s = w ? w->segs : NULL; s; s = s->next) {
        st->n_segs++;
        if (s->name) st->n_strings++;
        if (s->word) count_word(s->word, st);
        if (s->pat) count_word(s->pat, st);
        if (s->rep) count_word(s->rep, st);
        if (s->sub) rc_ast_count(s->sub, st);
    }
}

void rc_ast_count(const rc_node *n, rc_ast_stats *st)
{
    for (const rc_node *p = n; p; p = p->next) {
        st->n_nodes++;
        switch (p->kind) {
        case RN_SIMPLE:
            st->n_words += p->u.simple.argc;
            for (uint32_t i = 0; i < p->u.simple.argc; i++) count_word(p->u.simple.argv[i], st);
            break;
        case RN_FOR:
            st->n_words += p->u.forc.nitems;
            for (uint32_t i = 0; i < p->u.forc.nitems; i++) count_word(p->u.forc.items[i], st);
            rc_ast_count(p->u.forc.body, st);
            break;
        case RN_CASE:
            count_word(p->u.casec.word, st);
            for (rc_case_item *it = p->u.casec.items; it; it = it->next) {
                for (uint32_t i = 0; i < it->npats; i++) count_word(it->pats[i], st);
                rc_ast_count(it->body, st);
            }
            break;
        case RN_FUNC:
            rc_ast_count(p->u.func.body, st);
            break;
        default:
            break;
        }
        rc_ast_count(p->a, st);
        rc_ast_count(p->b, st);
        rc_ast_count(p->c, st);
    }
}

static const char *node_name(int kind)
{
    static const char *n[] = {
        "SIMPLE","LIST","ANDOR","PIPE","NOT","BG","IF","WHILE","FOR","CASE",
        "SUBSHELL","GROUP","FUNC","ARITH","TEST"
    };
    return (kind >= 0 && kind < 15) ? n[kind] : "?";
}

void rc_ast_dump(const rc_node *n, int indent, void (*out)(const char *line, void *ud), void *ud)
{
    char buf[512];
    for (const rc_node *p = n; p; p = p->next) {
        snprintf(buf, sizeof buf, "%*s%s (行 %u)", indent, "", node_name(p->kind), p->line);
        out(buf, ud);
        if (p->kind == RN_SIMPLE) {
            for (uint32_t i = 0; i < p->u.simple.argc; i++) {
                const rc_word *w = p->u.simple.argv[i];
                const char *kindname = "?";
                if (w->segs) {
                    switch (w->segs->kind) {
                    case RW_LIT: kindname = "字面量"; break;
                    case RW_VAR: kindname = "变量"; break;
                    case RW_SPECIAL: kindname = "特殊参数"; break;
                    case RW_CMDSUB: kindname = "命令替换"; break;
                    case RW_ARITH: kindname = "算术"; break;
                    case RW_PARAM: kindname = "参数展开"; break;
                    case RW_TILDE: kindname = "波浪号"; break;
                    default: kindname = "其它"; break;
                    }
                }
                snprintf(buf, sizeof buf, "%*sargv[%u] 首片段=%s%s", indent + 2, "", i, kindname,
                         (w->flags & RWF_QUOTED) ? "（含引号）" : "");
                out(buf, ud);
            }
            for (rc_assign *a = p->u.simple.assigns; a; a = a->next) {
                snprintf(buf, sizeof buf, "%*s赋值 %s%s", indent + 2, "", a->name, a->append ? " (追加)" : "");
                out(buf, ud);
            }
            for (rc_redir *r = p->redirs; r; r = r->next) {
                snprintf(buf, sizeof buf, "%*s重定向 op=%d fd=%d", indent + 2, "", r->op, r->fd);
                out(buf, ud);
            }
        }
        if (p->a) rc_ast_dump(p->a, indent + 2, out, ud);
        if (p->b) rc_ast_dump(p->b, indent + 2, out, ud);
        if (p->c) rc_ast_dump(p->c, indent + 2, out, ud);
    }
}

/* if / elif 的公共解析：进入时关键字已吃掉。
   ⚑ elif 必须递归当 if 解析：早期写成 parse_command(P)，而 elif 不是命令关键字，
     于是 elif 那一刻之后的东西（then/else/fi 及其中的命令）全被当顶层命令执行——
     现象就是"三个分支全都执行了 + s2a: then: 未找到该命令"。 */
static rc_node *parse_if_after_kw(parser *P, const tok *t)
{
    rc_node *n = node_new(RN_IF, t->line);
    n->a = parse_list_tokens_until_multi(P, (const char *[]){ "then", NULL });
    skip_newlines(P);
    if (at_kw(P, "then")) adv(P);
    else diag(P->L, cur(P)->line, cur(P)->col, "if 缺少 then");
    n->b = parse_list_tokens_until_multi(P, (const char *[]){ "else", "elif", "fi", NULL });
    skip_newlines(P);
    if (at_kw(P, "elif")) {
        const tok *et = cur(P);
        adv(P);
        n->c = parse_if_after_kw(P, et);      /* elif → 嵌套 if；fi 由最深一层吃掉 */
        return n;
    }
    if (at_kw(P, "else")) {
        adv(P);
        n->c = parse_list_tokens_until(P, "fi");
        skip_newlines(P);
    }
    if (at_kw(P, "fi")) adv(P);
    else diag(P->L, cur(P)->line, cur(P)->col, "if 缺少 fi");
    n->redirs = parse_redirs(P, NULL);
    return n;
}
