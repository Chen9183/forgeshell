#!/bin/bash
# 铸壳工具链 · 加固/加密链路压力测试（可复跑）
# 用法: bash tests/stress_protection.sh [s2a路径]
S=${1:-$(cd "$(dirname "$0")/.." && pwd)/bin/s2a}
T=$(mktemp -d); cd "$T" || exit 1
PASS=0; FAIL=0
ok(){ printf '  \033[32m✓\033[0m %s\n' "$1"; PASS=$((PASS+1)); }
no(){ printf '  \033[31m✗\033[0m %s\n' "$1"; FAIL=$((FAIL+1)); }
chk(){ if [ "$2" = "$3" ]; then ok "$1"; else no "$1 (期望=$2 实得=$3)"; fi; }

printf '== A 基础功能\n'
printf 'x=5; echo "base-ok: $((x*2))"\n' > base.sh   # 单行输出，便于严格比对
$S -o base.elf base.sh >/dev/null 2>&1 && chk "编译+运行" "base-ok: 10" "$(./base.elf)" || no "编译失败"
chk "s2a ash" "ash-ok" "$($S ash -c 'echo ash-ok')"
chk "s2a qemu" "1" "$($S qemu --version >/dev/null 2>&1 && echo 1 || echo 0)"
$S -r base.elf >/dev/null 2>&1 && ok "内置模拟器 -r" || no "内置模拟器 -r"

printf '== B POSIX 符合性套件（308 条）\n'
cd "$(dirname "$S")/../tests/posix_conformance" 2>/dev/null && sh run.sh >/dev/null 2>&1
if [ -f all.sh ]; then
  b=$(bash all.sh 2>&1 | tail -1); o=$("$S" -o pc.elf all.sh >/dev/null 2>&1; ./pc.elf 2>&1 | tail -1)
  chk "与 bash 基准一致" "$b" "$o"; rm -f pc.elf sec.elf one.elf
else no "找不到 posix_conformance 套件"; fi
cd "$T" || exit 1

printf '== C 20 轮：口令锁（每产物不同口令）\n'
f=0
for i in $(seq 1 20); do
  printf 'echo r%s\n' "$i" > r.sh
  $S -P --password "pw$i" -o r$i.elf r.sh >/dev/null 2>&1 || { f=1; break; }
  [ "$(./r$i.elf --password "pw$i")" = "r$i" ] || { f=1; break; }
  ./r$i.elf --password "pw$(( i % 20 + 1 ))" >/dev/null 2>&1 && { f=2; break; }
done
[ $f = 0 ] && ok "20 组：对口令可跑 + 交换口令必失败" || no "第 $i 组异常(码 $f)"

printf '== D 同一口令 × 10 份产物（考验每产物随机盐）\n'
f=0
for i in $(seq 1 10); do
  printf 'echo same%s\n' "$i" > s.sh
  $S -P --password SAME -o s$i.elf s.sh >/dev/null 2>&1 || { f=1; break; }
  [ "$(./s$i.elf --password SAME)" = "same$i" ] || { f=1; break; }
done
[ $f = 0 ] && ok "10 份同口令产物全部可解" || no "第 $i 份异常"
[ "$(tail -c 3000 s1.elf | md5sum)" != "$(tail -c 3000 s2.elf | md5sum)" ] && ok "两份盐/密文不同" || no "两份内容雷同"

printf '== E --only-enc（无口令 10 轮 / 有口令 10 轮）\n'
f=0
for i in $(seq 1 10); do
  $S --only-enc base.elf e$i.enc >/dev/null 2>&1 || { f=1; break; }
  [ "$(./e$i.enc)" = "base-ok: 10" ] || { f=1; break; }
done
[ $f = 0 ] && ok "无口令加密 ×10 直接可跑" || no "无口令第 $i 轮异常"
f=0
for i in $(seq 1 10); do
  $S --only-enc base.elf p$i.enc --password "op$i" >/dev/null 2>&1 || { f=1; break; }
  [ "$(./p$i.enc --password "op$i")" = "base-ok: 10" ] || { f=1; break; }
  ./p$i.enc >/dev/null 2>&1 && { f=2; break; }                 # 不给口令必须失败
  ./p$i.enc --password wrong >/dev/null 2>&1 && { f=3; break; } # 错口令必须失败
done
[ $f = 0 ] && ok "有口令加密 ×10：对口令可跑/不给或错都失败" || no "第 $i 轮异常(码 $f)"

printf '== F 口令四种来源（各 3 轮）\n'
f=0
printf 'pwF' > pw.txt
for i in 1 2 3; do
  PWX=pwF $S -P --password-env PWX -o fe$i.elf base.sh >/dev/null 2>&1   # 编译期也要设变量
  [ "$(PWX=pwF ./fe$i.elf --password-env PWX)" = "base-ok: 10" ] || { f=1; break; }
  [ "$(./fe$i.elf --password-file pw.txt)" = "base-ok: 10" ] || { f=2; break; }
  [ "$(S2A_PASSWORD=pwF ./fe$i.elf)" = "base-ok: 10" ] || { f=3; break; }
  [ "$(./fe$i.elf --password pwF)" = "base-ok: 10" ] || { f=4; break; }
