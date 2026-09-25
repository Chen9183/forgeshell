    .text
    .globl sb_sbfiz
sb_sbfiz: ldr x2,[x0]
          sbfiz x3,x2,#3,#20
          str x3,[x1]
          ret
    .globl sb_sbfiz2
sb_sbfiz2: ldr x2,[x0]
          sbfiz x3,x2,#1,#8
          str x3,[x1]
          ret
    .globl sb_sbfx
sb_sbfx:  ldr x2,[x0]
          sbfx x3,x2,#5,#9
          str x3,[x1]
          ret
