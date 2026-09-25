/* rt_protect.c —— 产物侧防护：密码解锁 / 字符串池加解密 / 完整性自检 / 多层反调试
 *
 * 【为什么这样设计】
 *   · 编译器源码要公开，所以**没有任何密钥写在源码里**：每次编译从 /dev/random 读真随机数，
 *     层-1 密钥 = SHA-256(seed)，seed 本身随产物携带（但可用密码再包一层）。
 *   · 密码不存哈希：编译期用密码派生密钥（SHA-512(pw)）加密**作者名标记**，运行期同密钥解密
 *     并比对明文作者名。解出来不是那个名字 = 密码错 → 报错退出（这解决了“解错了无法判断”的问题）。
 *   · 反调试是多层交叉验证：任何一层命中都并入 bitmap，按 --anti-action 处置。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "rt_internal.h"
#include "../common/crypto.h"

/* 检测位 */
#define AD_PTRACE_TRACEME   (1u << 0)
#define AD_TRACER_PID       (1u << 1)
#define AD_MAPS_HOOK        (1u << 2)
#define AD_PARENT_COMM      (1u << 3)
#define AD_ENV_PRELOAD      (1u << 4)
#define AD_BREAKPOINT       (1u << 5)
#define AD_SELF_HASH        (1u << 6)
#define AD_TIMING           (1u << 7)
#define AD_STAT_TRACER      (1u << 8)

/* 只在“路径里出现”的情况下才算命中，避免 "r2"/"ida" 这类过泛词误报 */
static const char *const hook_names[] = {
    "gdb", "gdbserver", "lldb", "strace", "ltrace", "frida", "xposed",
    "substrate", "libinject", "radare2", NULL
};

static rc_detect_result last_result;
static int reported_once;

/* ============================ 小工具 ============================ */
static int read_file(const char *path, char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return (int)n;
}

/* 大小写不敏感包含；needle_path=1 时要求命中处位于一个路径里（前面出现过 '/'） */
static int contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (!nl) return 0;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               (char)((p[i] >= 'A' && p[i] <= 'Z') ? p[i] + 32 : p[i]) == needle[i]) i++;
        if (i == nl) return 1;
    }
    return 0;
}

static int contains_path_token(const char *maps, const char *tok)
{
    for (const char *p = maps; *p; p++) {
        if (*p != '/') continue;                 /* 只在路径起点开始找 */
        const char *slash = p;
        size_t i = 0;
        while (slash[i] && slash[i] != '\n' && slash[i] != ' ') {
            size_t k = 0;
            while (tok[k] && slash[i + k] &&
                   (char)((slash[i + k] >= 'A' && slash[i + k] <= 'Z')
                          ? slash[i + k] + 32 : slash[i + k]) == tok[k]) k++;
            if (!tok[k]) return 1;
            i++;
        }
    }
    return 0;
}

