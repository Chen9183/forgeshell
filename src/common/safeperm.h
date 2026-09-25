/* safeperm.h —— 给"路径"设权限的唯一入口：**只对普通文件动手**
 *
 * 背景（2026-09-19 事故，详见 docs/incident-devnull-20260919.md）：
 *   s2a 曾对用户指定的输出路径无条件执行 chmod(输出, 0755)。有人为了只看 AST dump
 *   用了 `s2a -o /dev/null <脚本>`，于是 chmod 落到 /dev/null 上；而容器的 /dev 与
 *   宿主 /dev 是**同一个 inode**，宿主 /dev/null 被改成 0755 →
 *   Android 的 webview_zygote（uid 1053，非 root）打不开 /dev/null → SIGABRT 自杀 →
 *   系统上所有依赖 WebView 的 App 全部启动失败（Firefox/Chrome 因自带引擎不受影响）。
 *
 * 规矩：**设备节点 / FIFO / 目录 / 符号链接一律不碰权限**。任何一个"按用户给的路径
 * 改权限/改状态"的程序都应走这里，别再直接调 chmod。
 *
 * Author: deepseek v4 flash & @Chen9183 (github)
 */
#ifndef S2A_SAFEPERM_H
#define S2A_SAFEPERM_H

#include <sys/types.h>
#include <sys/stat.h>

/* 只给普通文件设权限。
   返回 1 = 已设置；0 = 跳过（路径为空、不存在、或不是普通文件）。 */
static inline int s2a_set_perm(const char *path, mode_t mode)
{
    struct stat st;
    if (!path || !*path) return 0;
    if (stat(path, &st) != 0) return 0;          /* 不存在：什么都不做 */
    if (!S_ISREG(st.st_mode)) return 0;          /* 非普通文件：绝不改权限 */
    return chmod(path, mode) == 0 ? 1 : 0;
}

/* 路径是否"存在且不是普通文件"（设备/FIFO/目录/套接字…）。
   用于在用户误把设备节点等当输出路径时打一行提示，便于当场发现误用。 */
static inline int s2a_perm_special_path(const char *path)
{
    struct stat st;
    if (!path || !*path) return 0;
    if (stat(path, &st) != 0) return 0;
    return !S_ISREG(st.st_mode);
}

#endif /* S2A_SAFEPERM_H */
