/* rand.h —— 真随机数（编译期用）
 *
 * 【为什么不用 rand()/伪随机】源码要公开，密钥不能写死在源码里；每次编译必须取
 * 不可预测的真随机量，所以直接读 /dev/random（熵池可用时不会阻塞；读不到就报错，
 * 明确拒绝退回伪随机 —— 宁可编译失败也不悄悄降级安全性）。
 */
#ifndef S2A_RAND_H
#define S2A_RAND_H

#include <stddef.h>

/* 从 /dev/random 读 n 字节真随机；成功 0，失败 -1（并打印原因） */
int s2a_true_random(void *buf, size_t n);

/* 诊断：/dev/random 是否可用 */
int s2a_random_selftest(void);

#endif
