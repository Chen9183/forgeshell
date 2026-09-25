#!/usr/bin/env python3
"""emu_diff.py —— 模拟器「同址对拍」工具

用法:  python3 tests/emu_diff.py <artefact.elf> [步数]

原理：让真机（gdb 单步）与内置模拟器执行同一个产物，逐条比较 pc 与寄存器，
在没有栈布局差异的位置上找出**第一处不一致**，并打印上一条指令的反汇编——
那一条就是模拟器译错的指令。栈区地址（模拟器与真机的栈不在同一段）按“同一类”处理。
"""
import re, subprocess, sys, os, tempfile

ELF = sys.argv[1] if len(sys.argv) > 1 else "/tmp/e1.elf"
N   = int(sys.argv[2]) if len(sys.argv) > 2 else 400
S2A = os.path.join(os.path.dirname(__file__), "..", "bin", "s2a")

def emu_trace():
    env = dict(os.environ, S2A_EMU_REGS="1", S2A_EMU_TRACE_FROM="1", S2A_EMU_TRACE_TO=str(N + 50),
               S2A_EMU_ENV_STRICT="1")
    p = subprocess.run([S2A, "-r", "--emu-trace", ELF], capture_output=True, text=True, env=env, timeout=180)
    out = []
    for l in (p.stdout + p.stderr).splitlines():
        m = re.search(r'\[(0x[0-9a-f]+)\] ([0-9a-f]{8}) (x0=.*)$', l)
        if not m:
            continue
        regs = {k: int(v, 0) for k, v in re.findall(r'(x\d+|sp|nzcv)=(0x[0-9a-f]+|\d+)', m.group(3))}
        out.append((int(m.group(1), 16), int(m.group(2), 16), regs))
    return out

def native_trace():
    script = ["set startup-with-shell off", "set pagination off",
              "b *0x100068", "run", "set $i = 0", f"while $i < {N}",
              'printf "%#lx %#lx %#lx %#lx %#lx %#lx %#lx %#lx %#lx\\n", $pc, $x0, $x1, $x2, $x3, $x19, $x20, $sp, ($cpsr >> 28) & 0xf',
              "stepi", "set $i = $i + 1", "end", "quit"]
    with tempfile.NamedTemporaryFile("w", suffix=".gdb", delete=False) as f:
        f.write("\n".join(script) + "\n")
        path = f.name
    env = {"PATH": "/usr/bin:/bin"}   # 与模拟器内的最小环境逐字节一致（否则扫描循环次数会不同）
    p = subprocess.run(["/usr/bin/gdb", "-q", "-batch", "-x", path, ELF],
                       capture_output=True, text=True, env=env, timeout=600)
    os.unlink(path)
    out = []
    for l in p.stdout.splitlines():
        f = l.split()
        if len(f) == 9 and f[0].startswith("0x"):
            out.append([int(x, 16) for x in f])
    return out

def stackish(v):
    """栈/堆/mmap 这类“与具体地址布局相关”的指针：模拟器与真机不可能一致，按同一类处理。
    模拟器：栈 0x7d..-0x7e..，堆 0x50..，mmap 0x80..
    真机：栈与各段都在 0x7f_xxxx_xxxx（高位）区间"""
    if 0x7d000000 <= v < 0x80000000:      # 模拟器栈
        return True
    if v >= 0x1000000000:                  # 真机高位区（栈/映射）
        return True
    return 0x4f000000 <= v < 0x90000000    # 模拟器的 TLS/堆/mmap 区

def main():
    em = emu_trace()
    nat = native_trace()
    print(f"模拟器 {len(em)} 条 / 真机 {len(nat)} 条")
    regmap = {1: "x0", 2: "x1", 3: "x2", 4: "x3", 5: "x19", 6: "x20"}
    for i in range(min(len(em), len(nat))):
        pc, insn, regs = em[i]
        n = nat[i]
        if n[0] != pc:
            print(f"✗ 第 {i} 步 pc 不同：模拟 {hex(pc)} 真机 {hex(n[0])}"); return 1
        diffs = []
        # 先比标志位：分支走错往往因为 NZCV 不一致
        if 'nzcv' in regs and regs['nzcv'] != n[8]:
            diffs.append(f"nzcv: 模拟={regs['nzcv']:x} 真机={n[8]:x}")
        for idx, k in regmap.items():
            a, b = regs.get(k, 0), n[idx]
            # 任一侧是“布局相关指针”（栈/堆/mmap）就跳过：那差异来自栈布局，不是译码错误
            if stackish(a) or stackish(b):
                continue
            if a != b:
                diffs.append(f"{k}: 模拟={a:#x} 真机={b:#x}")
        if diffs:
            print(f"✗ 第 {i} 步 pc={hex(pc)} 首次不一致：" + "; ".join(diffs))
            print("   （上一条指令就是元凶）")
            pv, pi, _ = em[i - 1]
            dis = subprocess.run(["objdump", "-d", f"--start-address={hex(pv)}",
                                  f"--stop-address={hex(pv+4)}", ELF],
                                 capture_output=True, text=True).stdout.strip().splitlines()
            print(f"   上一条 pc={hex(pv)} insn={pi:#010x}")
            if dis: print("   " + dis[-1].strip())
            return 1
    print(f"✓ 前 {min(len(em), len(nat))} 步一致")
    return 0

sys.exit(main())
