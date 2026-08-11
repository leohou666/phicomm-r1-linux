# ARMv7 启动链、OP-TEE 与 GICv2 调试

本文把斐讯 R1 的真实 bring-up 过程整理成一套可复用的嵌入式 Linux 调试方法：先理解
BootROM、DDR loader、SPL、OP-TEE、U-Boot proper 和 Linux 各自负责什么，再以 R1 的
“次核已经 online，但系统仍然挂住”问题为案例，学习如何沿 PSCI、SGI、GICv2 和
TrustZone 边界逐层缩小范围。

它既是项目记录，也是面试复习材料。更完整的 U-Boot 配置、Kconfig 和移植基础见
[U-Boot 前置知识](uboot.md)；内核功能裁剪见
[Linux 内核裁剪方法论](kernel-trimming.md)。

## 1. 先建立证据等级

这类问题横跨 ROM、闭源 loader、安全固件、bootloader 和 Linux。若不区分证据等级，很容易
把“看起来像”写成“已经证明”。本文使用以下标签：

- **已验证**：R1 串口日志、寄存器快照、镜像对照或 clean kernel 复测直接支持；
- **强推断**：与所有现有证据一致，但尚缺少一个边界对照；
- **待确认**：仍有两个以上合理解释，不能下结论。

当前问题的结论是：

- **已验证**：OP-TEE 运行前，secure SPL 已看到 CPU0 的 `GICC_RPR=0`、`GICC_APR0=1`，
  且唯一 active 中断为 INTID 55；
- **已验证**：严格清理该状态后，clean Linux 5.10 双核稳定约 700 秒，四核 online 且越过
  旧约 30 秒冻结边界；
- **强推断**：INTID 55 对应 RK322x USB OTG，因此该状态来自 MaskROM/RockUSB RAM 下载链；
- **待确认**：究竟是 BootROM、usbplug/DDR loader 还是其交接代码最初 acknowledge 了
  INTID 55 却没有 deactivate，尚未通过旧厂商完整链的 secure 寄存器对照确定。

## 2. 从上电到 Linux：每一阶段做什么

### 2.1 通用 ARM SoC 启动链

```text
复位
  ↓
BootROM
  ↓  加载极小的第一阶段程序
TPL / DDR loader（若平台需要）
  ↓  DRAM 可用
SPL
  ↓  装载安全固件和完整 U-Boot
OP-TEE / secure monitor
  ↓  建立 PSCI/SMC 服务并返回 normal world
U-Boot proper
  ↓  装载 kernel、DTB、initramfs，执行 bootz
Linux
  ↓
/init → rootfs / 用户空间
```

这不是所有 ARM 板都固定采用的唯一顺序。例如有的平台没有 TPL，有的平台由 TF-A 提供
secure monitor，有的平台不使用 OP-TEE。但“前一阶段只做足够的初始化，再把控制权和机器状态
交给下一阶段”是共同模型。

### 2.2 BootROM

BootROM 是 SoC 内部不可修改的只读代码。它通常负责：

1. 根据启动引脚或内部状态选择 eMMC、SPI、USB 等介质；
2. 做最小的时钟和 SRAM 初始化；
3. 把下一阶段复制到片内 SRAM 或规定地址；
4. 做格式/校验检查并跳转。

BootROM 不是 U-Boot 的一部分。调试它时通常没有源码，只能依靠 ROM 协议、USB 枚举、加载器
格式、串口路标和下一阶段看到的寄存器状态来反推。

### 2.3 TPL、DDR loader 与 Rockchip 471

TPL 是 U-Boot 的 Tiny Program Loader，常用于 SRAM 极小且 DDR 初始化代码放不进 SPL 的平台。
它最重要的任务通常是 DDR training：设置 DDR PHY、DQS/DQ 时序和 DRAM 控制器，使大容量
DRAM 可用。

R1 的 RAM-only 链使用 vendor DDR blob，Rockchip USB 日志把这个阶段标成 `471`。要注意：

- `TPL` 是 U-Boot 的通用构建阶段名称；
- `471` 是本项目所见 Rockchip USB loader 协议/镜像阶段标签；
- 二者在当前链上承担相近的“先让 DRAM 可用”职责，但不能在所有平台上机械地画等号。

### 2.4 SPL

SPL 是 Secondary Program Loader。它比 U-Boot proper 小，运行环境也更受限制，典型任务包括：

- 建立最小 C 运行环境、栈和 global data；
- 只初始化装载下一阶段所需的 UART、MMC、SPI 或 USB；
- 解析 FIT；
- 把 OP-TEE、U-Boot proper 和 DTB 放到约定的 DRAM 地址；
- 做 cache/MMU 等交接清理并跳转。

R1 当前主线 SPL 由 Rockchip RAM loader 的 `472` 阶段送入。与 `471` 一样，`472` 是此链的
Rockchip 标签，不是“所有 SPL 都叫 472”。

