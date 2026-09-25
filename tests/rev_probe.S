    .text
    .globl  rv_rev
rv_rev:   ldr x2,[x0]
          rev x3,x2
          str x3,[x1]
          ret
    .globl  rv_rev32
rv_rev32: ldr x2,[x0]
          rev32 x3,x2
          str x3,[x1]
          ret
    .globl  rv_rev_w
rv_rev_w: ldr w2,[x0]
          rev w3,w2
          str w3,[x1]
          ret
    .globl  rv_clz
rv_clz:   ldr x2,[x0]
          clz x3,x2
          str x3,[x1]
          ret
