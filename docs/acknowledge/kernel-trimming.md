o k# Linux 内核裁剪方法论与实战

本文从嵌入式 Linux 工程师的视角，讲清楚如何在不破坏系统基本能力的前提下裁剪内核。目标有
两个：一是作为 R1 后续内核配置工作的操作手册；二是把本项目整理成一段能够在实习面试中讲清
问题、方法、证据和结果的工程经历。

> 文中通用原理来自 Linux 内核官方 Kconfig 文档；R1 的尺寸、日志和结论来自本仓库的实际
> 构建与上板记录。不要把 R1 上的某个配置集合直接当成其他板子的通用最小配置。

`SMP=y`、PSCI、OP-TEE 与 GICv2 的关系，以及 R1 INTID 55 问题的完整定位过程见
[ARMv7 启动链、OP-TEE 与 GICv2 调试](arm-boot-gicv2.md)。

---

## 1. 先定义“裁剪成功”

内核裁剪不是把 `.config` 中的 `y` 删除得越多越好，也不是只追求 zImage 最小。工程上的成功
标准是：在满足产品和诊断需求的条件下，减少代码、初始化路径、攻击面、启动时间和维护成本。

可以把目标写成一个约束问题：

```text
最小化：镜像大小 + 启动耗时 + 常驻内存 + 攻击面 + 维护成本

约束：
  启动链可用
  用户空间 ABI 完整
  必需硬件可用
  故障可诊断
  有可靠回滚路径
```

常见指标并不等价：

| 指标 | 能说明什么 | 不能说明什么 |
|---|---|---|
| zImage 大小 | 压缩后的传输和存储成本 | 解压后的内存、运行时正确性 |
| vmlinux 大小 | 未剥离 ELF 的总体规模 | 实际驻留内存 |
| `.text/.data/.bss` | 代码、初始化数据和零初始化数据 | 驱动 probe 是否成功 |
| 启用的 CONFIG 数 | 配置复杂度的粗略代理 | 功能是否完整、依赖是否正确 |
| 启动日志行数 | 初始化路径的可见程度 | 没打印的功能是否不存在 |
| boot time | 到某个里程碑的时间 | 长期稳定性和外设正确性 |

因此，裁剪前必须先写“验收合同”。R1 救援内核当前的合同是：

1. 开源 OP-TEE 提供 PSCI，Linux 启动 CPU0–CPU3；
2. UART2 控制台可交互并具有 controlling TTY；
3. initramfs 的 `/init` 能执行；
4. procfs、sysfs、devtmpfs、devpts 能挂载；
5. `/proc/uptime` 连续运行超过旧 30 秒故障边界；
6. 保留 eMMC、USB、RK805 RTC 和 ramoops 的后续验证能力；
7. 不写 eMMC，失败时可退回已验证的 clean v9。

如果没有这份合同，“能看到 shell 提示符”很容易被误判为成功。

## 2. 内核配置不是驱动清单

### 2.1 一个可用系统有五层依赖

```text
用户空间程序和 init 脚本
        ↓ 需要 syscall、ABI、伪文件系统
内核通用设施
        ↓ 需要总线、时钟、中断、内存管理
SoC 与设备驱动
        ↓ 需要匹配的 Device Tree 节点
固件接口：PSCI / SMC / OP-TEE
        ↓
Bootloader 传入 kernel、initramfs、DTB 和启动参数
```

“关掉 CAN/NFS/NTFS”是在设备驱动或文件系统层做减法；`FUTEX`、`EPOLL`、
`BINFMT_SCRIPT`、`ARM_PSCI` 则属于另外几层。它们都叫 `CONFIG_*`，但风险完全不同。

### 2.2 建议按领域分类，而不是按字母浏览

