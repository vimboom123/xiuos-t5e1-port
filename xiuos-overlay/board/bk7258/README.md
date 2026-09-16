# board/bk7258/ —— 待实现

XiUOS 的 BK7258 板级支持包。**模仿 `board/nuvoton-m2354/`。**

## 要创建的文件（参照 nuvoton-m2354）

| 文件 | 作用 | 状态 |
|---|---|---|
| `.defconfig` | 板级默认配置 | 待建 |
| `board.c` | 板级初始化（时钟、串口、tick 源） | 待建 |
| `board.h` | 板级头 | 待建 |
| `link.lds` | **链接脚本** — 按 BK7258 真实内存布局重写 | 待建 |
| `config.mk` | 构建变量 | 待建 |
| `Kconfig` | 配置项 | 待建 |
| `Makefile` | | 待建 |
| `third_party_driver/` | Beken UART / GPIO / Timer / Flash 对接 | 待建 |
| `amp/`（可选） | 双核 IPC，参照 `board/rzg2ul-m33/amp` | 待建 |

## 内存布局要点（BK7258）

现有构建产物给出的分区占用（供 `link.lds` 参考）：

```
bl2  32,480 B     limit    65,536 B
cp   968,852 B    limit 1,048,576 B
ap 1,843,200 B    limit 3,670,016 B
OTA  1,705.0K / 2,940.0K (58.0%)
```

SRAM 与 PSRAM 分区段名（来自构建 map）：

```
FLASH  IRAM  RAM  SWAP  ITCM0/1/2  DTCM0/1/2  SPINLOCK
PSRAM_SLAB_USER  PSRAM_SLAB_AUDIO  PSRAM_SLAB_ENCODE
PSRAM_SLAB_DISPLAY  PSRAM_STACK_HEAP  PSRAM_HEAP  PSRAM_SECTION
```

## 待办

- [ ] 读 `board/nuvoton-m2354/` 的 6 个核心文件，列出一个 XiUOS BSP 的最小契约
- [ ] 从 `app.map` / `sdkconfig` / `auto_partitions.csv` 反推 BK7258 布局
- [ ] 判定 AP / CP 两侧是否都需要 BSP