SPL 与 U-Boot proper 是两个不同的构建上下文。常见差异有：

| 项目 | SPL | U-Boot proper |
|---|---|---|
| 目标 | 尽快装载下一阶段 | 提供完整 bootloader 功能 |
| 空间 | SRAM/早期 DRAM，严格受限 | DRAM，限制宽松得多 |
| 配置 | `CONFIG_SPL_*`、`CONFIG_SPL_BUILD` | 完整 U-Boot 配置 |
| DT | 常是裁剪后的 SPL DT | 完整 control DT |
| 驱动 | 仅装载路径必需项 | MMC、USB、网络、文件系统、命令等 |
| 调试 | 早期 UART 单字符路标很重要 | console、`md`、`fdt`、`mmc` 等命令可用 |

所以“U-Boot proper 能识别某设备”不能推出“SPL 也编进了同一驱动/DT 节点”；反过来也一样。

### 2.5 OP-TEE 在这条链中的位置

OP-TEE OS 运行在 ARM TrustZone secure world。R1 当前使用的 RK322x OP-TEE 3.7 建立
secure monitor，并为 normal world 提供 PSCI 服务。Linux DT 中：

```dts
psci {
    compatible = "arm,psci-1.0", "arm,psci-0.2";
    method = "smc";
};
```

`method = "smc"` 的含义是：Linux 通过 Secure Monitor Call 进入固件，请它执行 CPU
上电、下电等特权操作。Linux 不直接替 OP-TEE 写 PMU/复位寄存器。

在本项目中，SPL 从 FIT 中：

1. 把 OP-TEE firmware 装到 `0x68400000`；
2. 把 U-Boot proper 装到 `0x61000000`；
3. 把 DTB 地址作为交接参数；
4. 进入 OP-TEE；
5. OP-TEE 初始化 secure world 后切回 normal world，进入 U-Boot proper。

这解释了为什么日志会先出现 `I/TC: Initialized`，随后才出现完整的 `U-Boot ...` banner。

### 2.6 U-Boot proper

U-Boot proper 是用户通常看到 `=>` 提示符的完整 U-Boot。它负责：

- 枚举 DRAM、MMC、串口等设备；
- 读取 kernel、DTB、initramfs；
- 修正 `/chosen`、bootargs 或内存信息；
- 检查镜像格式和地址；
- 按 ARM Linux boot protocol 跳到 kernel。

R1 当前手动启动的核心形式是：

```text
bootz <zImage-address> <initramfs-address>:<size> <dtb-address>
```

U-Boot 并不会把“自己的驱动”传给 Linux。它传递的是镜像、DTB、命令行和机器状态；Linux
随后使用自己的驱动重新接管硬件。

### 2.7 交接的是文件，也是状态

“U-Boot 有没有给 kernel 传完整东西”不能只看 zImage、DTB、initramfs 三个地址。真正的交接
契约还包括：

- DRAM 内容和 reserved-memory 不重叠；
- CPU 所处执行状态、异常级/安全世界符合协议；
- MMU、cache 和分支预测状态符合要求；
- 次核处于固件可管理的状态；
- timer、UART、GIC 等硬件没有不可恢复的半完成事务；
- DT 的 CPU、PSCI、GIC、timer 描述与固件实现一致。

R1 这次恰好说明：三个文件都完整、CPU1 也能启动，系统仍可因早期遗留的 GIC active priority
状态挂死。它是“机器状态交接错误”，不是“U-Boot 少传了一个文件”。

## 3. GICv2 基础

### 3.1 GIC 解决什么问题

GIC（Generic Interrupt Controller）把外设和 CPU 产生的中断，按目标 CPU、优先级和安全组
进行仲裁并交给处理器。GICv2 主要分成两部分：

```text
外设 / 软件触发
      ↓
GIC Distributor（GICD）
  全局 enable、group、priority、target、pending、active
      ↓
每 CPU 的 CPU Interface（GICC）
  priority mask、running priority、acknowledge、EOI/deactivate
      ↓
CPU IRQ/FIQ 异常入口
```

R1 当前 DT 使用的物理窗口是：

| 模块 | 地址 | 用途 |
|---|---:|---|
| GIC Distributor | `0x32011000` | 全局中断分发状态 |
| GIC CPU interface | `0x32012000` | 当前 CPU 的接收与完成状态 |

地址来自 [R1 GIC400 DT A/B](../../kernel/dts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dts)
和实机可读寄存器，不应推广成所有 RK322x 板的唯一映射。

### 3.2 SGI、PPI、SPI

| 类型 | INTID | 产生者和范围 | 常见用途 |
|---|---:|---|---|
| SGI | 0–15 | 软件触发，发送给一个或多个 CPU | Linux IPI、reschedule、call function |
| PPI | 16–31 | 每 CPU 私有 | ARM architected timer、local watchdog |
| SPI | 32–1019 | 共享外设 | UART、MMC、USB、GPIO 等 |