| 类别 | 典型配置 | 裁错后的表现 |
|---|---|---|
| 架构与执行环境 | `MMU`、`CPU_V7`、`AEABI`、`VFP` | 镜像不能运行或程序非法指令 |
| SMP 与固件 | `SMP`、`ARM_PSCI`、`HOTPLUG_CPU` | 只启动 CPU0 或次核失败 |
| 时间 | arch timer、`POSIX_TIMERS`、time32、RTC | 调度正常但应用计时/日历时间异常 |
| 用户空间 ABI | `FUTEX`、`EPOLL`、`EVENTFD`、`SYSVIPC` | libc、线程、事件循环或服务异常 |
| 程序格式 | `BINFMT_ELF`、`BINFMT_SCRIPT` | ELF 可执行但 shebang 脚本 `ENOEXEC` |
| 虚拟文件系统 | `PROC_FS`、`SYSFS`、`DEVTMPFS` | `/proc`、`/sys`、设备节点不可用 |
| 存储链路 | block、MMC、控制器、分区、文件系统 | 有控制器日志但找不到根文件系统 |
| 网络链路 | socket ABI、协议栈、总线、设备驱动、固件 | 能创建 socket 但没有网卡，或反之 |
| 诊断能力 | printk、kallsyms、pstore、SysRq | 系统更小，但死机后没有证据 |
| 产品外设 | CAN、DRM、sound、media、NFS | 对应产品功能缺失，通常可独立裁剪 |

先保住前六类，再按产品需求处理后四类，是比“看到陌生选项就关”更稳妥的顺序。

## 3. Kconfig 必须掌握的原理

### 3.1 `bool`、`tristate` 与 `y/m/n`

Kconfig 中常见类型：

- `bool`：只能为 `y` 或 `n`；
- `tristate`：可以是内建 `y`、模块 `m` 或禁用 `n`；
- `string/int/hex`：字符串或数值；
- `choice`：一组候选中的互斥选择。

`CONFIG_FOO=y` 表示代码链接进内核；`CONFIG_FOO=m` 表示生成模块；禁用在配置文件中通常写成：

```text
# CONFIG_FOO is not set
```

关闭 `CONFIG_MODULES` 后，所有依赖模块机制的 `m` 选择都会受到影响。救援镜像可以全内建，正式
系统则常把非启动必需驱动做成模块，以便升级和按需加载。

### 3.2 `depends on`、`select`、`imply` 与默认值

这四个概念决定了“我在 fragment 中写了 `y`，为什么最终没有生效”：

- `depends on BAR`：BAR 不满足时，FOO 的可选上限被压低，FOO 可能不可见或不能取 `y`；
- `select BAR`：FOO 反向强制 BAR 至少达到某个值；
- `imply BAR`：较弱的建议，BAR 仍可能被依赖或用户选择关闭；
- `default`：只在用户没有明确赋值时生效，多个可见默认值中先出现的通常优先。

Linux 官方文档特别提醒要谨慎使用 `select`，因为它可能强制目标符号而不检查目标自身依赖。
配置使用者要记住另一个现实：你显式关闭的符号可能被其他选项重新 `select` 回来。

所以 fragment 是“意图”，经过 Kconfig 求值后的最终 `.config` 才是“事实”。

### 3.3 Kconfig 和 Kbuild 分工

Kconfig 决定符号值，Kbuild/Makefile 决定哪些对象真正参加编译：

```make
obj-$(CONFIG_FOO) += foo.o
```

启动日志里看到某个驱动注册，说明它通常已经内建并执行初始化；构建输出中看到：

```text
AR drivers/media/built-in.a
```

却不代表 media 驱动真的启用。空目录也会生成 `built-in.a`。应检查最终 `.config`、实际对象、
符号表和运行日志，而不是凭一行 `AR` 判断。

## 4. `.config`、defconfig 和 fragment 的关系

### 4.1 四种文件角色

| 文件/目标 | 角色 |
|---|---|
| `defconfig` | 一个经过维护的架构或产品配置起点 |
| fragment/miniconfig | 只表达项目希望覆盖的少量配置 |
| `.config` | Kconfig 展开后的完整、实际编译输入 |
| `savedefconfig` 结果 | 相对 Kconfig 默认值压缩后的配置表示 |

不要把 fragment 当作完整配置，也不要只提交一个构建目录中的 `.config` 而不记录基线内核版本。
同一个 fragment 合并到不同内核版本或不同 defconfig，最终结果可能不同。

### 4.2 常用配置目标

```sh
make ARCH=arm multi_v7_defconfig  # 使用 ARM 多平台已知基线
make ARCH=arm allnoconfig         # 尽可能全部关闭
make ARCH=arm olddefconfig        # 接受新选项默认值，非交互解析依赖
make ARCH=arm menuconfig          # 交互式配置
make ARCH=arm nconfig             # 另一种交互界面，搜索体验较好
make ARCH=arm savedefconfig       # 生成精简 defconfig
make ARCH=arm listnewconfig       # 查看升级后新增、尚未决定的符号
```

