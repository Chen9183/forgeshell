/* nat_trace.c —— 极省内存的真机单步记录器（替代 gdb 对拍用）
 *
 * 用法: nat_trace <elf> [步数]
 * 输出: 每行 "pc x0 x1 ... x30 sp nzcv"（十六进制），供 tests/emu_diff2.py 与模拟器轨迹对拍。
 * 为什么不用 gdb：gdb 常驻几十~上百 MB，本机内存紧张时会把宿主拖进 OOM；
 * 这个记录器只用 fork+ptrace(PTRACE_SINGLESTEP)，RSS 几百 KB。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <errno.h>

/* aarch64 的寄存器集合（struct user_pt_regs）*/
struct regs64 { unsigned long long regs[31]; unsigned long long sp; unsigned long long pc; unsigned long long pstate; };

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "用法: %s <elf> [步数]\n", argv[0]); return 2; }
    long maxsteps = argc > 2 ? atol(argv[2]) : 20000;

    pid_t pid = fork();
    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, 0, 0) != 0) { perror("traceme"); _exit(127); }
        execv(argv[1], (char *[]){ argv[1], NULL });
        perror("execv");
        _exit(127);
    }
    if (pid < 0) { perror("fork"); return 1; }

    int status;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return 1; }
    if (WIFEXITED(status)) { fprintf(stderr, "子进程直接退出（未被跟踪？）\n"); return 1; }

    /* 关闭子进程的 ASLR 无关；逐条步进并记录 */
    for (long i = 0; i < maxsteps; i++) {
        struct regs64 r;
        struct iovec io = { &r, sizeof r };
        if (ptrace(PTRACE_GETREGSET, pid, (void *)1 /* NT_PRSTATUS */, &io) != 0) break;
        unsigned nzcv = (unsigned)((r.pstate >> 28) & 0xF);
        printf("%llx %llx %llx %llx %llx %llx %llx %llx %llx\n",
               r.pc, r.regs[0], r.regs[1], r.regs[2], r.regs[19], r.regs[20], r.sp, r.regs[30], nzcv);
        if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) != 0) break;
        if (waitpid(pid, &status, 0) < 0) break;
        if (WIFEXITED(status) || WIFSIGNALED(status)) break;
    }
    ptrace(PTRACE_KILL, pid, 0, 0);
    waitpid(pid, &status, 0);
    return 0;
}
