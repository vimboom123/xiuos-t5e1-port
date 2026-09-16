/*
 * BK7258 (涂鸦 T5-E1) AP 核链接脚本
 *
 * 模板：board/nuvoton-m2354/link.lds 的**段布局**（照抄，一段不删）
 * MEMORY 段：按 BK7258 实测内存布局重写
 *
 * 内存布局出处（详见 docs/06-board-bk7258-checklist.md §2）：
 *   t5_os/build/bk7258/tuya_app/partitions/ram_regions.h
 *   + 真实链接脚本 bk7258_ap_out.ld
 *   + app.elf 向量表里读出的初始 _sp = 0x28063800（独立佐证 AP_RAM 顶端）
 *
 * SRAM 总 640 KiB @0x28000000，五段精确闭合（0x10000+0x53800+0x3BF00+0x100+0x800 = 0xA0000）：
 *   0x28000000  AP_SPINLOCK   64 KiB   Beken 核间自旋锁
 *   0x28010000  AP_RAM       334 KiB   <-- 本脚本用的就是这一块
 *   0x28063800  CP_RAM       244 KiB   CP 核，越界就是踩别人内存
 *   0x2809F700  PWR_MNG      256 B
 *   0x2809F800  SWAP           2 KiB
 *
 * FLASH：0x02000000 + CONFIG_AP_VIRTUAL_PARTITION_OFFSET(=0x00120000) = 0x02120000
 *        长度 0x380000 = 3.5 MiB。与 app.elf 里 .vectors 段地址 0x02120000 完全吻合。
 *
 * 【不要动的东西】下面这些段是 XiUOS 的组件注册机制，删任何一个都会静默失效：
 *   KEEP(*(.isr_vector))        向量表，必须在 flash 最前
 *   KEEP(*(shellCommand))       否则 shell 里一条命令都没有
 *   KEEP(*(.isrtbl.idx))/.isrtbl  中断服务表（DECLARE_HW_IRQ 填充）
 *   KEEP(*(.g_service_table))   服务表
 *   .ARM.exidx + _sidata       启动代码靠它定位 .data 初值
 *   _sbss/_ebss/__bss_end      board.h 的 HEAP_BEGIN 依赖 __bss_end
 */

MEMORY
{
    flash (rx) : ORIGIN = 0x02120000, LENGTH = 0x380000   /* 3.5 MiB */
    sram  (rw) : ORIGIN = 0x28010000, LENGTH = 0x53800    /* 334 KiB */
}

ENTRY(Reset_Handler)
_system_stack_size = 0x4000;    /* 16 KiB 系统栈（MSP），模板是 8 KiB，这里放宽 */

SECTIONS
{
    .text :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))            /* 启动代码里的向量表 */
        . = ALIGN(4);
        *(.text)                        /* 其余代码 */
        *(.text.*)
        *(.rodata)                      /* 只读数据 */
        *(.rodata*)

        /* shell 命令表 */
        . = ALIGN(4);
        _shell_command_start = .;
        KEEP (*(shellCommand))
        _shell_command_end = .;
        . = ALIGN(4);

        /* 中断服务表 */
        __isrtbl_idx_start = .;
        KEEP(*(.isrtbl.idx))
        __isrtbl_start = .;
        KEEP(*(.isrtbl))
        __isrtbl_end = .;
        . = ALIGN(4);

        /* 服务表 */
        PROVIDE(g_service_table_start = ABSOLUTE(.));
        KEEP(*(.g_service_table));
        PROVIDE(g_service_table_end = ABSOLUTE(.));

        PROVIDE(_etext =  ABSOLUTE(.));

        . = ALIGN(4);
        _etext = .;
    } > flash = 0

    /* .ARM.exidx 必须单独成段：启动代码靠 _sidata 定位 .data 初值 */
    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        _sidata = .;
    } > flash
    __exidx_end = .;

    /* 系统栈放在 SRAM 低端。_sp 指向它的顶端，栈向下生长。
     * 与 BK7258 原厂把 _sp 放在 0x28063800（SRAM 高端）不同，
     * 但两者都在 AP_RAM 内、都不越界；我们跟 XiUOS 既有约定走。
     * startup/boot.S 会显式 ldr r0, =_sp / msr msp, r0。 */
    .stack :
    {
        . = . + _system_stack_size;
        . = ALIGN(4);
        _sp = .;
    } > sram

    /* 已初始化数据，VMA 在 sram、LMA 在 flash */
    .data : AT (_sidata)
    {
        . = ALIGN(4);
        _sdata = . ;

        *(.data)
        *(.data.*)

        . = ALIGN(4);
        _edata = . ;
    } > sram

    __bss_start = .;
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;

        *(.bss)
        *(.bss.*)
        *(COMMON)

        . = ALIGN(4);
        _ebss = . ;
    } > sram
    __bss_end = .;

    _end = .;

    /* 堆从 __bss_end 长到 AP_RAM 顶端；越到 0x28063800 就是 CP 核的地盘了。 */
    PROVIDE(_heap_end = 0x28063800);
}
