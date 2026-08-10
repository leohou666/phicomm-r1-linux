# 主线 Linux Bring-up

本文档记录斐讯 R1 的主线内核选择、最小板级配置、构建方式和首次启动边界。按时间展开的实际操作、失败和经验仍记录在[逆向学习记录](reverse-engineering-journal.md)中。

## 当前结论

目标内核固定为 Linux `6.18.42` LTS。源码来自 kernel.org stable 仓库的 `v6.18.42` 标签，项目使用独立浅仓库 `build/kernel-src`，不会切换或修改用户已有的 Linux 源码工作树。

截至 2026-08-05，以下内容已验证：

- `v6.18.42` 源码标签可精确检出；
- `arm-none-eabi-` 工具链能够完成 ARM zImage 构建；
- 项目最小配置片段能够合并到 `multi_v7_defconfig`；
- R1 外置 DTS 能够引用上游 `rk3229.dtsi` 并编译为 DTB；
- DTB 反编译后仍包含预期的 512 MiB RAM、UART2、RK805、eMMC 和 USB peripheral 节点；
- 最小救援 initramfs 能够确定性生成，内容为静态 32 位 ARM BusyBox、`/init` 和必要目录。
- 可供厂商 `bootrk` 解析的 Android boot image 已确定性生成、完成离线拆包并通过实机扩展 SHA 校验。
- 原厂 U-Boot Fastboot 已实机进入并完成只读查询：`secure: yes`、`unlocked: no`，因此 Fastboot RAM download 当前不可用。
- misc BCB 已能强制直接启动 recovery；修正 DEBUG_LL UART 虚拟映射后，主线内核已完整启动到 BusyBox 救援 shell。
- U-Boot 仍存活时的受控回滚已实测：PCB 按键进入 Loader，恢复原始 misc BCB 并读回比较后，Android 正常启动。这不代表真 MaskROM 裸机整盘恢复已验证。
- 同代 `cmd_bootrk.c` 与实机日志一致：`bootrk` 把 kernel 地址改写为编译期 `CONFIG_KERNEL_RUNNING_ADDR`/`CONFIG_KERNEL_LOAD_ADDR`，不读 `kernel_addr_r`；env 可选启动分区，不能修改这个装载地址。

主线首次启动已完成。四个 Cortex-A7 CPU、512 MiB RAM、UART2 控制台和 eMMC HS200 已在实机日志中验证；PMIC 电压表、Rockchip parameter 分区解析和 USB 仍需继续处理。

## 最小硬件范围

首轮 DTS 只描述获得串口 shell 和只读发现 eMMC 所需的节点：

| 功能 | 当前配置 | 证据等级 |
|---|---|---|
| SoC | RK3229 | 原厂 DTB、启动日志已验证 |
| RAM | `0x60000000` 起始，512 MiB | 原厂启动日志已验证 |
| 控制台 | UART2，`0x11030000`，1500000 8N1，`uart21_xfer` | 原厂 DTB、串口实测已验证 |
| eMMC | `0x30020000`，8-bit，HS200 1.8 V | 主线已枚举 User Area、boot0、boot1 和 RPMB；尚未解析厂商 parameter 分区 |
| PMIC | I2C0 `0x18`，RK805 | 原厂启动日志已验证 |
| PMIC IRQ | GPIO1_B4，低电平 | 由原厂 DTB 数字 GPIO 解码，待主线实机验证 |
| USB | OTG controller，peripheral 模式 | 保守配置；VBUS/ID 和实机枚举待验证 |

Wi-Fi、Bluetooth、音频、功放和不确定的 USB host/VBUS GPIO 暂不加入首轮启动范围。

## 构建

准备精确固定的源码：

```sh
scripts/prepare-kernel-source.sh
```

验证源码版本：

```sh
git -C build/kernel-src describe --exact-match --tags HEAD
git -C build/kernel-src status --short --branch
```

预期分别看到 `v6.18.42` 和 detached HEAD。

构建内核与 DTB：

```sh
scripts/build-kernel.sh
```

构建救援 initramfs：

```sh
scripts/build-initramfs.sh
```

如果不使用本地 recovery 中提取的 BusyBox，可指定另一个静态 32 位 ARM 版本：

```sh
BUSYBOX=/path/to/static-arm-busybox \
  scripts/build-initramfs.sh
```

输出位于：

```text
build/artifacts/zImage
build/artifacts/rk3229-phicomm-r1.dtb
build/artifacts/kernel.config
build/artifacts/r1-initramfs.cpio.gz
build/artifacts/r1-resource.img
build/artifacts/r1-mainline-boot.img
build/artifacts/r1-mainline-recovery.img
```

`build/`、原厂 BusyBox 和其他从设备提取的文件都是本地工作产物，不应提交到公开仓库。

## initramfs 行为

`initramfs/init` 只执行以下动作：

1. 将标准输入输出绑定到 `/dev/console`；
2. 挂载 proc、sysfs、devtmpfs 和 devpts；
3. 输出内核版本、命令行、内存信息和块设备列表；
4. 提示没有挂载或修改任何存储；
5. 循环启动 BusyBox shell。

它没有自动 mount eMMC、写分区、修改 U-Boot 环境或运行刷写工具。

## Rockchip boot image

原厂 U-Boot 是 `2014.10-RK322X-06`。对其镜像进行字符串检查后确认：

- 默认 `bootcmd=bootrk`；
- `bootrk` 解析 Android boot image 的 kernel、ramdisk 和 second-stage Rockchip resource image；
- 没有找到 `bootz` 命令；
- 内置 Fastboot 的 `download` 和 `boot` 处理路径；
- Fastboot 下载是否允许由 `fastboot_unlocked` 状态控制；
- Secure Boot 启动日志显示 `SecureBootEn = 0`，但这不能代替 Fastboot lock 状态检查。

生成 RAM 启动候选镜像：

```sh
scripts/build-boot-image.sh
```

脚本沿用原厂 Android boot header 的 base `0x60000000`、kernel offset `0x00408000`、tags offset `0x00088000` 和 page size 16384。原厂 ramdisk offset `0x02000000` 与主线链接内核范围冲突，原厂 second offset `0x00f00000` 也与 12 MiB zImage 冲突，因此分别调整为 `0x04000000` 和 `0x06000000`。主线 DTB 被封装为 resource image 中的 `rk-kernel.dtb`。

