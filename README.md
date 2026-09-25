# 铸壳工具链（ForgeShell）· `s2a`

把 **shell 脚本**编译成 **aarch64 纯静态 ELF** 的自包含工具链。
产物没有 `PT_INTERP`、没有 `.dynamic`，不依赖目标机上的 libc 或 shell —— 拷过去就能跑。

工具链**自己**还带三件顺手能用的东西：**内置 AArch64 模拟器**、**内嵌 qemu**、**内嵌 dash（真 ash）**，
外加一整套**加密/加固**能力（KDF v2 密钥派生、字符串池加密、完整性自检、口令锁、整文件加密）。

> 加密与加固的**全部参数、字节布局与局限**见 [`DESIGN.md`](DESIGN.md)。
> 绝对完整的用户手册：`s2a --help-full`；构建与开发笔记：`s2a --help-dev`。

---

## 1. 克隆与构建

```sh
git clone https://github.com/Chen9183/forgeshell.git
cd forgeshell
make            # 出两个：bin/s2a（带符号）与 bin/s2a-s（strip 过，功能一致）
make test       # 可选：自检（发射器/前端解析/端到端编译运行/POSIX 一致性）
./bin/s2a -h    # 看看帮助
```

依赖：`musl-gcc`、`ld`、`upx`（**只在构建工具链时需要**；编译脚本时不需要任何外部工具）。
`musl-gcc` 通常来自 `musl-tools` 包（Debian/Ubuntu：`apt install musl-tools upx-ucl`）。

仓库里只有源码 + 两个构建输入（`assets/*.upx`），**没有构建产物**：`build/`（模板、内嵌 blob、帮助密文）与
`bin/` 全部是 `make` 现场生成的，已在 `.gitignore` 里。整个构建约 1~2 分钟，无需联网。

| 构件 | 大小 | 说明 |
|---|---|---|
| `bin/s2a` | 2,889,056 B | 编译器本体 |
| `bin/s2a-s` | 2,845,616 B | 同上，strip 过（分发用） |
| `build/template.elf` | 445,680 B | 运行时模板（静态 musl，编译期内嵌进 s2a） |
| `build/launcher.elf` | 88,184 B | 产物外壳（`-easy` / `--only-enc` 两模式） |
| `assets/qemu-aarch64-static.upx` | 1,893,000 B | 内嵌 qemu（UPX -9） |
| `assets/ash.upx` | 94,816 B | 内嵌 dash 0.5.12（自编静态 ash，UPX -9） |
| `assets/help*.txt` | — | 四档帮助源码（**编译时加密**成密文后嵌入，`strings` 捞不到） |

---

## 2. 快速上手

```sh
s2a -o hello.elf hello.sh && ./hello.elf         # 编译 → 运行（产物约 451KB，不带任何工具）
s2a ash -c 'echo 内嵌 dash; echo $((6*7))'       # 用 s2a 自带的 dash（产物里没有这个）
s2a qemu --version                               # 用 s2a 自带的 qemu
s2a -r hello.elf                                 # 内置模拟器解释执行（缺指令自动回退内嵌 qemu）
s2a -easy -o easy.elf hello.sh && ./easy.elf     # 最简模式（外壳 + dash + 脚本，约 183KB）
s2a --only-enc easy.elf easy.enc && ./easy.enc   # 整文件加密（任意 ELF 都行）
s2a -P -o prot.elf hello.sh                      # 防护：池加密 + 完整性自检 + 反调试（无需口令）
s2a -P --password-file pw.txt -o locked.elf hello.sh
./locked.elf --password-file pw.txt arg1         # 运行期解锁，其余参数照旧给脚本
s2a -h | --help | --help-full | --help-dev       # 四档帮助（51 / 252 / 477 / 114 行）
s2a -license                                     # MIT 许可证全文
```

---

## 3. 命令行速查

| 选项 | 作用 |
|---|---|
| `-o <文件>` | 指定产物路径 |
| `-e '<文本>'` | 直接编译一段脚本文本 |
| `-O0 … -O4` | 优化级别（`-F` 顺带拉到 4） |
| `-s` | strip 产物（去符号表） |
| `-g1 … -g5` | 调试输出：阶段统计 / AST / IR / 机器码 / 全量 |
| `-P` / `-F` | 加固（池 AES 加密 + SHA-512 完整性自检 + 反调试）；`-F` 最全 |
| `--password <值>` | 口令锁（编译期用；运行期解锁同样支持这四种来源） |
| `--password-env <变量>` | 口令取自环境变量 |
| `--password-file <文件>` | 口令取自文件（**内容原样**＝`cat`；`echo` 会多换行，建议 `printf`） |
| （兜底）`S2A_PASSWORD` | 上面都没给时读这个环境变量 |
| `--anti-action <策略>` | `warn`(默认) / `report` / `silent` / `fake` / `delay` / `stop` |
| `-easy` | 最简模式：外壳 + 内嵌 dash + 脚本正文 |
| `--only-enc <in> <out>` | 整文件加密（任意 ELF） |
| `-r` / `-qemu` | 内置模拟器解释执行 / 直接用 qemu 执行 |
| `--emu-own` / `--no-fallback` | 遇到未实现的东西不回退（调试模拟器用） |
| `-X`, `--iso` | 走外部 iso 项目 |
| `-h` / `--help` / `--help-full` / `--help-dev` | 四档帮助（快 / 常用 / 完整 / 开发） |
| `-v` / `-license` | 版本 / 许可证 |

