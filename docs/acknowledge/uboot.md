# U-Boot 前置知识

本文档从 ARM/Linux 嵌入式工程师的视角，系统介绍 U-Boot 的架构、配置、移植和调试流程。目标是作为面试准备材料以及 R1 项目 U-Boot bring-up 工作的方法论基础。

> 文中以 R1 实机（RK3229 / ARM Cortex-A7 / eMMC / 512 MiB DRAM）为参考平台，但原理适用于所有 Rockchip 和主流 ARM SoC。

---

## 1. U-Boot 是什么

### 1.1 Bootloader 的本质

一颗 SoC 上电时，CPU 先从固化的 ROM（BootROM）开始执行。BootROM 的职责极简：加载一小段代码到片内 SRAM 并跳转过去。这段代码叫 **first-stage bootloader**。

U-Boot 就是其中使用最广泛的**开源**、**跨平台** bootloader。它运行在裸机环境（没有 OS），负责完成硬件初始化后加载并启动操作系统内核。

```
CPU 上电
  → 固化 BootROM（SoC 内部，只读）
    → first-stage loader（DDR 初始化，片内 SRAM 运行）
      → U-Boot proper（完整 bootloader，DRAM 运行）
        → Linux kernel
```

### 1.2 U-Boot 的三层结构

现代 U-Boot 根据硬件限制拆分为三阶段，每阶段在前一阶段初始化好的更大内存空间中运行：

| 阶段 | 全称 | 运行内存 | 大小限制 | 典型职责 |
|---|---|---|---|---|
| **TPL** | Tiny Program Loader | 片内 SRAM（极小，几 KB） | ~12 KiB | DDR 初始化（training）、PLL 配置 |
| **SPL** | Secondary Program Loader | 片内 SRAM 或大 SRAM | ~100 KiB | 加载 U-Boot proper 或 OP-TEE 到 DRAM |
| **U-Boot proper** | 完整 U-Boot | DRAM（几百 MiB） | 无严格限制 | 完整外设驱动、文件系统、网络、脚本引擎 |

**为什么分三层？** 片内 SRAM 通常只有几十 KiB，装不下完整 U-Boot（几百 KiB～几 MiB）。TPL 和 SPL 是"用极小空间做最少事情然后腾出更大空间"的必然选择。

部分平台可以跳过 TPL：如果 BootROM 自身完成了 DDR 初始化，或者 SPL 足够小能放进 SRAM 并自行初始化 DDR。

### 1.3 U-Boot 在嵌入式 Linux 生态中的位置

```
┌─────────────────────────────────────────┐
│ 应用层                                   │
│   systemd, PipeWire, BlueZ, shairport    │
├─────────────────────────────────────────┤
│ Linux kernel + DTB                       │
├─────────────────────────────────────────┤  ← U-Boot 在这里交棒
│ U-Boot proper                            │
│   SPL                                    │
│   [TPL]                                  │
├─────────────────────────────────────────┤
│ BootROM（固化，不可修改）                │
└─────────────────────────────────────────┘
```

U-Boot 是"Linux 之前最后一段可控代码"。它的质量直接影响启动可靠性、恢复能力和安全启动链的完整性。

---

## 2. ARMv7 启动全流程

### 2.1 从电源到内核：完整时序

以 R1 的 RK3229（ARM Cortex-A7）为例，一次冷启动经历以下阶段：

```
时间线 →

BootROM (片内 ROM)
│  读取 bootstrap 引脚，确定启动介质（eMMC? USB? SPI?）
│  初始化基本时钟、UART debug
│  从介质加载 TPL/SPL 到片内 SRAM，校验 Checksum
│  跳转到 SRAM 入口
↓

TPL (片内 SRAM, ~12 KiB)
│  配置 PLL、DDR PHY
│  执行 DDR training（校准 DQS/DQ timing）
│  初始化 DRAM 控制器
│  从介质加载 SPL 到 DRAM 或 SRAM
│  跳转
↓

SPL (DRAM 或大 SRAM, ~100 KiB)
│  初始化必要的 pinctrl（如 UART debug）
│  配置 eMMC 的 bus-width、HS200 timing
│  从 eMMC/SPI 加载 OP-TEE 到 DRAM（安全启动链）
│  从 eMMC/SPI 加载 U-Boot proper 到 DRAM
│  跳转到 U-Boot proper
↓

U-Boot proper (DRAM, 完整功能)
│  初始化所有外设（MMC、USB、Display、Ethernet 等）
│  读取 boot 命令（bootcmd）
│  从介质加载 kernel + DTB + initramfs 到 DRAM
│  设置 ATAGs 或 DTB
│  跳转到 kernel entry（zImage 解压入口）
↓

Linux kernel
  自解压 → 初始化 → initramfs → rootfs → 用户空间
```

### 2.2 关键地址（R1 实机）

```
0x00000000 - 0x5FFFFFFF  保留 / 外设映射
0x60000000                DRAM 起始（512 MiB）
0x60000000               SPL 加载地址（CONFIG_SPL_TEXT_BASE）
0x61000000               U-Boot proper 加载地址（CONFIG_TEXT_BASE）
0x61100000               初始栈顶（CONFIG_CUSTOM_SYS_INIT_SP_ADDR）
0x61800800               默认 loadaddr（CONFIG_SYS_LOAD_ADDR）
0x62000000               原厂 kernel 地址（CONFIG_KERNEL_RUNNING_ADDR）
0x68400000               原厂 Trust OS 保护区
0x7FFFFFFF               DRAM 结束
0x11030000               UART2 基址（DEBUG_UART_BASE）
```

### 2.3 为什么 SPL 要标路标（breadcrumbs）

SPL 没有 printf/console。在 UART debug 充分初始化之前，唯一能用的调试手段是**手动写 UART TX FIFO**。

这就是 R1 项目中 breadcrumb patch 的原理：在汇编级关键位置插入 UART 寄存器写入，输出单个 ASCII 字符作为"路标"：

```asm
.macro R1_UART_MARK value
    ldr r12, =0x11030000          @ UART2 基址
.Lr1_uart_wait_:
    ldr r11, [r12, #0x14]         @ 读 LSR
    tst r11, #0x20                @ 检查 THRE（发送保持寄存器空）
    beq .Lr1_uart_wait_
    mov r11, #\value
    str r11, [r12]                @ 写字符到 TX
.endm
```

输出序列 `S R M 0 1 2 3` 表示：