官方支持通过 `KCONFIG_ALLCONFIG=<mini.config> make allnoconfig` 生成 miniconfig 基线；本项目使用
`scripts/kconfig/merge_config.sh` 合并基础 fragment 和板级 fragment，思路相同。

### 4.3 三种裁剪起点

| 策略 | 优点 | 风险 | 适用场景 |
|---|---|---|---|
| 已知 defconfig 上做减法 | 最容易保住基础 ABI | 遗留驱动多、体积较大 | 首次 bring-up、发行版 |
| `allnoconfig` 上做加法 | 最小、依赖显式 | 极易漏掉隐藏基础能力 | 固定硬件、成熟验收矩阵 |
| 分阶段混合 | 先可用，再建立白名单 | 需要维护两条配置证据 | 嵌入式产品最推荐 |

R1 的经验是：先用 `multi_v7_defconfig` 建立可启动且稳定的 v9，再用 `allnoconfig` 做 v10/v11。
如果一开始就追求绝对最小，缺少对照组时很难区分硬件、固件和配置问题。

## 5. 一套可复用的裁剪流程

### 第 0 步：冻结已知可用基线

保存以下证据：

```sh
cp build/kernel/.config build/artifacts/known-good.config
sha256sum build/artifacts/zImage build/artifacts/known-good.config
strings build/kernel/vmlinux | grep -m1 '^Linux version '
```

同时保存完整启动日志、DTB 哈希、initramfs 哈希、启动命令和 uptime。没有这些身份信息，后续
失败时很容易把旧镜像、旧 DTB 或旧 initramfs 混在一起。

### 第 1 步：定义硬件与软件需求矩阵

从四类证据建立清单：

1. 原理图、SoC TRM 和板级物料：有哪些真实硬件；
2. DTB/DTS：Linux 会枚举哪些设备；
3. 已知可用启动日志：实际 probe 过哪些驱动；
4. rootfs 与应用：用户空间依赖哪些系统调用、文件系统、网络协议和模块。

示例：R1 没有 PCIe 和 CAN，启动介质是 eMMC，因此 PCI、CAN、MTD/UBI 可以进入黑名单；但
AirPlay 最终需要网络协议栈，AK7755 最终需要 I2C/I2S/ASoC，不能因为当前最小 DT 未启用它们
就永久删除。

### 第 2 步：建立三张表

```text
KEEP：启动与基础 ABI，任何裁剪都不得破坏
LATER：当前救援阶段不用，但产品阶段会启用
DROP：硬件不存在或产品明确不需要
```

R1 示例：

| KEEP | LATER | DROP |
|---|---|---|
| ARMv7/MMU、PSCI、GIC、timer、UART | Wi-Fi、Bluetooth、ASoC、AK7755 | CAN、PCI、NFS server |
| initramfs、proc/sys/devtmpfs、futex | USB gadget、更多文件系统 | NTFS、CIFS、DRM、camera |
| eMMC、ext4、pstore、kallsyms | cpufreq、thermal、完整 PM | 非 RK322x SoC 驱动 |

`LATER` 很重要：它避免“现在没 probe”被错误解释成“产品永远不需要”。

### 第 3 步：先裁大块、低耦合功能

第一轮优先关闭能明确证明无用的子系统：

- 其他 SoC/平台；
- 不存在的总线，如 PCI、NAND/MTD；
- 不需要的产品外设，如 CAN、显示、摄像头；
- 不使用的网络文件系统；
- 不使用的压缩算法和调试框架。

不要第一轮就碰进程、时间、内存管理、文件描述符事件、程序格式和固件接口。这些配置体积不一定
大，却可能让用户空间以非常间接的方式失败。

### 第 4 步：每轮只改变一个逻辑集合

一个“变量”可以是一组有明确边界的配置，例如“关闭所有其他 Rockchip SoC 时钟”，而不是一次
同时关闭网络、SMP、文件系统和诊断能力。推荐迭代：

```text
A：已知可用基线
B：只裁其他架构/SoC
C：再裁不存在的总线
D：再裁不用的文件系统
E：再裁产品外设
```

失败时比较最后一个成功版本和当前版本，搜索范围会小很多。

### 第 5 步：解析依赖并审计最终配置

合并配置后必须运行 `olddefconfig`，再检查最终文件：

