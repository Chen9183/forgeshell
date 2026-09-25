#!/usr/bin/env python3
"""emu_diff2.py —— 省内存版「模拟器 vs 真机」逐条对拍

用 tests/nat_trace（自己写的 ptrace 单步记录器，几百 KB 内存）代替 gdb，
逐条比较 pc / 寄存器 / NZCV，找出**第一处不一致**并打印上一条指令——
那条就是模拟器译错或语义错的指令。

用法: python3 tests/emu_diff2.py <artefact.elf> [步数]
"""
import re, subprocess, sys, os

ELF = sys.argv[1] if len(sys.argv) > 1 else "/tmp/e1.elf"
N   = int(sys.argv[2]) if len(sys.argv) > 2 else 20000
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S2A = os.path.join(ROOT, "bin", "s2a")
NAT = "/tmp/nat_trace"

def emu_trace():
    env = dict(os.environ, S2A_EMU_REGS="1", S2A_EMU_ENV_STRICT="1",
               S2A_EMU_TRACE_FROM="1", S2A_EMU_TRACE_TO=str(N + 50),
               S2A_EMU_MAX_STEPS=str(N + 1000))
    p = subprocess.run([S2A, "-r", "--emu-trace", ELF], capture_output=True, text=True,
                       env=env, timeout=600)
    out = []
    for l in (p.stdout + p.stderr).splitlines():
        m = re.search(r'\[(0x[0-9a-f]+)\] ([0-9a-f]{8}) (x0=.*)$', l)
        if not m:
            continue
        regs = {k: int(v, 0) for k, v in re.findall(r'(x\d+|sp|nzcv)=(0x[0-9a-f]+|\d+)', m.group(3))}
        out.append((int(m.group(1), 16), int(m.group(2), 16), regs))
    return out

def nat_trace():
    env = {"PATH": "/usr/bin:/bin"}
    p = subprocess.run([NAT, ELF, str(N)], capture_output=True, text=True, env=env, timeout=600)
    rows = []
    for l in p.stdout.splitlines():
        f = l.split()
        if len(f) == 9:
            rows.append([int(x, 16) for x in f])
    return rows

def stackish(v):
    """栈/堆/mmap 这类与布局相关的地址：模拟器与真机不可能一致，按同一类处理"""
    return (0x01000000 <= v < 0x20000000) or v >= 0x1000000000

def disasm(pc):
    out = subprocess.run(["objdump", "-d", f"--start-address={hex(pc)}",
                          f"--stop-address={hex(pc+4)}", ELF],
                         capture_output=True, text=True).stdout.strip().splitlines()
    return out[-1].strip() if out else "?"

def main():
    em = emu_trace()
    nat = nat_trace()
    print(f"模拟器 {len(em)} 条 / 真机 {len(nat)} 条（最多 {N} 步）")
    # 真机行: pc x0 x1 x2 x19 x20 sp lr nzcv   → 索引 0..8
    idxmap = {1: "x0", 2: "x1", 3: "x2", 4: "x19", 5: "x20", 7: "x30"}
    for i in range(min(len(em), len(nat))):
        pc, insn, regs = em[i]
        n = nat[i]
        if n[0] != pc:
            print(f"✗ 第 {i} 步 pc 不同：模拟 {hex(pc)} 真机 {hex(n[0])}")
            print(f"   上一条 pc={hex(em[i-1][0])}  {disasm(em[i-1][0])}")
            return 1
        diffs = []
        if 'nzcv' in regs and regs['nzcv'] != n[8]:
            diffs.append(f"nzcv: 模拟={regs['nzcv']:x} 真机={n[8]:x}")
        for idx, k in idxmap.items():
            a, b = regs.get(k, 0), n[idx]
            if stackish(a) or stackish(b):
                continue
            if a != b:
                diffs.append(f"{k}: 模拟={a:#x} 真机={b:#x}")
        if diffs:
            print(f"✗ 第 {i} 步 pc={hex(pc)} 首次不一致：" + "; ".join(diffs))
            print(f"   ★ 元凶：pc={hex(em[i-1][0])}  {disasm(em[i-1][0])}")
            return 1
    print(f"✓ 前 {min(len(em), len(nat))} 步一致")
    return 0

sys.exit(main())
