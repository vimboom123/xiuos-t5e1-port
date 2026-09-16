# 07 — 构建环境与上游缺陷

要在本机构建 XiUOS，先得把五个坑填掉。这份文档记录每个坑的**现象、根因、修法**，
以及已经固化进 `tools/` 的两个脚本。

> 复现命令：
> ```
> wsl -- bash /mnt/d/ds/xizi-build.sh nuvoton-m2354          # 基线板
> wsl -- bash /mnt/d/ds/xizi-build.sh bk7258 --overlay       # 本移植的板子
> ```

---

## 0. 环境结论（实测）

| 项 | 值 |
|---|---|
| 构建环境 | **WSL2 Ubuntu 22.04**（Windows 侧没有 make，XiUOS 的 Makefile 是纯 POSIX 的） |
| 交叉工具链 | `gcc-arm-none-eabi` **10.3.1**（`apt` 的 `15:10.3-2021.07-4`） |
| 其它依赖 | `kconfig-frontends`（提供 `kconfig-conf` / `kconfig-mconf`）、`build-essential`、`rsync` |
| 为什么不用 Windows 原生的工具链 | TuyaOpen 自带的 `gcc-arm-none-eabi-10.3-2021.10` 是 Windows `.exe`；从 WSL 调用它做交叉编译，路径语义会打架。**装一份 Linux 原生的更省事，且版本号一致（都是 10.3）。** |
| 上游代码放哪 | `~/xiuos/`（WSL 原生盘）。9p 上（`/mnt/d`）编译几百个 `.o` 慢得离谱。 |
| 所需磁盘 | XiUOS `XiZi_IIoT_Macro` 342 MB + `APP_Framework` ≈ 250 MB |

---

## 坑 1：目录层级必须复刻上游 —— 这是最隐蔽的一个

**现象**：`kconfig-conf` 报

```
/home/<user>/xizi/Kconfig:10: can't open file "/home/<user>/xizi/../../APP_Framework/Kconfig"
```

**根因**：XiUOS 在仓库里的真实层级是

```
<repo>/Ubiquitous/XiZi_IIoT_Macro/     <- KERNEL_ROOT
<repo>/APP_Framework/                  <- 与 Ubiquitous 平级
```

而 `XiZi_IIoT_Macro/Kconfig`、`path_kernel.mk`、顶层 `Makefile` 里有大量
`$(KERNEL_ROOT)/../../APP_Framework/...` 的引用。**只拷 `XiZi_IIoT_Macro` 层级就塌了。**

**这个坑的杀伤力被严重低估**：它不报错退出，只是让 Kconfig 解析失败，
于是 **所有配置符号拿不到默认值** —— 表现为满屏 `undeclared`，
看起来像代码问题，其实是配置没生成。见坑 4。

**修法**：WSL 里复刻同样的层级：

```
~/xiuos/Ubiquitous/XiZi_IIoT_Macro/
~/xiuos/APP_Framework/
```

### 坑 1 的第二个面向：`SRC_APP_DIR` 必须导出

层级对了之后，`kconfig-conf` 还会报：

```
APP_Framework/Kconfig:8: can't open file "./Framework/Kconfig"
...:3: warning: environment variable SRC_APP_DIR undefined
```

`APP_Framework/Kconfig` 第 3–5 行是这么取路径的：

```
config APP_DIR
    string
    option env="SRC_APP_DIR"
    default "."
source "$APP_DIR/Framework/Kconfig"
```

而顶层 `Makefile:40` 写着 `export SRC_APP_DIR := ../../APP_Framework`。
**不导出这个环境变量，`$APP_DIR` 就退化成 `.`**，于是去找 `./Framework/Kconfig`。

所以 kconfig 那一步的命令行环境必须同时具备：

```bash
export KERNEL_ROOT="$DST"                    # -> Kconfig 里的 $KERNEL_DIR
export BSP_ROOT="$DST/board/$BOARD"          # -> $BSP_DIR
export SRC_APP_DIR="../../APP_Framework"     # -> $APP_DIR
```

### 坑 1 的第三个面向：`APP_Framework` 也要剥 CRLF

它自己的 `Kconfig` 同样带 CR，会让 kconfig 报
`warning: ignoring unsupported character ''` 并干扰 source 解析。
实测那棵树要修 **4,753 个文件**（`XiZi_IIoT_Macro` 是 5,965 个）。

**三个面向合起来才是完整的坑 1。** 任何一个没处理，症状都是同一个：
`.config` 补不全 → 满屏 `NAME_NUM_MAX` / `MEM_ALIGN_SIZE` undeclared → 看起来像代码问题。

---

## 坑 2：整棵树是 CRLF，扔进 WSL 直接崩

**现象**：

```
./tool/hosttools/xsconfig.sh: /bin/bash^M: bad interpreter: No such file or directory
```

**根因**：Windows 上 `git clone` 出来时 `core.autocrlf` 把整棵树转成了 CRLF。
不只是 `.sh` —— **Makefile 里的 `\r` 会附到每条命令末尾**，是更广泛的地雷。

**修法**：同步到 WSL 后剥一遍。用 `grep -rlI` 只挑含 CR 的**文本**文件，二进制文件自然被跳过：

```bash
while IFS= read -r f; do sed -i 's/\r$//' "$f"; done < <(grep -rlI $'\r' . 2>/dev/null)
```

实测一棵树要修 **5,965 个文件**。

---

## 坑 3：上游 bug —— 两个 `prepare_ahwstack.c` 缺 `<stdint.h>`

**现象**：

```
arch/arm/cortex-m23/prepare_ahwstack.c:390:21: error: unknown type name 'uintptr_t'
```

**根因**：这是个**抄漏**。三份同名文件的 include 块对比：