```sh
make -C "$KERNEL_SRC" O="$KERNEL_BUILD" \
  ARCH=arm CROSS_COMPILE=arm-none-eabi- olddefconfig

grep '^CONFIG_ARM_PSCI=' "$KERNEL_BUILD/.config"
grep '^CONFIG_BINFMT_SCRIPT=' "$KERNEL_BUILD/.config"
```

如果 fragment 请求了选项但最终没有出现：

1. 在源码中搜索其 Kconfig 定义；
2. 阅读 `depends on`、所属 `if/menu` 和 `choice`；
3. 搜索谁在 `select`/`imply` 它；
4. 检查架构、编译器和模块状态是否改变可见性；
5. 再改上层依赖，不要直接手改最终 `.config` 后就结束。

常用命令：

```sh
grep -Rnw "$KERNEL_SRC" -e 'config ARM_PSCI'
grep -Rnw "$KERNEL_SRC" -e 'select ARM_PSCI_FW'
"$KERNEL_SRC/scripts/diffconfig" old.config new.config
```

### 第 6 步：做静态门禁

至少检查版本、大小、必需项、禁止项和源码状态：

```sh
stat -c '%n %s' "$KERNEL_BUILD/arch/arm/boot/zImage" "$KERNEL_BUILD/vmlinux"
strings "$KERNEL_BUILD/vmlinux" | grep -m1 '^Linux version '

grep -E '^CONFIG_(SMP|ARM_PSCI|BINFMT_SCRIPT|PROC_FS|FUTEX)=y$' \
  "$KERNEL_BUILD/.config"

grep -E '^CONFIG_(CAN|NFS_FS|NTFS_FS|PCI|MTD|DRM|SOUND)=(y|m)$' \
  "$KERNEL_BUILD/.config" && exit 1 || true

git -C "$KERNEL_SRC" status --short
```

项目中可以把必需项与禁止项做成脚本，让 CI 在构建后检查。只检查 fragment 没有意义，门禁对象
必须是解析后的 `.config`。

进一步分析体积可以使用：

```sh
size "$KERNEL_BUILD/vmlinux"
nm --size-sort --print-size "$KERNEL_BUILD/vmlinux" | tail -n 50
"$KERNEL_SRC/scripts/bloat-o-meter" old-vmlinux new-vmlinux
```

### 第 7 步：RAM-only 上板，按层验收

不要一上来只运行 `uptime`。建议按以下顺序检查：

```sh
cat /proc/version
cat /proc/cmdline
cat /sys/devices/system/cpu/online
mount
cat /proc/uptime
cat /proc/interrupts
cat /proc/meminfo
ls -l /dev/console /dev/ttyS2
tty
```

然后检查硬件节点、应用依赖和长期稳定性。每一项都要知道它验证的是哪一层。

### 第 8 步：失败时先做症状到层级的映射

| 症状 | 优先检查 |
|---|---|
| 内核完全无输出 | 镜像身份、加载地址、解压、earlycon、DTB |
| 只有 CPU0 | PSCI、spin-table、`enable-method`、SMP/hotplug、固件 |
| `/init` `error -8` | shebang、`BINFMT_SCRIPT`、解释器架构/权限 |
| `/proc` 不存在 | `PROC_FS` 与 init 是否实际执行、mount 是否成功 |
| `/dev` 空 | devtmpfs 配置、挂载、设备驱动 probe |
| `can't access tty` | controlling TTY、devpts、`setsid`/`cttyhack` |
| uptime 正常、date 错 | RTC/设备树/NTP/时区，而非 arch timer |
| socket 可创建但没网卡 | 协议栈有、总线/驱动/固件/DT 缺失 |
| 配置写了 `y` 但最终为 `n` | Kconfig 上层依赖或 choice |

### 第 9 步：成功后再生成长期配置

只有上板通过验收矩阵后，才可以：

1. 更新已知可用基线；
2. 保存最终 `.config`、fragment、哈希和日志；
3. 用 `savedefconfig` 评估是否形成长期 defconfig；
4. 做多次冷启动、压力和升级测试；
5. 提交 Git。

构建成功不是阶段完成，能进 shell 也不等于验收完成。

### 第 10 步：冻结救援核心，用外设 fragment 逐层扩展

稳定的最小内核不应随着每个外设试验反复增删。R1 后续把配置按职责分层：

```text
allnoconfig
  + kernel/config/r1.fragment                  # 板级公共配置
  + kernel/config/r1-5.10-rescue-minimal.fragment
  + kernel/config/r1-5.10-peripheral-emmc.fragment
  + 后续某一个 peripheral-usb/wifi/audio fragment
```

