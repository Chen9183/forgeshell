/* rt_template_main.c —— 静态 musl 运行时模板的入口
 *
 * 编译器把模板 ELF 内嵌进自己，编译脚本时：
 *   ① 解析模板符号表拿到 rc_* 的绝对地址；
 *   ② 把生成代码作为新 PT_LOAD 追加到文件末尾；
 *   ③ 把下面这个 rc_user_main 指针 patch 成生成代码入口。
 * 于是产物的执行路径是：内核 → musl 的 _start → __libc_start_main → main() → 生成代码。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

typedef long (*rc_main_fn)(long, char **);

static long rc_no_code(long argc, char **argv)
{
    (void)argc; (void)argv;
    fprintf(stderr,
            "这是 铸壳工具链 ForgeShell 的运行时模板，尚未注入生成的代码。\n"
            "（如果你看到这条消息，说明产物是被手工截断/改坏的）\n");
    return 1;
}

/* 必须落在 .data 里：编译器要按符号地址在文件里就地改写这个指针 */
__attribute__((section(".data"), used, aligned(8)))
rc_main_fn rc_user_main = rc_no_code;

int main(int argc, char **argv)
{
    if (!rc_user_main) rc_user_main = rc_no_code;
    return (int)rc_user_main((long)argc, argv);
}