离线验证包括：注入 Rockchip 扩展哈希前，标准 `unpack_bootimg` 能读出 kernel、ramdisk 和 second；Rockchip `resource_tool --unpack` 能提取 `rk-kernel.dtb`；提取结果与输入逐字节一致。最终镜像再由 `scripts/add-rockchip-boot-hashes.py` 验证厂商 SHA-1/SHA-256 字段；连续构建结果逐字节一致。标准 `unpack_bootimg` 会把 Rockchip 放在 `extra_cmdline` 区域的二进制摘要误当 UTF-8 文本，不能用于最终镜像验证。

当前候选 boot image 为 12,697,600 字节，比原厂 12,582,912 字节的 boot 分区大 114,688 字节。因此它绝对不能写入 boot 分区。Fastboot RAM download 是否允许这个大小，要先读取 lock 状态与 `max-download-size`。

构建脚本还将 boot image 放在精确 33,554,432-byte 容器开头并清零尾部，生成 `r1-mainline-recovery.img`。它严格等于 recovery 分区的 `0x10000` sectors，地址区间与重复构建检查已通过。第一版候选曾成功写入并立即读回，但随后被 Android 自动恢复；第二次直接重启 recovery 时被 U-Boot 的 Rockchip 扩展 SHA 校验拒绝；修正摘要后的第三次启动已通过校验并到达 zImage 入口。

### Rockchip boot image 扩展哈希

第二次实机测试中，U-Boot 在跳转内核前报告 `boot or recovery image sha mismatch!`，随后尝试无效的 backup 分区并退入 `rockusb`。这证明标准 AOSP `mkbootimg` 生成的 image ID 不足以通过 R1 厂商 U-Boot。

同代 Rockchip U-Boot 的 `SecureNSModeBootImageShaCheck()` 与原厂 boot/recovery 头部共同确认了算法。除 kernel、ramdisk、second 及各自的 32-bit little-endian size 外，厂商摘要还覆盖 `tags_addr`、`page_size`、两个 `unused` 字段、16-byte board name 和 512-byte cmdline。SHA-1 写入标准 `id` 字段；扩展区在 `0x26c` 写入 flag `256`，并从 `0x270` 写入同一输入的 SHA-256。

`scripts/add-rockchip-boot-hashes.py --verify` 已同时精确复现原厂 boot 和 recovery 的两组摘要。算法对照源码为 FriendlyARM `uboot-rockchip` 的 `nanopi4-v2014.10_oreo` 分支、commit `62645e6fefc9294f60befbb2e8032a35f67b1145` 中 `board/rockchip/common/SecureBoot/SecureVerify.c`。构建脚本现自动注入并复核这些字段。实机不再报告 SHA mismatch，证明修正有效。

### 首次进入主线 zImage

向 misc 分区内 `bootloader_message` 写入 `boot-recovery` 后，U-Boot 报告 `got recovery cmd from misc.`，随后成功解析修正候选并输出：

```text
kernel   @ 0x62000000 (0x00b73200)
ramdisk  @ 0x65bf0000 (0x00099018)
Loading Device Tree to 65600000 ... OK
Starting kernel ...
```

首次日志之后没有任何输出。补入 decompressor UART debug 后，实机新增输出：

```text
C:0x620010A0-0x62B73200->0x6207E800-0x62BF0960
Uncompressing Linux... done, booting the kernel.
```

这证明 zImage 已检测到重叠、把自身搬到 `0x6207e800`，并成功把内核解压后跳转。BCB、recovery 读取、Rockchip SHA、resource DTB、zImage 自搬移和解压路径均已验证；当前故障范围缩小到解压后 ARM 内核入口到串口控制台注册之间。

该日志还推翻了“Android header 地址决定实机装载位置”的旧假设。厂商 `bootrk` 实际覆盖了 header 地址：压缩 zImage 输入位于 `0x62000000–0x62b73200`；链接内核按 `_text=0xc0208000`、`_end=0xc20dfc10` 换算，预计物理输出位于 `0x60208000–0x620dfc10`。输入与输出尾部约重叠 `0xdfc10` bytes。decompressor 实机输出已证明自搬移避让成功，该重叠不是当前直接根因。

原配置虽有正常内核 `earlycon`，却没有 decompressor debug，因而连 `Uncompressing Linux...` 也不会显示。项目现为这台固定板启用 `DEBUG_LL_UART_8250` 和 `DEBUG_UNCOMPRESS`，UART 物理地址为 `0x11030000`、register shift 为 2、使用 32-bit access。上游默认禁止 multiplatform 使用固定 decompressor UART；`patches/linux-6.18.42/0001-arm-allow-board-specific-decompressor-debug.patch` 仅为 R1 bring-up 放开这一限制，`scripts/prepare-kernel-source.sh` 会可复现地应用它。

实机解压完成后发现配置虽有 `DEBUG_LL` 和正常 `earlycon`，但 `CONFIG_EARLY_PRINTK` 实际为关闭。补入 `CONFIG_EARLY_PRINTK=y` 和 `earlyprintk` 后，实机已输出 CPU `0xf00`、Linux 版本、`Phicomm R1` model 和 `legacy bootconsole [earlycon0] enabled`，证明已进入 `start_kernel()` 并解析 DTB。日志随后静默。新候选移除显式 `earlycon=uart8250,...`，只保留已实证工作的 DEBUG_LL `earlyprintk`；32 MiB 镜像连续两次打包逐字节一致，新 SHA-256 为：

```text
8b74dca655323921edc8a05c35251867025626c3611f070ed4e4def3199a5762
```

该候选实机继续输出到 CMA 初始化，随后明确报告 `BUG: not creating mapping for 0x11030000 at 0x11030000 in user region`。复核发现 `CONFIG_DEBUG_UART_VIRT` 错误地等于物理 UART 地址 `0x11030000`；这是用户虚拟地址区，ARM early debug 映射代码拒绝使用。已验证错误配置和映射拒绝，尚待上板验证的推断是：后续 DEBUG_LL 访问该未映射地址造成静默。

第一版映射修复把 DEBUG_LL 虚拟地址设为 `0xfed00000`，上板后只剩 decompressor 输出。ARM `head.S` 按 1 MiB section 建立初始调试 UART 映射，故虚拟地址必须保留物理 `0x11030000` 的低 20-bit 偏移 `0x30000`；正确地址为 `0xfed30000`。下一候选使用该地址，并继续使用 `earlyprintk console=ttyS2,1500000n8`。33,554,432-byte recovery 连续两次构建一致，文件 SHA-256 为：

```text
046b7a060d646e7d1ef79abb98606714f63820930c927fa42d9d97331803de05
```

