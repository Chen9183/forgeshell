/* rand.c —— 见 rand.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "rand.h"

int s2a_true_random(void *buf, size_t n)
{
    unsigned char *p = buf;
    size_t got = 0;
    int fd = open("/dev/random", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "s2a: 打不开 /dev/random（%s）：拒绝使用伪随机数，编译中止\n", strerror(errno));
        return -1;
    }
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            fprintf(stderr, "s2a: 读 /dev/random 失败: %s\n", strerror(errno));
            close(fd);
            return -1;
        }
        if (r == 0) { fprintf(stderr, "s2a: /dev/random 读到 EOF\n"); close(fd); return -1; }
        got += (size_t)r;
    }
    close(fd);
    return 0;
}

int s2a_random_selftest(void)
{
    unsigned char a[32], b[32];
    if (s2a_true_random(a, sizeof a) != 0) return -1;
    if (s2a_true_random(b, sizeof b) != 0) return -1;
    return memcmp(a, b, sizeof a) == 0 ? -1 : 0;
}