| 字符 | 位置 | 含义 |
|---|---|---|
| `S` | `reset:` | 进入 reset 向量 |
| `R` | `save_boot_params_ret:` | BootROM 参数保存完毕 |
| `M` | 调用 `_main` 前 | 即将进入 C 运行环境 |
| `0` | `_main` 入口 | 汇编阶段入口 |
| `1` | 建栈后 | SP 已设置 |
| `2` | gd 初始化后 | global data 可用 |
| `3` | `debug_uart_init()` 前 | 即将初始化 UART |
| `4` | `debug_uart_init()` 后 | UART 已初始化 |
| `5` | `board_init_f()` 前 | 即将进入板级初始化 |
| `6` | `board_init_f()` 后 | 板级初始化完成 |

**如果实机输出停在 `3` 没有 `4`**，说明 hang 在 `debug_uart_init()` 内部——这就是 R1 的实际情况，最终确认是 NS16550 驱动的 TEMT 等待循环导致的阻塞。

---

## 3. 配置系统：Kconfig 与 defconfig

### 3.1 三层选择：ARCH → SoC → Board

U-Boot 使用和 Linux 内核相同的 Kconfig 系统。配置从大到小有三层：

```
ARCH (arm, arm64, riscv, ...)
  └─ CONFIG_ARCH_ROCKCHIP=y
       └─ CONFIG_ROCKCHIP_RK322X=y          ← SoC 级别
            └─ CONFIG_TARGET_PHICOMM_R1=y   ← 板级
```

**SoC 级**负责包含时钟驱动、pinctrl 驱动、MMC 控制器驱动等共享代码。

**板级**负责选 GPIO 引脚、内存大小、启动介质、UART 端口等板子差异。

### 3.2 defconfig 的作用

`configs/phicomm-r1_defconfig` 是一个精简配置片段，只覆盖与默认值不同的选项：

```makefile
CONFIG_ARM=y                          # 架构
CONFIG_ARCH_ROCKCHIP=y                # SoC 厂商
CONFIG_ROCKCHIP_RK322X=y              # SoC 型号
CONFIG_TARGET_PHICOMM_R1=y            # 目标板
CONFIG_DEFAULT_DEVICE_TREE="rockchip/rk3229-phicomm-r1"  # DTB 路径
CONFIG_TEXT_BASE=0x61000000           # U-Boot 运行地址
CONFIG_SPL_TEXT_BASE=0x60000000       # SPL 运行地址
CONFIG_DEBUG_UART_BASE=0x11030000     # 调试串口基址
CONFIG_DEBUG_UART=y                   # 启用调试串口
CONFIG_BAUDRATE=1500000               # 波特率
CONFIG_BOOTDELAY=-1                   # 自动启动（-1=不等待按键）
CONFIG_ANDROID_BOOT_IMAGE=y           # 支持解析 Android boot image
```

使用方式：

```bash
make phicomm-r1_defconfig    # 应用板级配置
make menuconfig               # 微调（可选）
make -j$(nproc)              # 构建
```

### 3.3 关键 CONFIG 选项分类

| 类别 | 示例 | 决定什么 |
|---|---|---|
| **地址** | `TEXT_BASE`, `SPL_TEXT_BASE`, `SYS_LOAD_ADDR` | 各阶段加载地址 |
| **启动介质** | `MMC_DW_ROCKCHIP`, `SPL_MMC` | 从 eMMC/SD/SPI/NAND 启动 |
| **调试** | `DEBUG_UART`, `DEBUG_UART_BASE`, `DEBUG_UART_SKIP_INIT` | 早期 UART 输出 |
| **启动方式** | `ANDROID_BOOT_IMAGE`, `FIT`, `CMD_BOOTZ` | 如何加载内核 |
| **存储保护** | `# CONFIG_MMC_WRITE is not set` | 禁止写 eMMC（诊断安全） |
| **安全** | `SPL_OPTEE_IMAGE`, `FIT_SIGNATURE` | OP-TEE 链和镜像签名 |
| **Rockchip 特殊** | `ROCKCHIP_MASKROM_IMAGE` | 生成 BootROM USB 可加载的镜像 |

注意 `# CONFIG_XXX is not set` 的写法——不是 `=n`，Kconfig 语法要求显式禁用用注释形式。

### 3.4 Kconfig 组织层次

```
arch/arm/Kconfig                    ← ARCH + SoC 选项
arch/arm/mach-rockchip/Kconfig      ← 厂商级选项
arch/arm/mach-rockchip/rk322x/Kconfig  ← SoC 级：定义了 TARGET_EVB_RK3229 / TARGET_PHICOMM_R1
board/rockchip/phicomm_r1/Kconfig   ← 板级：SYS_BOARD / SYS_VENDOR / SYS_CONFIG_NAME
drivers/serial/Kconfig              ← DEBUG_UART 选项
```

---

## 4. U-Boot 中的 Device Tree

### 4.1 DT 的三层叠加

U-Boot 的 DT 有独特的层级关系：

```
rk3229.dtsi                      ← 上游 dts/upstream/ (SoC 定义)
  └─ rk3229-phicomm-r1.dts      ← 板级 DTS：include 上游、覆盖板级差异
       ├─ rk3229-phicomm-r1-u-boot.dtsi  ← U-Boot 专用 DT 补充
       │    └─ rk322x-u-boot.dtsi         ← SoC 级 U-Boot 通用补充
       └─ 编译生成 rk3229-phicomm-r1.dtb
```

`-u-boot.dtsi` 是 U-Boot 独有的概念：它自动叠加到对应板级 DTS 上，用于添加 U-Boot 需要的额外节点（如 `binman` 布局）和属性（`bootph-all`）。

### 4.2 bootph-* 属性

DT 节点在 SPL/TPL 阶段是否可用，由此系列属性控制：

| 属性 | SPL | TPL | U-Boot proper |
|---|---|---|---|
| `bootph-all` | ✅ | ✅ | ✅ |
| `bootph-some-ram` | ✅ | ✅ | ❌ |
| `bootph-pre-ram` | ✅ | ❌ | ❌ |
| 无属性（默认） | ❌ | ❌ | ✅ |

**作用**：SPL/TPL 的内存和 flash 空间极度有限。用 `bootph-*` 精确控制每个节点是否编译进对应阶段的 DTB，可以大幅缩小 DT 体积。

R1 的 UART2 节点在 `-u-boot.dtsi` 中被标记 `bootph-all`，确保从 SPL 开始就能用串口。

### 4.3 DT 在 SPL 中的精简

SPL 的 DTB 中会自动删除大量不需要的属性：

```makefile
CONFIG_OF_SPL_REMOVE_PROPS="pinctrl-0 pinctrl-names clock-names interrupt-parent \
    assigned-clocks assigned-clock-rates assigned-clock-parents"
```

这些属性在 SPL 阶段不需要（SPL 不管理复杂外设），删除后可以显著减小 SPL DTB 体积。