`rescue-minimal` 负责启动、四核 PSCI、串口、initramfs、基础 ABI 和诊断能力；外设 fragment
只声明对应总线、控制器、供电和文件系统依赖。当前 v11 为过渡方便，已经包含部分 eMMC/USB
配置；单独的 eMMC fragment 仍有价值，因为它明确记录“谁拥有这些选项”，以后从救援核心移除
外设驱动时，不会丢失可复现配置。

Kconfig 只决定驱动是否存在，DT 决定这块板上有哪些设备、地址、中断、时钟、引脚和供电关系。
所以一次严格外设 A/B 应使用同一 zImage 和 initramfs：A 线加载已验证的最小 DT，B 线只替换为
目标外设 DT。若 A 失败，先处理内核/镜像身份；若 A 成功而 B 失败，才调查该外设的 DT、时钟、
复位、pinctrl、regulator 和驱动 probe。

## 6. “基础 Linux 能力”检查表

这不是所有产品的绝对必选项，而是一个适合嵌入式救援/常规用户空间的审计起点。

### 6.1 启动、架构和 SMP

```text
MMU, CPU_V7, AEABI
VFP/NEON（若用户空间按硬浮点/NEON 构建）
SMP, NR_CPUS
ARM_PSCI/ARM_PSCI_FW 或平台实际使用的次核启动方式
ARM_GIC, ARM_ARCH_TIMER
```

`SMP=y` 只表示内核具有 SMP 框架，不表示固件接口和 DT 的 CPU enable method 已就绪。

### 6.2 程序与用户空间 ABI

```text
BINFMT_ELF, BINFMT_SCRIPT
MULTIUSER, BASE_FULL
FUTEX
POSIX_TIMERS, COMPAT_32BIT_TIME（旧 32 位用户空间）
EPOLL, SIGNALFD, TIMERFD, EVENTFD
AIO, ADVISE_SYSCALLS
MEMBARRIER, RSEQ
SYSVIPC, INOTIFY_USER
```

这些选项通常比某个外设驱动小得多，却可能是 libc、线程库、事件循环和服务管理器的隐含依赖。

### 6.3 initramfs 与虚拟文件系统

```text
BLK_DEV_INITRD
正确的 RD_GZIP/RD_XZ 等解压算法
PROC_FS, PROC_SYSCTL
SYSFS
DEVTMPFS, DEVTMPFS_MOUNT
TMPFS, SHMEM
UNIX98_PTYS, devpts
```

内核“支持 procfs”和用户空间“已经把 procfs 挂载到 `/proc`”是两件事。

### 6.4 存储

完整链路通常是：

```text
CONFIG_BLOCK
  → 控制器总线/时钟/reset/pinctrl
  → MMC/NVMe/SATA/USB storage 子系统
  → 具体 host controller
  → 分区解析
  → 根文件系统驱动
```

启动根文件系统所需的驱动必须内建，或者包含在能先被加载的 initramfs 中。把根设备驱动做成
模块，却又没有 initramfs 加载该模块，会形成启动闭环。

### 6.5 网络

网络也不是一个开关：

```text
socket/Unix/packet ABI
  → IPv4/IPv6 协议栈
  → cfg80211/mac80211 或 Ethernet PHY/MDIO
  → SDIO/USB/PCIe 等总线
  → 具体设备驱动
  → firmware + NVRAM
  → 用户空间 DHCP/WPA/BlueZ
```

救援阶段可以保留协议栈而暂不编设备驱动；产品阶段再按实际硬件逐层启用。

### 6.6 时间的四个概念

面试和排障中要区分：

1. **clocksource**：单调计数来源，例如 ARM architected timer；
2. **clockevent**：产生调度 tick/定时事件；
3. **POSIX/time syscall ABI**：应用获取和等待时间的接口；
4. **RTC/wall clock**：掉电后保留的日历时间，通常还需要用户空间 NTP 和时区数据。

因此 `/proc/uptime` 不存在不一定是“内核没有时间”；`date` 错也不一定是 timer 驱动坏了。

### 6.7 诊断能力

建议 bring-up 阶段保留：

```text
PRINTK, PRINTK_TIME
KALLSYMS
IKCONFIG + IKCONFIG_PROC
MAGIC_SYSRQ + serial SysRq
PSTORE/PSTORE_RAM/PSTORE_CONSOLE
PROC_FS, SYSFS
```

