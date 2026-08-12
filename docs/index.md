# 项目索引

本文档是斐讯 R1 Linux 项目的统一入口，用来说明当前进度、下一步工作、文档分工和本地证据位置。详细的操作过程保存在[逆向学习记录](reverse-engineering-journal.md)中。

学习记录中的外部技术结论必须同时保留可追溯来源：优先链接到论坛的具体回复、固定 commit/blob、原始补丁、数据手册或维护者文档，并记录作者、日期、标识符、必要的 commit 与校验和。引用内容必须与 R1 实机事实、跨设备推断和未验证问题分开表述；只有主题页、仓库首页或搜索结果不能替代原始出处。完整规则见 [`AGENTS.md`](../AGENTS.md#evidence-and-writing-rules)。

## 项目目标

在保留可靠恢复路径的前提下，为斐讯 R1 建立可控、可裁剪、可复现的 Linux 启动与音频系统，最终支持 Wi-Fi、蓝牙 A2DP Sink、AirPlay、PipeWire 和 AK7755 DSP。

## 当前状态

Phase 0“保护现场”已完成大部分采集工作：串口、MaskROM、完整 eMMC User Area 备份、关键分区提取、boot/recovery 拆解以及原厂无线和音频固件导出均已完成。整个备份和拆解过程没有向 eMMC 写入数据。

开源 OP-TEE RAM-only 目标已达成：R1 已通过 MaskROM `db`、vendor DDR 471、主线 SPL/U-Boot 2026.10-rc1、开源 RK322x OP-TEE 3.7 和 clean Linux 5.10.262 进入 BusyBox 救援 shell。双核 clean v8 由用户确认 uptime 约 700 秒；修正一键脚本默认 cleanup SPL 后，四核 clean v9 也确认 CPU0–CPU3 online、uptime 到 72.92 秒且四核 IPI 均有增长，越过旧约 30 秒冻结边界。

白名单式四核救援 v10 的实机日志暴露了两项过度裁剪：缺少 `BINFMT_SCRIPT` 使 `/init` 以 `ENOEXEC` 失败，内核回退 `/bin/sh`，所以虽已编入 `PROC_FS` 却没有挂载 `/proc`；缺少 `ARM_PSCI` 又使系统只启动 CPU0。v10 已判为无效基线。修正后的 v11 补回 PSCI、脚本执行、time32/POSIX timers、futex、epoll/timerfd/eventfd、inotify、SysV IPC、RTC 和低成本诊断接口，仍明确关闭 CAN、NFS/NTFS、PCI、MTD、图形、声音和无线驱动。v11 zImage 为 2,721,856 B，尚待 RAM-only 实机验证。

外设 bring-up 已改为“冻结救援核心 + 单外设分层”：构建脚本可依次合并多个 config fragment，
并为每次试验生成带 tag 的 zImage、DTB、最终配置和预处理 DTS。首个
`rescue-v11-emmc-a1` 候选保持 v11 最终配置不变（除版本后缀），只在新 DT 中启用 eMMC、
RK805 regulator 及其前置链，同时显式禁用 USB。主机构建和静态 DT 审计已通过；下一步以同一
新 zImage 跑最小 DT 的 A 线已经由用户确认四核、虚拟文件系统、shell 和 uptime >30 秒均正常。
eMMC B1 随后成功只读枚举 RK805、HS200 Samsung `8GME4R`、user/boot0/boot1/RPMB，但
CPU1–CPU3 在 eMMC probe 前启动失败，所以完整板级 DT 仍不是合格基线。下一 B2 保持内核、
initramfs 与 eMMC 描述不变，只把 arch timer 合同恢复为已验证最小 DT 的形式；实机三个次核
仍依次超时，故 timer 单变量假说已否定。当前 C1 改为直接复用已验证四核稳定的 multi_v7
clean v9 zImage，保持 B2 DTB 与修正 initramfs 不变，以判断问题来自 v11 裁剪 config 还是完整
板级 DT。C1 实机已使 CPU0–CPU3 在 35 ms 内全部 online，且 eMMC 继续正常枚举，故边界明确在
内核 config/早期执行差异；C2 只补 Cortex-A7 erratum 814220 后仍为单核，已否定该单变量。
当前 B3 保持失败 v11 kernel 和完整 eMMC DT，只删除 SMP 前 CRU 批量 `assigned-clocks/rates`，
可在后续最小化阶段检查 clock reprogramming 是否参与故障。当前外设 bring-up 不再等待该二分，
直接以已验证四核/eMMC 的约 10 MiB multi_v7 C1 为工作基线。

用户补交的完整 B1 日志再次确认上述边界，并暴露 initramfs 只缺 BusyBox `echo` 等入口链接；
该问题已在构建脚本中补齐。为减少后续反复串口下载，已完成一个尚待实机的 U-Boot proper
USB DFU RAM-only 候选：DFU/MMC、Fastboot、RockUSB 和 mass-storage 后端均关闭。v11 kernel、
修正 initramfs 与 B2 DTB 已封装为单个 Linux FIT，可在 U-Boot 中下载到 `0x6a800000` 后
`bootm`；所有 payload 已在主机侧解包逐字节核对。用户确认 USB 下载已足够快并运行到 B2
Linux，但保存日志没有包含传输计时。C1 的 10,809,808-byte FIT 已通过同一路径验证四核与
eMMC；C2 FIT 也完成 hash 验证和启动但仍单核。当前 USB 脚本默认恢复为 C1 FIT，B3 仅保留
为后续裁剪诊断件。

当前第一个外设单变量是 USB Host A1：保持 C1 kernel/initramfs 不变，只在 DT 中恢复原厂日志
已证明可注册的三组 EHCI/OHCI fixed-host 控制器；DWC2 OTG 继续禁用，避免重新进入 INTID 55
路径。A1 FIT 已完成 DT 状态、子镜像哈希和提取后逐字节核对，尚待 RAM-only 实机验证。
DDR 471 的 `300MHz` 仅表示初始化频率；RK322x DDR DVFS 所需的 DMC/devfreq 驱动、TEE SMC
ABI 与 timing/频点表尚未建立，已作为后续 TODO，不阻塞当前外设 bring-up。

用户随后确认 R1 成品是没有 USB 外设接口、也没有 SD 卡槽的智能音箱。USB Host A1 因而降级为
SoC/DFU 研究产物，不再安排实机外设测试；真正的板载外设顺序改为 SDIO Wi-Fi、UART 蓝牙、
I2C/SPI 控制与 I2S 音频。Wi-Fi 首轮只恢复 `0x30010000` SDIO 和 GPIO2_D2 WL_REG_ON，先证明
AP6255/AP6335 SDIO function 枚举，再单独加入 cfg80211/brcmfmac 与固件/NVRAM。

Wi-Fi SDIO A1 已完成主机构建：继续使用 C1 kernel/initramfs，DT 仅增加 GPIO2_D2 power
sequence 与 `0x30010000` 4-bit/37.5 MHz SDIO。最终 DT/FIT 已静态核对并解包比较；下一步
RAM-only 上板只判断 SDIO card/function 是否出现，不提前要求 `wlan0`。

Wi-Fi A1 随后实机通过：`30010000.mmc` 成功执行 pwrseq，`mmc1` 以约 35.7 MHz 枚举焊接的
SDIO card，sysfs 出现三个 vendor `0x02d0`/device `0xa9bf` function；四核仍为 `0-3`，eMMC
继续正常。由此已验证板载 Wi-Fi 确实走 SDIO。下一步 A2 只增加 cfg80211/brcmfmac 与已提取
的 BCM43455/AP6255 固件、NVRAM，目标是创建 `wlan0`。

Wi-Fi A2 已完成主机构建：A1 DT 不变，multi_v7 v9 config 只增加内建 cfg80211/brcmfmac SDIO
及调试输出，新 initramfs 按主线文件名携带原厂已验证的 BCM43455 firmware/AP6255 NVRAM。
11,468,836-byte FIT 已解包逐字节核对，尚待 RAM-only 实机创建 `wlan0`；扫描与联网不属于 A2。

A2 随后实机通过：brcmfmac 识别 BCM4345/6、下载并运行 7.45.100.6 firmware，`wlan0` 已绑定
到 `30010000.mmc` 的 SDIO function 1。当前缺 `regulatory.db` 和 CLM blob，日志明确提示信道
可能受限。用户进一步确认该版本可长期运行，CPU0–CPU3 的 IPI2/IPI3 均有活动，四核稳定性
回归通过。A3a 主机产物现已加入 Fedora `wireless-regdb 2026.05.30`、`linux-firmware 20260622`
提供的 BCM43455 CLM blob，并以强制 kernel cmdline 设置初始监管域 `CN`；FIT 已解包逐字节
核对。A3a 随后实机通过：CN cmdline 生效，regdb/CLM 缺失警告均消失，`wlan0` 与四核 IPI
活动保持正常。A3b 已打包一个约 8 KiB、无 libc/libnl 依赖且隐藏 BSSID 的 freestanding
nl80211 扫描器，FIT 静态审计通过，下一步只做实机扫描。
A3b 随后实机扫描成功：在 2.4 GHz 和 5 GHz 共返回 9 个 BSS，`scan_entries=9` 且退出码为 0；
工具按设计未输出 BSSID，也未关联网络。首版 ELF 被内核提示 executable stack，已在可复现
构建脚本中加入 `-z noexecstack` 并重新封装，当前默认 FIT 的该警告修正版尚未重复上板。

外设顺序现已进入 UART Bluetooth。原厂 R1 实际使用 UART1 的第二复用组 GPIO3_B6/B5
（TX/RX）和 GPIO3_A7/A6（CTS/RTS），不是主线 `rk322x.dtsi` 默认 GPIO1 组。Bluetooth A1
已保持 A3 kernel 不变，只在 DT 加入该 alternate pinctrl，并用 GPIO hog 将 GPIO2_D5
BT_REG_ON 拉高；没有 serdev 子节点、没有 HCD 下载。下一步从 `/dev/ttyS1` 以 115200 8N1
和硬件流控发送标准 HCI Reset，先验证物理 UART/controller ROM 链路。
首个 A1 实机在 init 前失败：GPIO2 注册自己的 BT_REG_ON hog 时返回 `-EPROBE_DEFER`，导致整个
gpio2/pinctrl 注册失败，并连带阻塞 eMMC、UART1/UART2 和 I2C。A1r2 已移除 gpio-hog，改用
根节点 always-on fixed regulator，让 BT_REG_ON 在 gpiochip 注册后申请；kernel/initramfs 和
UART alternate pinctrl 均不变，下一步重跑启动与 HCI Reset。
A1r2 随后成功进入 shell，`ttyS1` 注册且 GPIO2_D5/`bt_reg_on` 为 output-high；关闭/开启 RTS/CTS、
手动解绑再绑定 regulator 形成低→高复位后，115200 HCI Reset 仍无响应。主线 RK3228 pinctrl
源码确认 GPIO3_B5 会自动设置 GRF 0x50 bit 11 的 UART1-1 route，因此不采用盲目交换 TX/RX。
A1r3 依据原厂 DT 再拉高主机输出 GPIO3_D3 BT_WAKE，HOST_WAKE 暂不绑定，待重新测试 ROM HCI。
A1r3 实机进一步证明 UART1-1 pinmux 与两个控制 GPIO 正确，HCI Reset 后 UART 统计为 `tx:4 rx:0`；
即 SoC 已发送而 controller 完全未回应。A1r4 现将此前仅为 module、实际未加载的 RK805 32 kHz
clock provider 改为内建，并以 always-on consumer 开启 CLK32KOUT2，作为下一项单变量实机测试。
模组丝印进一步确定为 AzureWave AW-CM256SM/CYW43455。其参考启动时序要求 LPO 有效、
BT_DEV_WAKE 为低后再拉高 BT_REG_ON，说明 A1r3/A1r4 把 GPIO3_D3 保持高并不正确。A1r5 已按
上述顺序重做 DT，复用同一 A1r4 kernel，等待 RAM-only HCI Reset 验证。
A1r5 实机三项状态均符合预期但仍为 `tx:4 rx:0`；对照主线 `hci_bcm` 后，下一单变量改为
REG_ON 后将 BT_WAKE 从初始 low 切至 high，并等待至少 100 ms 再发送 HCI。
A1r6 已改用标准 `hci_bcm` serdev，内建 Bluetooth/HCI UART/BCM 驱动并绑定 LPO、三个控制/唤醒
GPIO、RTS/CTS；原厂 BCM4345 HCD 已加入 initramfs。FIT 已完成静态审计，下一步实机以驱动日志、
firmware 请求名和 `/sys/class/bluetooth/hci0` 为验收证据。
A1r6 随后实机成功创建 `hci0` 并识别 BCM4345C0；UART 双向、RTS/CTS、LPO 与控制 GPIO 全部
通过。驱动明确请求 `brcm/BCM4345C0.hcd`，A1r7 已为原厂 HCD 增加该精确别名，等待验证 Patch
下载和固件 build 变化。
A1r7 实机已完成 HCD Patch：controller 从 `build 0000` 更新到 `build 0124`，`hci0` 正常存在，
UART patchram 双向传输量与日志一致。Bluetooth 内核 bring-up 阶段完成；下一步是加入最小 BlueZ/
management 用户空间，先验证 controller power、LE/BR-EDR 扫描，再进入配对与 A2DP。
A1r8 已在主机端加入约 9 KiB、无 libc/BlueZ daemon 依赖的 Bluetooth Management client，支持
controller info、power、BR/EDR inquiry 和 LE scan，并打包 Wi-Fi/蓝牙交替扫描与四核 IPI 检查脚本；
新 FIT 已完成静态审计，尚待 RAM-only 实机验证，不能据此声称扫描或共存已通过。
A1r8 随后已实机完成 power、capabilities、BR/EDR inquiry 和 LE scan：LE 20 秒收到 29 个广播
report，退出码 0，current settings 为 powered+BR/EDR+LE。当前天线损坏，故该结果只验证协议与
收发链，不用于评价 RSSI/覆盖；CMAC crypto config 和 Wi-Fi/四核长时共存仍待完成。
用户随后确认交替扫描共存测试通过，且 Wi-Fi 继续发现 5 GHz 网络；A1r8 management 阶段完成。
A1r9 只把原为 module、initramfs 不加载的 AES 改为 built-in，使已有 built-in CMAC 能创建
`cmac(aes)`；最终 config 相对 A1r8 kernel 仅此一项变化，DTB/initramfs 不变，等待实机确认
power on 的 CMAC context 警告消失。通过后进入完整 BlueZ 最小用户态。
A1r9 随后实机通过：power on 返回 0，LE 10 秒收到 19 个 report，current settings 保持
powered+BR/EDR+LE，过滤日志中不再出现 CMAC crypto context 错误。Bluetooth management 与
进入 BlueZ 前的内核 crypto 前置条件均已完成；当前进入最小 BlueZ、D-Bus 与配对 agent 打包。

为避免每次从 MaskROM 下载，已开始准备 eMMC 常驻链，但尚未写设备。当前候选不是把 RAM 调试
SPL 原样刷入：它使用已在 R1 验证、SHA-256 固定的 DDR v1.06 blob 作为 external TPL，后接启用
MMC、关闭 YMODEM 和所有 MMC 写能力的主线 SPL，再从 raw LBA `0x6000` 加载开源 OP-TEE +
U-Boot proper FIT。ID block/FIT、Rockchip RAM 测试 loader 及 manifest 已在主机生成并解包逐字节
核对；A5 raw 写区间分别为 `0x40..0xa7` 和 `0x6000..0x664e`，均早于 raw `misc@0xa000`。A1 零写入实机
测试发现 alias/eMMC 节点被 SPL DT 裁掉，故只尝试两个 RAM loader；A2 改用 `&emmc` 路径并以
`bootph-all` 保留 `/mmc@30020000` 及 CRU 依赖，实机已经进入 `Trying to boot from MMC1`。
A2 随后的 `-38` 是 raw 失败后回退到未实现 FS loader 的最终错误。A3 首读探针实机得到
`sector=4000 count=1 got=1 hdr=4c4f414445522020`（`LOADER  `），直接证明 mainline MMC 视图
相对旧 parameter 逻辑视图需要 `+0x2000` sectors；原候选加载地址不可用。A4 已将 SPL FIT
读取地址改为 mainline LBA `0x6000`，随后 usbplug `rl 0x4000` 同样读到 `LOADER`，证明 `wl/rl`
使用 raw 地址，因此 A5 将 FIT 写入与读取目标统一为 `0x6000`，并完成构建、
pack/unpack 与 payload 逐字节核验。下一步仍只用 RAM loader，要求看到 `sector=6000 count=1 got=1`
和原厂 `TOS     ` header；A4 实机现已完全满足该标准，证明块读与双 LBA 合同正确。下一步运行
只读安装预检。首轮已证明真 MaskROM → RAM usbplug、A223/eMMC 查询与 IDB 双读可工作，但也
发现历史整盘备份来自厂商 Rockusb 的参数逻辑视图，不能把其中 `0x40` 的全零内容当作 raw IDB。
修正版会把当前 raw IDB 双读一致结果单独保存为原厂恢复片，再核对 trust 目标区与已有备份。
该预检现已完全通过：raw IDB 与 trust/FIT 目标均连续双读一致且恢复哈希固定。A5 事务式安装脚本
和独立原厂恢复脚本已生成并完成 dry-run/语法审计；二者锁定 LocationID、A223、容量、输入 SHA、
raw `0x40`/`0x6000` 两个范围并强制写后读回，不自动复位；安装异常的现场回滚也会再次读取
两个原始切片并逐字节比较，无法确认回滚时会明确要求禁止断电。用户明确授权后，A5 已于
2026-08-12 写入 raw `0x6000..0x664e` 和 `0x40..0xa7`；两处均完成立即读回比较，
输出 `A5 INSTALL WRITEBACK VERIFIED`，未触发回滚、未复位。当前下一步是在 UART 原始
日志已开启的前提下做首次冷启动。该冷启动现已通过：BootROM 执行 DDR v1.06
和主线 SPL，SPL 从 MMC1 raw `0x6000` 读到 FIT magic `d00dfeed`，开源 OP-TEE
完成 `Initialized` 并转交 normal world，U-Boot proper 枚举 480 MiB 可用 DRAM 与
eMMC 后进入 `=>`。常驻固件链因而已摆脱 MaskROM/USB/YMODEM；下一步仍不写
eMMC，从该 U-Boot 把已验证 Linux FIT 装入 RAM，回归四核、无线与 >30 s 稳定性。
USB 首次未枚举导致 `dfu-util` 未找到设备，重新插拔数据线后 A1r9 FIT 已成功下载，
三个子镜像 SHA-256 全部通过并进入 rescue shell。同时确认地址必须写为
`0x6a800000`；无 `0x` 的含字母地址会被 `bootm` 误当子命令。四核、>30 s、Wi-Fi
与 Bluetooth 本次尚未重新输出证据，仍保留为下一项回归。

下一外设阶段转入音频硬件。用户实物确认 DSP 完整型号为 `AK7755EN`；AKM
官方资料确认其为 36-pin HVQFN、RAM-based DSP，支持 I²C/SPI 控制。Audio I2C A1
已在主机构建：完全复用 A1r9 kernel/initramfs，DT 只启用 I2C1 100 kHz，没有
I2C client，不操作 AK7755EN PDN、功放 shutdown/mute 或任何总线 payload。FIT 的三个
payload 已解包逐字节核对，下一步 RAM-only 实机只验证 controller/clock/pinmux 及旧功能回归。
Audio I2C A1 随后实机通过：`/dev/i2c-1` 已创建，GPIO0_A2/A3 被
`11060000.i2c` 以 `i2c1-xfer` 占用，`pclk_i2c1` 已 prepare 且可按需 gate；Wi-Fi 固件和
BCM4345C0 build 0124 仍正常。AK7755EN PDN 与功放两脚仍未申请、未切换。数据手册
已确认无寄存器副作用的身份命令为 `0x60`、期望返回 `0x55`；下一步先读取
三个未托管 GPIO 的当前方向/实际电平，再设计功放保持 shutdown+mute 的 PDN A2。
该只读采样因串口粘贴把 `32` 与下一条 `echo` 连成 `32echo`，GPIO3 三个返回值无法可靠
对应到 DR/DDR/EXT_PORT，故不把它们当作电平结论。Audio A2 已改为确定性安全链：先令
TPA3118D2 SDZ=low、MUTE=high，再令 AK7755EN PDN=high；I2S/ASoC 仍关闭。initramfs
新增静态 `r1-ak7755-id`，只对 `/dev/i2c-1` 地址 `0x19` 执行一次 command `0x60` +
repeated-start 读一字节。A2 FIT 已完成主机构建、DT 审计和三个 payload 的解包逐字节核对，
当前默认 DFU 脚本已指向它。A2 随后实机通过：debugfs 与 GPIO EXT_PORT 同时确认
GPIO35=high、GPIO111=low、GPIO113=high，功放保持 shutdown+mute；身份工具得到
`AK7755EN device_id=0x55 expected=0x55` 且退出码为 0。因此 R1 的 I2C1 `0x19`
已由器件定义的身份事务确认为 AK7755EN，而不只是由原厂 DT 名称推断。下一步先回归本版
四核、uptime >30 s、Wi-Fi 与 Bluetooth，再从原厂驱动/日志提取最小初始化和 DSP RAM
下载边界；在此之前不解除功放安全状态。
公开驱动溯源也已完成首轮审计：Rockchip 官方 `release-3.10`、`release-3.14`、
`release-4.4`、`develop-4.4` 及三个旧产品分支的完整 tree 均没有 AK7755；公开代码索引
也没有命中 R1 的 `rockchip,ak7755-audio`、`ak7755_pram_data2.bin` 或 CRC 日志。
用户新找到的 hello/kasa Linux 3.10 `ak7755.c` 改变了源码边界：它是 AKM 版权、
GPL-2.0-or-later 的完整旧式 ASoC codec driver，具有 I²C/SPI、`ak7755-AIF1`、
PRAM/CRAM/OFREG/ACRAM firmware download 和 CRC16。其 DT/DAI/文件名指纹与 R1 完全对应；
更强的实证是 R1 data2 的 `B8/B4/B2` 命令头与之相同，按其算法重算 PRAM/CRAM CRC
得到 `0x9916/0x4453`，恰好匹配原厂日志。因此它很可能是 R1 codec driver 的上游祖先。
代码仍有旧 ASoC API、全局状态、firmware 泄漏和把 CRC mismatch 当成功等 vendor-code
缺陷，不能整文件搬入 5.10。Ingenic SDK `8addc4a9...` 与用户给出的 IPC-SDK
`1986333e...` 则是同一家族的精简 OSS3 实现：后者头文件逐字节相同，C 文件只有五处
板级差异，二者都没有 firmware/CRC，继续只作为寄存器交叉参考。
用户补充的 Ambarella S2L `codec_ak7755.c` 又提供了 SPI mode 3、PDN 复位、采样率和 bypass
寄存器初始化样本，但同样没有 DSP RAM 下载或 ASoC，而且文件头明确为 Ambarella
confidential/proprietary；项目只记录固定提交和行为线索，不复制其代码或寄存器序列。
现已改以长期基线 Linux 6.18.42 实现只支持 R1 I²C 的最小 component：ID、受控 reset、
直接请求本地 data2 PRAM/CRAM、严格 size/命令头/CRC/错误返回和资源释放。A3 不注册 PCM
DAI/sound card，不带 SPI、misc ioctl、OFREG/ACRAM 或内嵌 DSP program，且通过 supply
dependency 始终保持 TPA3118D2 shutdown+mute。主机整核构建、最终 config/DTB、initramfs
固件清单和 FIT 子镜像逐字节比较均已通过；当前 DFU 默认已指向 13.7 MiB 的
`r1-linux-mainline-6.18-ak7755-fw-a3.itb`。A3 随后已在 RAM-only 实机通过：ID `0x55`、
PRAM `0x9916`、CRAM `0x4453` 全部由驱动和器件硬件 CRC 链确认；Wi-Fi 扫描得到 28 个
2.4/5 GHz BSS，Bluetooth LE 10 秒得到 20 个 report，四核 IPI 均活动。蓝牙管理初始化
发生于 31.867 秒且之后完整执行 10 秒扫描，因此已越过 30 秒稳定性门槛。下一步为 Audio
A4 主机候选现已完成：保持功放 shutdown+mute，加入受限 AK7755 DAI、I2S2 和专用 minimal
machine card；实机证据显示板上没有 codec MCLK，因此 CPU 只提供 BCLK/LRCK，AK7755 从
BICK 派生时钟。首版固定 48 kHz、stereo、S16、32fs，DSP 仍停止且禁止播放。整核、DTB、
14,339,592-byte FIT、config 和三个 FIT payload 的逐字节回读均已通过；默认 DFU 脚本已钉到
`r1-linux-mainline-6.18-ak7755-dai-a4.itb` 的 SHA-256
`245e705f07ad5d0ed585ad91e2a3b9c3379e199ca54205cba7bc7aa75ef32ba5`。A4 随后已由 RAM-only
实机验证核心链：正确版本后缀、PRAM/CRAM CRC、`RK_AK7755` card、双向 PCM、I2S2 四针
pinmux、12.288 MHz clock rate、功放 shutdown+mute 以及 CPU0-3 online 均符合设计。未打开
PCM stream 时 `sclk_i2s2` 被 gate、`hclk_i2s2_2ch` 保持工作，这是预期 idle 状态，不代表
时钟失败。本轮没有贴出 A4 自身的 uptime >30 s 或无线扫描结果，故这些仍列为待补回归，
不借用 A3 证据，也仍禁止播放声音。
原厂 data2 二进制没有公开再分发许可，仍只留在 `backup/` 和本地生成的 initramfs，不能
提交公开仓库。

为缩短重复进入 U-Boot 的时间，已确认不能简单把 SPL 与 ITB 再次拼接：当前 USB472 本来就是
39 KiB SPL 紧跟约 797 KiB FIT，而实机地址探针已经证明 MaskROM `0x472` 有效交付窗口只覆盖
SPL，后续 payload 不可靠。新的 RAM-only 加速候选改为 SPL 直接轮询 UART2 RX 寄存器，绕过
通用 console/scheduler 路径，目标是在 `--tx-gap-us 0` 下可靠传输；尚未实机，不替换默认链。

本轮已将两条学习线整理为可独立复习的专题文档：[Linux 内核裁剪方法论](acknowledge/kernel-trimming.md)
记录 v9→v10→v11 的白名单裁剪和失败审计；[ARMv7 启动链、OP-TEE 与 GICv2 调试](acknowledge/arm-boot-gicv2.md)
记录 BootROM/471/SPL/OP-TEE/U-Boot proper 的边界、PSCI/SMP/GICv2 基础，以及从次核路标到
secure INTID55/APR0 的完整定位和 clean kernel 验证。下一实机步骤是
`rescue-v11-emmc-a1` 的最小-DT/eMMC-DT RAM-only A/B，不改变 eMMC 内容。

早期主线 bring-up 基线中，Linux `6.18.42` 从 recovery 分区经原厂 `bootrk` 启动，修正为 `CONFIG_DEBUG_UART_VIRT=0xfed30000` 后完整进入 BusyBox 救援 shell。四核 Cortex-A7、512 MiB RAM、UART2 控制台和 Samsung eMMC HS200 均已实机工作；initramfs 没有自动挂载或修改存储。该旧链的四核和双核会在约 30 秒停止，单核已稳定超过 135 秒；watchdog、次核 idle、默认 RCU stall 窗口、停止前的 timer/IPI/RCU 失活、PSCI binding、NO_HZ、SMP timer migration、816 MHz ARMCLK 和完整上游外设 DT 均已排除。Linux `5.10.262` 使用同一最小 DT 和双核基线仍复现同类停止，证明不是 6.18 特有回归；后续 RAM-only 诊断已将当前混合链问题定位并修复为 RockUSB 遗留 GIC INTID 55/APR0，见上段最新状态。

受控回滚流程已实测：在 eMMC 中的 U-Boot 仍存活时，通过 PCB 按键进入 U-Boot Loader，在已验证的 USB 物理端口上将原始全零 BCB 写回绝对 LBA `0x008020`，立即读回比较后复位，Android 正常启动。真 MaskROM 下也已成功把匹配的 DDR/usbplug loader 下载到 RAM，串口进入 `UsbHook`，且没有写存储。独立 `TARGET_PHICOMM_R1` 已通过原厂 DDR 471 + 主线 SPL/U-Boot 472 的纯 RAM 链进入 U-Boot 2026.10-rc1 提示符，并从 eMMC 只读加载主线 recovery 中的 zImage、ramdisk 和 DTB，最终以单核进入 BusyBox 救援 shell。现代 U-Boot 原始 LBA 视图相对 Rockchip Loader 逻辑分区地址需要加 `FwPartOffset=0x2000` sectors。无 Trust OS 时保留多核 CPU 节点会在首个 `PSCI_CPU_ON` SMC 立即 panic；删除次核节点后启动正常。已核对 Armbian 的 RK322x box 链：它并未绕开 Trust，而是由 SPL 从 FIT 把带 RK322x PSCI 后端的开源 OP-TEE 装入 `0x68400000` 后再进入 U-Boot。社区记录的专有 Trust OS 30/60 秒 watchdog 冻结与 R1 现象高度吻合。最新 R1 开源 OP-TEE RAM 候选已修正 FIT shrink、外置 data-offset 和 FDT append 问题，实机得到 `FITF os=17 ret=0` 后停止在 OP-TEE 跳转边界附近；重启后已在干净的 `build/u-boot` 重建最小源码状态（8 个补丁按序重放 + `r1=CONFIG_TEXT_BASE` + 新增 `L/M/N/O/P/Q/R/T` 跳转路标补丁），修复了 binman `TEE` 变量为空导致 OP-TEE 数据缺失的问题。为绕过 MaskROM 472 交付窗口，当前 SPL 改由 UART 接收外置 FIT；实机已进入 YMODEM 接收并输出 CRC 请求 `C`。首传失败已在主机侧复现为发送端误用 `sz -Y`（它仍发 ZMODEM），尚未进入 FIT 解析；须改用真正的 YMODEM 发送器 `sb`。引导区全损坏后的实际写回恢复仍未完成；eMMC hardware boot0/boot1 已完成只读备份。完整工作现场见[交接记录](../handoff.md)。

### 已验证事实

| 项目 | 结果 | 证据或说明 |
|---|---|---|
| 串口 | `1500000 8N1`，无流控 | [完整冷启动日志](../backup/bringup_dmesg.md) |
| Android 串口 shell | `uid=2000(shell)`，SELinux `Enforcing`，无 `su` | [逆向学习记录](reverse-engineering-journal.md) |
| USB MaskROM | VID `0x2207`，PID `0x320b` | [USB 信息](../backup/00-usb-info.txt) |
| MaskROM RAM Loader | 匹配 DDR V1.06/Boot1 2.37 的 loader 已由 `db` 成功下载，串口进入 `UsbHook`；`rci/rid/rfi` 全部通过且未写存储。usbplug 因 `bcdUSB=0x0200` 被工具误标为 Maskrom | [逆向学习记录](reverse-engineering-journal.md) |
| 现代 U-Boot RAM 候选 | 原厂 DDR 471 + 主线 472 已从 RAM 进入 U-Boot 2026.10-rc1 交互提示符；512 MiB DRAM、DM 和 eMMC 已枚举 | [逆向学习记录](reverse-engineering-journal.md) |
| RK322x 开源 OP-TEE | 423,248-byte `rk322x_tee_os.bin` 已在 R1 由 SPL FIT 装入 `0x68400000`，PSCI v1.0 双核与四核 Linux 均进入 shell；双核用户确认约 700 秒，四核已直接记录到 72.92 秒 | [clean v8 首次 shell 日志](../build/artifacts/clean-v8-open-optee-first-shell-20260810.log) |
| R1 原厂 Trust OS 版本 | 提取自 trust 分区的 `r1-vendor-tee.bin`（332,232 B，SHA-256 `aecdf2b7...`）内部版本串 `1.0.1-54-g0d46013`，构建于 2016-09-29；OP-TEE 派生，比 rkbin v1.90/v2.00 都早。rkbin 的 "2.0"（`rk322x_tee_v2.00.bin`，2019-01-31）内部仍是 `1.0.1-86-g31e775b` | [逆向学习记录](reverse-engineering-journal.md) |
| 外置启动介质 | R1 PCB 没有 SD 卡槽，不能采用通用 RK322x 盒子的 SD 启动流程 | 用户实物确认 |
| USB 调试供电 | 必须使用原装电源；USB-TTL 仅接 GND/TX/RX，不能用其 5 V 给整机供电 | [逆向学习记录](reverse-engineering-journal.md) |
| SoC 返回信息 | `41 32 32 33`，ASCII 为 `A223` | [逆向学习记录](reverse-engineering-journal.md) |
| eMMC User Area | 15,269,888 个 512-byte 扇区，共 7,818,182,656 字节 | `backup/r1-emmc-user.img` |
| eMMC hardware boot0/boot1 | 已备份：`backup/boot/r1-emmc-boot0.img` / `r1-emmc-boot1.img`（4 MiB 各，逐字节相同，SHA-256 `70b4abfd87fa2e201ce17ddbf6886009ac9e70c64d2cc09880f61bef5604fdb9`）；内含 Rockchip loader（"RK32" 代码段 ~59 KB），BootROM 是否实际从 boot 分区启动未直接验证 | [逆向学习记录](reverse-engineering-journal.md) |
| Wi-Fi 固件 | 实际加载 `fw_bcm43455c0_ag.bin` | 启动日志与导出文件一致 |
| Wi-Fi 板级参数 | 实际加载 `nvram_ap6255.txt` | 启动日志与导出文件一致 |
| Bluetooth 固件 | 已导出 `BCM4345.hcd`，是否为实际加载文件仍待确认 | 原厂 system 镜像 |
| Bluetooth 控制 | UART1（`0x11020000`/`ttyS1`）；BT power 为 GPIO2_D5；RTS 为 GPIO3_A6 | 原厂 DTB、init 配置与启动日志 |
| AK7755 | 已导出并确认加载 `ak7755_pram_data2.bin`、`ak7755_cram_data2.bin`；同时导出 `ak7755_ofreg_data2.bin` | 启动日志与原厂 system 镜像 |
| AK7755 连接 | I2C1 地址 `0x19`，音频连接 I2S2（`0x100e0000`） | 原厂 DTB 与启动日志 |
| 主线内核基线 | Linux `6.18.42` LTS，ARM zImage 构建通过 | [主线 Linux Bring-up](mainline-bringup.md) |
| 最小主线 DTB | 25,020 字节，反编译后关键节点完整 | [主线 Linux Bring-up](mainline-bringup.md) |
| 救援 initramfs | 静态 32 位 ARM BusyBox，重复构建逐字节一致；当前默认 BusyBox 来自原厂 recovery并静态链接 bionic，故时区调用会提及 `ANDROID_DATA/ANDROID_ROOT` | [主线 Linux Bring-up](mainline-bringup.md) |
| U-Boot Fastboot | 可由 Android shell 进入；`secure: yes`、`unlocked: no` | [主线 Linux Bring-up](mainline-bringup.md) |
| 公开 R1 Root 教程 | 实质为从 LBA 0 写入第三方 3166 预 Root 整盘镜像，不是原系统内提权，也不会自动解锁 U-Boot Fastboot | [逆向学习记录](reverse-engineering-journal.md) |
| 原厂 recovery ADB | 已实机枚举；`adb root` 后 adbd 仍为 UID 2000，SELinux 阻止向 tmpfs/cache 推送；UART XMODEM 可向 RAM 传输 | [逆向学习记录](reverse-engineering-journal.md) |
| 主线 recovery 启动 | Linux 6.18.42 已启动到交互式 BusyBox shell；四核、RAM、UART2 和 eMMC HS200 已工作 | [主线 Linux Bring-up](mainline-bringup.md) |
| Linux 5.10 SMP 对照 | 5.10.262 最小-DT 双核实机仍复现约 30 秒全局停止，排除 6.18 特有回归；精确末尾日志尚未保存 | [主线 Linux Bring-up](mainline-bringup.md) |
| Android recovery 自动恢复 | `init.rc` 启动 `install-recovery.sh`，使用 `recovery-from-boot.p` 重建非原厂 recovery；首次测试因此启动回原厂 3.10 | [逆向学习记录](reverse-engineering-journal.md) |
| 受控实机回滚 | U-Boot 存活时，PCB 按键可进 Loader；恢复原始 misc BCB 并读回比较后，Android 已正常启动 | [逆向学习记录](reverse-engineering-journal.md) |

## 已完成阶段

- 接通并验证 UART，保存完整冷启动日志。
- 确认原厂 Android shell 的权限和 SELinux 限制。
- 焊接并调通 USB 数据连接，稳定进入 RK3229 MaskROM。
- 修正本地主机端 `rkdeveloptool` 对该设备 USB configuration 的兼容问题。
- 只读备份完整 eMMC User Area。
- 提取 `parameter-idb.img`、`uboot.img`、`trust.img`、`resource.img`、`kernel.img`、`boot.img`、`recovery.img` 和 `system.img`。
- 从 `system.img` 导出 Wi-Fi、Bluetooth 和 AK7755 固件，并用启动日志确认其中的实际加载项。
- 拆解 boot/recovery，提取 kernel、ramdisk、Rockchip resource、DTB、init、fstab 和 SELinux policy。
- 确认 boot/recovery 使用相同的 kernel 和 DTB，主要差异位于 ramdisk。
- 固定 Linux `6.18.42` LTS，并完成主机端 zImage 与 R1 最小 DTB 构建。
- 制作只挂载虚拟文件系统、不自动挂载或修改存储的最小救援 initramfs。
- 生成并离线拆包验证 `bootrk` RAM 启动候选镜像；该镜像大于原厂 boot 分区，明确禁止写入分区。
- 实机进入 U-Boot Fastboot 并完成只读 getvar；确认锁定状态会阻止 Fastboot RAM download。
- 核对公开 R1 Root 教程并排除直接采用：其核心是从 LBA 0 覆盖第三方预 Root 镜像，风险和写入范围均大于本项目需要。
- 原厂 recovery 已成功枚举为 ADB 设备；确认普通 `adb shell` 因默认 `/system/bin/sh` 缺失而失败，`adb root` 后 adbd 仍为 UID 2000，SELinux 阻止 ADB 向可用中转目录写入。
- 发现并修正旧 boot header 中 zImage/second 和解压内核/ramdisk 的地址重叠，生成精确 32 MiB 的 recovery 候选镜像。
- MaskROM recovery 写入与立即读回验证成功；确认先启动 Android 会自动恢复原厂 recovery，因此该次日志不是主线内核日志。
- 修正后的 Rockchip SHA 已获实机 U-Boot 接受，misc BCB 已验证能强制直接进入 recovery，启动控制权到达 zImage 入口。
- decompressor UART 实机输出已证明 zImage 自搬移和解压完成，排除厂商固定 `0x62000000` 装载与输出区重叠为当前直接根因。
- 在 U-Boot 仍存活的前提下，通过 PCB 按键 Loader 恢复原始 misc BCB，读回校验后 Android 正常启动；recovery/BCB 受控回滚链已验证。
- 修正 DEBUG_LL UART 虚拟映射为 `0xfed30000`，主线 Linux 6.18.42 已从 recovery 完整启动到救援 shell。
- Linux 5.10.262 最小-DT 双核候选已上板并复现同类约 30 秒停止，内核版本二分路径结束。
- 真 MaskROM 下匹配 DDR/usbplug loader 的纯 RAM `db` 下载成功，建立了不依赖 eMMC U-Boot 的 USB 执行入口。
- 原厂 DDR 471 + 主线 SPL/U-Boot 472 的纯 RAM 链已实机进入 U-Boot 2026.10-rc1 提示符；正式 OP-TEE 仍待加入。
- 实机运行 hybrid 候选，确认原厂 DDR 471 成功、完整主线 472 在首条输出前停止；生成并解包验证 68-byte 472 入口探针。
- 实机验证最小 472 入口探针成功执行，排除 BootROM 交接问题；生成并解包验证主线 SPL 最早期 UART 路标候选。
- 实机用路标把首个 SPL 停止点定位到 `save_boot_params()`；生成 phase-aware weak-stub A/B 修正版并完成解包验证。
- 实机验证 phase-aware 修正有效并推进至 `SRM0123`；定位 NS16550 `TEMT` 等待，生成复用 471 UART 的 skip-init 候选。
- 审计并停止使用 RK3229 EVB 派生候选；建立独立 Phicomm R1 target、512 MiB/uart21 最小 DT 和禁用存储写能力的 RAM 诊断构建。
- 实机确认 R1 专用候选仍停在 `SRM0123`，排除 EVB pinmux 假说；生成并离线验证完全绕过 `debug_uart_init()` 调用的 A/B 候选。
- 实机验证绕过 `debug_uart_init()` 后推进到 `SRM012345`；生成并离线验证 `board_init_f()` 内部 A–K 路标候选。
- 实机确认 `board_init_f()` 路标停在 `B`，定位到 `spl_early_init()`；生成并离线验证 malloc/DTB/DM 小写路标候选。
- 实机得到 `SRM012345ABab`，定位到 `fdtdec_setup()`；生成并离线验证运行时 DTB magic 探针。
- 实机确认运行时 DTB magic 为错误值 `n`；生成并离线验证 DTB 32 位值十六进制打印探针。
- 重启后在干净 `build/u-boot` 重建最小 R1 源码状态：8 个补丁按依赖顺序重放、修复 `spl-optee-return-address` 补丁格式、新增 `optee-jump-breadcrumbs` 补丁（`L/M/N/O/P/Q/R/T`），按固定 commit 重新获取并验证 OP-TEE blob，修复 binman `TEE` 变量为空问题，生成并离线逐字节验证复现版与跳转路标版两个新 loader。

## 当前阶段与下一步

当前进入主线基础硬件验证阶段：

1. Fastboot 已确认锁定，不执行可能擦除 userdata 的 unlock。
2. 已通过反汇编排除 `upgrade_tool RUN`：它不发送本地镜像内容，只描述存储 offset 与 size。
3. 原厂 recovery ADB 权限已确认不足；UART XMODEM RAM 中转已验证，不再尝试通过 ADB sync 或切换 SELinux permissive。
4. 厂商 `bootrk` 固定把 zImage 放到 `0x62000000`，decompressor 已实证能自搬移并完成解压，主线内核现已进入 shell。
5. PCB 按键 Loader、eMMC 查询、BCB 读写回滚和 Android 恢复已实测闭环；ASM1074 Hub 重枚举会改变 LocationID，刷写脚本现要求唯一 Loader 并复核 SoC、eMMC ID 和容量，可选用 `R1_LOCATION_ID` 锁定位置。
6. DEBUG_LL UART 虚拟地址 `0xfed30000` 已实机验证，8250 驱动能平滑接管同一 UART2 控制台。
7. 最小原厂同构 DT 仍在约 30 秒停止，排除完整上游 `rk322x.dtsi` 外设节点冲突。tracefs 试验因极简 shell 缺少 `[` 命令而没有启用任何事件，随后阻塞在 `trace_pipe`，不能作为冻结证据。
8. Linux `5.10.262-phicomm-r1` 最小-DT 双核对照已复现同类停止，排除 6.18 特有回归。R1 没有 SD 卡槽，不能直接采用 Armbian 的 SD 启动链。
9. 真 MaskROM 下独立 `TARGET_PHICOMM_R1` 已进入现代 U-Boot 提示符；开源 OP-TEE FIT 曾得到 `FITF os=17 ret=0` 并推进到 OP-TEE 跳转边界。因 472 交付窗口限制，当前采用瘦身 SPL + UART YMODEM 外置 FIT：实机已到 `Trying to boot from UART` 并输出 `C`，证明接收端已就绪。首传的 `sz -Y` 实际发出 ZMODEM `rz` 前导，故被 YMODEM 接收端 NAK；本机伪串口已复现，属于主机协议错误而非 R1、FIT 或 OP-TEE 失败。下一次仅用串口终端的本地命令功能（必须独占同一串口，1500000 8N1、无流控）运行 `sb -k -vv build/artifacts/r1-ymodem-fit.itb`；成功标准首先是 `Loaded 796672 bytes`，随后记录 `sm`、`L/M/N/O/P/Q/R/T`。全程只执行 RAM `db`，不覆盖 parameter/idb、U-Boot、trust。
10. YMODEM 已实机完整传入 FIT，开源 OP-TEE `3.7.0` 已初始化并提供 PSCI v1.0。secure SPL 证明 MaskROM/RockUSB 在进入 TEE 前遗留唯一 active 的 USB OTG INTID 55 与 APR0=`1`；精确清理后 `GC` 为 RPR=`0xff`、APR0=`0`、无 active 位。clean v8 随后正常越过旧 `No ATAGs?` 边界，CPU0/CPU1 online，于 2.078 秒执行 `/init` 并进入 shell；用户确认 uptime 约 700 秒。原 CPU0 IRQ/SGI 死锁及当前 RAM-only SMP 冻结已解决。
11. clean 四核 v9 已用修正后的 INTID55 cleanup SPL 实机通过。白名单救援 v11 的最小-DT A 线四核正常，完整 eMMC DT 的 B1/B2 都只读枚举成功但 CPU1–CPU3 未 online；B2 已否定 arch-timer，C2 又否定 Cortex-A7 814220 单变量。C1 用同一完整 B2 DT/initramfs 换回 multi_v7 v9 后四核与 eMMC 同时通过，现作为外设工作基线；B3 CRU A/B 延后到最小化阶段。
12. 已从 C1 构建 USB Host A1，但用户确认成品没有可用 USB 外设口，该支线已降级为 SoC/DFU 研究产物；板载 SDIO Wi-Fi、UART Bluetooth（含 BCM4345C0 HCD build 0124、AES/CMAC）均已实机通过。
13. eMMC A5 常驻开源启动链已经写后读回并冷启动通过；Linux 仍只经 U-Boot DFU 装入 RAM。Linux 6.18.42 Audio A3 已实机确认 AK7755EN ID、PRAM/CRAM 硬件 CRC、四核、>30 s、2.4/5 GHz Wi-Fi 和 Bluetooth LE 全部通过。下一步为 A4 DAI/I2S2/machine-card 枚举，功放继续 shutdown+mute，不写 eMMC、不播放音频。

## 文档导航

| 文档 | 用途 |
|---|---|
| [当前工作交接](../handoff.md) | 当前卡点、重启后的源码状态、关键产物、安全边界和下一步最小操作 |
| [项目说明](../README.md) | 总体目标、预期硬件、软件方案和开发阶段 |
| [任务清单](../TODO.md) | 各阶段待办与完成状态 |
| [Bring-up 手册](bringup.md) | 串口、原厂系统采集、备份和早期启动操作 |
| [主线 Linux Bring-up](mainline-bringup.md) | 6.18 LTS、最小 DTS、initramfs、构建与首次启动边界 |
| [逆向清单](reverse-engineering.md) | Wi-Fi、Bluetooth、AK7755、DTB 和 init 配置的调查项目 |
| [逆向学习记录](reverse-engineering-journal.md) | 按时间记录实际操作、失败、证据、结论和经验 |
| [系统架构](architecture.md) | 目标启动链、内核、连接、音频和服务设计 |
| [音频子系统](audio.md) | AK7755、ASoC、PipeWire 和音频调试顺序 |
| [Recovery 前置知识](acknowledge/recovery.md) | Android / Rockchip recovery 模式的基础知识 |
| [SELinux 前置知识](acknowledge/selinux.md) | SELinux 概念、AVC 解读与绕过策略 |
| [U-Boot 前置知识](acknowledge/uboot.md) | U-Boot 架构、配置、移植与调试 |
| [ARMv7 启动链、OP-TEE 与 GICv2 调试](acknowledge/arm-boot-gicv2.md) | BootROM/471/SPL/OP-TEE/U-Boot proper 分工、PSCI/SMP/GICv2 原理、INTID 55 实战与面试问答 |
| [Linux 内核裁剪方法论](acknowledge/kernel-trimming.md) | Kconfig/Kbuild、白名单裁剪、依赖审计、实机验收、R1 v10 反例与面试表达 |
| [主机侧调试工具](acknowledge/debug-tools.md) | strings/objdump/nm/libfdt/pack-unpack 等工具的场景、用法与判读 |
| [Firmware 说明](../firmware/README.md) | 原厂固件的本地保存原则 |
| [Patches 说明](../patches/README.md) | 后续补丁的组织方式 |

## 本地证据与产物

以下文件用于本机研究，通常不应提交到公开仓库。

| 路径 | 内容 |
|---|---|
| `backup/bringup_dmesg.md` | 原厂系统完整冷启动串口日志 |
| `backup/00-device-maskrom.txt` | MaskROM 设备枚举结果 |
| `backup/00-usb-info.txt` | USB 描述符信息 |
| `backup/r1-emmc-user.img` | 完整 eMMC User Area 只读备份 |
| `backup/partitions/` | 从整盘备份提取的关键分区镜像 |
| `backup/extracted/system/` | 从原厂 `system.img` 导出的文件树 |
| `backup/unpacked/boot/` | boot kernel、ramdisk、resource、DTB 和反编译 DTS |
| `backup/unpacked/recovery/` | recovery kernel、ramdisk、resource、DTB 和反编译 DTS |
| `firmware/rk322x_loader_v1.06.237.bin` | 匹配原厂 DDR V1.06/Boot1 2.37；已在真 MaskROM 下通过 `db` 下载并进入 RAM usbplug |
| `build/artifacts/r1-mainline-recovery.img` | 精确 32 MiB 的 Linux 5.10.262 最小-DT 双核对照 recovery；已离线验证并上板复现同类停止 |
| `build/artifacts/r1-hybrid-mainline-uboot-ram-prompt.bin` | 原厂 DDR 471 + 主线 SPL/U-Boot 472 的纯 RAM 提示符候选；实机 471 成功、472 无输出，不含 OP-TEE |
| `build/artifacts/r1-uart472-entry-probe-loader.bin` | 原厂 DDR 471 + 68-byte UART2 入口探针；已实机打印 `R1 472 ENTRY OK`，证明 BootROM 能进入 472 |
| `build/artifacts/r1-spl-uart-breadcrumbs-loader.bin` | 原厂 DDR 471 + 带 `S/R/M/0–6` 路标的主线 SPL/U-Boot 472；实机只到 `S`，定位于 `save_boot_params()` |
| `build/artifacts/r1-spl-phase-fix-loader.bin` | phase-aware `save_boot_params` 修正 + UART 路标；实机推进到 `SRM0123`，验证修正有效 |
| `build/artifacts/r1-spl-uart-skip-init-loader.bin` | 历史 EVB 派生诊断产物；虽已解包验证，但因板级 DT 来源不符而停用，禁止继续上板 |
| `build/artifacts/r1-phicomm-r1-uboot-ram-debug-loader.bin` | 独立 Phicomm R1 target + 512 MiB/uart21 DT + 只读命令面；实机仍停在 `SRM0123`，保留为对照 |
| `build/artifacts/r1-phicomm-r1-uboot-ram-debug.config` | 上述 R1 专用候选的完整 U-Boot 配置 |
| `build/artifacts/r1-phicomm-r1-uboot-bypass-debug-init-loader.bin` | R1 专用、纯 RAM、直接从汇编绕过 `debug_uart_init()` 空调用的下一 A/B；已解包逐字节验证 |
| `build/artifacts/r1-phicomm-r1-uboot-board-init-trace-loader.bin` | R1 专用、纯 RAM、带 `board_init_f()` A–K 内联 UART 路标；实机停在 `B`，保留为对照 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-common-trace-loader.bin` | R1 专用、纯 RAM、细分 `spl_early_init()` 的 malloc/DTB/DM 小写路标；实机停在 `ab`，保留为对照 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-dtb-memory-probe-loader.bin` | R1 专用、纯 RAM、直接检查运行时 `__bss_end` DTB magic；实机得到 `n`，保留为对照 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-dtb-hex-loader.bin` | R1 专用、纯 RAM、打印运行时 `__bss_end` 的实际 32 位值；已解包逐字节验证，当前下一候选 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-dtb-location-loader.bin` | 已否定：强制 `+0x3c4` 后实机得到 `smMissing DTB`，仅保留为失败证据 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-dtb-generic-loader.bin` | R1 专用、纯 RAM、官方 `boot_merger` 封装；实机已进入 U-Boot 2026.10-rc1 交互提示符 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-os-loader.bin` | R1 原厂 DDR 471 + 开源 RK322x OP-TEE + R1 主线 SPL/U-Boot；实机在跳入 OP-TEE 前因无效 FDT 停止，保留为失败对照 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-loader.bin` | 最后实机 OP-TEE 路标候选；已得到 `FITF os=17 ret=0` 并推进到 OP-TEE 跳转边界，当前文件 SHA-256 `169ead28ed3f8a7e658557a834ea102d62d6857d390808d8c8c417a398bcba12` |
| `build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-169ead28.bin` | 上一条的历史冻结副本（带 SHA 前缀，防止重建覆盖） |
| `build/artifacts/r1-phicomm-r1-uboot-optee-fit-repro-loader.bin` | 重启后重建的复现版（无新逻辑）：880,917 B，SHA-256 `3b45373a...`，离线逐字节验证 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-loader.bin` | 重启后重建的 OP-TEE 跳转路标候选；实机复现历史 `SRM012345ABab` 停止（缺 DTB 探针补丁），已废弃为证据，SHA-256 `b41a2955...` |
| `build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-dtbprobe-loader.bin` | 已废弃：DTB 落在 471 训练暂存区（0xE000-0xF900）被覆盖，实机 `sn782e54f1`，SHA-256 `ec7f650d...` |
| `build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-ddrpad-loader.bin` | 已废弃：8 KB 填充不足（471 数据区延伸远超 0xF940），实机 `sn8f6642af` |
| `build/artifacts/r1-phicomm-r1-uboot-vendor-tee-v2.00-ddrpad-loader.bin` | 已废弃：同上，SHA-256 `5e4eb78f...`（重建版） |
| `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-loader.bin` | 当前实机 472：SPL 瘦身（`__bss_end` 0x9200）+ UART YMODEM 交付链，852,245 B，SHA-256 `8031e57044ae0f2506bb77d5c022534adf989d23e85da373918a4469cfc08434`；已实机到达 YMODEM CRC 请求 `C` |
| `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxtrace-loader.bin` | RAM-only YMODEM 接收诊断版：超时会输出 `R1XM timeout stage=0..5`；854,289 B，SHA-256 `c78ed30e668d3085c415f04d66373929f7f938934fcaa698b9cb60878df3c3e6`；待实机验证 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxfix-loader.bin` | 当前 RAM-only YMODEM RX 修正版：取消 `DEBUG_UART_SKIP_INIT`，SPL 显式初始化 UART2-1；854,289 B，SHA-256 `d23b86fd5e2d54e808b99880b1ede71917e6f9237e1270903bd9d93262fcab98`；待实机验证 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxfast-loader.bin` | RAM-only YMODEM 接收优化版：UART2-1 显式初始化 + 连续 payload 不再逐字节 `schedule()`；854,289 B，SHA-256 `7220f8b7508cca136d87ba33098b8bf2851d9a1343a5442568ce2a8414aeda3f`；配合主机每字节 50 µs 节流已完整接收 FIT |
| `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-pretee-trace-loader.bin` | RX-fast 的 secure GIC 只读探针；实机在 cache cleanup 前后都得到 RPR=`0`、APR0=`1`、仅 ISACTIVER1 bit 23（INTID 55）active，证明异常早于 OP-TEE。854,293 B，SHA-256 `05b804d7bfdbd403c187f379dd23e332e808d75e8c0dc99d85c0cccea9f8d810`，CRC32 `f3c48aa7`。 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-int55-cleanup-ab-loader.bin` | secure GIC 精确清理 A/B：仅在完整异常签名匹配时清理 INTID 55/APR0。实机 `GC` 恢复 RPR=`0xff`、APR0=`0`、`V-`，并使 clean v8 双核进入 shell。854,293 B，SHA-256 `ff47e369966feac248510aaa7577e54484e3ecfb80a53fef0f99d818d087bd50`，CRC32 `7ff26f3d`。 |
| `build/artifacts/r1-ymodem-fit.itb` | 待发送的 A 线 FIT（开源 OP-TEE）：796,672 B，SHA-256 `eb848a90f15d7fa29854d635916478801193b1b5827f293f8157d831bc368493`；含 U-Boot 345,128 B、OP-TEE 423,248 B 和 R1 FDT 25,752 B |
| `build/artifacts/r1-ymodem-fit-dtb.itb` | A 线 FIT：U-Boot loadable 改为附带 DTB 的 371,360 B 镜像；822,318 B，SHA-256 `5687f549a82d3f2e0b51fe064df05a4b623ba180872c581132e6ecbe6a49cd84`；实机已完成开源 OP-TEE → U-Boot proper 交接 |
| `build/artifacts/r1-ymodem-fit-dtb-rk-v2.00.itb` | B 线 FIT：仅将 A 线开源 OP-TEE 替换为 Rockchip 官方 RK322x TEE v2.00，U-Boot proper/DTB 逐字节相同；v7 实机同样得到 CPU0 RPR=`0x00`、CPU1=`0xff`，排除开源 OP-TEE 3.7 独有错误。732,994 B，SHA-256 `4ebc55f53e998d85da7dd6d812935dd87818c6953eb7dea996fa4b882019ab7e`。 |
| `scripts/boot-r1-optee-uboot.py` | 一键 RAM-only 启动器：先独占串口、只执行 `rkdeveloptool db`、等待 SPL 的 YMODEM `C` 再以 50 µs 节流发送已校验 FIT；不执行 eMMC 写入；发送完成默认立即释放串口并通知 Fedora/niri 桌面会话 |
| `scripts/generate-clangd.sh` | 从 Kbuild `.cmd` 刷新 Linux 6.18 与 5.10 的 clangd `compile_commands.json`；只读解析既有构建命令，不编译、不访问设备 |
| `scripts/clangd-call-tree.py` | 通过 clangd LSP 将内核函数 incoming/outgoing 调用层级导出为可复制 Markdown；不执行目标代码 |
| `build/artifacts/rk3229-phicomm-r1-minimal-psci-v1.dtb` | 仅改变 PSCI 为 `arm,psci-1.0`, `arm,psci-0.2` 的 Linux 5.10 最小-DT A/B；1,790 B，SHA-256 `85605813b11b6b8744de044f3e954809c0b1baf93eb764acd016830cd4653436` |
| `build/artifacts/zImage-5.10-psci-v1-secondary-trace` | Linux 5.10.262、PSCI 1.0/0.2 最小 DT 对照；含 CPU1 `A`–`V`、CPU0 completion/unpark 路标和 CPU1 reschedule SGI `i/j/k`，10,162,688 B，SHA-256 `7cf6ee88e81e465a9a85ca731f64c3fdac2b8f9042b02de73791ba0e8db539a9`；仅 RAM YMODEM 测试 |
| `build/artifacts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dtb` | GIC-only DT A/B：仅改为上游 RK322x `arm,gic-400` 与四段 GIC 窗口；1,798 B，SHA-256 `3a8b8685652f39690edc2141454a7f397ced99f4563975a84a1f2c39fd660a12`；仅 RAM YMODEM 测试 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-sgi-trace` | Linux 5.10.262 GIC/CPU-hotplug 下一轮诊断内核：新增 `4`–`8`、`d/e`、`h/l`、`W/X` 路标；10,162,688 B，SHA-256 `ba6c79afc2f3672f48fe3badd4d795d722930fe7b4398b33e889fc869b8a1372`；仅 RAM YMODEM 测试 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-kdevtmpfs-create-trace-v2` | Linux 5.10.262、仅跟踪 `kdevtmpfs` 创建路径的候选：`M`–`T` 分别标记请求、`kthreadd`、子线程及 completion 返回；保留正常可睡眠 completion。10,162,688 B，SHA-256 `bc405e07b076aba3745cd4ac534dbb776d8df259b5aed36ca206ad364c3cbd0b`；待 RAM YMODEM 实机测试。 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-kdevtmpfs-cpu0-ipi-trace-v3` | Linux 5.10.262 `kdevtmpfs` 下一候选：附加各任务 CPU 编号、`complete()` 返回点及 CPU1→CPU0 CALL_FUNC/RESCHEDULE IPI/GIC 路标，所有等待保持正常实现；10,162,688 B，SHA-256 `b115261f40db73b3a36625875d7ab649a523a10a874c7cd7492c21a1fc973840`；待 RAM YMODEM 实机测试。 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-cpu1-to-cpu0-sgi-filter-ab-v4` | Linux 5.10.262 反向 SGI A/B：实机 `D01fE` 证明绕过 target map 后仍无 CPU0 handler，已排除 `gic_cpu_map[0]`/target-list 编码；10,166,784 B，SHA-256 `e33874dfcf8c39352a813c4e5df8c0f5c15e8ede55b68fa7e1ad9b6755875c58`，CRC32 `71b38bb3`。 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-register-snapshot-v5` | Linux 5.10.262 GIC 接收侧诊断：保留 v4，并在 CPU0/CPU1 GIC 初始化前后输出 IGROUPR0、SGI/PPI enable/pending/active、GICD/GICC control 与 PMR；10,166,784 B，SHA-256 `5ebe13a90b52f104a45e3822506b1e97a71da376be2732a67af238dedf638bcd`，CRC32 `f118e436`；待 RAM-only 实机测试。 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-register-printk-v6` | v5 的可靠输出修正版；实机四组快照证明 CPU0/CPU1 normal-world 可见 pre/post 状态完全一致，Linux 仅把两核 GICC_PMR 同样从 `0` 设为 `0xf0`；排除可见 GIC 初始化差异。10,166,784 B，SHA-256 `33284f675c48e0e7753163829a443ee5d0ac44d00682b7de627e812032b7ab2c`，CRC32 `b53f1e7a`。 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-psci-wait-state-v7` | 实机证明 CPU0 IRQ unmasked、PPI 30 pending/HPPIR=`0x1e`，但 GICC_RPR 从 Linux 最早期到等待前始终异常为 `0x00`；CPU1 RPR 正常为 idle `0xff`。根因高度收敛到 OP-TEE 主核遗留 secure active interrupt/APR，阻挡普通 PPI/SGI 抢占。10,166,784 B，SHA-256 `533f4c8b2f2354b6ff3425cae92eaac40ba0a5f0d46389f2c954d9f3371fe3bb`，CRC32 `712226dd`。 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-clean-post-int55-v8` | 从未修改的 Linux 5.10.262 commit `065a677fad98` 独立构建的清洁对照；不含 UART 路标、polling completion 或 SGI filter A/B。实机经 INTID55 cleanup loader 双核进入 shell，用户确认 uptime 约 700 秒。10,166,784 B，SHA-256 `fc5d1e207ffc143c2d34cb59296f0e9b07b3051e7e085404f37873e2d85cd5e7`，CRC32 `a262021a`。 |
| `build/artifacts/zImage-5.10-psci-v1-gic400-clean-4core-v9` | clean v8 的四核单变量 A/B；实机确认 CPU0–CPU3 online、uptime 到 72.92 秒且四核 IPI 均有增长。10,158,592 B，SHA-256 `5bc8624169e60ef4558cc78cb80ae887a2a4f798c2a824a9afecece29d3c2565`，CRC32 `14f97549`。 |
| `build/artifacts/zImage-5.10-r1-rescue-minimal-4core-v10` | 已否定的过度裁剪候选：缺 `BINFMT_SCRIPT` 导致 `/init` `ENOEXEC`，缺 `ARM_PSCI` 导致只启动 CPU0。2,328,112 B，SHA-256 `e17c84d45130124a2c453be8f5b2bd7f46238bf0dd9744106c27cf1979e95333`；仅保留为失败证据。 |
| `build/artifacts/zImage-5.10-r1-rescue-baseline-4core-v11` | 修正 v10 过度裁剪的下一候选：补回 PSCI、脚本格式、标准时间/事件/同步 ABI、RTC 和救援诊断接口，仍不含 CAN/NFS/NTFS/PCI/MTD/图形/声音。2,721,856 B，SHA-256 `997fcf92c97a6f623650daea226df40179684827f1ce3bce89ef3b449037cb7b`；待上板。 |
| `build/artifacts/zImage-rescue-v11-emmc-a1` | 冻结救援核心后的首个外设分层内核；最终配置相对 v11 只有版本后缀变化。2,721,840 B，SHA-256 `5013b84d149e431f0c2886be8b4d0f8e0aea2e96acdd0de08b058332cb23861b`；配最小 DT 的 A 线已由用户确认四核、虚拟文件系统、shell 和 uptime >30 秒正常。 |
| `build/artifacts/rk3229-phicomm-r1-rescue-v11-emmc-a1.dtb` | eMMC-only DT 候选：PSCI v1.0/v0.2，eMMC 8-bit/RK805 前置链启用，USB PHY/OTG 禁用。22,629 B，SHA-256 `7d343820991e8bc7592114c303e7a663b719e5c1015417bd1e044038d30635bc`；仅静态审计，待实机。 |
| `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-int55-cleanup-directrx-loader.bin` | 保留已验证 INTID55/APR0 cleanup，仅将 SPL YMODEM RX 改为直接轮询 UART2 寄存器的无节流候选。854,293 B，SHA-256 `ee8466d7217c5b9d52990d6c4393d32c4aeb8c63f92cdddfc6b02eb4e06e3015`；仅主机构建/解包验证，待 RAM-only 实机。 |
| `build/artifacts/clean-v8-open-optee-first-shell-20260810.log` | clean v8 经开源 OP-TEE 的首次双核 shell 日志；直接记录到 uptime 8.99 秒，用户另确认约 700 秒。15,438 B，SHA-256 `035930590c099a04285d6ee2db955d156e63e75fc26e6d150eb6097469d960e1`。 |
| `build/artifacts/r1-phicomm-r1-uboot-vendor-tee-v2.00-loader.bin` | 已废弃：无填充版 B 线，实机同遭 DTB 覆盖，SHA-256 `a4e3c7f6...` |
| `build/tee/rk322x_tee_v2.00.bin` | 官方 v2.00 TEE blob（333,896 B，SHA-256 `a568cba0...`），专有二进制，不提交仓库 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace.config` | 上述跳转路标候选的完整 U-Boot 配置（含 `SPL_LOAD_FIT_FULL`、`SPL_FIT_IMAGE_TINY`） |
| `build/tee/rk322x_tee_os.bin` | 按固定 commit 重新下载并验证的开源 OP-TEE（423,248 B，SHA-256 `ff56bb3b...`）；构建时复制为 `build/u-boot/tee.bin`，不提交仓库 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-os.config` | 上述开源 OP-TEE RAM-only 候选的完整 U-Boot 配置 |
| `build/artifacts/mainline-first-shell-20260805.log` | 主线 Linux 6.18.42 首次进入救援 shell 的完整串口日志 |
| `build/artifacts/r1-linux-mainline-6.18-ak7755-fw-a3.itb` | 已实机通过的 RAM-only Audio A3：Linux 6.18.42、四核/无线回归配置、无 DAI 的 AK7755 ID + PRAM/CRAM 严格 CRC verifier；13.7 MiB，SHA-256 `3b5a4d788f7f66ab57c5dfc62d554b89754594c5a8473be2aff82a63a8e4679f` |
| `build/artifacts/r1-linux-mainline-6.18-ak7755-dai-a4.itb` | 已实机通过 Audio A4 核心链：AK7755 + I2S2 + 专用 machine card，固定 48 kHz/stereo/S16/32fs，card/PCM/pinmux/clock/safe GPIO/四核在线已验证；DSP stopped、功放 shutdown+mute、禁止播放。14,339,592 B，SHA-256 `245e705f07ad5d0ed585ad91e2a3b9c3379e199ca54205cba7bc7aa75ef32ba5`；A4 自身 >30 s 与无线回归待补。 |

## 安全边界

- recovery/BCB 回滚已通过实机验证，但真 MaskROM 裸机恢复尚未验证；在此之前不擦除或覆盖 parameter/idb、U-Boot、trust 及未备份的 eMMC hardware boot 区。
- 任何写入前都要重新确认设备、存储介质、扇区偏移、长度、镜像来源和可用备份。
- `UL`、`WL`、`EF`、`GPT`、`PRM` 等写入或擦除操作必须得到针对具体命令和目标的明确确认。
- 不公开提交整盘镜像、分区镜像、设备唯一信息或原厂专有固件。

## 文档维护规则

每完成一个有明确产物或验证结论的阶段，同一轮工作中应：

1. 将过程、失败、观察和经验追加到[逆向学习记录](reverse-engineering-journal.md)。
2. 更新本文档的当前状态、已完成阶段和下一步。
3. 更新[任务清单](../TODO.md)中的对应复选框。
4. 若确认了新的硬件、配置或操作事实，更新对应专题文档。

记录时区分“已验证”“推断”和“待确认”，不要把 RK322x 通用资料直接写成 R1 实机事实。

## 建议阅读顺序

首次了解项目：项目说明 → 本索引 → 逆向学习记录 → Bring-up 手册。

准备继续实机工作：本索引的“当前阶段与下一步” → 任务清单 → 对应专题文档 → 原始备份证据。