| 文件 | `#include <stdint.h>` |
|---|---|
| `arch/arm/shared/prepare_ahwstack.c`（参考副本） | ✅ 第 21 行有 |
| `arch/arm/cortex-m23/prepare_ahwstack.c` | ❌ 没有 |
| `arch/arm/cortex-m33/prepare_ahwstack.c` | ❌ 没有 |

该文件第 390 行用了 `uintptr_t`，于是编译器（GCC 10.3）直接罢工。

**修法**：加一行 `#include <stdint.h>`。已固化进 `tools/fix_upstream.py`，幂等。

---

## 坑 4：陈旧 `.defconfig` 缺 Kconfig 新增符号

**现象**：

```
kernel/include/xs_timer.h:41:26: error: 'NAME_NUM_MAX' undeclared
kernel/memory/byte_manage.c:641:35: error: 'MEM_ALIGN_SIZE' undeclared
```

**根因**：这两个符号**确实在 Kconfig 里有定义**（`kernel/Kconfig:267` 与 `:35`），
但各板的 `.defconfig` 是多年前的快照，没带上它们。
上游的解法是让你跑一次交互式 `make menuconfig`——那一步会顺带把缺的符号补成默认值。

**修法**：用非交互的等价物。`kconfig-frontends` 提供：

```bash
kconfig-conf --olddefconfig board/$BOARD/Kconfig
```

⚠️ **前提是坑 1 已修好。** 否则这条命令会静默失败（rc=0 或 rc=1），
`.config` 一行不加，然后你继续看到满屏 `undeclared`，误以为是代码问题。
脚本里把这条的 rc 明确打出来了。

---

## 坑 5：各板 `config.mk` 把 `CROSS_COMPILE` 写死

**现象**：

```
make[3]: /opt/gcc-arm-none-eabi-6-2017-q1-update/bin/arm-none-eabi-gcc: No such file or directory
```

**根因**：每块板的 `config.mk` 都硬编码了作者本机的工具链绝对路径，例如
`board/nuvoton-m2354/config.mk:1`：

```make
export CROSS_COMPILE ?= /opt/gcc-arm-none-eabi-6-2017-q1-update/bin/arm-none-eabi-
```

**修法**：好在它们用的是 `?=`。而 `?=` **不会覆盖已存在的环境变量**，
所以只要导出 `CROSS_COMPILE=arm-none-eabi-` 就能顶掉，不必改上游：

```bash
export CROSS_COMPILE="${CROSS_COMPILE:-arm-none-eabi-}"
```

> **本移植的 `board/bk7258/config.mk` 直接写成 `?= arm-none-eabi-`，不留这个坑。**

---

## 已固化的工具

| 脚本 | 作用 |
|---|---|
| `tools/fix_upstream.py` | 修坑 3（补 `<stdint.h>`），幂等，对任何板子都该跑 |
| `tools/apply_overlay.py` | 铺 `board/bk7258/` + 打两处板子选择补丁，幂等 |

两者都遵循「标记式插入」而不是 `diff/patch`：因为上游文件在不同 clone 上可能是 CRLF，
`patch` 会因上下文不匹配失败，而标记插入只依赖锚点行。

---

## 完整流程（`xizi-build.sh` 的六步）

| 步 | 动作 | 对应坑 |
|---|---|---|
| 1 | `rsync` 到 `~/xiuos/`，复刻 `Ubiquitous/` + `APP_Framework` 层级 | 坑 1 |
| 2 | 剥 CRLF（**两棵树都要**：`XiZi_IIoT_Macro` 5,965 + `APP_Framework` 4,753 个文件） | 坑 2 |
| 3 | `fix_upstream.py` | 坑 3 |
| 3b | 仅 `--overlay`：`apply_overlay.py` 铺 `board/bk7258` + 打补丁 | — |
| 4 | `cp .defconfig .config` → 导出 `SRC_APP_DIR` → `kconfig-conf --olddefconfig` → `xsconfig.sh` | 坑 1、4、5 |
| 5 | `make BOARD=<board>`，日志落 `/tmp/xizi-build.log` | — |
| 6 | 汇总：去重错误清单 / ELF 大小 / 关键符号 / 段布局 | — |

---

## 基线验证结果（2026-09-16 实测）

**先证明工具链和构建系统本身没问题，再去调我们自己的 BSP。** 用上游现成的板子做基线：

```
$ wsl -- bash /mnt/d/ds/xizi-build.sh nuvoton-m2354
make 退出码: 0
ELF: build/XiZi-nuvoton-m2354.elf  (1,505,588 字节)
   text    data     bss     dec     hex
 235752    3228   13116  252096   3d8c0
```

关键符号全部就位：

```
0000ded0 W Reset_Handler
0000d78c T IsrEntry              <- XiUOS 的二级中断派发入口
00010320 T SysTick_Handler
000395e4 T _shell_command_start  <- shell 命令表非空
20002000 B _sp
20003fdc B __bss_end             <- board.h 的 HEAP_BEGIN 依赖它
```

段布局与 `board/nuvoton-m2354/link.lds` 的设计一致：

```
.text   PROGBITS 00000000            <- flash 起点
.stack  NOBITS   20000000  大小 2000 <- SRAM 低端 8 KiB，_sp 指向其顶端
.data   PROGBITS 20002000
.bss    NOBITS   20002ca0
```

`.config` 由 218 行补到 **312 行**，`xsconfig.h` 由 123 行补到 **211 行** ——
这一步补上了 `CONFIG_MEM_ALIGN_SIZE=8` 与 `CONFIG_NAME_NUM_MAX=32`。

> 这一步的价值在于**把问题分离开**：基线编不过时是环境问题，基线编得过而 bk7258 编不过时，
> 问题就一定在 `board/bk7258/` 里。
