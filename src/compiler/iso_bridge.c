/* iso_bridge.c —— 与外部 iso 容器（/root/iso 项目）的联动
 *
 * 规则（用户定规）：
 *   · s2a 自身**不内嵌** iso；只在 PATH 里查找 `iso`（可选附加功能）；
 *   · 用户用 --iso 指定容器执行时：找到 iso 就用它的批处理模式跑（隔离由 iso 提供）；
 *   · 找不到 iso：**警告用户“指定的不是 ISO”**，并按规则回退到内置模拟器 -r；
 *     若用户加了 --no-fallback 则直接报错退出。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/wait.h>
#include "s2a.h"

static char *find_iso(void)
{
    const char *path = getenv("PATH");
    if (!path) path = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    char *copy = strdup(path), *save = NULL;
    static char found[4096];
    for (char *d = strtok_r(copy, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        snprintf(found, sizeof found, "%s/iso", d);
        if (access(found, X_OK) == 0) { free(copy); return found; }
    }
    free(copy);
    return NULL;
}

int s2a_iso_run(const s2a_options *o, const char *elf_path, int nargs, char **args)
{
    char *iso = find_iso();
    if (!iso) {
        s2a_warn("你指定的是 ISO 容器执行，但环境里找不到 iso 可执行文件（PATH 中没有 `iso`）");
        s2a_warn("—— 你指定的不是 ISO，因此不按 ISO 走，改用内置模拟器 -r");
        if (o->no_fallback) {
            s2a_error("--no-fallback：不回退，退出");
            return 3;
        }
        return s2a_emu_run(o, elf_path, nargs, args);
    }

    /* 把产物拷进容器再执行：iso 批处理模式 --copy-in <宿主路径>:<容器路径> --exec '<命令>' */
    char cmd[8192];
    size_t off = 0;
    off += (size_t)snprintf(cmd + off, sizeof cmd - off, "%s --copy-in %s:/s2a_target --exec '/s2a_target", iso, elf_path);
    for (int i = 0; i < nargs && off + 64 < sizeof cmd; i++) {
        off += (size_t)snprintf(cmd + off, sizeof cmd - off, " '\\''%s'\\''", args[i]);
    }
    off += (size_t)snprintf(cmd + off, sizeof cmd - off, "'");
    if (o->debug_level >= 1) fprintf(stderr, "[s2a] 容器执行: %s\n", cmd);
    int rc = system(cmd);
    if (rc == -1) { s2a_error("调用 iso 失败"); return 3; }
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
}