---

## 5. 板级移植

### 5.1 需要创建的文件

对于一个新的 Rockchip 板子，需要创建以下文件：

```
board/rockchip/{vendor}_r1/
├── Kconfig              ← 板级 Kconfig
├── board.c              ← 板级 C 代码（通常很小，调用 SoC 共享函数）
└── Makefile

configs/
└── {board}_defconfig    ← 板级 defconfig

dts/upstream/src/arm/rockchip/
└── rk3229-{board}.dts   ← 板级 DTS（include SoC dtsi）

arch/arm/dts/
└── rk3229-{board}-u-boot.dtsi  ← U-Boot 专用 DT 补充

include/configs/
└── {board}.h            ← 板级头文件（新版 U-Boot 已趋于不必要，通常只 include SoC 通用头）

arch/arm/mach-rockchip/rk322x/
└── Kconfig              ← 在这里添加 TARGET_{BOARD} 条目
```

### 5.2 最小 DTS

```dts
// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/dts-v1/;
#include "rk3229.dtsi"

/ {
    model = "Phicomm R1";
    compatible = "phicomm,r1", "rockchip,rk3229";

    aliases {
        mmc0 = &emmc;       // 指定块设备别名
        serial2 = &uart2;   // 指定控制台 UART
    };

    chosen {
        stdout-path = "serial2:1500000n8";  // 内核控制台
    };

    memory@60000000 {
        device_type = "memory";
        reg = <0x60000000 0x20000000>;      // 512 MiB @ 0x60000000
    };
};

&emmc {
    bus-width = <8>;           // 8-bit eMMC
    cap-mmc-highspeed;
    mmc-hs200-1_8v;            // HS200 模式（高速）
    non-removable;
    status = "okay";
};

&uart2 {
    current-speed = <1500000>; // 串口波特率
    pinctrl-names = "default";
    pinctrl-0 = <&uart21_xfer>;  // 使用 pinmux group 1
    status = "okay";
};
```

### 5.3 最小 defconfig 关键项解释

```makefile
CONFIG_ARM=y                          # CPU 架构
CONFIG_SKIP_LOWLEVEL_INIT=y           # 跳过 ARM 低级初始化（由 BootROM/TPL 完成）
CONFIG_SPL_SKIP_LOWLEVEL_INIT=y       # SPL 也跳过
CONFIG_ARCH_ROCKCHIP=y                # SoC 厂商
CONFIG_ROCKCHIP_RK322X=y              # SoC 系列
CONFIG_TARGET_PHICOMM_R1=y            # 板级目标
CONFIG_DEFAULT_DEVICE_TREE="rockchip/rk3229-phicomm-r1"  # DTB 路径

# 地址布局
CONFIG_TEXT_BASE=0x61000000           # U-Boot proper 链接/运行地址
CONFIG_SPL_TEXT_BASE=0x60000000       # SPL 链接/运行地址
CONFIG_SYS_LOAD_ADDR=0x61800800       # 默认 loadaddr
CONFIG_SPL_STACK_R_ADDR=0x60600000    # SPL 使用 DRAM 栈的地址

# 调试
CONFIG_DEBUG_UART=y                   # 启用 debug UART
CONFIG_DEBUG_UART_BASE=0x11030000     # UART2 基址
CONFIG_DEBUG_UART_CLOCK=24000000      # UART 时钟（24 MHz）
CONFIG_DEBUG_UART_SHIFT=2             # 寄存器偏移 shift（32-bit 寄存器用 2）
CONFIG_BAUDRATE=1500000               # 波特率 1.5 Mbps
CONFIG_DEBUG_UART_SKIP_INIT=y         # 不重新初始化 UART（沿用 BootROM 配置）

# 启动选项
CONFIG_ANDROID_BOOT_IMAGE=y           # 支持 Android boot image
CONFIG_FIT=y                          # 支持 Flattened Image Tree
CONFIG_BOOTDELAY=-1                   # 自动启动，不等按键

# Rockchip MaskROM 镜像
CONFIG_ROCKCHIP_MASKROM_IMAGE=y       # 生成 BootROM USB 兼容的 471/472 镜像
CONFIG_SPL_NO_BSS_LIMIT=y             # SPL BSS 不计入镜像大小限制
CONFIG_SPL_MAX_SIZE=0x100000          # SPL 最大 1 MiB

# 禁写保护（诊断候选）
# CONFIG_MMC_WRITE is not set         # 禁止 MMC 写命令
# CONFIG_CMD_SAVEENV is not set       # 禁止保存环境变量
```

### 5.4 board.c 的最小实现

```c
// board/rockchip/phicomm_r1/board.c
#include <common.h>
#include <dm.h>
#include <asm/arch-rockchip/hardware.h>

int board_init(void)
{
    return 0;  // 最小实现，外设由 DM（驱动模型）自行探测
}
```

大多数 Rockchip 板子的 `board.c` 几乎为空：DM (Driver Model) 框架根据 DTB 自动绑定驱动，板级代码只处理 SoC 共享代码未覆盖的特殊情况（如特定 GPIO 的默认电平）。

---

## 6. 构建流程与产物

### 6.1 构建命令

```bash
export CROSS_COMPILE=arm-none-eabi-
make phicomm-r1_defconfig
make -j$(nproc)
```

产物在构建目录中：

### 6.2 产物清单

| 文件 | 说明 | 典型大小 |
|---|---|---|
| `spl/u-boot-spl.bin` | SPL 裸二进制 | ~30 KiB |
| `tpl/u-boot-tpl.bin` | TPL 裸二进制（如有） | ~12 KiB |
| `u-boot.bin` | U-Boot proper 裸二进制 | ~500 KiB |
| `u-boot.img` | U-Boot proper 带 legacy header | ~500 KiB |
| `u-boot-rockchip-usb471.bin` | 供 BootROM USB 下载的 TPL 镜像 | ~30 KiB |
| `u-boot-rockchip-usb472.bin` | 供 BootROM USB 下载的 SPL+U-Boot 镜像 | ~520 KiB |
| `u-boot.dtb` | U-Boot 自己的设备树 | ~25 KiB |

### 6.3 Rockchip MaskROM Image (471/472)

当 `CONFIG_ROCKCHIP_MASKROM_IMAGE=y` 时，U-Boot 使用 **binman** 工具（`tools/binman`）打包镜像。binman 是一个声明式镜像打包工具，在 DTS 中描述镜像布局，编译时自动生成。

命名含义：
- **471** = TPL（BootROM USB protocol 约定了阶段编号）
- **472** = SPL + U-Boot proper payload