static unsigned long long rd_counter(void)
{
    unsigned long long v;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

/* ---------------------------------------------------------------
 * 栅栏密码（2 轨）+ 字节异或 —— 与编译期 protect.c 完全一致的小型混淆算法。
 * 只用于把 16 字节校验常量打散，强度由 AES-256 / SHA-2 承担。
 * --------------------------------------------------------------- */
static void rail_fence_xor(const unsigned char *in, unsigned char *out, int n,
                           const unsigned char *key, int klen, int mode)
{
    /* 与编译期 protect.c 完全相同的实现：先按 key 逐字节异或，再 2 轨交错输出。
     * 运行期也走同一方向（存的是加密值，算出同样的加密值再比对）——方向必须一致。 */
    unsigned char tmp[64];
    int half;
    (void)mode;
    if (n > 64) n = 64;
    half = n / 2;
    for (int i = 0; i < n; i++) tmp[i] = (unsigned char)(in[i] ^ key[i % klen]);
    {
        int i0 = 0, i1 = 0;
        for (int i = 0; i < n; i++) {
            if (i % 2 == 0) out[i] = tmp[i0 < half ? i0++ : 0];
            else            out[i] = tmp[half + (i1 < n - half ? i1++ : 0)];
        }
    }
}

/* ============================ 密码解锁 + 池解密 ============================ */
/* 密码校验：① 解出校验料 V；② 用 seed 派生 K 与 SHA-256，算栅栏校验值；③ 与产物里的常量比对。
 * 满盘皆是密文/打散值——产物里没有任何明文。 */
/* 每产物随机盐：与编译器一致，取池密文（此处 img->pool）前 16 字节 */
static unsigned char g_pw_salt[16];

static int check_password(rc_image *img, const char *pw)
{
    memcpy(g_pw_salt, img->pool, 16);   /* 每产物盐：与编译器同取池密文前 16 字节 */
    unsigned char kp[64];
    pbkdf2_sha256(pw, strlen(pw), g_pw_salt, 16, S2A_KDF_ITERS, kp, 64);   /* 盐=本产物的池密文前 16 字节 */   /* KDF v2：PBKDF2-HMAC-SHA256 */

    unsigned char key[32], nonce_m[16], nonce_p[16];
    memcpy(key, kp, 32);
    memcpy(nonce_m, kp + 32, 16);
    memcpy(nonce_p, kp + 48, 16);

    /* ① 解开 seed（层-1 密钥材料也被密码保护） */
    rc_aes256_ctr(key, nonce_p, img->seed, sizeof img->seed);

    /* ② 解出校验料 V */
    uint32_t mlen = img->marker_len;
    if (mlen == 0 || mlen > 64 || !img->author_check) return -1;
    unsigned char V[80];
    memcpy(V, img->marker, mlen);
    rc_aes256_ctr(key, nonce_m, V, mlen);

    /* ③ 同样的派生：K = SHA-256(seed)[:16]，校验值 = 栅栏( SHA-256(V ‖ seed)[:16], K ) */
    unsigned char h[32], K[32], chk[16];
    sha256_ctx sc;
    sha256_init(&sc);
    sha256_update(&sc, V, mlen);
    sha256_update(&sc, img->seed, sizeof img->seed);
    sha256_final(&sc, h);
    kdf_strong(img->seed, 32, "protect-check", img->seed + 32, 16, K);   /* KDF v2：与编译器同参数 */
    rail_fence_xor(h, chk, 16, K, 16, 0);
    if (getenv("S2A_LOCK_DEBUG")) {
        fprintf(stderr, "[lock调试] V=%02x%02x h=%02x%02x seed=%02x%02x\n", V[0], V[1], h[0], h[1], img->seed[0], img->seed[1]);
        fprintf(stderr, "[lock调试] mlen=%u seed0=%02x%02x%02x%02x K0=%02x%02x 算出=%02x%02x%02x%02x 存=%02x%02x%02x%02x\n",
                mlen, img->seed[0],img->seed[1],img->seed[2],img->seed[3],
                K[0],K[1], chk[0],chk[1],chk[2],chk[3],
                img->author_check[0],img->author_check[1],img->author_check[2],img->author_check[3]);
    }
    memset(h, 0, sizeof h);
    memset(K, 0, sizeof K);
    memset(V, 0, sizeof V);

    return memcmp(chk, img->author_check, 16) == 0 ? 0 : -1;
}

static void derive_and_decrypt_pool(rc_image *img)
{
    unsigned char key1[32], nonce1[16], d[32];
    /* 层-1：key = SHA-256(seed[0..31])，nonce = seed[32..63] */
    kdf_strong(img->seed, 32, "protect-pool", img->seed + 32, 16, d);    /* KDF v2：与编译器同参数 */
    memcpy(key1, d, 32);
    memcpy(nonce1, img->seed + 32, 16);
    rc_aes256_ctr(key1, nonce1, (unsigned char *)img->pool, img->pool_size);
    memset(d, 0, sizeof d);
    memset(key1, 0, sizeof key1);
}

int rc_protect_self_check(void)
{
    rc_image *img = rt_g.image;
    if (!img || !(img->flags & RIF_SELFCHECK) || !img->code || !img->code_sha512)
        return 0;
    unsigned char h[64];
    sha512_buf(img->code, img->code_size, h);
    if (memcmp(h, img->code_sha512, 64) != 0) return 1;
    /* 软件断点扫描：ARM64 的 BRK 编码高 11 位 = 0b11010100000 */
    const unsigned char *p = img->code;
    for (uint32_t i = 0; i + 4 <= img->code_size; i += 4) {
        unsigned int ins = (unsigned int)p[i] | ((unsigned int)p[i + 1] << 8)
                         | ((unsigned int)p[i + 2] << 16) | ((unsigned int)p[i + 3] << 24);
        if ((ins & 0xFFE00000u) == 0xD4200000u) return 1;
    }
    return 0;
}

int rc_protect_image_unlock(rc_image *img, const char *password)
{
    memcpy(g_pw_salt, img->pool, 16);   /* 每产物盐：与编译器同取池密文前 16 字节 */
    if (!img) return 0;

    if (img->flags & RIF_LOCKED) {
        if (!password || !*password) {
            fprintf(stderr,
                    "此产物已加密：需要密码才能运行。\n"
                    "用法: %s --password '你的密码' [脚本参数...]\n",
                    rt_g.arg0 ? rt_g.arg0 : "prog");
            return 70;
        }
        if (check_password(img, password) != 0) {
            fprintf(stderr, "密码错误：解密校验失败，拒绝运行。\n");
            return 70;
        }
    }

    if (img->flags & RIF_AES_POOL) {
        if (img->flags & RIF_LOCKED) {
            unsigned char kp[64];
            memcpy(g_pw_salt, img->pool, 16);
            sha512_buf(password, strlen(password), kp);
            unsigned char key[32], nonce[16];
            memcpy(key, kp, 32);
            memcpy(nonce, kp + 48, 16);
            /* 密码层已把 seed 解开（check_password），这里不再重复 */
            (void)key; (void)nonce;
        }
        derive_and_decrypt_pool(img);
    }
    return 0;
}

void rc_protect_after_unlock(void)
{
    rc_image *img = rt_g.image;
    rc_detect_result r;
    /* 没有开启防护（未加 -P/-F）的产物不做任何反调试：避免误伤正常程序 */
    if (!img || !(img->flags & RIF_PROTECTED)) return;
    /* ⚑ 逃生门要在**探测之前**生效：以前只在动作阶段检查，于是
       `S2A_NO_ANTIDEBUG=1` 仍会跑探测（包括会自锁的 ptrace 探测），
       用户按提示关也关不掉。现在彻底跳过。 */
    if (getenv("S2A_NO_ANTIDEBUG")) return;
    int hits = rc_antidebug_probe(&r);
    int report = (img && (img->flags & RIF_REPORT)) || getenv("S2A_ANTIDEBUG");
    if (img && img->anti_action == RC_ANTI_WARN) report = 0;   /* warn 只报一行，不打全表 */
    if (hits > 0) {
        if (report) rc_antidebug_report_print(&r);
        rc_antidebug_act(&r);
    } else if (report) {
        printf("[s2a 防护] %u 项检测全部通过，未发现调试/注入迹象\n", r.checks_run);
    }
}

void rc_protect_pool_decrypt(void)
{
    rc_image *img = rt_g.image;
    if (img && (img->flags & RIF_AES_POOL)) derive_and_decrypt_pool(img);
}

/* ============================ 反调试：多层探测 ============================ */
static void get_comm(long pid, char *out, size_t cap)
{
    char path[64], buf[256];
    snprintf(path, sizeof path, "/proc/%ld/comm", pid);
    out[0] = 0;
    if (read_file(path, buf, sizeof buf) > 0) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        snprintf(out, cap, "%s", buf);
    }
}