设备树的 GIC SPI 编号不直接写最终 INTID。通常：

```text
INTID = 32 + GIC_SPI specifier
```

因此 `GIC_SPI 23` 对应 `32 + 23 = INTID 55`。Linux 5.10.262 固定 commit
`065a677fad98698de04279ba2cb152a472ab8b1f` 的
[`rk322x.dtsi`](https://github.com/gregkh/linux/blob/065a677fad98698de04279ba2cb152a472ab8b1f/arch/arm/boot/dts/rk322x.dtsi#L711-L715)
把该 SPI 分配给 USB OTG 控制器；这是把寄存器位映射回具体外设的关键一步。

### 3.3 一次中断的状态机

理解 GIC 问题最重要的不是“中断有没有 enable”，而是中断所处状态：

```text
inactive
   │ 外设拉线或软件触发
   ▼
pending
   │ CPU interface acknowledge（读 IAR）
   ▼
active
   │ active 期间再次触发
   ├──────────────→ active + pending
   │ handler 完成并 EOI/deactivate
   ▼
inactive（或回到 pending）
```

只清 pending 不一定能修复 active；只 disable 也不会自动消除已经 active 的 running priority。
在采用 priority drop 与 deactivation 分离的模式时，写 `EOIR` 和写 `DIR` 还承担不同阶段的
职责。调试时必须结合当前 CPU interface 模式阅读 Arm GICv2 规范，而不是随意向寄存器写零。

### 3.4 优先级为什么会把 CPU“堵死”

GIC 中数值越小，优先级越高。几个关键寄存器：

| 寄存器 | 含义 | R1 调试价值 |
|---|---|---|
| `GICC_PMR` | Priority Mask Register | 控制哪些优先级可送给 CPU |
| `GICC_RPR` | Running Priority Register | 当前 CPU interface 认为正在运行的最高优先级 |
| `GICC_HPPIR` | Highest Priority Pending Interrupt | 观察候选 pending INTID |
| `GICC_APR0..3` | Active Priorities | 记录 active priority level |
| `GICC_IAR` | Interrupt Acknowledge | handler 读取 INTID并进入 active |
| `GICC_EOIR` | End of Interrupt | priority drop，模式相关地也可 deactivate |
| `GICC_DIR` | Deactivate Interrupt | EOImode 分离时显式 deactivate |

R1 异常 CPU0 的 `RPR=0x00`。零是最高优先级，所以普通 timer PPI 和 Linux SGI 即使 pending，
也无法抢占它。CPU1 的正常空闲值则是 `RPR=0xff`。

这解释了一个表面矛盾：日志能看到 CPU1 online，甚至共享内存 completion 值已经变化，但 CPU0
仍无法正常推进。共享内存一致性和 CPU1 执行路径没有坏；坏的是 CPU0 接收普通中断的通道。

### 3.5 Distributor 与 per-CPU/banked 状态

调试时要区分：

- GICD 中 SPI 的 enable/pending/active 多为全局共享状态；
- SGI/PPI 的部分 GICD 寄存器视图是 per-CPU/banked；
- GICC 是当前 CPU interface 的状态，同一个地址由不同 CPU 访问会得到各自结果；
- 开启 TrustZone security extensions 后，secure/non-secure 访问权限、Group 0/Group 1 和部分
  寄存器视图还会不同。

因此，normal-world Linux 读到 `GICD_ISACTIVER0=0`，不代表 secure Group 0 或 secure CPU
interface 中一定没有遗留 active priority。R1 最终正是把探针前移到 secure SPL 后才看清完整
状态。

## 4. PSCI、SMP 与 GIC 是三条相连但不同的链

Linux 启动次核的简化过程是：

```text
CPU0: prepare secondary entry / shared state
CPU0: PSCI_CPU_ON via SMC
secure firmware: power/reset CPU1, set entry
CPU1: secondary_startup → secondary_start_kernel
CPU1: 初始化 per-CPU GIC/timer/scheduler
CPU1: 标记 online，唤醒 CPU0
CPU0/CPU1: 依靠 SGI/IPI 继续调度和同步
```

这里至少有三种不同故障：

1. **PSCI/电源链坏**：CPU1 根本没有执行入口；
2. **次核初始化坏**：CPU1 进入汇编但没完成 `secondary_start_kernel()`；
3. **GIC/IPI 接收链坏**：CPU1 已 online，但某个 CPU 收不到 SGI/PPI，后续 completion、调度、
   timer 或 RCU 卡死。

所以看到 `psci_cpu_on()` 返回成功，只能证明固件接受请求；看到 `CPU1: ...` 和 online 才能证明
次核完成 Linux 初始化；看到 `/proc/interrupts` 中各 CPU 的 IPI 持续增长，才进一步证明跨核
中断在工作。

## 5. R1 实战：怎样从“开第二核就挂”定位到 INTID 55

### 5.1 最初现象

开源 OP-TEE 已打印：

```text
psci_cpu_on: core_id: 1
Secondary CPU Switching to normal world boot
```

Linux 停在：

```text
smp: Bringing up secondary CPUs ...
```

最初可能性很多：

- DT 的 CPU MPIDR 或 `enable-method` 错；
- OP-TEE 的 PSCI 实现没有正确返回次核；
- Linux 次核页表、cache 或 coherency 错；
- GIC target map 错，CPU1 的 IPI 送不到 CPU0；
- CPU0 IRQ 被 mask；
- secure firmware 遗留 GIC 状态；
- timer 或后续驱动初始化挂死。

不能在这个阶段直接“把设备树调回去”或“换 OP-TEE”。正确做法是给每条假说设计单变量证据。

### 5.2 第一步：给次核路径加路标

先在 Linux `secondary_startup` 汇编和 `secondary_start_kernel()` C 路径加入单字符 UART 路标。
实机依次到达：

```text
ABCDE ... F ... N ... R
```

随后更细的路标证明 CPU1：

- 已进入 Linux C 代码；
- 已完成 per-CPU 初始化；
- 已打开 IRQ/FIQ/abort；
- 已标记 online；
- 已进入 idle/hotplug 后续路径。

这一步直接排除了“OP-TEE 没有拉起 CPU1”和“大部分次核入口代码出错”。

### 5.3 第二步：把同步方向拆开

CPU0 等 CPU1 和 CPU1 唤醒 CPU0 是两个方向。通过临时 polling completion A/B，实机得到
`xop` 等路标，证明：

- CPU1 写入的共享状态对 CPU0 可见；
- CPU0 的 `__cpu_up()` 能在不依赖首个中断唤醒时返回；
- cache coherency 和 completion 数据本身不是根因。

随后正常流程仍在依赖 CPU1→CPU0 SGI/IPI 的位置停止，范围缩到“反向中断链”。

注意：polling 是诊断旁路，不是正式修复。最终必须用未改 Linux 的 clean kernel 复测。

### 5.4 第三步：排除 SGI target 编码

针对“`gic_cpu_map[0]` 错”假说，临时使用 GICv2 `TargetListFilter=1`，让 CPU1 向除自己外的
CPU 发 SGI，绕过显式 CPU0 target map。日志证明 SGI 写入发生，但 CPU0 handler 仍未进入。

因此目标列表编码不是根因，应该检查接收端 CPU interface。

### 5.5 第四步：比较 CPU0/CPU1 GIC 状态

在两核 GIC 初始化前后打印：

- `GICD_CTLR`、`IGROUPR0`；
- SGI/PPI enable、pending、active；
- `GICC_CTLR`、`PMR`、`RPR`、`HPPIR`、`APR`；
- CPU0 CPSR，确认 IRQ 是否 unmasked。

关键观察是：

```text
CPU0: IRQ unmasked, PPI 30 pending, HPPIR=0x1e, RPR=0x00
CPU1: RPR=0xff
```

Linux 对两核可见的初始化基本相同，PMR 都被设置为 `0xf0`。但 CPU0 的 running priority 从
最早快照到后续等待点始终为零。pending timer 进不了 handler，不是因为 Linux 没 enable，而是
CPU interface 认为已有最高优先级中断正在运行。

### 5.6 第五步：对 OP-TEE 做 A/B

为了验证“开源 OP-TEE 3.7 自己造成遗留状态”，项目构造了严格 A/B：

- A 线：开源 RK322x OP-TEE 3.7；
- B 线：Rockchip RK322x TEE v2.00；
- U-Boot proper、DTB、kernel 和加载地址保持相同。

B 线仍出现 CPU0 `RPR=0x00`、CPU1 `RPR=0xff`。这不能证明两份 TEE 都绝对无 bug，但足以
排除“仅开源 OP-TEE 3.7 特有实现错误”作为当前最简解释。

### 5.7 第六步：把探针前移到 secure SPL

如果 Linux normal world 看不全 secure GIC 状态，就继续向更早阶段移动观测点。项目在 SPL
进入 OP-TEE 前增加只读 snapshot，并在 `cleanup_before_linux()` 前后分别打印 `GB`、`GA`：

```text
GB: RPR=00000000 APR0=00000001 ... ISACTIVER1=00800000
GA: RPR=00000000 APR0=00000001 ... ISACTIVER1=00800000
```

由此得到：

1. 异常在 OP-TEE 执行前已存在；
2. cache/MMU 交接清理没有改变它；
3. 唯一 active 位是 `ISACTIVER1 bit 23`；
4. `INTID = 32 + 23 = 55`；
5. DTS 将该 INTID 映射到 USB OTG。

这一步是整个定位中最重要的方法论：**当某层看到的状态不完整时，不要继续在同一层无限加
日志，而要跨过权限/生命周期边界，到状态最早存在且仍可见的阶段观察。**

### 5.8 第七步：严格签名的最小清理

最终补丁位于
[u-boot-phicomm-r1-optee-prejump-gic-int55-cleanup.patch](../../patches/u-boot-phicomm-r1-optee-prejump-gic-int55-cleanup.patch)。
它只有在以下条件全部成立时才写寄存器：

```text
GICC_RPR  == 0
GICC_APR0 == 1
GICC_APR1..3 == 0
所有 GICD_ISACTIVERn 中只有 INTID 55 为 1
```

匹配后执行：

1. 保存并暂时关闭 `GICC_CTLR`；
2. 在 `GICD_ICENABLER1` mask INTID 55；
3. 在 `GICD_ICPENDR1` 清 pending；
4. 在 `GICD_ICACTIVER1` 清 active；
5. 清 `GICC_APR0`；
6. `dsb`/`isb` 保证顺序；
7. 恢复原 `GICC_CTLR`；
8. 再打印 `GC` 快照。

为什么不直接初始化整个 GIC？因为 SPL 不知道后续 secure firmware 对所有中断的完整策略，
粗暴 reset 可能破坏其他启动来源、secure watchdog 或固件状态。严格签名让 workaround 只匹配
当前已验证异常；不匹配就什么都不做。

### 5.9 第八步：用 clean kernel 证明不是诊断代码“治好了”

诊断内核曾包含 UART 路标、polling completion 和 SGI filter。这些都会改变时序，不能作为
最终成功证据。项目从未修改的 Linux 5.10.262 commit 独立构建 clean v8：

- 同一 OP-TEE、DTB、initramfs 和命令行；
- 删除所有诊断旁路；
- 只保留 SPL 的精确 GIC cleanup。

结果：双核进入 shell并由用户确认 uptime 约 700 秒。随后 clean v9 只删除 `maxcpus=2`，四核
CPU0–CPU3 全部 online，uptime 到 72.92 秒，四核 reschedule/function-call IPI 均有增长。

这建立了完整因果链：

```text
secure SPL 中存在 INTID 55 active + APR0/RPR=0
  → CPU0 普通 PPI/SGI 无法被接受
  → timer/IPI/completion/调度逐步停止
  → 严格清理相同状态
  → clean Linux 双核、四核恢复并越过旧边界
```

## 6. 一套可复用的 GIC/SMP 调试流程

### 6.1 先问“最后确定执行到哪里”

按执行层级布置不会依赖复杂子系统的路标：

```text
BootROM/loader → SPL entry → SPL C → FIT load → OP-TEE entry
→ normal-world return → U-Boot proper → zImage entry
→ secondary_startup → secondary_start_kernel → CPU online → idle
```

早期路标尽量只做一件事：轮询 UART TX ready 并写一个字符。不要在栈、BSS、DM 或 console
尚未可靠时调用复杂 `printf()`。

### 6.2 将“发送”和“接收”分开

对于 IPI：

- 发送端是否执行 `GICD_SGIR` 写入？
- Distributor 是否把 SGI 标为 pending？
- target CPU 的 CPU interface 是否 enable？
- PMR/RPR 是否允许递送？
- CPU CPSR 的 IRQ 位是否 unmask？
- handler 是否读 IAR？
- handler 是否正确 EOIR/deactivate？

只证明发送函数返回，不能证明对端收到。

### 6.3 建议的只读快照

在写任何 workaround 之前，至少抓取：

```text
GICD_CTLR
GICD_TYPER
GICD_IGROUPR0...
GICD_ISENABLER0...
GICD_ISPENDR0...
GICD_ISACTIVER0...
GICC_CTLR
GICC_PMR
GICC_RPR
GICC_HPPIR
GICC_APR0...
CPSR / 当前 CPU / MPIDR
```

同一套快照要在多个边界比较：

1. Linux GIC 初始化前后；
2. PSCI 调用前后；
3. CPU0 与 CPU1；
4. OP-TEE 前后的 secure/normal world；
5. cache cleanup 前后；
6. 修复前后的严格 A/B。

### 6.4 从 INTID 反查设备树

若 `ISACTIVERn` 某一位为 1：

```text
INTID = 32 × register_index + bit_index
```

例如：

```text
ISACTIVER1 bit 23
= 32 × 1 + 23
= INTID 55
= GIC_SPI 23
```

再在 DTS/DTSI 搜索 `GIC_SPI 23`。不要只按设备树节点名猜；最终要核对 interrupt specifier、
controller binding 和实际寄存器位。

### 6.5 A/B 的设计原则

一个有用的 A/B 应只改变一个解释变量：

- OP-TEE A/B：只换 secure firmware；
- DT A/B：只换 PSCI binding 或 GIC compatible/reg；
- kernel A/B：同 commit，只加入一个探针或旁路；
- CPU 数 A/B：同 kernel，只限制 `maxcpus=2`；
- cleanup A/B：同链，只在严格签名匹配时清一个状态。

每个镜像都应有可辨识版本后缀、大小、SHA-256 和明确执行命令。否则“测到上一个内核/loader”
会让所有推理失效。

### 6.6 成功不能只看一行日志

建议分层验收：

| 层级 | 成功证据 |
|---|---|
| PSCI | 固件接受 `CPU_ON`，CPU1 到达入口 |
| Linux SMP | `CPU1:`、`SMP: Total ...`、online mask 正确 |
| GIC/IPI | `/proc/interrupts` 中各 CPU 的 IPI 计数增长 |
| timer/scheduler | `/proc/uptime` 持续增长、shell 可响应 |
| 用户目标 | 超过历史失败边界，例如 uptime > 30 s |
| 回归控制 | clean kernel、冷启动重复、不同核心数仍通过 |

## 7. 常见误区

### 7.1 “CPU1 online，所以 GIC 没问题”

错误。CPU1 可以依靠 PSCI 和共享内存走到 online，而 CPU1→CPU0 的 SGI、CPU0 timer PPI 或
某个 per-CPU interface 仍可能坏。

### 7.2 “DT 写了 PSCI 1.0，固件就一定支持”

错误。DT 描述的是 normal world 应该怎样调用固件，不会凭空实现 PSCI。必须由 OP-TEE、
TF-A 或其他 secure firmware 提供匹配服务。

### 7.3 “Linux 看到 active=0，所以没有 active 中断”

错误。要考虑 per-CPU bank、secure/non-secure view 和 Group 0/Group 1。R1 的异常必须在
secure SPL 中才能完整看到。

### 7.4 “HPPIR 显示 timer，所以 timer 驱动坏了”

不一定。HPPIR 能提示 pending 候选，但若 RPR 已被更高优先级 active 状态占住，CPU 仍不会
接受该 timer。要把 PMR、RPR、APR、CPSR 和 active state 一起看。

### 7.5 “换 U-Boot/OP-TEE 后好了，就是新组件修复了”

不够严谨。还可能是时序、地址、DT、cache 或早期硬件状态变化。必须设计保持其他载荷一致的
A/B，并把观测点前移到发生差异的最早边界。

### 7.6 “直接清所有 pending/active 最省事”

风险很高。它可能掩盖根因、破坏 secure firmware 契约，或让其他启动来源失败。优先只读采样，
再做完整签名匹配的最小写入，最后用 clean 软件栈验证。

## 8. 如何阅读相关源码

### 8.1 Linux 次核与 PSCI

建议用 `rg` 和 clangd 追这些入口：

```sh
rg -n 'secondary_startup|secondary_start_kernel' arch/arm
rg -n 'psci_cpu_on|cpu_on' drivers/firmware arch/arm
rg -n '__cpu_up|cpu_up' arch/arm kernel
```

阅读时画出两个调用方向：CPU0 发起 `CPU_ON`，CPU1 执行 secondary entry；不要把两条栈混成
一条。

### 8.2 Linux GICv2

```sh
rg -n 'gic_raise_softirq|GIC_DIST_SOFTINT' drivers/irqchip arch/arm
rg -n 'gic_cpu_init|gic_handle_irq' drivers/irqchip arch/arm
rg -n 'GICC_RPR|GICC_HPPIR|GICC_APR' drivers/irqchip
```

重点跟踪：初始化、SGI 发送、异常入口读 IAR、handler 结束写 EOIR/DIR，以及 CPU hotplug 的
per-CPU 初始化。

### 8.3 U-Boot SPL → OP-TEE

```sh
rg -n 'jump_to_image_optee|spl_optee_entry' arch common
rg -n 'cleanup_before_linux' arch/arm
rg -n 'CONFIG_SPL_BUILD|SPL_OPTEE_IMAGE' .
```

本项目最终 cleanup patch 挂在 `jump_to_image_optee()` 中，位置是
`cleanup_before_linux()` 之后、`spl_optee_entry()` 之前。这使它仍在 secure SPL 视图中，且
恰好位于问题状态交给 OP-TEE 之前。

### 8.4 用 clangd 看调用链

仓库已有：

```sh
scripts/generate-clangd.sh
scripts/clangd-call-tree.py --help
```

生成 `compile_commands.json` 后，可在编辑器中从 `secondary_start_kernel()`、
`gic_raise_softirq()`、`jump_to_image_optee()` 分别使用 go to definition、find references 和
call hierarchy。先建立阶段/CPU 边界，再进入函数细节，效率远高于从启动日志逐行猜。

## 9. 面试表达模板

### 9.1 一分钟项目叙述

> 我在 RK3229 设备上做了一条纯 RAM 的主线 U-Boot、开源 OP-TEE 和 Linux 启动链。最初
> Linux 在拉起第二核时挂死。通过汇编和 C 级串口路标，我证明 CPU1 已进入
> `secondary_start_kernel`、完成 online，并把问题收敛为 CPU1 到 CPU0 的中断接收链。
> SGI target-filter A/B 排除了目标映射，GIC 快照发现 CPU0 的 RPR 长期为最高优先级 0，而
> CPU1 正常为 0xff。换 Rockchip TEE 的 A/B 仍复现后，我把只读探针前移到 secure SPL，发现
> OP-TEE 运行前已经遗留唯一 active 的 INTID 55 和 APR0 bit。它映射到 USB OTG，和
> MaskROM/RockUSB 下载链一致。我实现了严格签名匹配的最小清理，并用未改的 Linux 5.10
> clean kernel 验证双核 uptime 约 700 秒、四核 online 且跨过原 30 秒边界。这个过程让我形成
> 了跨 BootROM、SPL、secure firmware 和 Linux 的分层定位方法。

### 9.2 STAR 结构

| 项目 | 内容 |
|---|---|
| Situation | 主线 U-Boot + OP-TEE 能进入 Linux，但一开次核系统冻结 |
| Task | 区分 PSCI、次核入口、cache coherency、GIC 和 DT，找到可复现根因 |
| Action | 路标追踪、双向 completion、SGI filter、GIC register snapshot、TEE A/B、secure SPL 前移探针、严格 cleanup |
| Result | 定位 OP-TEE 前 INTID55/APR0 遗留；clean 双核约 700 s，四核 online 并越过 30 s |
| Reflection | bootloader 交接不仅是镜像地址，也是权限和硬件状态；成功必须用 clean A/B 闭环 |

## 10. 常见八股问答

### Q1：SPL 和 U-Boot proper 有什么区别？

SPL 是受大小和早期环境限制的次级加载器，只初始化装载下一阶段必需的硬件；U-Boot proper 在
DRAM 中运行，提供完整驱动模型、命令、文件系统和启动策略。两者通常有不同配置和 DT 内容。

### Q2：TPL 又是什么？

TPL 是更小、更早的 Tiny Program Loader，常用于 DDR 初始化代码放不进 SPL 的场景。并非
所有平台都有 TPL。R1 当前 vendor 471 DDR loader 职责类似，但 471 是 Rockchip 链标签。

### Q3：为什么有 OP-TEE 以后还需要 U-Boot？

OP-TEE 负责 secure world、可信执行环境和本项目所需的 PSCI；U-Boot proper 负责 normal-world
启动策略、加载 kernel/DTB/initramfs 和交互恢复。两者职责不同。

### Q4：PSCI 是什么？

PSCI 是 ARM 系统软件与操作系统之间的电源管理接口。Linux 可通过 SMC/HVC 请求 CPU on/off、
system reset/suspend 等操作，具体硬件细节由更高特权固件实现。

### Q5：GIC Distributor 和 CPU interface 各做什么？

Distributor 管理全局中断的组、enable、priority、target、pending/active；每 CPU interface
负责本核 priority mask、running priority、acknowledge 和中断完成。

### Q6：SGI、PPI、SPI 的区别？

SGI 是软件生成中断，常用于 IPI；PPI 是每 CPU 私有中断，如 arch timer；SPI 是共享外设中断。
GICv2 中常见 INTID 范围分别是 0–15、16–31、32–1019。

### Q7：pending 和 active 有什么区别？

pending 表示中断已提出但尚未被 CPU acknowledge；读取 IAR 后进入 active。handler 完成必须
正确 priority drop/deactivate，否则 active priority 可长期阻挡其他中断。

### Q8：RPR、PMR、HPPIR 分别怎么看？

PMR 是接收优先级门限；RPR 是当前 running priority；HPPIR 提示最高优先级 pending INTID。
三者要与 CPSR、enable、pending、active、APR 一起分析，单看一个寄存器容易误判。

### Q9：为什么 CPU1 已 online，系统还会死？

CPU1 online 只证明 PSCI 和次核初始化的大部分路径成功。后续调度、completion、timer 和 RCU
依赖双向 IPI/PPI；R1 中 CPU0 的 GIC running priority 被遗留状态占住，所以反向链仍失败。

### Q10：为什么 Linux 看不到根因？

根因跨越 TrustZone 安全边界。normal world 可见的 GIC state 不一定包含 secure Group 0 或
banked CPU interface 的完整状态；secure SPL 在 OP-TEE 前观察才看到 INTID55/APR0。

### Q11：如何判断是 DT、U-Boot 还是固件问题？

不要按组件名猜。先证明最后执行位置，再做单变量 A/B：换 PSCI binding、换 TEE但保持其他
载荷相同、比较 CPU0/CPU1 寄存器、把探针前移。能够找到“状态最早已经错误”的阶段，才接近
责任边界。

### Q12：U-Boot 到底传什么给内核？

直接对象是 kernel、DTB、可选 initramfs 和 bootargs；隐含契约还包括 RAM布局、cache/MMU、
CPU状态、安全世界、次核和中断控制器状态。U-Boot 不把自身驱动传给 Linux。

### Q13：为什么不用初始化整个 GIC？

全量重置可能破坏 secure firmware 或其他启动路径的合法状态，也会掩盖根因。R1 采用完整异常
签名匹配，只处理唯一 INTID55 active/APR0 状态，不匹配时不写寄存器。

### Q14：怎样证明修复不是偶然改变时序？

删除诊断内核中的 polling、路标和 SGI filter，用同一源码 commit 的 clean kernel 复测；再从
双核到四核做单变量扩展，并检查 uptime 与每核 IPI 计数。

### Q15：这次最重要的方法论是什么？

把复杂启动链切成明确边界，用最小路标回答“执行到哪”，用寄存器回答“状态是什么”，用严格
A/B 回答“哪个变量造成差异”，最后用 clean 系统回答“修复是否真实”。

## 11. 可继续做的练习

1. 对照 Arm GICv2 规范，为每个 snapshot 字段写出 offset、访问权限和 banked 规则；
2. 从 `GICD_ISACTIVER1=0x00800000` 手算 INTID，再在 RK322x DTS 中反查设备；
3. 用 clangd 分别画出 CPU0 `__cpu_up()` 和 CPU1 `secondary_start_kernel()` 调用链；
4. 解释为什么 polling completion 只能作为诊断，不能合入正式内核；
5. 设计旧厂商链 secure GIC 对照，验证究竟哪个阶段应负责清理 INTID55；
6. 将当前通用 `arch/arm/lib/spl.c` workaround 收敛到 R1/RockUSB 板级 hook，避免影响其他板；
7. 做多次冷启动和四核长期测试，记录每次 `GB/GA/GC`、online mask、IPI 和 uptime。

## 12. 参考资料与本地证据

### 12.1 官方资料

- Arm, *ARM Generic Interrupt Controller Architecture Specification, GIC architecture version 2.0,
  ARM IHI 0048B.b*，GICv2 中断类型、状态机、优先级、CPU interface 与 security extensions，
  访问于 2026-08-10：
  <https://developer.arm.com/documentation/ihi0048/bb>
- U-Boot maintainers, *Generic SPL framework*，TPL/VPL/SPL 和 U-Boot proper 的构建与装载框架，
  访问于 2026-08-10：
  <https://docs.u-boot.org/en/v2023.10/develop/spl.html>
- U-Boot maintainers, *Board Initialisation Flow*，`board_init_f()`、relocation 和
  `board_init_r()` 阶段，访问于 2026-08-10：
  <https://docs.u-boot.org/en/stable/develop/init.html>
- U-Boot maintainers, *Devicetree Control in U-Boot*，control FDT 与阶段相关 DT，访问于
  2026-08-10：
  <https://docs.u-boot.org/en/v2024.04/develop/devicetree/control.html>
- U-Boot maintainers, *Flattened Image Tree FIT image format*，FIT 中 kernel、firmware、FDT、
  loadables 和 configuration 的组织方式，访问于 2026-08-10：
  <https://docs.u-boot.org/en/latest/usage/fit/howto.html>
- OP-TEE maintainers, *Core architecture*，secure/normal world、secure monitor 与 OP-TEE
  core 结构，访问于 2026-08-10：
  <https://optee.readthedocs.io/en/latest/architecture/core.html>
- OP-TEE maintainers, *OP-TEE OS source repository*，访问于 2026-08-10：
  <https://github.com/OP-TEE/optee_os>
- Linux stable maintainers，`linux-5.10.y` commit
  `065a677fad98698de04279ba2cb152a472ab8b1f`，`arch/arm/boot/dts/rk322x.dtsi`
  第 711–715 行，USB OTG 使用 `GIC_SPI 23`，访问于 2026-08-10：
  <https://github.com/gregkh/linux/blob/065a677fad98698de04279ba2cb152a472ab8b1f/arch/arm/boot/dts/rk322x.dtsi#L711-L715>

### 12.2 R1 本地证据

- [主线 Linux Bring-up](../mainline-bringup.md)：当前阶段结论和 clean v8/v9 验证；
- [逆向学习记录](../reverse-engineering-journal.md)：每次 A/B、命令、哈希和串口观察；
- [secure SPL GIC 只读探针](../../patches/u-boot-phicomm-r1-optee-prejump-gic-snapshot.patch)：
  `GB/GA` 快照实现；
- [INTID55 精确清理补丁](../../patches/u-boot-phicomm-r1-optee-prejump-gic-int55-cleanup.patch)：
  完整签名和最小写入；
- [R1 GIC400 DT A/B](../../kernel/dts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dts)：
  GIC 地址与 compatible；
- [`clean-v8-open-optee-first-shell-20260810.log`](../../build/artifacts/clean-v8-open-optee-first-shell-20260810.log)：
  clean 双核首次进入 shell 的保存日志；
- [`zImage-5.10-psci-v1-gic400-clean-4core-v9`](../../build/artifacts/zImage-5.10-psci-v1-gic400-clean-4core-v9)：
  四核 clean 候选，实机 online/IPI/uptime 结果记录于 journal。