`u-boot-rockchip-usb471.bin` 由 BootROM 通过 USB 下载到片内 SRAM 执行。471 初始化 DDR 后，BootROM 再下载 `u-boot-rockchip-usb472.bin` 到 DRAM 执行。

R1 项目利用了这一机制：使用原厂 V1.06 DDR blob 作为 471（因为主线 TPL 的 DDR 参数可能不匹配），只替换 472 为主线 SPL/U-Boot，实现"只走 USB、不写 eMMC"的 RAM 诊断。

---

## 7. Boot Flow 与启动选择

### 7.1 bootcmd 决策链

U-Boot proper 的 `bootcmd` 环境变量决定了启动策略。常见模式：

```bash
# distro boot（标准 Linux 发行版）
bootcmd=distro_bootcmd
# 依次尝试 USB → MMC → PXE → DHCP

# Android boot（原厂 Rockchip）
bootcmd=bootrk
# 读取 androidboot 参数，决定加载 boot 或 recovery

# 固定启动
bootcmd=bootz ${kernel_addr_r} - ${fdt_addr_r}
# 直接启动指定地址的 kernel + DTB
```

### 7.2 Android boot image 的启动（bootrk）

原厂 U-Boot 使用 `bootrk` 命令来启动 Android boot image：

```
bootrk 的执行流程：
1. 根据 misc BCB 或启动参数决定目标分区（boot 或 recovery）
2. 从分区读取 Android boot image header
3. 校验 Rockchip 扩展 SHA（SHA-1 + SHA-256）
4. 把 kernel_addr 改写为编译期固定的 CONFIG_KERNEL_LOAD_ADDR
5. 把 ramdisk 放在 gd->arch.rk_boot_buf_addr
6. 跳转到 kernel 入口
```

**关键点**：`bootrk` **不读** U-Boot 环境变量 `kernel_addr_r`。它使用编译期常量。这意味着不能通过改 U-Boot 环境来改变 kernel 加载地址——必须修改 boot image header 本身。

### 7.3 misc BCB 分区

misc 分区在 R1 的 parameter 中定义为 LBA `0x004000`，大小 4096 字节。它在 Android 启动链中有两个关键角色：

```
+-------------------+
| bootloader_message  |  offset 0:   32-byte "boot-recovery" 等命令
| (2048 bytes)        |  U-Boot 启动时读取此字段决定进 recovery
+-------------------+
| wipe 等操作状态     |  offset 2048:  factory reset 等状态
+-------------------+
```

写入 `boot-recovery` 到 offset 0 即可强制下次启动进入 recovery：

```sh
busybox echo -n boot-recovery > /dev/block/platform/30020000.rksdmmc/by-name/misc
```

U-Boot 进入 recovery 后会自动清除 misc 中的命令，避免无限循环。

### 7.4 Rockchip Boot SHA 扩展

R1 的原厂 U-Boot 对 Android boot image 做了额外的"安全"校验，覆盖范围远超标准 AOSP：

标准 AOSP `mkbootimg`：只对 kernel + ramdisk + second 做 SHA-1，写入 32-byte `id` 字段。

**Rockchip 扩展**：除 kernel、ramdisk、second 外，还覆盖 `tags_addr`、`page_size`、两个 `unused` 字段、16-byte board name 和 512-byte cmdline。

输出：
- SHA-1 → 标准 `id` 字段（`offset 0x248`）
- SHA-256 + flag `256` → 扩展区（`offset 0x26c` 开始）

R1 项目已实现 `scripts/add-rockchip-boot-hashes.py` 精确复现这个算法。若缺少这两个摘要，U-Boot 会打印 `boot or recovery image sha mismatch!` 并拒绝启动。

---

## 8. Rockchip 内存布局与启动阶段

这是整个文档中最关键的章节。理解 Rockchip 的内存布局和启动阶段，才能理解"哪个镜像该放哪里"、"哪个程序跑在哪块内存里"。

### 8.1 内存空间总览（RK3229 / ARMv7）