int rc_antidebug_probe(rc_detect_result *out)
{
    rc_detect_result r;
    memset(&r, 0, sizeof r);

    /* ① ptrace 主动探测：★绝不在主进程里 ptrace(PTRACE_TRACEME)★
     *    主进程 TRACEME 等于把「父进程」登记成 tracer，而父进程（shell、终端、
     *    任何已有 tracer）不会去处理 ptrace 事件 → 本进程永远停在 ptrace_stop：
     *    用户看到 `[1]+ Stopped`，脚本只跑到一半就卡住（间接跑 setsid/后台更像"静默"）。
     *    以前 `-F`（防护等级 3）默认就会踩这个坑，所以 `-F` 产物在这台机器上基本没法跑。
     *    现在：默认完全被动（TracerPid/maps/时序/断点扫描），只有显式
     *    S2A_ANTIDEBUG_AGGRESSIVE=1 才探测，而且把探测放到**子进程**里做。 */
    r.checks_run++;
    {
        const char *aggr = getenv("S2A_ANTIDEBUG_AGGRESSIVE");
        if (aggr && *aggr && *aggr != '0') {
            pid_t p = fork();
            if (p == 0) {
                int bad = (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0);
                _exit(bad ? 1 : 0);      /* 被跟踪 → TRACEME 失败 */
            }
            if (p > 0) {
                int st = 0;
                for (;;) {               /* 子进程可能停在 ptrace 事件上，作为它的 tracer 要放行 */
                    pid_t w = waitpid(p, &st, WUNTRACED);
                    if (w == p && WIFSTOPPED(st)) { ptrace(PTRACE_CONT, p, NULL, NULL); continue; }
                    if (w < 0 && errno == EINTR) continue;
                    break;
                }
                if (WIFEXITED(st) && WEXITSTATUS(st) == 1) r.bitmap |= AD_PTRACE_TRACEME;
            }
        }
    }

    /* ② /proc/self/status 的 TracerPid */
    r.checks_run++;
    {
        char buf[8192];
        if (read_file("/proc/self/status", buf, sizeof buf) > 0) {
            const char *p = strstr(buf, "TracerPid:");
            if (p) {
                long tp = strtol(p + 10, NULL, 10);
                r.tracer_pid = tp;
                if (tp != 0) { r.bitmap |= AD_TRACER_PID; r.bitmap |= AD_PTRACE_TRACEME; }
            }
        }
    }

    /* ③ /proc/self/maps：调试器/注入框架的特征串 */
    r.checks_run++;
    {
        char *buf = malloc(1 << 20);
        if (buf) {
            if (read_file("/proc/self/maps", buf, 1 << 20) > 0) {
                for (int i = 0; hook_names[i]; i++) {
                    if (contains_path_token(buf, hook_names[i])) {
                        r.bitmap |= AD_MAPS_HOOK;
                        snprintf(r.detail, sizeof r.detail, "maps 命中: %s", hook_names[i]);
                        break;
                    }
                }
            }
            free(buf);
        }
    }

    /* ④ 父进程名（gdb / strace 启动的典型特征） */
    r.checks_run++;
    {
        long ppid = (long)getppid();
        get_comm(ppid, r.tracer_comm, sizeof r.tracer_comm);
        for (int i = 0; hook_names[i]; i++)
            if (contains_ci(r.tracer_comm, hook_names[i])) { r.bitmap |= AD_PARENT_COMM; break; }
    }

    /* ⑤ 预载 / 审计环境变量（LD_PRELOAD 注入） */
    r.checks_run++;
    {
        const char *e;
        if (((e = getenv("LD_PRELOAD")) && *e) ||
            ((e = getenv("LD_AUDIT")) && *e) ||
            ((e = getenv("S2A_DEBUG_TRACE")) && *e)) r.bitmap |= AD_ENV_PRELOAD;
    }

    /* ⑥ 软件断点 / 完整性自检 */
    r.checks_run++;
    if (rc_protect_self_check() != 0) r.bitmap |= (AD_BREAKPOINT | AD_SELF_HASH);

    /* ⑦ 时序：调试器单步/断点会带来数量级差异（取多次采样的最小间隔做基线） */
    r.checks_run++;
    {
        unsigned long long best = ~0ull, worst = 0;
        for (int i = 0; i < 12; i++) {
            unsigned long long a = rd_counter(), b;
            volatile unsigned long long acc = 0;
            for (int k = 0; k < 2000; k++) acc += (unsigned long long)k * 2654435761u;
            b = rd_counter();
            if (b - a < best) best = b - a;
            if (b - a > worst) worst = b - a;
        }
        /* 判据：同一段固定工作量的耗时“离散度”。单步/断点会让某次样本暴涨，
         * 而不同 CPU 频率的机器上最小值仍稳定 —— 因此用 worst > best*40 且 worst 可观。 */
        if (best > 0 && worst > best * 40ull && worst > 10000ull) r.bitmap |= AD_TIMING;
    }

    /* ⑧ /proc/self/stat 的第 4 字段（父进程）与状态交叉验证 */
    r.checks_run++;
    {
        char buf[1024];
        if (read_file("/proc/self/stat", buf, sizeof buf) > 0) {
            for (int i = 0; hook_names[i]; i++)
                if (contains_ci(buf, hook_names[i])) { r.bitmap |= AD_STAT_TRACER; break; }
        }
    }

    uint32_t n = 0;
    for (unsigned b = 0; b < 32; b++) if (r.bitmap & (1u << b)) n++;
    r.hits = n;
    if (out) *out = r;
    last_result = r;
    reported_once = 0;
    return (int)r.hits;
}