等系统稳定且有替代诊断链路后，再讨论关闭哪些。为了省几十或几百 KiB 删除唯一故障证据，通常
得不偿失。

## 7. R1 实战：v9 → v10 → v11

### 7.1 已知可用 v9

clean Linux 5.10.262 v9 以 `multi_v7_defconfig` 为起点：

```text
启用 y/m 项：3285
zImage：10,158,592 B
实机：CPU0–CPU3 online，uptime 超过 30 秒，四核 IPI 有增长
```

优点是基础能力完整，缺点是包含大量其他 ARM 平台、CAN、网络文件系统、PCI、MTD、图形和声音
配置。它是裁剪前的可靠对照组。

### 7.2 过度裁剪 v10

v10 从 `allnoconfig` 白名单启用硬件和文件系统：

```text
启用 y/m 项：447
zImage：2,328,112 B
```

主机静态审计确认 CAN/NFS/NTFS 等已经关闭，但实机出现：

```text
smp: Brought up 1 node, 1 CPU
Failed to execute /init (error -8)
Run /bin/sh as init process
cat: can't open '/proc/uptime': No such file or directory
```

最容易做出的错误结论是“procfs 被裁掉、内核时间没了”。实际最终 `.config` 中：

```text
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
CONFIG_DEVTMPFS=y
# CONFIG_BINFMT_SCRIPT is not set
# CONFIG_ARM_PSCI is not set
```

因果链是：

```text
缺 BINFMT_SCRIPT
  → #!/bin/busybox sh 无法执行
  → /init 没有运行
  → mount -t proc/sysfs/devtmpfs 从未执行
  → /proc 不存在

缺 ARM_PSCI
  → Linux 无法通过 OP-TEE 启动次核
  → SMP 框架存在但只有 CPU0 online
```

这是裁剪方法论中最重要的一课：**观察到的缺失功能，可能是更上游初始化失败的二阶结果。**

### 7.3 修正后的 v11

v11 不退回庞大的多平台配置，而是在白名单上补回基础 ABI：

```text
启用 y/m 项：508
zImage：2,721,856 B
相对 v10：+393,744 B
相对 v9：-7,436,736 B（约缩小 73%）
```

补回的主要集合：

- `BINFMT_SCRIPT`；
- `ARM_PSCI`、hotplug、CPU idle/PM；
- multiuser、futex、POSIX timers、time32；
- epoll/signalfd/timerfd/eventfd、AIO、membarrier/rseq；
- inotify、SysV IPC、RTC；
- kallsyms、内嵌配置、printk 时间戳、SysRq；
- initramfs 常用 applet 和 `setsid cttyhack`。

继续关闭的集合：CAN、NFS/NFSD、NTFS、CIFS/9P、PCI、MTD/UBI、DRM/fb、sound、media、
无线驱动和模块。v11 的最终实机结论必须等上板日志，不能由主机构建成功替代。

### 7.4 本项目的可复现构建

```sh
scripts/build-initramfs.sh

KERNEL_SRC=/tmp/r1-linux-5.10-clean-src \
KERNEL_BUILD=build/kernel-5.10-rescue-baseline-v11 \
KERNEL_DEFCONFIG=allnoconfig \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10-rescue-minimal.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dts \
JOBS=8 GENERATE_COMPILE_COMMANDS=0 \
scripts/build-kernel.sh
```

配置意图位于
[`kernel/config/r1-5.10-rescue-minimal.fragment`](../../kernel/config/r1-5.10-rescue-minimal.fragment)，
时间线与实机证据位于[逆向学习记录](../reverse-engineering-journal.md)。

## 8. 常见错误与技巧

### 8.1 只看 fragment，不看最终 `.config`

错误：fragment 写了 `CONFIG_FOO=y`，就认为 FOO 一定生效。

正确：合并、`olddefconfig` 后检查最终值；CI 也检查最终值。

### 8.2 把 Device Tree 当成驱动

DT 节点描述硬件，内核配置决定驱动是否存在。通常要同时满足：

```text
DT status = "okay"
+ compatible 能匹配驱动
+ 驱动已内建/模块已加载
+ clock/reset/regulator/IRQ 等资源正确
= probe 才可能成功
```

删除 DT 节点不会让驱动代码从镜像消失；关闭驱动也不会自动修改 DT。

### 8.3 直接使用 `localmodconfig`

