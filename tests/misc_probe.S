    .text
    .globl mp_adc
mp_adc:   ldp x2,x3,[x0]          // x2,x3
          adds x4,x2,x3           // 设置 C（溢出）
          adc  x5,x2,x3
          stp  x4,x5,[x1]
          ret
    .globl mp_sbc
mp_sbc:   ldp x2,x3,[x0]
          subs x4,x2,x3
          sbc  x5,x2,x3
          stp  x4,x5,[x1]
          ret
    .globl mp_ngc
mp_ngc:   ldr x2,[x0]
          cmp x2,#1               // 制造进位
          ngc x3,x2
          str x3,[x1]
          ret
    .globl mp_ldxrb
mp_ldxrb: ldrb w2,[x0]
          ldxrb w3,[x0]
          stxrb w4,w2,[x1]
          stp w3,w4,[x1,#16]
          ret
    .globl mp_swp
mp_swp:   ldr w2,[x0]
          ldaxr w3,[x0]
          stlxr w4,w2,[x1]
          stp w3,w4,[x1,#16]
          ret
    .globl mp_sbfiz
mp_sbfiz: ldr x2,[x0]
          sbfiz x3,x2,#3,#20
          str x3,[x1]
          ret
    .globl mp_sbfx
mp_sbfx:  ldr x2,[x0]
          sbfx x3,x2,#5,#9
          str x3,[x1]
          ret
    .globl mp_extr
mp_extr:  ldp x2,x3,[x0]
          extr x4,x2,x3,#13
          str x4,[x1]
          ret
    .globl mp_bfi
mp_bfi:   ldp x2,x3,[x0]
          bfi x2,x3,#5,#9
          str x2,[x1]
          ret