void rc_antidebug_report_print(const rc_detect_result *r)
{
    if (!r) return;
    printf("[s2a 防护] 反调试检测报告：%u 项检测，命中 %u 项\n", r->checks_run, r->hits);
    if (r->bitmap & AD_PTRACE_TRACEME) printf("  · 已被跟踪（TracerPid≠0 或 TRACEME 探测命中）\n");
    if (r->bitmap & AD_TRACER_PID)     printf("  · TracerPid = %ld（非 0 = 正在被调试）\n", r->tracer_pid);
    if (r->bitmap & AD_MAPS_HOOK)      printf("  · 内存映射存在调试/注入特征：%s\n", r->detail);
    if (r->bitmap & AD_PARENT_COMM)    printf("  · 父进程可疑：%s\n", r->tracer_comm);
    if (r->bitmap & AD_ENV_PRELOAD)    printf("  · 存在预载类环境变量（LD_PRELOAD/LD_AUDIT）\n");
    if (r->bitmap & AD_BREAKPOINT)     printf("  · 代码段发现断点指令或校验不符\n");
    if (r->bitmap & AD_SELF_HASH)      printf("  · SHA-512 完整性自校验失败\n");
    if (r->bitmap & AD_TIMING)         printf("  · 时序异常（疑似单步/断点停顿）\n");
    if (r->bitmap & AD_STAT_TRACER)    printf("  · /proc/self/stat 含调试特征\n");
    fflush(stdout);
}

