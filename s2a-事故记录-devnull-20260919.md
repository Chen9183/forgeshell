# s2a 事故记录：产物 chmod 把宿主 `/dev/null` 改成 0755，导致所有 WebView App 起不来

- **发生时间**：2026-09-18 22:42 – 23:04（最后一次 23:04:48）
- **暴露时间**：2026-09-19 10:45（用户启动 App 时）
- **记录时间**：2026-09-19
- **当前状态**：宿主 `/dev/null` 已 `chmod 666` 修回，**系统已恢复**；s2a 代码里的炸弹**已拆**（2026-09-19 修，见文末「已修记录」）。

---

## 一句话

`s2a` 编译完会对**用户指定的输出路径**无条件执行 `chmod(输出, 0755)`。当时为了只看 AST dump 用了 `-o /dev/null` 丢弃产物，于是这句话变成了 `chmod("/dev/null", 0755)`；而容器的 `/dev` 直通宿主 `/dev`（同一 inode），**宿主设备节点的权限被改**，安卓的 `webview_zygote` 因此无法打开 `/dev/null` 而自杀，所有依赖系统 WebView 的 App 全部启动失败。

---

## 症状（当时现场）

- DSH App（`deepseekharness.chen9183.github`）与 X 浏览器（`com.mmbox.xbrowser`）启动白屏 / 秒退
- Firefox、Chrome 正常（自带渲染引擎，不走系统 WebView）→ **这是 WebView 故障的判别指纹**
- `webview_zygote` tombstone 反复出现：

```
signal 6 (SIGABRT)   Cmdline: webview_zygote   uid: 1053
Abort message: 'JNI FatalError called: (zygote) Failed to open /dev/null: Permission denied'
  #06 libandroid_runtime.so (android::zygote::ForkCommon ...)
  #05 libandroid_runtime.so (android::zygote::ZygoteFailure ...)
```

- 现场权限：`crwxr-xr-x root root u:object_r:null_device:s0 /dev/null` → 应为 `crw-rw-rw-`（SELinux 标签正常，纯权限位被改）

## 因果链

1. `webview_zygote` 以 **uid 1053（非 root）** 运行，fork 渲染子进程时要用 `/dev/null` 兜底 fd（`open("/dev/null", O_WRONLY)`）。
2. `/dev/null` 是 0755 → 非 root 只读不可写 → `EACCES` → `ForkCommon` 失败 → `JNI FatalError` → `abort()`，zygote 自杀。
3. 孵化器一死，任何 App 加载 WebView 都拿不到渲染进程 → 全部白屏/闪退。

## 证据链（可复核）

| 证据 | 内容 |
|---|---|
| 宿主 ctime | `2026-09-18 23:04:48.613982973`（`stat /dev/null`） |
| 会话记录 | `/.dsh/sessions/--root--/session-2316c8ac-483d-40c7-abb3-5900addb1a34/` 里 `tool/call` 时间戳同为 `09-18 23:04:48`，命令为 `... s2a -g2 -o /dev/null ck.sh 2>&1 \| head -8` |
| 踩坑次数 | 同一会话 `-o /dev/null` 共 7 次：22:42:06 h1.sh / 22:44:02 h4.sh / 22:46:05 w1.sh / 22:46:15 w2.sh / 22:48:10 l2.sh / 22:51:17 d.sh / 23:04:48 ck.sh |
| 直通性 | 容器与宿主 `/dev/null` 同为 `dev=16 inode=10` → **同一个 inode** |

---

## 关键环境事实（务必记住）

- **chroot 的 `/dev` 就是宿主 `/dev`**（同一 inode）。在容器里对 `/dev/*` 做 `chmod`/写入/删除，**直接作用到宿主设备节点**，可以搞挂系统服务。
- 同理，容器 `/tmp` 与宿主 `/tmp` 是同一个目录。
- 因此在容器里跑任何"给路径设权限/写特殊文件"的程序（编译器、安装器、打包器）时，只要它接受用户路径参数，就有踩宿主设备节点的风险。