Rockchip SHA-1/SHA-256 字段也已离线复核。

### 首次进入主线救援 shell

`CONFIG_DEBUG_UART_VIRT=0xfed30000` 候选已在 R1 实机启动成功。完整日志保存在本地 `build/artifacts/mainline-first-shell-20260805.log`，SHA-256 为 `4b12c13810a35bcef4b98141ee13cea1eccebf6cce961980ec834e48477cd095`。关键验证结果如下：

- 四个 Cortex-A7 CPU 均由 PSCI 拉起，内核报告 `SMP: Total of 4 processors activated`；
- 512 MiB DRAM 被识别，扣除保留区和 64 MiB CMA 后可用内存约 409 MiB；
- 8250 驱动接管 UART2，`ttyS2` 保持 1500000 baud，early console 正常退出；
- DesignWare MMC 以 HS200 模式识别 Samsung `8GME4R`，User Area 为 7.28 GiB；
- `mmcblk0boot0` 和 `mmcblk0boot1` 各为 4 MiB，RPMB 为 512 KiB；
- 内核执行 `/init`，最终在 `/dev/console` 上进入交互式 BusyBox shell；
- initramfs 没有自动挂载或修改任何 eMMC 文件系统。

当前 `/proc/partitions` 只列出整块 `mmcblk0` 和两个 hardware boot 区，没有原厂 Android 分区。这与原厂使用 Rockchip parameter 分区表一致，不能把“eMMC 成功枚举”误写为“分区已可用”。此外，RK805 regulator 当前拒绝 408 MHz 和 600 MHz 的 CPU OPP，initramfs 也缺少 `echo` applet；两项均未阻止首次进入 shell，后续分别修正。

后台 uptime 心跳证明系统越过 11.36 秒 deferred-probe 信息，实际在约 30 秒才停止，因此该 timeout 不是直接根因。原厂 init 配置明确描述 30 秒 watchdog，而主线板级 DTS 此前没有启用 SoC `0x110a0000` watchdog。下一诊断镜像恢复原命令行并只启用 `snps,dw-wdt` 节点，让已配置的 boot-enabled watchdog handler 接管可能由 bootloader 遗留的运行中计数器；recovery SHA-256 为 `3ae236c9d3ec6b12e16884eb12a569de4189852c3d3872320588f360532bfd95`。

该候选仍在约 30 秒停止，且主机 CH341 在现场保持枚举。启用节点本身没有解决问题；在否定 watchdog 前仍需确认驱动 probe 和 `0x110a0000` 硬件寄存器的实际状态。下一步只读连续采样 CR、TORR、CCVR 和 STAT，判别计数器是否运行、是否被内核 reload，以及停止前是否进入 interrupt stage。

寄存器实测 `CR=0x8`，enable bit 为 0；`CCVR=0xffff` 恒定且 `STAT=0`，watchdog 因此被排除。下一单变量候选撤销 WDT 节点，只加入 `maxcpus=1`，用于判别异常 PSCI `v65535.65535`、secondary CPU、idle 或 RCU 路径。recovery SHA-256 为 `ece57f96a42f5796e9b6d7f9729a2726a3e0fac0a1fd54af68bfd979cbeebdaf`。

该单核候选已稳定超过 135 秒，四核候选则在约 30 秒稳定停止，确认故障依赖 SMP/secondary CPU 路径。当前只把下一候选改为 `maxcpus=2`，继续区分“任意次核即触发”与“特定 CPU 数量/核心触发”；具体根因仍保持为开放问题。

双核 recovery 已完成两次一致构建，SHA-256 为 `173fd98bd4457dd26bb253b38ff4f2342a394c8ba4006e42bd705a5e99fd59f7`，尚未上板。

双核实机仍在 30 秒前停止，证明任意 secondary CPU 在线即可触发。下一候选保持双核，只加入通用 `nohlt` 强制 idle polling，以区分次核 WFI/idle 与更早的 PSCI/SMP/RCU 路径。

`maxcpus=2 nohlt` recovery 已完成两次一致构建，SHA-256 为 `5e6031888cfdfb82caa2bc941c98ebdce53d1005e7261ecc3bfeac1e9fe959cc`，尚未上板。

该候选仍在约 30 秒停止，排除次核 WFI/idle。下一步不重新刷写，而是在启动后把 RAM 中的 `rcupdate.rcu_cpu_stall_timeout` 从 21 临时改为 5 秒，尝试在停止前获得 RCU stall 的 CPU/GP/stack 证据。

运行时参数已确认是 5，但停止前没有任何 RCU stall 输出，因此默认 stall 检测窗口假设被排除。下一步在双核 `nohlt` 候选上只读采样 per-CPU interrupt/softirq 计数，确认 CPU1 的 timer、IPI 和 RCU 活性。

CPU1 的 arch timer、IPI、TIMER/SCHED/RCU 计数均持续推进到停止前，排除次核提前失联。原厂 DT 使用带显式 function ID 的 PSCI 0.1 binding，而上游 DTS 错把这版旧 Trust OS 描述成 PSCI 1.0/0.2，直接解释了异常的 `v65535.65535`。下一双核候选只把 PSCI 节点覆盖回原厂 0.1 接口。

PSCI 0.1 双核候选已完成两次一致构建。recovery 大小为 33,554,432 bytes，文件 SHA-256 为 `608faafbfc2bd5faad646d30ece72ff7d0987227935b97f65a41a6139f65839b`，Rockchip SHA-1 为 `68651ac1abb67eb8d7e4c9a9cfad4d646e2af47b`，Rockchip SHA-256 为 `c40a8fe440c80320b931fb0f8437f93f2d60ec82e7adb6c47d3501e021ea7619`。该候选尚未上板；验证标准是双核心跳稳定超过 60 秒，并检查启动日志不再出现异常的 `PSCIv65535.65535`。

该候选实机仍在 uptime `28.71` 秒后停止，因此 PSCI binding 不是约 30 秒冻结的根因。启动日志同时显示 RK805 的 `vdd_arm` 约束最低为 1.0 V，OPP core 因此删除需要 0.95/0.975 V 的 408/600 MHz OPP，剩余最低 OPP 为 816 MHz。U-Boot 启动时明确报告 ARM PLL 为 600 MHz；下一单变量候选保留 PSCI 0.1 和双核，只加入 `cpufreq.off=1`，用于判断 cpufreq 注册后的升频、OPP 电压或双核电流负载是否导致冻结。

