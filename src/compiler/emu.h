#ifndef S2A_EMU_H
#define S2A_EMU_H
/* 内置 AArch64 解释器对外接口 */
extern int s2a_emu_last_reason;   /* 0=正常 1=缺指令（可回退 qemu） 2=访存故障 3=步数上限 */
#endif