## 当前已做的临时修复

```
hop chmod 666 /dev/null        # 立即生效，无需重启
```

自助诊断（以后遇到"某类 App 突然都起不来"）：

```
hop stat -c '%A %n' /dev/null                       # 必须是 crw-rw-rw-
hop logcat -d -b crash | grep "Failed to open /dev/null"
```

---

## 拆弹待办（**已完成**，见文末「已修记录」）

### 需要修改的调用点（`chmod(..., 0755)`）

用户可控的输出路径（**必须修**）：

- `src/compiler/elf.c:281` — `chmod(outpath, 0755)`（普通编译路径）
- `src/compiler/main.c:555` — `chmod(out, 0755)`
- `src/compiler/main.c:856` — `chmod(o.output, 0755)`（`-easy` 路径）
- `tools/s2a-enc.c:64` — `chmod(argv[2], 0755)`（直接是命令行参数）

内部临时/解包路径（**建议一并修**，防同类事故）：

- `src/compiler/main.c:157` — `chmod(tmp, 0755)`
- `src/compiler/main.c:628` — `chmod(tmp, 0755)`
- `src/launcher/launcher.c:96` — `chmod(tmp, 0755)`
- `src/launcher/launcher.c:256` — `chmod(p, 0755)`

### 建议补丁

加一个统一 helper（放在公共头，例如 `src/common/`），**只对普通文件设权限**：

```c
#include <sys/stat.h>

/* 只给普通文件设权限：设备节点/FIFO/目录/符号链接一律跳过。
   背景：曾对 -o /dev/null 的输出执行 chmod 0755，把宿主 /dev/null 改成 0755，
   导致 Android webview_zygote（uid 1053 非 root）无法打开 /dev/null 而 SIGABRT，
   系统上所有依赖 WebView 的 App 全部启动失败（2026-09-19 事故）。 */
static void s2a_set_perm(const char *path, mode_t mode)
{
    struct stat st;
    if (!path || !*path) return;
    if (stat(path, &st) != 0) return;      /* 不存在就什么都不做 */
    if (!S_ISREG(st.st_mode)) return;      /* 非普通文件绝不动权限 */
    chmod(path, mode);
}
```

然后把上表各处 `chmod(x, 0755)` 换成 `s2a_set_perm(x, 0755)`。

### 可选增强（体验向）

- 给 s2a 加"只 dump 不写产物"的开关（如 `-o -` 或 `--no-output`），这样以后看 AST 不必拿 `/dev/null` 当垃圾桶。
- 输出路径是非普通文件时可打印一行提示（不报错），便于发现类似误用。

### 回归测试（修完必跑）

```sh
# 1) 权限保护必须生效：非普通文件不被改动
cd /root/rev-shell/v2
hop stat -c '%A' /dev/null                     # 记录修前：应为 crw-rw-rw-
printf 'echo hi\n' > /tmp/rg.sh
./bin/s2a -g2 -o /dev/null /tmp/rg.sh >/dev/null 2>&1
hop stat -c '%A' /dev/null                     # 断言：仍是 crw-rw-rw-

# 2) 普通文件产物行为不变（仍可执行）
./bin/s2a -o /tmp/rg.elf /tmp/rg.sh && /tmp/rg.elf     # 期望输出 hi
ls -l /tmp/rg.elf                                       # 期望 -rwxr-xr-x

# 3) 原有回归套件
bash tests/run_e2e.sh
```

---

## 铁的纪律（写进习惯，避免复发）

1. **永远不要用 `-o /dev/null` 丢产物** —— 用 `-o /tmp/scratch.elf`（或者修完后用新的 `--no-output`）。
2. 在容器里跑任何"按参数改权限/写文件"的工具时，先想一秒：**这个路径会不会落到 `/dev` 或别的宿主设备节点上**。
3. 遇到"某类 App 突然集体起不来、而 Firefox/Chrome 正常"，第一时间 `hop stat -c '%A' /dev/null`。