禁用 cpufreq 的双核候选已完成两次一致构建。recovery 大小为 33,554,432 bytes，文件 SHA-256 为 `5748bf2ff17fe75fd48b30ad21b0bddad1ed7716a7f2c14da8fad1ad1e49a0ab`，Rockchip SHA-1 为 `767fba0292a157ac4b9d477c84c828cf0f834f57`，Rockchip SHA-256 为 `6c8d9d2236577b2b4d014274e28f8e796b8ba610a73d174373fdfeebe7f1f0c4`。该候选尚未上板。

实机确认 `/proc/cmdline` 包含 `cpufreq.off=1`，且 `/sys/devices/system/cpu/cpufreq` 不存在，但心跳仍只到 uptime `27.68` 秒；cpufreq/OPP 转换不是冻结根因。另一个潜在问题也经首次日志排除：尽管静态 DTB 的 memory node 覆盖完整 512 MiB，U-Boot 在交给内核前已把 early memory ranges 修成 `0x60000000–0x683fffff` 与 `0x68500000–0x7fffffff`，Trust OS 的 `0x68400000–0x684fffff` 未进入普通页分配器。下一步复用当前镜像，通过 CPU hotplug 立即 offline CPU1，稳定后再 online，以定位冻结计时起点。

当前镜像启动后立即 offline CPU1，系统已稳定超过 60 秒，远越过双核约 29 秒边界。这证明 secondary CPU 启动本身没有留下会在固定 boot uptime 触发的损坏；冻结要求 CPU1 持续在线。救援 shell 没有 controlling TTY，因此前台无限循环不能由串口 `Ctrl-C` 中断。下一次启动改用后台 `sleep` 在单核稳定运行 70 秒后自动 online CPU1，同时前台持续记录 uptime，以测量冻结相对 CPU1 online 的时间。

后台任务在 uptime `74.33` 秒开始 online CPU1，`74.63` 秒确认 online mask 为 `0-1`；心跳继续到 `98.73` 秒后停止。因此 `CPU_ON` 调用本身成功，冻结在次核重新上线约 24–26 秒后发生，计时不是从整机上电固定开始。当前内核启用 `CONFIG_NO_HZ_IDLE`，SMP timer migration 默认为 1；soft/hard lockup detector 与 workqueue watchdog 均未编译。下一 RAM-only A/B 在双核启动后立即把 `/proc/sys/kernel/timer_migration` 写为 0。

实机确认 `timer_migration=0` 且 online mask 为 `0-1`，双核心跳仍只到 uptime `27.37` 秒，SMP timer migration 被排除。下一候选保留当前 PSCI 0.1、双核和禁用 cpufreq 的基线，只加入 `nohz=off` 关闭 dynamic tick；若仍复现，则由后台定时任务在停止前多次向 `/proc/sysrq-trigger` 写 `l`，采集两颗 CPU 的实时 backtrace。

`nohz=off` 双核候选已完成两次一致构建。recovery 大小为 33,554,432 bytes，文件 SHA-256 为 `e377631aaff17d2f2c37dcddea464d830ba9223d9bb26b75059f4e671ba93be4`，Rockchip SHA-1 为 `65293581e60d3a6f34e04b5e420b019f666911b4`，Rockchip SHA-256 为 `d02d7b79f52c3cc184c77dbdae4f5ed84e3e3a5bd318e17fe6105e8dcc073259`。该候选尚未上板。

实机 `/proc/cmdline` 确认 `nohz=off` 生效，但心跳仍只到 uptime `27.94` 秒。uptime `21.38` 和 `27.41` 的两次 SysRq 全 CPU backtrace 都成功，CPU1 均位于 `default_idle_call`，且能响应由 CPU0 发出的跨核 backtrace 请求；次核直到停止前不到两秒仍未锁死。NO_HZ 被排除。

随后源码核对纠正了此前的频率结论：`cpufreq.off=1` 只禁用 cpufreq 子系统，上游 `rk322x.dtsi` 的 CRU `assigned-clock-rates` 仍在时钟框架初始化时独立请求 ARMCLK 816 MHz。当前测试因此只排除了动态调频，尚未验证 U-Boot 的 600 MHz。下一候选保持其余诊断参数不变，只在板级 DTS 把第二个 assigned rate（ARMCLK）从 816 MHz 覆盖为驱动明确支持的 600 MHz。

编译后 DTB 反编译确认 ARMCLK assigned rate 为 `0x23c34600`，即 600,000,000 Hz。该候选 recovery 大小为 33,554,432 bytes，连续两次构建一致；文件 SHA-256 为 `d0f18fd80c02027b3f199a817f69ca7ad9a19f7fa3e369f7067cec602b2f00f9`，Rockchip SHA-1 为 `e2a02390d780b2e6a320bbdcd1ae50b6ed543a76`，Rockchip SHA-256 为 `047aa2bc159afe5627fbb24fd61254cd91fae2102c469ed1f066efaf97cee316`。该候选尚未上板。

600 MHz 双核候选实机心跳仍只到 uptime `28.95` 秒，冻结边界没有变化，ARMCLK 816 MHz 被排除。原厂 3.10 DT 不能直接用于主线，因为其中大量私有 Rockchip binding 不受 6.18 驱动支持；因此新增 `kernel/dts/rk3229-phicomm-r1-minimal.dts`，只移植实机原厂 DT 已验证的 CPU、PSCI 0.1、两路 arch timer、两区 GIC、UART2 与 RAM 描述。它不 include 上游 `rk322x.dtsi`，也不描述 CRU、RK805、eMMC、USB、IOMMU 或 power domain。构建脚本可通过 `BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal.dts` 显式选择，不替换完整板级 DTS。

最小 DTB 反编译核对后为 1,599 bytes。对应 recovery 连续两次构建一致，大小 33,554,432 bytes，文件 SHA-256 为 `e70a392e6e3cc30853d23aa5dbcc2c3ee52b86b873a5d7808c9fc39e0d6a87c8`，Rockchip SHA-1 为 `8dbca049e9fc6247899b3bbdbd7b021a6cc485a0`，Rockchip SHA-256 为 `00be6be1206db60cebed32109fa331443abff5a59c5560cccb5fc0f6a8fb9879`。该候选尚未上板；因 DT 不描述 eMMC，内核不会枚举或访问存储。