`localmodconfig` 根据当前主机已加载模块缩小配置，适合给当前 PC 定制内核；对交叉编译板卡很
危险，因为主机加载的 x86/Fedora 模块不能代表 RK3229 板载硬件，也看不到尚未 probe 的设备。

更安全的方法是使用目标板日志、DT、rootfs 需求和已知配置建立白名单。

### 8.4 一次裁几百个选项后直接上板

构建能通过只说明静态依赖和链接基本成立。像 v10 一样，运行时仍可能丢失 shebang、PSCI 或
系统调用。大批量裁剪后应先做配置门禁，再按功能组二分，不要盲目逐项猜。

### 8.5 为了体积过早删除调试能力

bring-up 阶段的优化目标不是最终量产体积。先保留串口、符号、pstore 和 SysRq，定位完成后再
测它们各自的尺寸收益。诊断配置也应分级：低成本基础诊断保留，ftrace/perf/KASAN 等重工具按需
启用。

### 8.6 忽略 rootfs 与内核的契约

换 libc、BusyBox、systemd 或容器运行时，都会改变内核 ABI 需求。裁剪不能只由硬件工程师看
DT 完成；还应从用户空间构建配置、系统调用跟踪、启动服务和错误日志反推需求。

### 8.7 用压缩后体积判断收益

两个功能的源码量相同，压缩率可能差很多。应同时比较 zImage、vmlinux section、符号尺寸、
启动时间和运行内存。真正产品化时还要测功耗、I/O 延迟和长期稳定性。

## 9. 面试表达模板

### 9.1 一分钟项目叙述

> 我在 RK3229 设备上适配主线 U-Boot、开源 OP-TEE 和 Linux 5.10。内核最初使用
> multi_v7_defconfig，四核可以稳定启动，但 zImage 约 10.2 MB，包含大量无关平台和驱动。
> 我先冻结可用基线，再根据 DT、启动日志和用户空间需求把配置分成 KEEP/LATER/DROP，使用
> allnoconfig 加板级 fragment 建立白名单。第一版降到 2.33 MB，但实机暴露两个问题：缺
> BINFMT_SCRIPT 导致 init 脚本 ENOEXEC，从而没有挂载 procfs；缺 ARM_PSCI 导致 SMP 内核只
> 启动 CPU0。我通过完整日志和最终 .config 而不是 fragment 定位原因，补回 PSCI、futex、
> epoll、POSIX timer 等基础 ABI，得到约 2.72 MB 的 v11，同时继续关闭 CAN、NFS、NTFS、PCI、
> MTD、图形和声音。这个过程让我形成了“先验收合同、再分层裁剪、最终配置门禁、RAM-only
> A/B 验证”的方法。

### 9.2 STAR 结构

| 部分 | 可以怎么讲 |
|---|---|
| Situation | 多平台 defconfig 能启动，但体积和无关驱动过多 |
| Task | 在保持四核 OP-TEE 启动与救援能力的前提下裁剪 |
| Action | 冻结 v9；需求分类；allnoconfig 白名单；静态门禁；实机 A/B；日志反推依赖 |
| Result | 10.16 MB → 2.72 MB；发现并修复 init/PSCI 基础能力遗漏；形成可复现文档与产物 |

不要只说“使用 menuconfig 关闭了很多驱动”。面试官更关心你如何证明哪些能关、失败后如何定位、
怎样避免拿错镜像，以及你是否理解硬件、DT、固件、内核和用户空间之间的边界。

## 10. 高频八股问题

### Q1：defconfig 和 `.config` 有什么区别？

defconfig 是配置起点或相对默认值的精简表示；`.config` 是结合 Kconfig 默认值、依赖、选择关系和
用户覆盖后生成的完整结果，实际编译以 `.config` 为准。

### Q2：`depends on` 和 `select` 有什么区别？

`depends on` 限制当前符号的可见性和最大取值；`select` 是反向依赖，会强制另一个符号达到至少
某个值，并可能绕过目标依赖，所以应谨慎使用。

### Q3：内建和模块怎么选择？

启动到根文件系统之前必须使用的驱动要内建，或者放在能被先加载的 initramfs 中；热插拔、非
关键外设适合模块。模块减少常驻代码并便于升级，但引入加载、签名、版本和 rootfs 管理成本。

### Q4：为什么 `SMP=y` 仍可能只有一个 CPU？