done
[ $f = 0 ] && ok "--password/-env/-file/S2A_PASSWORD 全通" || no "第 $i 轮来源 $f 失败"

printf '== G 篡改 / 截断 / 自检有效性\n'
LAU=$(stat -c%s ../build/launcher.elf 2>/dev/null || echo 0)
$S -P -o prot.elf base.sh >/dev/null 2>&1
# 在"追加的代码/池区"（模板之后、文件末尾之前）做 20 点扫描，看自检能抓到几处
tot=0; hit=0; sz=$(stat -c%s prot.elf)
for k in $(seq 1 20); do
  off=$(( sz - 300 * k ))
  [ $off -lt $(( LAU + 1000 )) ] && break
  cp prot.elf scan.elf
  printf '\x5a' | dd of=scan.elf bs=1 seek=$off conv=notrunc 2>/dev/null
  tot=$((tot+1))
  ./scan.elf >/dev/null 2>&1 || hit=$((hit+1))
done
if [ $hit -gt 0 ]; then ok "池/代码区改动被自检抓到（$hit/$tot 个采样点）"; else no "自检未抓到任何改动（0/$tot）"; fi
cp prot.elf p2.elf; ./p2.elf >/dev/null 2>&1 && ok "未改动时正常运行" || no "未改动却不跑"
# 密文：从尾部索引里读真实 off1，再改它的头部 4 字节
$S --only-enc base.elf oe.enc >/dev/null 2>&1
OFF1=$(python3 -c "import struct;d=open('oe.enc','rb').read();print(struct.unpack('<Q',d[-112:-104])[0])")
cp oe.enc cor.enc; printf '\x00\x00\x00\x00' | dd of=cor.enc bs=1 seek="$OFF1" conv=notrunc 2>/dev/null
rc=$(./cor.enc >/dev/null 2>&1; echo $?)
chk "改密文头部 4 字节（off1=$OFF1） → 解出非 ELF 被拒" "70" "$rc"
cp oe.enc cut.enc; truncate -s $(( $(stat -c%s cut.enc) - 40 )) cut.enc
rc=$(./cut.enc >/dev/null 2>&1; echo $?)
chk "截断尾部 → 外壳干净报错（非 0）" "非0" "$([ "$rc" != 0 ] && echo 非0 || echo 0)"
cp oe.enc mid.enc; printf '\x00\x00\x00\x00' | dd of=mid.enc bs=1 seek=$(( OFF1 + 8000 )) conv=notrunc 2>/dev/null
rc=$(./mid.enc >/dev/null 2>&1; echo $?)
printf '  \033[33m!\033[0m 已知局限：改密文中段 rc=%s（AES-CTR 无认证，仅校验头部 ELF 魔数 → 能防看、不防改）\n' "$rc"

printf '== H 明文泄漏检查\n'
printf 'echo LEAK_MARKER_QZ\n' > lk.sh
$S -o lk_p.elf lk.sh >/dev/null 2>&1; $S -P -o lk_p2.elf lk.sh >/dev/null 2>&1; $S -F -o lk_p3.elf lk.sh >/dev/null 2>&1
$S -easy -o lk_e.elf lk.sh >/dev/null 2>&1; $S --only-enc lk_p2.elf lk_o.enc >/dev/null 2>&1
chk "无防护（应命中 1）" "1" "$(grep -c LEAK_MARKER_QZ lk_p.elf)"
chk "-P（应 0）" "0" "$(grep -c LEAK_MARKER_QZ lk_p2.elf || true)"
chk "-F（应 0）" "0" "$(grep -c LEAK_MARKER_QZ lk_p3.elf || true)"
chk "-easy（脚本正文，应 1）" "1" "$(grep -c LEAK_MARKER_QZ lk_e.elf || true)"
chk "only-enc（应 0）" "0" "$(grep -c LEAK_MARKER_QZ lk_o.enc || true)"
chk "s2a 里的帮助明文（应 0）" "0" "$(strings -a "$S" | grep -c '铸壳工具链' || true)"

printf '== I 性能\n'
t0=$(date +%s%N); ./lk_p2.elf >/dev/null; t1=$(date +%s%N); echo "  带防护启动: $(( (t1-t0)/1000000 )) ms"
t0=$(date +%s%N); for i in $(seq 1 20); do $S -o bench.elf base.sh >/dev/null 2>&1; done; t1=$(date +%s%N)
echo "  编译 20 次: $(( (t1-t0)/1000000 )) ms（每次 $(( (t1-t0)/20000000 )) ms）"
t0=$(date +%s%N); for i in $(seq 1 20); do ./e1.enc >/dev/null; done; t1=$(date +%s%N)
echo "  加密产物启动 20 次: $(( (t1-t0)/1000000 )) ms"

printf '\n== 汇总: PASS=%s FAIL=%s\n' "$PASS" "$FAIL"
cd /; rm -rf "$T"
[ "$FAIL" = 0 ]