最小原厂同构 DT 实机仍在约 30 秒内停止，因此完整上游 `rk322x.dtsi` 的 CRU、PMIC、USB、eMMC、IOMMU 与 power-domain 描述不是根因。剩余核心差异之一是 6.18 默认启用 `CONFIG_ARM_ARCH_TIMER_EVTSTREAM`，它会在每颗 CPU online 时修改 `CNTKCTL`，原厂 3.10 配置没有对应行为。下一最小-DT 候选只加入早期参数 `clocksource.arm_arch_timer.evtstrm=0`；若仍失败，停止在 6.18 内逐项猜测，转向较老主线 LTS 做版本二分。

关闭 event stream 的最小-DT recovery 已连续两次一致构建，大小为 33,554,432 bytes；文件 SHA-256 为 `fb410dca52963a69ebf5a2de262306a9da7deb9cf5372cbb50facb2caf4146dc`，Rockchip SHA-1 为 `de69b99516e8c4bb753ff52042b296487becfb31`，Rockchip SHA-256 为 `4c62b4ef3be0eeeb510016e1c9a7f6b4317fbc90b8bc221833916d66db320b29`。该候选尚未上板。

为捕获无 printk 的全局硬冻结，下一诊断镜像启用 `CONFIG_FUNCTION_TRACER` 与 `CONFIG_PSTORE_FTRACE`，并在最小 DT 中把 `0x7bf00000–0x7bffffff` 作为 1 MiB ramoops reserved-memory。该区域位于已验证 RAM 中、低于从 `0x7c000000` 开始的 CMA，且避开 zImage、ramdisk、resource 与 `0x68400000–0x684fffff` Trust OS。512 KiB ftrace 区按 CPU 分区，另保留 128 KiB console 与 128 KiB kmsg record；`flags=1` 使用 per-CPU ftrace buffer，避免跨核写锁。用户态显式启用记录，冻结后由硬件 watchdog 暖复位，再从 pstore 读取；断电会丢失该证据。

最终 DTB 反编译确认 ramoops 节点和各区大小均已进入产物，最终内核配置也确认 `CONFIG_FUNCTION_TRACER=y`、`CONFIG_PSTORE_FTRACE=y`、`CONFIG_PSTORE_RAM=y` 与 `CONFIG_PSTORE_CONSOLE=y`。33,554,432-byte recovery 连续两次打包逐字节一致，文件 SHA-256 为 `bd4fe88dd09a55e11779172c9f9e8ec3a3c51d3b1caac25e01f758847a34c849`，Rockchip SHA-1 为 `31e60e779ee3fbfc504f2966a9816f9ec90500e4`，Rockchip SHA-256 为 `17233851b4ddc66302614515bf90316046831d8042085e2040f88df3029914f4`。刷写脚本允许哈希已同步；该候选尚未上板。

上板时先 offline CPU1，避免在诊断设施准备好前触发冻结。确认 ramoops 和 debugfs 后开启 persistent ftrace，再直接配置已实测可读的 DesignWare WDT 寄存器。`TORR=0xff` 同时把 TOP/TOPINIT 设为最大值，`CR=0x0b` 保留原有 reset pulse length、启用两阶段 interrupt-then-reset；按该 IP 的行为，第二阶段到期执行暖复位。WDT 一旦启用不能由普通寄存器写停止，因此只有在确认 trace 节点存在后才执行：

```sh
/bin/busybox printf '0\n' > /sys/devices/system/cpu/cpu1/online
/bin/busybox mkdir -p /sys/kernel/debug /sys/fs/pstore
/bin/busybox mount -t debugfs debugfs /sys/kernel/debug
/bin/busybox mount -t pstore pstore /sys/fs/pstore
/bin/busybox dmesg | /bin/busybox grep -i -e ramoops -e pstore
/bin/busybox cat /proc/iomem | /bin/busybox grep -i 7bf
/bin/busybox ls -l /sys/kernel/debug/pstore/record_ftrace
/bin/busybox printf '1\n' > /sys/kernel/debug/pstore/record_ftrace
/bin/busybox cat /sys/kernel/debug/pstore/record_ftrace

/bin/busybox devmem 0x110a0004 32 0x000000ff
/bin/busybox devmem 0x110a000c 32 0x00000076
/bin/busybox devmem 0x110a0000 32 0x0000000b
/bin/busybox devmem 0x110a000c 32 0x00000076
/bin/busybox devmem 0x110a0000 32
/bin/busybox devmem 0x110a0008 32
/bin/busybox printf '1\n' > /sys/devices/system/cpu/cpu1/online
```

暖复位后的第二次启动不要再次开启 ftrace；先挂载 pstore 并读取末尾。串口软件应从第一次启动前就把全部会话记录到主机文件：

```sh
/bin/busybox mkdir -p /sys/fs/pstore
/bin/busybox mount -t pstore pstore /sys/fs/pstore
/bin/busybox ls -la /sys/fs/pstore
/bin/busybox tail -n 500 /sys/fs/pstore/ftrace-ramoops*
```

若没有 `ftrace-ramoops*`，依次检查第一次启动是否出现 ramoops probe、WDT 是否真的产生暖复位，以及旧 U-Boot/DDR 初始化是否清除了该段 DRAM。persistent ftrace 给出的是各 CPU 最后的函数调用链，而不是冻结后凭空取得的寄存器现场；若全函数记录的额外开销改变了复现时间，则再缩小过滤范围或改用硬件 JTAG。

### Linux 5.10.262 版本对照候选

交互式 tracefs 试验使用了 shell 条件命令 `[ -e ... ]`，但当前极简 initramfs 没有 `[` applet，因此四个事件检查全部失败；随后 `cat trace_pipe` 只是正常阻塞等待从未启用的事件。该次输出不能说明内核在 tracefs 中冻结，也没有产生有效 trace。

为把“6.18 行为变化”与“旧 Trust OS/硬件固有问题”分开，新增独立的 Linux `v5.10.262` 源码树 `build/kernel-src-5.10` 和输出树 `build/kernel-5.10`，保留 6.18 源码与输出。构建仍使用同一个 1,849-byte 最小 DT、`maxcpus=2`、`cpufreq.off=1`、`nohz=off` 和 `clocksource.arm_arch_timer.evtstrm=0`，因此主要变量是内核版本。命令为：

```sh
KERNEL_SRC=build/kernel-src-5.10 \
KERNEL_BUILD=build/kernel-5.10 \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal.dts \
rtk scripts/build-kernel.sh
```