void rc_antidebug_act(const rc_detect_result *r)
{
    rc_image *img = rt_g.image;
    uint32_t action = img ? img->anti_action : RC_ANTI_WARN;
    if (getenv("S2A_NO_ANTIDEBUG")) return;          /* 诊断逃生门 */
    if (action == RC_ANTI_REPORT) return;
    if (action == RC_ANTI_WARN) {
        /* 说清楚是哪一项命中：很多安卓终端/root 环境本身 TracerPid 就非 0，
         * 用户需要能分辨“环境如此”还是“真被调试”。*/
        fprintf(stderr, "[s2a 防护] 检测到调试/注入迹象（%u 项）：", r ? r->hits : 0);
        if (r) {
            if (r->bitmap & AD_PTRACE_TRACEME) fprintf(stderr, " ptrace已被跟踪");
            if (r->bitmap & AD_TRACER_PID)     fprintf(stderr, " TracerPid=%ld", r->tracer_pid);
            if (r->bitmap & AD_MAPS_HOOK)      fprintf(stderr, " 映射含调试特征");
            if (r->bitmap & AD_PARENT_COMM)    fprintf(stderr, " 父进程=%s", r->tracer_comm);
            if (r->bitmap & AD_ENV_PRELOAD)    fprintf(stderr, " 预载环境变量");
            if (r->bitmap & AD_BREAKPOINT)     fprintf(stderr, " 断点/自校验");
            if (r->bitmap & AD_SELF_HASH)      fprintf(stderr, " 完整性自校验");
            if (r->bitmap & AD_TIMING)         fprintf(stderr, " 时序异常");
            if (r->bitmap & AD_STAT_TRACER)    fprintf(stderr, " /proc/stat特征");
        }
        fprintf(stderr, "；策略 warn 继续执行（想看全表：--anti-action report，"
                        "想彻底关掉动作：S2A_NO_ANTIDEBUG=1）\n");
        return;
    }
    if (action == RC_ANTI_FAKE) {
        /* 假结果：把状态码改成 0 并继续（让分析者以为程序正常） */
        rt_g.last_status = 0;
        return;
    }
    if (action == RC_ANTI_DELAY) {
        struct timespec ts = { 2, 0 };
        nanosleep(&ts, NULL);
    }
    /* 默认：静默退出（不打印任何东西，避免暴露检测点） */
    (void)r;
    _exit(0);
}

void rc_protect_tick(void)
{
    /* 运行期偶发抽查：只做便宜的检查（完整性 + TracerPid） */
    static unsigned counter;
    if ((++counter & 0x3F) != 0) return;
    rc_detect_result r;
    if (rc_protect_self_check() != 0) {
        memset(&r, 0, sizeof r);
        r.bitmap = AD_SELF_HASH;
        r.hits = 1;
        rc_antidebug_act(&r);
    }
    (void)reported_once;
    (void)last_result;
}
