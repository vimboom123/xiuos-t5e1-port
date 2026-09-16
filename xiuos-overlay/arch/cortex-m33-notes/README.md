# arch/cortex-m33 判定记录

上游 `arch/arm/cortex-m33/` **已存在**，共 8 个文件：

| 文件 | 大小 | 作用 | BK7258 可直接用？ |
|---|---|---|---|
| `arm32_switch.c` | 6,393 B | 上下文切换 | 待判 |
| `pendsv.S` | 7,146 B | PendSV | 待判 |
| `prepare_ahwstack.c` | 11,521 B | 栈准备 | 待判 |
| `interrupt.c` | 1,991 B | 中断框架 | 待判 |
| `arch_interrupt.h` | 1,767 B | 中断接口 | 待判 |
| `syscall_gcc.S` | 1,629 B | 系统调用 | 待判 |
| `trustzone.c` | 2,331 B | TrustZone | 待判 |
| `Makefile` | 133 B | | |

## 需要逐项确认

- [ ] **中断向量表**：BK7258 的向量表布局与默认 M33 是否一致（有无 Beken 私有偏移）
- [ ] **时钟源**：tick 由哪个定时器驱动，`arch` 是否假设了 SysTick
- [ ] **TrustZone**：上游 `trustzone.c` 是否已支持「Secure / Non-secure 分区」形态 ——
      这直接关系到「无线放 Secure 侧」这条备选路径（见 `docs/05` 的 ①）
- [ ] **FPU**：BK7258 是 M33F（带单精度 FPU），上下文切换是否保存 FPU 寄存器

## 备注

上游另有两块 Cortex-M33 BSP（`board/rzg2ul-m33`、`board/rzv2l-m33`），
但**都是瑞萨 AMP 从核**（主核 A55 跑 Linux、RPMsg 通信），
启动与时钟可能依赖主核，**不能直接照抄到独立 MCU**。