构建脚本现把相对 `KERNEL_SRC`、`KERNEL_BUILD`、`BOARD_DTS` 和可选 `KERNEL_EXTRA_FRAGMENT` 都相对项目根目录解析，避免 `make -C` 把相对 `O=` 误解到源码树内部。5.10 专用 fragment 关闭了需要宿主 `gmp.h` 的 ARM per-task stack-canary GCC 插件，但保留 `CONFIG_STACKPROTECTOR_STRONG=y`；最小 DT 没有 Wi-Fi 节点，因此同时关闭 5.10 默认的 `CFG80211`，避免其隐藏 signed-regdb 配置强制编译需要 OpenSSL 开发头文件的 `extract-cert`。这些兼容项不影响共享的 6.18 fragment。

最终配置确认 `CONFIG_SMP=y`、`CONFIG_FUNCTION_TRACER=y`、`CONFIG_PSTORE_FTRACE=y` 和强制命令行均生效。zImage 为 10,166,784 bytes，DTB 为 1,849 bytes。32 MiB recovery 连续两次封装逐字节一致，文件 SHA-256 为 `1bbd729a09cf1e81f8d2ebd5d8226e2ac33e78cb0252eb3df5c6820ed7e555f2`，Rockchip SHA-1 为 `bb200b2a19412e6a5ef4dd7471f7a22da1242b43`，Rockchip SHA-256 为 `bb40520dc8c10a54de9505c32f9e43aec49b4fd6994c580c2bad28ee2e58e844`。刷写 helper 的允许哈希已同步。

该镜像随后已上板。用户实测报告 Linux 5.10.262 双核仍出现与 6.18 相同的约 30 秒全局停止；没有保存本轮完整串口日志，因此精确最后 uptime 暂缺。这个版本对照足以排除“6.18 特有回归”，后续不在 5.10–6.18 范围继续二分。由于最小 DT 已排除大部分外围驱动，下一主线是以同一最小内核对照现代 U-Boot + OP-TEE 固件链；另一条次级假设是 secondary CPU 持续在线后的 VDD_ARM 供电裕量。两者都不得以直接覆盖当前 eMMC idb/U-Boot/trust 的方式试探。

### 首次 recovery 写入的自动恢复现象

MaskROM 已成功将候选写入 recovery，并且立即读回与候选逐字节一致。但随后正常启动 Android 会运行 boot ramdisk 中的 `flash_recovery` service；`/system/bin/install-recovery.sh` 检测到 recovery 不符合原厂哈希后，会用 `/system/recovery-from-boot.p` 将它恢复。因此首次捕获日志仍显示原厂 kernel `0x62000000`、ramdisk `0x65bf0000` 和 Linux 3.10，并不表示主线镜像已获得控制权或启动失败。

后续不能采用“写 recovery → 启动 Android → reboot recovery”。应在原厂 recovery 已运行时将候选推到 RAM，由 recovery root shell 通过 `/dev/block/platform/30020000.rksdmmc/by-name/recovery` 写 recovery、立即读回比较，然后直接 `adb reboot recovery`。此路径不启动 Android 的自动恢复服务。

不要硬编码 `mmcblk0p8` 或 `mmcblk0p9`。实测 recovery 模式下 by-name 指向 `mmcblk0p9`，因为该启动模式把 parameter 注册为首个分区；其他模式的编号可能不同。写前必须确认 by-name 解析结果和块设备容量恰为 33,554,432 bytes。recovery SELinux enforcing 会阻止 adbd 向 RAM tmpfs 推送；串口 `init` 域又被明确拒绝 `{ setenforce }` 和将 tmpfs `{ relabelto } shell_data_file`，而 `adb root` 后 adbd 仍为 UID 2000。最终使用 recovery BusyBox 的 `rx` 和主机 `sx -k`，通过 UART XMODEM 将 gzip 压缩候选送入 `/tmp`。

### Fastboot 实机结果

从原厂 Android 串口 shell 执行以下命令可进入 U-Boot Fastboot：

```sh
setprop sys.powerctl reboot,fastboot
```

串口随后确认：

```text
Restarting system with command 'fastboot'.
reboot fastboot.
```

主机运行 `scripts/check-fastboot.sh` 的结果：

```text
product: fastboot
version-bootloader: jenkins-R1-master-user-3448
secure: yes
unlocked: no
getvar:max-download-size FAILED (remote: 'unknown variable')
```

U-Boot 启动日志中的 `SecureBootEn = 0` 表示没有启用签名验证；Fastboot 的 `secure: yes` 和 `unlocked: no` 表示 Fastboot 下载保护仍处于锁定状态，两者不矛盾。原厂 U-Boot 镜像明确包含锁定时拒绝 download 的错误路径，因此当前不执行 `fastboot boot`。

不要尝试 `fastboot oem unlock` 或类似命令。原厂 U-Boot 字符串表明其 unlock 流程会涉及 `userdata` erase；这既会改变设备状态，也可能删除数据。

公开的 R1 Root 教程也不能绕过这一层锁。该方法用 `rkdeveloptool wl 0x0 r1_root_rush.img` 从 LBA 0 覆盖第三方 3166 预 Root 镜像，并非在当前 Android 中取得 root。Android UID 0 和 U-Boot 的 `fastboot_unlocked` 属于不同层级；前者不会自动改变后者。项目不采用这种未审计的整盘覆盖方式。

原厂 recovery 已实机枚举为 ADB 设备。`adb shell` 失败是因为 recovery ramdisk 没有 adbd 默认查找的 `/system/bin/sh`，不是 USB transport 失败；ramdisk 实际包含 `/sbin/sh` 和 `/sbin/busybox`。实测 `adb root` 在重连等待时超时，重启后的 adbd 仍为 UID 2000、SELinux domain `u:r:adbd:s0`，所以不能依赖 ADB sync 向 RAM 中转候选。

## 已验证启动路径与安全边界

当前实机路径为：PCB 按键进入仍存活的 U-Boot Loader，受控写入 recovery 和 3-sector BCB，完整读回比较，再由原厂 `bootrk recovery` 启动主线镜像。该路径已经进入 shell，但不是零写入 RAM boot，也不是引导区全损坏后的真 MaskROM 恢复。

优先级如下：

1. Fastboot 只读能力检查已经完成；锁定状态阻止 `fastboot boot`；
2. 不通过会擦除 userdata 的 unlock 流程来换取 RAM 启动；
3. `upgrade_tool RUN` 已经通过反汇编排除：它只发送存储 offset 和文件 size 描述，不发送三个文件的内容，不能加载主机上的自定义 boot image；
4. 原厂 recovery ADB 权限、recovery 定点写入、读回和 BCB 回滚均已验证；继续把 recovery 作为可回滚试验区；
5. 不使用 `UL`、`WL`、`EF`、`GPT` 或其他擦写命令来试探启动。