![](https://opensource.rock-chips.com/images/c/cd/Rockchip_bootflow20181122.jpg)

```
物理地址空间:

0x00000000 ──────────────────────── 外设映射区（MMIO）
    │
    │  UART, GPIO, CRU(时钟), GRF(通用寄存器),
    │  eMMC 控制器, USB OTG, I2C, SPI 等
    │
0x10000000 ────────────────────────
    │
    │  (RK3229 的片内 SRAM 可能映射在此区间)
    │
0x60000000 ─────── DRAM 起始 ────────  512 MiB DDR3
    │  ← SPL_TEXT_BASE (SPL 加载地址)
    │
    │  SPL 运行区域 (~0x60000000 - 0x60100000)
    │
0x60600000 ─────── SPL_STACK_R_ADDR ───  SPL 栈
0x61000000 ─────── TEXT_BASE ──────────  U-Boot proper 加载地址
    │
    │  U-Boot proper 运行区域
    │
0x61100000 ─────── SYS_INIT_SP_ADDR ────  初始栈顶
0x61800800 ─────── SYS_LOAD_ADDR ───────  默认 loadaddr
0x62000000 ─────── 原厂 kernel 加载地址
0x65bf0000 ─────── 原厂 ramdisk 加载地址
    │
0x68400000 ─────── OP-TEE 保留区 ────────  原厂 Trust OS / 开源 OP-TEE
0x68500000 ─────── OP-TEE 区域结束
    │
0x7fffffff ─────── DRAM 结束 (512 MiB)
```

### 8.2 片内 SRAM vs DRAM：为什么分阶段

| 内存类型 | 大小 | 何时可用 | 用途 |
|---|---|---|---|
| **片内 SRAM** | ~几十 KiB | 上电即用 | BootROM + TPL 的"第一块工作内存" |
| **DDR DRAM** | 512 MiB | **TPL 完成 DDR training 之后** | SPL + U-Boot + kernel 的运行空间 |

核心矛盾：**DDR 控制器必须被初始化才能用，但初始化代码本身需要内存来跑。** 解决方案就是分阶段——TPL 只用 SRAM，完成 DDR 初始化后，后续阶段才能用 DRAM。

### 8.3 eMMC 上的镜像布局

Rockchip 定义了标准的 eMMC 分区偏移（LBA = 逻辑块地址，1 sector = 512 bytes）：

```
LBA              大小        镜像               内容
──────────────────────────────────────────────────────────
0x000000          4 KiB      parameter           分区表 (mtdparts)
0x000040          ~           idbloader.img       TPL + SPL 或 DDR + miniloader
0x002000          4 MiB      uboot (原厂)        原厂 U-Boot (miniloader 路径)
0x004000          4 MiB      trust               ARM Trusted Firmware / OP-TEE (miniloader 路径)
0x006000          12 MiB     boot                Android boot.img (kernel+ramdisk+DTB)
0x00C000          12 MiB     recovery            Android recovery.img
...               ...

*** 开源 TPL/SPL 路径 ***
0x000040          ~          idbloader.img       TPL + SPL (mkimage 打包)
0x004000          ~          u-boot.itb          U-Boot proper + OP-TEE (FIT 镜像)

*** 原厂 miniloader 路径 (R1 的实际情况) ***
0x002000          4 MiB      uboot.img           U-Boot (loaderimage 打包)
0x004000          4 MiB      trust.img           OP-TEE (trust_merger 打包)
```

**R1 走的是 miniloader 路径**。原厂用 Rockchip 闭源的 `miniloader` 作为 stage-2 loader，不是开源的 U-Boot TPL/SPL。R1 的 eMMC 参数文件中 uboot 和 trust 的 LBA 偏移与上表一致。

### 8.4 两种启动路径对比

Rockchip 有两套完全不同的 bootloader 方案：

#### 路径 A：原厂 miniloader（R1 正在用的）

```
BootROM (片内 ROM)
  ↓ 从 eMMC LBA 0x40 加载 miniloader 到 SRAM
miniloader (Rockchip 闭源二进制)
  ├── DDR 初始化 (ddr.bin)
  ├── 返回 BootROM
  │   ↓ BootROM 再次从 eMMC 加载
  ├── trust.img → 加载到 DRAM → OP-TEE 初始化
  ├── uboot.img → 加载到 DRAM 0x61000000
  │   ↓
U-Boot proper → 解析 parameter 分区 → 执行 bootcmd
  ↓
boot.img → kernel
```

特点：
- miniloader 是闭源的，但稳定
- trust.img 和 uboot.img 分开存放
- uboot.img 由 Rockchip 的 `loaderimage` 工具打包（不是标准 U-Boot.bin）

#### 路径 B：开源 U-Boot TPL/SPL（项目目标）

```
BootROM (片内 ROM)
  ↓ 从 eMMC LBA 0x40 加载 idbloader.img 到 SRAM
idbloader.img:
  ├── TPL (u-boot-tpl.bin): DDR training → back_to_bootrom
  │   ↓ BootROM 再次从 eMMC 加载
  └── SPL (u-boot-spl.bin): 加载到 DRAM 0x60000000
        ↓
      从 eMMC LBA 0x4000 加载 u-boot.itb (FIT 镜像)
        ├── OP-TEE → DRAM 0x68400000
        └── U-Boot proper → DRAM 0x61000000
        ↓
U-Boot proper → distro boot / bootrk
  ↓
kernel
```

特点：
- 全部开源，可调试、可修改
- 使用 FIT (Flattened Image Tree) 打包多个组件
- TPL/SPL 由 U-Boot 源码编译，不需要 Rockchip 闭源工具

### 8.5 idbloader.img 的生成

idbloader 是写给 BootROM 看的"第一段代码"。生成方式：

```bash
# 开源路径：TPL + SPL
tools/mkimage -n rk322x -T rksd -d tpl/u-boot-tpl.bin idbloader.img
cat spl/u-boot-spl.bin >> idbloader.img

# 原厂路径：DDR blob + miniloader
tools/mkimage -n rk322x -T rksd -d rk322x_ddr_v1.06.bin idbloader.img
cat rk322x_miniloader_v2.46.bin >> idbloader.img
```

`mkimage -T rksd` 会给镜像加上 Rockchip 的 IDBlock header（magic + 校验和），这是 BootROM 识别 idbloader 的前提。

### 8.6 u-boot.itb（FIT 镜像）

开源路径中，FIT 镜像取代了分开的 uboot.img + trust.img。它是一个声明式打包格式：

```
u-boot.itb (FIT image)
├── images/
│   ├── tee@1     → tee.bin (OP-TEE, armv7) 或 bl31.elf (ATF, armv8)
│   ├── fdt@1     → u-boot.dtb
│   ├── u-boot@1  → u-boot-nodtb.bin
│   └── ...
├── configurations/
│   └── conf@1:
│       firmware = "tee@1"
│       loadables = "u-boot@1"
│       fdt = "fdt@1"
```

FIT 在 DTS 中描述（`arch/arm/dts/rockchip-u-boot.dtsi`），binman 在编译时根据此描述打包。

SPL 加载 u-boot.itb 后，解析 FIT 结构，将各组件放到目标地址：
- `tee@1` → `0x68400000`（ARM 架构下固定从 `CFG_SYS_SDRAM_BASE + 0x08400000` 开始）
- `u-boot@1` → `CONFIG_TEXT_BASE`（`0x61000000`）
- `fdt@1` → 紧邻 U-Boot proper

### 8.7 原厂 miniloader 镜像的特殊打包

原厂 miniloader 路径需要 Rockchip 专有工具打包：

```bash
# uboot.img（不是标准 u-boot.bin 或 u-boot.img）
tools/loaderimage --pack --uboot u-boot.bin uboot.img 0x61000000
#    ↑ 给 u-boot.bin 加上 miniloader 可识别的 header，写入加载地址

# trust.img
tools/trust_merger RKTRUST_RK322XTRUST.ini
#    ↑ 按 ini 配置打包 ATF/OP-TEE
```

R1 提取的 `backup/partitions/uboot.img` 就是这种格式——不是标准的 U-Boot FIT。

### 8.8 BootROM USB 协议

RK3229 在所有启动介质失败时，自动进入 **MaskROM USB** 模式。主机通过 `rkdeveloptool` 通信：

```bash
# 查询芯片信息
rkdeveloptool rci        # → "A223" (RK3229)

# 读取 eMMC
rkdeveloptool rl <LBA> <sectors> output.bin

# 下载镜像到 SRAM/DRAM 并执行（纯 RAM，不写存储）
rkdeveloptool db loader.bin

# 写 eMMC（危险）
rkdeveloptool wl <LBA> input.bin
```

`db` 命令会把镜像拆成两部分：USB plug（在 SRAM 中运行，初始化 USB）和 loader（在 DRAM 中运行）。这是 **RAM-only 诊断的核心入口**——完全不走 eMMC 的 boot 链。

R1 项目用此路径：`rkdeveloptool db` 下载 471 (原厂 DDR) + 472 (主线 SPL/U-Boot) 到 DRAM 执行，实现零写诊断。

### 8.9 back-to-BROM 机制

Rockchip 的多阶段启动有一个独特设计：每个阶段完成后**把控制权还给 BootROM**，BootROM 再加载下一阶段：

```
BootROM → TPL (做 DDR training) → back_to_bootrom
       → BootROM → SPL (加载 OP-TEE + U-Boot) → ...
```

实现：TPL 完成后设置特殊返回值，跳转到 BootROM 已知的返回地址。BootROM 根据当前 stage 指针继续从介质加载下一阶段。

在 U-Boot 代码中，这对应 `save_boot_params()` 的强实现（`CONFIG_ROCKCHIP_BACK_TO_BROM`）。R1 项目中曾发现 SPL 错误包含了本应只在 TPL 中使用的强实现，通过 phase-aware 补丁修复。

### 8.10 DDR 参数的重要性

DDR training 是启动过程最脆弱的环节。错误的参数导致的不稳定从**偶发数据损坏**到**完全无法启动**都有可能。

Training 参数维度：
- DRAM 芯片时序（CAS latency, tRCD, tRP, tRAS）
- DQS/DQ 信号校准值（每块 PCB 的走线长度不同）
- ODT（终端电阻）和 drive strength
- PHY 级 DLL 延迟微调

因此 R1 项目在 bring-up 阶段**保留原厂 DDR blob 作为 471**（已验证可工作的 training），只在 472 使用主线代码。

### 8.11 OP-TEE / Trust OS 链

完整安全启动链在 SPL 和 U-Boot proper 之间插入 OP-TEE：

```
SPL → 加载 tee.bin 到 0x68400000
    → OP-TEE 初始化 PSCI (CPU 开关、suspend)、安全时钟等
    → OP-TEE 通过 SMC 返回 SPL
    → SPL 继续加载 U-Boot proper
    → U-Boot proper → kernel
        → kernel 通过 SMC 调用 OP-TEE 的 PSCI 服务 (cpuidle, hotplug, ...)
```

**这个链对 R1 的稳定性至关重要**。R1 实机使用原厂 Trust OS 并启用 SMP 后会在约 30 秒冻结整个系统；Armbian 维护者也记录了专有 RK322x Trust OS 在不同板上出现 30 秒、60 秒或 30 分钟 watchdog 冻结，而开源 OP-TEE 不含此问题，但会失去 DDR 动态缩放和 “virtual power off” 等原厂特性（[论坛原帖](https://forum.armbian.com/topic/34923-csc-armbian-for-rk322x-tv-box-boards/?comment=227602&do=findComment)）。

### 8.12 parameter 分区

Rockchip 在 eMMC LBA 0 存储分区表，格式为 key=value：

```
FIRMWARE_VER: 6.0.0
MACHINE_MODEL: rk322x_box
MAGIC: 0x5041524B
CMDLINE: mtdparts=rk29xxnand:
    0x00002000@0x00002000(uboot),
    0x00002000@0x00004000(trust),
    0x00004000@0x00006000(boot),
    0x00004000@0x0000A000(kernel),
    0x00004000@0x0000E000(resource),
    0x00004000@0x00012000(kpanic),
    0x00008000@0x00016000(backup),
    0x00004000@0x0001E000(recovery),
    ...
```

格式为 `size@offset(name)`，offset 和 size 以 sector (512 bytes) 为单位。例如 `recovery` 分区位于 LBA `0x1E000`，大小为 `0x10000` sectors（ = 32 MiB）。

U-Boot 启动时解析此分区表，注册 `/dev/block/platform/30020000.rksdmmc/by-name/` 下的符号链接。

**注意**：recovery 模式下 by-name 映射的 `mmcblk0pN` 编号与正常模式不同，因为 parameter 分区是动态解析而非固化的 GPT。必须使用 by-name 路径而非硬编码 `mmcblk0p9`。

---

## 9. R1 项目中的 U-Boot 诊断

### 9.1 问题描述

主线 U-Boot（基于上游 `v2025.01` 前后）通过 MaskROM `db` 命令下载到 R1 后，SPL 无法到达 U-Boot proper 提示符。

### 9.2 诊断方法论：单变量 A/B 测试

核心原则：每次只改变一个变量，在主机编译两个候选（A 和 B），分别上板测试。同一个代码改动只做一件事。

```
A: 基线候选（已知停在 X 处）
B: 加上一个改动，看是停在 X 还是前进到 Y

如果 B 停在 X → 这个改动不是阻塞因素
如果 B 前进到 Y → 这个改动是阻塞因素，继续下一轮
```

### 9.3 诊断历程（节点摘录）

| 轮次 | 变量 | 结果 | 结论 |
|---|---|---|---|
| 1 | 原厂 471 + 主线 472（无 breadcrumb） | 无输出 | SPL 可能在 banner 前 hang |
| 2 | 加 `S/R/M/0–6` breadcrumb | 只到 `S` | hang 在 `save_boot_params()` |
| 3 | phase-aware `save_boot_params()` | `SRM0123` | 跨过 save_boot_params，停在 UART init |
| 4 | `CONFIG_DEBUG_UART_SKIP_INIT` + ns16550 patch | `SRM0123` | UART 驱动跳过了，但 hang 没变 |
| 5 | 汇编层直接绕过 `debug_uart_init()` | `SRM012345` | 确认是 `debug_uart_init` 调用本身阻塞 |
| 6 | 独立 `TARGET_PHICOMM_R1`（替换 EVB DT） | `SRM0123` | 板级 DT 纠正是必要的，但没改变这个 hang |
| 7 | A–K breadcrumb in `board_init_f()` | 待上板 | 定位 `board_init_f()` 内部的停止点 |

### 9.4 诊断补丁的临时性

所有 breadcrumb、phase-aware、ns16550 skip-init 补丁都是**临时诊断工具**，目的不是功能改进。一旦 SPL 能稳定到达 U-Boot 提示符，这些补丁会被整体删除，只保留板级移植的 DTS + defconfig 作为正式产物。

---

## 10. 面试 / 八股要点

### 10.1 高频问题速查

**Q: U-Boot 和 BIOS 的区别？**
A: BIOS 是 x86 的传统固件接口，UEFI 是其现代替代。U-Boot 是 ARM 生态中主流开源 bootloader，功能上覆盖了 x86 的 BIOS+UEFI 角色。U-Boot 也可以编译为 UEFI payload。

**Q: SPL / TPL 为什么存在？**
A: 片内 SRAM 太小（几十 KiB），装不下完整 U-Boot（几百 KiB～MiB）。SPL/TPL 用极小空间完成 DDR 初始化，腾出 DRAM 后再加载完整 U-Boot。

**Q: defconfig 和 .config 的关系？**
A: `defconfig` 是展开 `.config` 的**精简起点**。`make xxx_defconfig` 把 defconfig 中的值写入 `.config`，其余选项保留 Kconfig 默认值。`.config` 是实际编译使用的完整配置。

**Q: bootph-all 的作用？**
A: 控制 DT 节点是否在 SPL/TPL 阶段可用。SPL 空间有限，不带 `bootph-*` 的节点会被从 SPL DTB 中丢弃。

**Q: 如何调试 SPL hang？**
A: 在汇编入口点手写 UART 寄存器输出"路标"字符（breadcrumb），逐个函数调用前后放置，根据最后看到的字符定位 hang 范围。

**Q: bootrk 和 distro boot 的区别？**
A: `bootrk` 是 Rockchip 私有启动命令，专门解析 Android boot image header，使用编译期固定地址。`distro_bootcmd` 是 U-Boot 标准的"扫描介质、加载 kernel+DTB+initrd"流程，适用于通用 Linux 发行版。

**Q: Rockchip MaskROM 镜像的 471/472 是什么？**
A: BootROM USB 下载的两个阶段镜像。471=TPL（DDR 初始化），472=SPL+U-Boot payload。编号是 BootROM USB 协议的阶段标识。

### 10.2 关键概念对照表

| 概念 | 对应 CONFIG / 文件 | 作用 |
|---|---|---|
| SPL 加载地址 | `CONFIG_SPL_TEXT_BASE` | SPL 的链接和运行地址 |
| U-Boot 加载地址 | `CONFIG_TEXT_BASE` | U-Boot proper 的链接和运行地址 |
| 调试串口 | `CONFIG_DEBUG_UART_BASE` + `CONFIG_BAUDRATE` | 早期 UART 输出 |
| 板级识别 | `CONFIG_TARGET_*` + `CONFIG_DEFAULT_DEVICE_TREE` | 选择板级 DTS 和 Kconfig |
| 启动介质 | `CONFIG_SPL_MMC` / `CONFIG_SPL_SPI` 等 | SPL 从哪里加载下一阶段 |
| 启动命令 | `bootcmd` 环境变量 | U-Boot proper 启动时执行的命令 |
| MaskROM 打包 | `CONFIG_ROCKCHIP_MASKROM_IMAGE` | 生成 BootROM USB 兼容镜像 |
| OP-TEE 链 | `CONFIG_SPL_OPTEE_IMAGE` | SPL 是否加载 OP-TEE |

---

## 11. 相关阅读

- [U-Boot 官方文档](https://docs.u-boot.org/)
- [U-Boot 源码](https://source.denx.de/u-boot/u-boot)
- [Rockchip U-Boot 指南](https://opensource.rock-chips.com/wiki_Boot_option)
- [Recovery 前置知识](recovery.md) — Android recovery 模式与 boot image 格式
- [SELinux 前置知识](selinux.md) — SELinux 概念与绕过策略
- [主线 Linux Bring-up](../mainline-bringup.md) — R1 主线内核与 U-Boot 诊断的详细构建说明
- [逆向学习记录](../reverse-engineering-journal.md) — 按时间的面包屑诊断记录

## 12. U-Boot 移植思路八股模板

第 5 节是"要建哪些文件"，第 10 节是"面试常问什么"。这两者都只是清单；真正难的是
**思路**：移植不是一个"照着模板改代码"的动作，而是一套固定的问答流程。本节把这套
流程写成八股文式的固定模板：每一股回答一个固定问题，填完八股，移植就完成了八成。
R1 项目全流程已按此模板走完（证据见[逆向学习记录](../reverse-engineering-journal.md)）。

### 核心心智模型（先读这个）

一句话：**启动链上的每个阶段，移植者只负责三件事——入口（从哪个地址开始跑）、
资源（跑起来要什么：栈、时钟、串口、内存）、出口（跑完把控制权交给谁）。**

U-Boot 不是一块大铁板，而是一串接力跑：

```text
BootROM(MaskROM)
  → DDR 初始化(外部 471)      ← 不在 U-Boot 内，但 U-Boot 要"沿用它的成果"
  → SPL(472)                  ← 最小 C 环境：栈+全局数据+DM+串口+DRAM 探测
  → OP-TEE / ATF              ← secure monitor，提供 PSCI/SMP
  → U-Boot proper             ← 完整命令行、驱动、bootcmd
  → Linux
```

每一棒只有"入口/资源/出口"三件事。所以：
- 串口没输出 = 某个阶段的"资源"没满足（栈没建、时钟没开、UART 被重复初始化破坏）；
- 停在某处 = 出口没准备好（下一阶段镜像不在约定地址、FDT 无效、交接寄存器约定不符）；
- 大多数移植工作 = 把"每个阶段缺什么"从猜测变成证据。

诊断纪律（本项目最重要的实战经验）：
1. **一次只改一个变量**（A/B 对照）：怀疑 A 就只改 A，实机对比；
2. **离线验证先行**：打包→解包→CRC→FIT 地址→逐字节核对，全部在主机侧做完，
   实机只验证"我猜的停止点"对不对；
3. **路标二分**：在怀疑区间的中点打一个单字符 UART 标记，看最后字符落在哪半，
   每轮把区间减半。R1 从 `S` 一路分到 `SRM012345ABabsmcdefCDEFGHIJ`，每个字符
   都是一次二分的结果；
4. **从已验证链出发**：先让"已知能跑的东西"原样跑起来（原厂 DDR + 主线 SPL），
   再逐步替换，绝不一开始就全换。

下面八股中带 `【R1】` 的是本项目的真实答案，可直接当作填写范例。

### 破题：这块板要跑到哪一步？

问题：移植的**终点**是什么？提示符？从 eMMC 启动？双核 Linux？SMP 稳定？
终点决定深度：只要提示符，可以不碰 OP-TEE；要 SMP，就必须处理 secure monitor/PSCI。

手段：把终点写成一两句可验收的话。

【R1】终点 = "不写 eMMC 的前提下，原厂 DDR 471 + 主线 SPL/U-Boot 472 +
开源 OP-TEE，从 RAM 启动至少双核 Linux，uptime 超过 30 秒冻结边界"。
验收标准不是"能打印更多字符"，而是上面这条链整体成立。

### 承题：从什么出发？什么是不准动的地基？

问题：现在手上**已验证**的事实有哪些？哪些东西是地基、必须原样沿用，哪些是
参考、不准照搬？

手段：列一张"已验证事实"表（来源必须是本板证据：原厂镜像、串口日志、备份、
实测值），再列一张"外部参考"表（其他板子资料，标注为推断）。

【R1】地基：SoC 是 RK3229；512 MiB DDR3、原厂训练 300 MHz；UART2
`1500000 8N1`；eMMC 已只读备份；真 MaskROM `db` 能下载 RAM loader。
不准照搬：RK3229 EVB 的 DDR/PMIC/板级 DT（曾导致串口消失，已实机否定）；
Armbian 的 rk322x-box 链只参考它的开源 OP-TEE，不照搬它的 DDR/DT。

### 起讲：这条启动链上有几棒？每一棒的入口/资源/出口是什么？

问题：目标板的启动链按什么顺序走？每棒分别由谁初始化、交接时哪些状态必须活着？

手段：画（或写）出完整链，逐棒标注三件事。特别标注**外部棒**（如原厂 DDR
471）——它不在 U-Boot 源码内，但 U-Boot 必须"继承"它配置好的串口/内存。

【R1】`MaskROM → DDR471 → SPL472 → OP-TEE → U-Boot proper → Linux`。
关键交接点：471 完成后 UART2 已配置好，SPL 必须**跳过重初始化**（
`CONFIG_DEBUG_UART_SKIP_INIT`），否则会把配置好的串口打回无效状态——这是
`SRM0123` 停止点的根因；SPL 跳 OP-TEE 前要把 `CONFIG_TEXT_BASE` 写进 `r1`
寄存器（Armbian 兼容约定）；OP-TEE 保留区在 `0x68400000`，U-Boot/FDT 不得覆盖。

### 入题：填移植决策表

问题：把"阶段模型"落到本板的具体数字。每一行都必须有出处（原厂镜像反查、
datasheet、实测），不许"看起来像"。

| 决策项 | 出处要求 | 【R1】答案 |
|---|---|---|
| SoC / arch / mach 目录 | 芯片丝印 + BootROM chip info | RK3229，`mach-rockchip/rk322x` |
| DRAM 大小与基址 | DDR 训练日志实测 | 512 MiB @ `0x60000000` |
| SPL 运行地址 `SPL_TEXT_BASE` | SoC 约定 + BootROM 加载地址 | `0x60000000` |
| U-Boot proper 地址 `TEXT_BASE` | 内存顶部以下、不与 TEE 冲突 | `0x61000000` |
| 调试串口 BASE/时钟/波特率 | 原厂 DTB + 冷启动日志 | `0x11030000`，24 MHz，1500000 |
| 内存映射 `SDRAM_BASE`/`NR_DRAM_BANKS` | DDR 日志 | `0x60000000`，2 banks |
| 下一阶段交接方式 | 上游 SoC 惯例 | FIT：firmware=op-tee，loadables=u-boot |
| 保留区（TEE/FDT/ramdisk） | 原厂 trust 分区地址 | TEE `0x68400000`，FDT 紧随其后 |

手段：先在现有 defconfig 里找到最接近的板子，逐行 diff，把每一行的差异解释清楚
再动手。禁止"整份抄过来"。

### 起股：写最小配置（defconfig）

问题：让这条链**能编译、能最小运行**需要哪些 CONFIG？每一项的取舍理由是什么？

手段：从 `configs/` 找一个同 SoC defconfig 复制，逐项删改到最小；把"调试输出"
"禁写存储"类安全项显式写进去。写完后离线检查 `.config` 关键项。

【R1】要点：`CONFIG_SPL_SKIP_LOWLEVEL_INIT=y`（DDR 已被 471 初始化）；
`CONFIG_DEBUG_UART_SKIP_INIT=y`（继承 471 串口）；`CONFIG_TEXT_BASE=0x61000000`；
`CONFIG_ENV_IS_NOWHERE=y` + 关 MMC 写（RAM 诊断阶段禁止任何存储写）；
`CONFIG_SPL_LOAD_FIT_FULL=y`（内嵌 FIT 数据，避免外置 data-offset 读取错误）；
`CONFIG_SPL_OPTEE_IMAGE=y` + 构建时 `TEE=tee.bin`（否则 binman 静默丢数据——
实测 `data-size=0`、configuration 退化为 `firmware=u-boot`）。

### 中股：写板级代码与最小 DTS

问题：哪些文件是必须新建的？每个文件里**只**放本板事实，哪些内容明确禁止放？

手段：按第 5 节清单建文件；DTS 只描述"本板实机确认"的节点；复杂外设
（PMIC、regulator、GMAC）在验证前一律不放。

【R1】教训：最小 DTS 只含 `memory`、`uart2`、`emmc` 三个节点就能进提示符；
EVB 派生 DT 带了一堆 R1 未验证的节点（12 V 输入、PWM regulator、GMAC），
曾把诊断带偏。`bootph-all` 只加在必须的 UART 上。

### 后股：构建、封装、离线验证

问题：编译产物如何变成 BootROM 能下载的 loader？如何证明封装没坏？

手段：固化一条可复现命令链（make → 打包 → 解包 → 逐字节核对 → 记录
SHA/CRC）。**所有核对手续必须在主机侧完成**，实机只验证行为。

【R1】命令链：`make CROSS_COMPILE=arm-none-eabi- DTC=/usr/bin/dtc TEE=tee.bin`
→ `rkdeveloptool pack`（config.ini 三条目：471=DDR、472=usb472、FlashData=DDR；
注意 `parseLoader` 索引是 0 基）→ `unpack` 后 471/472/FlashData 有效字节与输入
逐字节一致、padding 全零 → 检查 `u-boot.itb` 的 load/entry/configuration →
记录文件大小、SHA-256、pack CRC。曾踩坑：`TEE` 变量为空导致 OP-TEE 数据缺失；
手工重封装导致运行时 DTB 位置错误（`sn` 探针证据）——两次都是靠离线核对
+ 探针定位的。

### 束股：实机验证与诊断纪律

问题：上板后按什么规则推进？每个停止点怎么变成下一轮的证据？

手段：只执行授权命令（本项目只允许 `db` RAM 下载）；串口输出里放固定字符
路标；每轮**只**改一个变量；把输出、命令、结论按时间记入
[逆向学习记录](../reverse-engineering-journal.md)，并同步 index/TODO。

【R1】判读表：`sm` = 运行时 DTB magic 合法；`sn+hex` = DTB 未落到 `__bss_end`
（记录地址值）；`L/M/N/O/P/Q/R/T` = OP-TEE 跳转链各点，最后字符是 `T` 说明
控制权已进入 OP-TEE，停止排查 FIT，改查 OP-TEE 入口约定与 FDT 参数。

### 小结

移植 = 承题（地基）→ 起讲（阶段模型）→ 入题（决策表）→ 起股（最小配置）→
中股（板级文件）→ 后股（离线验证）→ 束股（实机诊断），每轮循环只改一个变量。
八股填完，板子不亮也只差一个"待定位的停止点"；用路标二分，每轮减半。