（可复核的会话记录：`session-2316c8ac-483d-40c7-abb3-5900addb1a34`，09-18 22:42–23:08 段。）

---

## 已修记录（2026-09-19）

**做法**：新增统一入口 `src/common/safeperm.h`（`static inline`，无需改构建依赖）：

```c
/* 只给普通文件设权限：设备节点/FIFO/目录/符号链接一律跳过 */
static inline int s2a_set_perm(const char *path, mode_t mode)
{
    struct stat st;
    if (!path || !*path) return 0;
    if (stat(path, &st) != 0) return 0;      /* 不存在：什么都不做 */
    if (!S_ISREG(st.st_mode)) return 0;      /* 非普通文件：绝不改权限 */
    return chmod(path, mode) == 0 ? 1 : 0;
}
/* 路径是否存在且不是普通文件（用于给用户一行提示） */
static inline int s2a_perm_special_path(const char *path);
```

**改动点（8 处 `chmod` 全部改走 helper，全项目再无直接 `chmod` 调用）**：

| 文件 | 位置 | 性质 | 处理 |
|---|---|---|---|
| `src/compiler/elf.c` | 编译产物输出 | 用户可控 | `s2a_set_perm` + 非普通文件提示 |
| `src/compiler/main.c` | `--only-enc` 输出 | 用户可控 | `s2a_set_perm` + 提示 |
| `src/compiler/main.c` | `-easy` 输出 | 用户可控 | `s2a_set_perm` + 提示 |
| `tools/s2a-enc.c` | 独立加密器输出 | 用户可控 | `s2a_set_perm` + 提示 |
| `src/compiler/main.c` | 内嵌工具解出（tmp） | 内部 | `s2a_set_perm` |
| `src/compiler/main.c` | 内嵌 qemu 解出（tmp） | 内部 | `s2a_set_perm` |
| `src/launcher/launcher.c` | easy 解包 ash（tmp） | 内部 | `s2a_set_perm` |
| `src/launcher/launcher.c` | enc 兜底落盘解密结果 | 内部 | `s2a_set_perm` |

**附带**：

- 输出路径不是普通文件时打一行 `s2a: 提示：输出 X 不是普通文件，已跳过权限设置`（当场就能发现误用）。
- `Makefile` 的 launcher 规则补上 `$(wildcard src/common/*.h)` 依赖（改 safeperm.h 会重编外壳）。
- 文案同步：`assets/help-*.txt`（四档）、`README.md`、`DESIGN.md`；外壳尺寸 87,928 → **88,184** 字节。

**回归结果（全绿）**：

```sh
hop stat -c '%A' /dev/null                                 # crw-rw-rw-（修前）
./bin/s2a -g2 -o /dev/null /tmp/rg.sh                      # 退出码 0，打一行提示
hop stat -c '%A' /dev/null                                 # crw-rw-rw-（修后，未被改动）✓
./bin/s2a -o /tmp/rg.elf /tmp/rg.sh && /tmp/rg.elf         # 输出 hi，权限 -rwxr-xr-x ✓
./bin/s2a --only-enc /tmp/rg.elf /dev/null                 # /dev/null 保持 crw-rw-rw- ✓
./bin/s2a -easy -o /dev/null /tmp/rg.sh                    # /dev/null 保持 crw-rw-rw- ✓
/tmp/s2a-enc /tmp/rg.elf /dev/null                         # 独立加密器同样受保护 ✓
bash tests/run_e2e.sh                                      # 全绿 ✓
bash tests/stress_protection.sh                            # PASS=21 FAIL=0 ✓
```

**仍未做（可选增强，留待需要时）**：

- `-o -` / `--no-output`：只 dump（AST/IR/机器码）不写产物的开关，这样看 dump 不必拿 `/dev/null` 当垃圾桶。
  （安全上已无必要 —— `-o /dev/null` 现在不会改权限；纯粹是体验向。）