退出码：`0` 成功 · `2` 用法 · `70` 口令缺失/错误 · `73` 解不出内嵌工具 ·
`124` 模拟器步数上限 · `126` 不可执行 · `127` 命令未找到 · `132` 未实现指令 · `139` 访存故障。

---

## 4. 加密与加固（速览）

| 能力 | 开关 | 要点 |
|---|---|---|
| **KDF v2** | 全部 | 口令与 seed 两条派生路径都走 **PBKDF2-HMAC-SHA256**（5000 次迭代，域分离盐） |
| 字符串池加密 | `-P` / `-F` | AES-256-CTR；密钥由 seed 经 KDF v2 派生；运行时解密到内存 |
| 完整性自检 | `-P` / `-F` | 代码段与池各带 SHA-512；校验料以密文形式存放；改一个字节都过不了 |
| 口令锁 | `--password[-env|-file]` | 池侧：`PBKDF2-HMAC-SHA256`(口令, 盐=池前 16 字节, 5000 次)；口令不对 → 退出码 70；**无需口令的 `-P`/`-F` 照常可用** |
| 整文件加密 | `--only-enc` | 外壳 + AES-256-CTR 密文 + 128 字节尾部；运行时解密到**匿名内存页**后 `fexecve`；**加了口令就是运行期门禁**（不给/给错 → 70） |
| 反调试 | `-P` / `-F` | 7 层交叉验证；**主进程绝不 ptrace 自锁**；`S2A_NO_ANTIDEBUG=1` 是逃生门 |
| 帮助文本加密 | 默认 | 四档帮助以 AES-256-CTR 密文嵌入（`strings s2a` 捞不到） |
| 输出路径保护 | 全部 | 设权限只对**普通文件**生效（`src/common/safeperm.h` 的 `s2a_set_perm()`）；`-o /dev/null` 之类不会改到设备节点（2026-09-19 事故后加） |

原则：**没有硬编码密钥**。随机材料一律取自 `/dev/random`，所以同一份脚本每次编译的密文都不同；
KDF 迭代数与两侧实现必须一致（编译器 ↔ 运行时镜像 ↔ 外壳），否则产物解出来是乱码。

---

## 5. 语言覆盖

- **POSIX：追求完整**。引用与转义、参数展开全套、特殊参数、算术展开、命令替换（可嵌套、可含 `case`）、
  IFS 字段切分、路径名展开、重定向全形式、管道与列表、`if/elif/else`、`case`、`for/while/until`、
  函数与 `local`、特殊与普通内建（含 `getopts`、`read -r/-d/-n`、`test/[`、`trap`）、
  `set` 全选项与 `set -o 名字`、非交互信号语义。
- **GNU：部分扩展**。`[[ ]]`（含通配与正则）、`(( ))`、`$(( ))` 里嵌 `$( )`、`${v:off:len}`、
  `${v^}` `${v^^}` `${v,}` `${v,,}`、`${v/p/r}` `${v//p/r}`、`${!v}`、`$'…'`、`<<<`、`&>` `&>>`。
- **不支持**：数组/关联数组、`shopt`、`{1..3}` 花括号展开。
- 与 bash/dash 的已知差异：`$-` 只列本工具链真正支持的选项；`${#*}`/`${#@}` 取 `$*` 拼接长度
  （POSIX/dash 口径，bash 给的是参数个数）；不做作业控制（`set -m`）。

---

## 6. 自检与测试

```sh
# POSIX 符合性：308 条，逐条与 bash/dash 对拍（套件在仓库里）
cd tests/posix_conformance && sh run.sh && ../../bin/s2a -o t.elf all.sh && ./t.elf

bash tests/run_e2e.sh          # 端到端：编译若干脚本并核对输出与退出码
tests/gen_isa_bulk.py          # 382 条指令批量对拍（模拟器 vs 真机）
```

判读口径：先跑 `bash all.sh` 做基准，再跑产物；两边都失败的是自检脚本自身写错。
当前状态：套件 308 条，产物与 bash 在 **303 条上完全一致**；余下 5 条是自检脚本自身写错
（两边都不过）——即 **0 条"只有产物失败"**。

---

## 7. 目录结构

```
src/common/    parse.c（词法/语法/AST，编译期与运行期同源）、crypto.c（AES-256-CTR / SHA-2 / HMAC / PBKDF2）、
               safeperm.h（设权限唯一入口：只对普通文件动手）
src/compiler/  main.c（驱动/选项/加密组装）、codegen.c、image.c、elf.c、emit_a64.c、protect.c、emu.c
src/runtime/   运行时模板：rt_main / rt_core / rt_exec / rt_builtin / rt_protect
src/launcher/  产物外壳（-easy / --only-enc 两模式）
tools/         help-enc（帮助文本加密）、s2a-enc（独立整文件加密器）
assets/        qemu/ash 的 UPX blob + 四档帮助 TXT
tests/         posix_conformance/（308 条）、run_e2e.sh、指令对拍脚本
```

---

## 8. 许可证

MIT License，Copyright (c) 2026 Chen9183（`s2a -license` 打印全文）。

Author: **deepseek v4 flash & @Chen9183 (github)**