`upgrade_tool RUN <uboot_addr> <trust_addr> <boot_addr> <uboot> <trust> <boot>` 不是自定义镜像 RAM 上传路径。对 `run_system()` 的反汇编确认，它只用三个本地文件取得 size，从未打开并发送文件内容；三个地址参数均左移 9 位后写入 56-byte 参数表，说明命令行地址使用 512-byte 存储单位。设备随后依据 offset 和 size 运行存储中已有的镜像。

因此当前没有已验证的零写入主线启动入口。recovery 定点写入、读回和 BCB 回滚已验证，现在可继续使用 recovery 作为可牺牲试验区；若要进一步放开 parameter/idb、U-Boot 或 trust，则必须先完成不依赖 eMMC U-Boot 的真 MaskROM 恢复演练，并确认 eMMC hardware boot0/boot1 的备份情况。

### MaskROM RAM U-Boot 提示符候选

U-Boot 官方 `CONFIG_ROCKCHIP_MASKROM_IMAGE` 能生成 BootROM USB 专用的 471 TPL 与 472 SPL+payload 镜像。首轮诊断为减少变量，保留已经实机验证的原厂 DDR V1.06 作为 471，只使用主线生成的 472。诊断配置不含 OP-TEE，禁用 autoboot、preboot 和 eMMC environment，仅用于确认控制权能否到达现代 U-Boot 提示符，不能作为最终 Linux SMP 固件链。

候选位于 `build/artifacts/r1-hybrid-mainline-uboot-ram-prompt.bin`，大小 551,189 bytes，SHA-256 为 `5478af8147c75a7bbe6603b1ef0ccec06e78a39a7f6d8b0dc2086e804f4bb195`。471/472 解包数据与输入逐字节一致，padding 全零，Rockchip CRC `0x21218e2f` 已独立复核。必须在真正的 BootROM MaskROM 中使用 `rkdeveloptool db`，不得使用任何存储写入命令：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-hybrid-mainline-uboot-ram-prompt.bin
```

串口保持 `1500000 8N1`。实机测试中，471 完成 DDR training 并打印到 `OUT`，但主线 472 没有产生任何 SPL/U-Boot 输出。已核对 SPL 的串口选项、UART2 base、1500000 波特率和 `0x60000000` 链接入口，尚不能区分 BootROM 未进入 472 与 SPL 在 banner 前停止。

为隔离这两个阶段，新增 `scripts/rk322x-uart472-probe.S` 和离线解包验证过的 `build/artifacts/r1-uart472-entry-probe-loader.bin`。其中 472 只有 68 bytes，复用 471 配置好的 UART2 打印 `R1 472 ENTRY OK` 后原地循环，不访问 eMMC。下一次真 MaskROM 冷启动只运行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-uart472-entry-probe-loader.bin
```

实机已在 `OUT` 后打印 `R1 472 ENTRY OK`，因此 472 交接、`0x60000000` 地址和 ARM 入口均已验证，完整候选的问题属于 SPL 自身。

下一版 `build/artifacts/r1-spl-uart-breadcrumbs-loader.bin` 在 `reset`、`save_boot_params()` 返回、`_main`、建栈、global data、`debug_uart_init` 和 `board_init_f` 前后依次打印 `S/R/M/0/1/2/3/4/5/6`；对应补丁为 `patches/u-boot-rk322x-spl-uart-breadcrumbs.patch`。运行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-spl-uart-breadcrumbs-loader.bin
```

实机只输出 `S`，证明停止位于 Rockchip 强 `save_boot_params()` 内部。该函数入口会立即使用 BootROM 遗留栈并调用 `setjmp()`，但 SPL 配置实际未启用 `SPL_ROCKCHIP_BACK_TO_BROM`；它因 TPL 选择的全局 helper 被意外链接进 SPL，并覆盖了无栈 weak stub。

`patches/u-boot-rockchip-phase-aware-save-boot-params.patch` 让强实现服从当前 phase 的 `ROCKCHIP_BACK_TO_BROM`。修正后的 `build/artifacts/r1-spl-phase-fix-loader.bin` 保留全部路标，执行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-spl-phase-fix-loader.bin
```

实机得到 `SRM0123`，验证 phase 修正有效；下一停止点位于 `debug_uart_init()`。反汇编确认 NS16550 实现在写 divisor 前等待 LSR `TEMT` bit，而当前已由 471 配置好的 UART 能持续满足路标使用的 `THRE` bit。

启用 `CONFIG_DEBUG_UART_SKIP_INIT=y` 并应用 `patches/u-boot-ns16550-honor-debug-uart-skip-init.patch` 后，`debug_uart_init()` 不再等待 TEMT 或重配 divisor。A/B 候选为：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-spl-uart-skip-init-loader.bin
```

该候选尚未来得及测试便发现仍继承 `rk3229-evb` 板级 DT，因此停止使用。`SRM0123` 的早期定位发生在 DT 解析之前仍然成立，但不得让 EVB 候选继续进入 `board_init_f()` 之后。

项目现新增 `patches/u-boot-phicomm-r1-board.patch`，定义独立 `TARGET_PHICOMM_R1`、512 MiB memory、原厂 DTB 已证明的 `uart21_xfer` 和 8-bit eMMC；不带 EVB regulator、GMAC 或 USB VBUS 配置。诊断 defconfig 同时关闭 environment 持久化以及 MMC/GPT/SPI/Fastboot/USB mass-storage/FAT 写能力。SPL 非 TPL 路径只读取 vendor 471 已写入 GRF 的 DRAM 容量，不重新训练 DDR。

R1 专用候选实机仍只得到 `SRM0123`，因此 EVB pinmux 不是这个停止点的原因。最终反汇编显示 `debug_uart_init()` 已不访问硬件，只剩 ARM `blx` 进入 Thumb 空函数再返回。下一 A/B 在 `CONFIG_DEBUG_UART_SKIP_INIT=y` 时从 `crt0.S` 完全删除这次调用，使路标 `3` 后直接执行 `4`：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-bypass-debug-init-loader.bin
```