SMP 只是通用框架。还需要 DT CPU 节点、正确的 `enable-method`、PSCI/spin-table 平台支持、
中断控制器、固件实现和次核入口都正确。R1 v10 就因为缺 `ARM_PSCI` 只启动 CPU0。

### Q5：为什么 `PROC_FS=y` 仍没有 `/proc`？

因为编译支持和运行时挂载是两件事。R1 v10 的 procfs 已编入，但 `/init` 因缺
`BINFMT_SCRIPT` 没有执行，mount 命令从未运行。

### Q6：怎样判断一个驱动能否裁掉？

交叉核对硬件 BOM/原理图、DT、已知启动日志、rootfs/应用需求和恢复路径；只凭“这次日志没有
出现”不够，因为设备可能尚未在 DT 启用或 probe 失败。

### Q7：怎样量化裁剪收益？

比较压缩镜像、vmlinux sections、符号尺寸、启动耗时、常驻内存和功能验收；保存相同工具链和
源码 commit 下的 A/B 数据。

### Q8：为什么不直接从 `allnoconfig` 开始？

它适合固定硬件的最终白名单，但会关闭许多不显眼的基础 ABI，甚至可能选择不适合目标的架构
choice。首次 bring-up 应先建立已知可用基线，再向白名单迁移。

### Q9：内核时间由什么组成？

要区分 clocksource、clockevent、POSIX/time syscall ABI 和 RTC/wall clock。uptime、调度 tick、
日历时间、时区/NTP 分属不同层，不能用一个 `date` 结果概括。

### Q10：Device Tree 和 Kconfig 谁决定设备能不能用？

两者共同决定。Kconfig/Kbuild 提供驱动代码，DT 描述实例和资源；还需固件、时钟、reset、供电、
IRQ 和 probe 都正确。

## 11. 建议练习

1. 用 `scripts/diffconfig` 比较 R1 v9、v10、v11，并把差异按本文分类；
2. 在不改硬件驱动的情况下只关闭 `BINFMT_SCRIPT`，预测并验证错误；
3. 只关闭 `ARM_PSCI`，解释为什么 `SMP=y` 仍只有 CPU0；
4. 比较 `PROC_FS=n` 与“procfs 未挂载”的日志和运行时差异；
5. 用 `bloat-o-meter` 找出 v10 → v11 增加的主要符号；
6. 为项目写一个脚本，自动检查 KEEP/DROP 配置和版本后缀；
7. 切换到完整 DT 后，逐层启用 eMMC、USB、Wi-Fi 和音频，每层只改变一个逻辑集合。

做到这些后，你掌握的不只是 menuconfig，而是一套可验证、可回滚、能解释因果链的内核配置
工程方法。

## 12. 参考资料与证据

### Linux 官方文档

- Linux kernel maintainers, *Kconfig Language*，Kconfig 类型、默认值、`depends on`、`select`
  与 `imply` 的权威说明，访问于 2026-08-10：
  <https://docs.kernel.org/6.7/kbuild/kconfig-language.html>
- Linux kernel maintainers, *Kconfig make config*，Linux 5.10 的配置目标、
  `KCONFIG_ALLCONFIG` 和 miniconfig 说明，访问于 2026-08-10：
  <https://docs.kernel.org/5.10/kbuild/kconfig.html>
- Rob Landley 等，*Ramfs, rootfs and initramfs*，initramfs、rootfs、cpio archive 和 init
  交接说明，访问于 2026-08-10：
  <https://docs.kernel.org/6.5/filesystems/ramfs-rootfs-initramfs.html>
- Linux kernel maintainers, *Explaining the “No working init found” boot hang message*，init
  执行失败的官方排查入口，访问于 2026-08-10：
  <https://docs.kernel.org/6.19/admin-guide/init.html>

### R1 本地证据

- [`build/artifacts/rescue-v10-four-core-20260810.log`](../../build/artifacts/rescue-v10-four-core-20260810.log)：
  v10 单核、`/init` `ENOEXEC` 和 fallback shell 的实机日志；
- [`kernel/config/r1-5.10-rescue-minimal.fragment`](../../kernel/config/r1-5.10-rescue-minimal.fragment)：
  v11 白名单配置意图；
- [主线 Linux Bring-up](../mainline-bringup.md)：v9/v10/v11 的阶段性结论；
- [逆向学习记录](../reverse-engineering-journal.md)：构建命令、哈希、失败和实机证据时间线。