实机得到 `SRM012345`，确认完全绕过该调用后执行流已到达 `board_init_f()`。下一候选在函数内加入 A–K 路标，依次覆盖 early init、driver model early init、CPU/timer、DRAM 信息和 preloader console：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-board-init-trace-loader.bin
```

实机停在 `B`，确认边界位于 `spl_early_init()`。下一候选用小写路标区分 early malloc、内嵌 DTB 检查、DM 扫描和自动 probe：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-spl-common-trace-loader.bin
```

实机得到 `SRM012345ABab`，确认边界位于 `fdtdec_setup()`。文件内 `__bss_end` 处 DTB 有效，下一候选在调用前直接读取运行时 DTB magic：`s` 表示开始读取，`m` 表示匹配，`n` 表示不匹配。

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-spl-dtb-memory-probe-loader.bin
```

实机得到 `...absn`，运行时 DTB magic 不正确。下一候选在 `n` 后打印该地址实际读出的 32 位十六进制值：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-spl-dtb-hex-loader.bin
```

该候选仍是纯 RAM、禁写诊断配置。最终恢复原生 `__bss_end` DTB 定位并改用 Rockchip 官方 `boot_merger` 封装后，实机路标完整通过 `SRM012345ABabsmcdefCDEFGHIJ`，SPL 从 RAM 启动 U-Boot proper，并进入 U-Boot 2026.10-rc1 交互提示符。实机报告 512 MiB DRAM、103 个设备、14 个 uclass，eMMC 为 `mmc@30020000: 0`。下一阶段才恢复 RK322x 的 `SPL_OPTEE_IMAGE` 并向 FIT 提供匹配的 `tee.bin`，构建正式 OP-TEE + U-Boot RAM 链。

### Armbian RK322x 开源 OP-TEE 对照

Armbian 的 `rk322x-box.tvb` 本身只选择通用 box U-Boot/DT；支持 SMP 的关键在 family
配置向 U-Boot 构建传入 `TEE=`。维护者的 `rk322x-opensource-tee` 分支及提交
`d80ff015a83b0cf9a2500a2312a31d42931a6da4` 提供 423,248-byte
`rk322x_tee_os.bin`，并把 `TEE=` 从专有 `rk322x_tee.bin` 切到该文件。二进制内含
`plat-rockchip/psci_rk322x.c`、`psci_cpu_on/off` 和 OP-TEE
`3.7.0-1-ga34a269b7-dev` 版本信息，SHA-256 为
`ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`。

Rockchip ARM32 FIT 模板把它装入 `0x68400000`，由它建立 secure monitor/PSCI 后再进入
U-Boot proper；这与 R1 原厂 Trust 地址吻合。社区还记录专有 RK322x Trust OS 存在
30 秒、60 秒或更长周期的 watchdog 冻结，和 R1 症状高度一致。该结论解释了 box
方案为何能启动主线多核，也把下一步收敛为“R1 DDR 471 + R1 U-Boot + 开源 RK322x
OP-TEE”的 RAM-only A/B；通用 box 的 DDR blob、DTB 和存储布局仍不复制到 R1。

R1 的首个开源 OP-TEE 候选已完成离线构建，产物为
`build/artifacts/r1-phicomm-r1-uboot-optee-os-loader.bin`，大小 860,433 bytes，
SHA-256 为 `7f617c52269e9fe4f29f6bcfa7716460e970363b646d70ac4021caf275174f0b`，
FIT 中 OP-TEE 的 load/entry 均为 `0x68400000`。471/472/loader 解包后有效字节均与
输入一致；该候选尚未上板，下一步仅执行 MaskROM `db`。

### MaskROM/RockUSB 遗留 GIC active interrupt

Linux 5.10 v7 实机显示 CPU0 的 GICC_RPR 从 GIC 初始化前直到等待 kthread completion 前始终为
`0x00`，而 CPU1 为正常 idle 值 `0xff`。随后在仍处于 secure state、尚未进入 OP-TEE 的 SPL
中读取 GIC，cache cleanup 前后的结果完全一致：GICC_APR0=`1`，所有 GICD_ISACTIVERn 中仅
ISACTIVER1 bit 23 为 1，即 GIC INTID 55。上游 `rk322x.dtsi` 的 `usb@30040000` 使用
`GIC_SPI 23`，换算后正是 INTID 55。已验证事实是异常早于 OP-TEE；“它由 MaskROM/RockUSB
USB OTG 交付路径遗留”目前是与地址映射一致的最强推断，尚未用旧厂商链作同组寄存器对照。

下一 RAM-only A/B 在 SPL 进入 TEE 前检查完整签名；仅当 RPR=`0`、APR0=`1` 且 INTID 55 是
唯一 active 中断时，才屏蔽并清除它的 pending/active 状态、清 APR0，随后输出 `GC` 快照。
任何签名差异都会跳过写操作。该试验不写 eMMC。

实机 `GC` 得到 RPR=`0xff`、APR0–3 全零且 `V-`，证明精确清理成功。随后同一 v7 Linux
在 CPU0 最早 GIC 快照即为 RPR=`0xff`/HPPIR=`0x3ff`，CPU1 online 后两核状态一致；此前
持续 pending 的 PPI 30 已消失，CPU hotplug、kdevtmpfs 创建与 completion 均完成，日志跨过
`devtmpfs: initialized` 并继续到 pinctrl、NET 与 DMA 初始化。因此旧停止点的因果链已经验证：
INTID 55/APR0 遗留阻挡 CPU0 普通 IRQ，进而阻挡 CPU1→CPU0 调度 IPI和 completion 唤醒。

为避免 v7 的逐字符路标、polling completion 和 SGI filter 干扰后续判断，已从未修改的
Linux 5.10.262 commit `065a677fad98` 独立构建 clean v8。它保留同一 PSCI v1/GIC400 DT、
initramfs 和 kernel command line，不包含上述诊断代码；下一实机标准是进入救援 shell并连续
打印 uptime 超过 30 秒。

clean v8 实机通过。启动日志确认镜像为无 `-dirty` 后缀的
`Linux 5.10.262-phicomm-r1 #1`；PSCI v1.0 启动 CPU1 后打印 `Brought up 1 node, 2 CPUs`，
正常越过 `No ATAGs?`，于 2.078 秒运行 `/init` 并进入 BusyBox shell。用户随后确认 uptime
超过 30 秒。保存的首 shell 日志只包含 `8.99 15.92`，因为 `/bin/sh` 找不到 `sleep` 链接；
因此日志直接验证双核 shell 与 8.99 秒，>30 秒作为用户实机确认单独记录。后续保存长期日志
应使用 `/bin/busybox sleep 5`，而不是未安装链接的 `sleep`。
