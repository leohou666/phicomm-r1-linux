# TODO

## P0：必须先完成

- [x] 保存完整冷启动日志
- [x] 完整备份 eMMC
- [x] 验证 U-Boot Loader 仍存活时的 recovery/BCB 回滚流程
- [ ] 验证真 MaskROM 状态下不依赖 eMMC U-Boot 的恢复流程
- [x] 确认并备份 eMMC hardware boot0/boot1（两者相同，SHA `70b4abfd...`，见 `backup/boot/`）
- [x] 稳定 MaskROM USB 链路并通过连续通信/读回测试
- [x] 提取原厂 DTB
- [ ] 提取 `/proc/config.gz`
- [ ] 保存 `/proc/partitions`、`/proc/mtd`、`lsblk` 输出
- [x] 提取 Wi-Fi / Bluetooth 固件
- [x] 提取音频配置和 AK7755 相关数据

## P1：启动系统

- [ ] 确认 RK3229 时钟、GPIO、pinctrl 和 regulator
- [x] 制作最小 initramfs
- [x] 核对 U-Boot Fastboot 锁状态
- [x] 评估公开 R1 预 Root 整盘镜像方案
- [x] 检查原厂 recovery ADB 权限
- [x] 修正主线 boot image 内存地址重叠
- [x] 生成精确 32 MiB recovery 候选镜像
- [x] 验证 recovery 定点写入与立即读回
- [x] 定位 Android 自动恢复原厂 recovery 的原因
- [x] 验证修正后的 Rockchip SHA 能通过 U-Boot 校验
- [x] 使用 misc BCB 强制直接启动 recovery
- [x] 验证 zImage 自搬移和解压完成
- [x] 定位解压后 ARM 内核最早期停机点（DEBUG_LL UART 虚拟地址落入用户区）
- [x] 实机验证修正后的 DEBUG_LL UART 虚拟映射
- [x] 验证 RAM-only 启动路径（MaskROM `db` → vendor DDR 471 → 主线 SPL/U-Boot → 开源 OP-TEE → 双核 Linux shell）
- [x] 从 recovery 启动主线内核并进入救援 shell
- [x] 构建可重复的 Linux 5.10.262 最小-DT SMP 对照镜像
- [x] 实机验证 Linux 5.10.262 双核是否仍在约 30 秒停止
- [x] 在真 MaskROM 下验证匹配 DDR/usbplug loader 的纯 RAM `db` 下载
- [x] 只读验证 RAM usbplug 的芯片信息、eMMC ID 与容量
- [x] 生成并离线验证原厂 DDR 471 + 主线 SPL/U-Boot 472 的 RAM 提示符候选
- [x] 实机确认 hybrid 候选的原厂 DDR 471 成功、完整主线 472 在首条串口输出前停止
- [x] 用最小 UART 探针验证 BootROM 是否进入 472
- [x] 运行主线 SPL 最早期 UART 路标候选并定位到 `save_boot_params()`
- [x] 实机 A/B 验证 phase-aware `save_boot_params` 修正版
- [x] 审计并停止使用继承 RK3229 EVB 板级 DT 的候选
- [x] 建立独立 Phicomm R1 U-Boot target、512 MiB/uart21 DT 和禁写诊断配置
- [x] 实机验证 R1 专用 RAM U-Boot 路标候选并通过绕过 `debug_uart_init()` 推进至 `SRM012345`
- [x] 运行 `board_init_f()` A–K 路标候选并将停止点定位到 `spl_early_init()`
- [x] 运行 `spl_early_init()` malloc/DTB/DM 小写路标候选并定位到 `fdtdec_setup()`
- [x] 运行 `__bss_end` DTB magic 内存探针并确认运行时 magic 错误
- [x] 读取并解释运行时 `__bss_end` 32 位值，确认错误值来自非官方 Loader 封装
- [x] 实机验证 RAM 候选进入现代 U-Boot 提示符
- [x] 核对 Armbian RK322x box 的 OP-TEE/FIT 链并定位开源 `rk322x_tee_os.bin`
- [x] 重启后在干净 `build/u-boot` 重建最小 R1 源码状态（补丁按序重放 + 跳转路标补丁，本地 commit `b0531571496`）
- [x] 按固定 commit 重新获取并验证 OP-TEE blob，修复 binman `TEE` 变量为空导致的 OP-TEE 数据缺失
- [x] 生成并离线逐字节验证两个新 loader（复现版 + `L/M/N/O/P/Q/R/T` 跳转路标版）
- [x] 实机 `db` 运行 UART YMODEM SPL，并确认串口到达 `Trying to boot from UART` 与 CRC 请求 `C`
- [x] 从 bridge 的 raw 板端日志确认首传在 YMODEM block 0 前超时，并生成含接收 stage 0–5 诊断的 RAM-only SPL loader
- [x] 以双向 raw 日志确认主机正确发送而 SPL RX 为零，生成显式 UART2-1 初始化的 RAM-only RX-fix loader
- [x] 实机用 RX-fix loader 收到 YMODEM 文件头和 0x52/0x53 bytes payload，定位 1.5M 下每字节 `schedule()` 导致 FIFO 丢包
- [x] 实机确认 RX-fast 移动 `schedule()` 后仍在 payload 0x52 稳定丢尾，确定需要主机端节流而非再改 UART mux
- [x] 以 RX-fast loader 节流完整传入旧 FIT，实机确认开源 OP-TEE 初始化；定位 U-Boot proper 缺少附加 DTB
- [x] 以 RX-fast loader + `scripts/ymodem-serial-bridge.py --tx-gap-us 50` 节流发送 `r1-ymodem-fit-dtb.itb`，实机进入开源 OP-TEE 后的 U-Boot 2026.10-rc1 提示符
- [x] 封装 RAM-only `db`、串口独占、等待 SPL YMODEM CRC 和节流 FIT 发送的一键启动器；默认产物 SHA-256 强制校验且不含 eMMC 写命令
- [x] YMODEM/一键 U-Boot 脚本默认在发送完成后立即释放串口，并通知 Fedora/niri 桌面会话
- [x] 生成当前 YMODEM 链的 Rockchip 官方 RK322x TEE v2.00 严格 A/B FIT，并离线验证唯一载荷变量
- [x] 在该 U-Boot 提示符只读加载 zImage、recovery initramfs 与 DTB，启动 clean v8 双核 Linux 进入 shell；用户实机确认 uptime 约 700 秒
- [x] 用同一 kernel/initramfs 仅替换为 PSCI 1.0/0.2 DTB；Linux 正确检出 PSCI v1.0，但 CPU1 仍停在开源 OP-TEE 的 normal-world return
- [x] 在 Linux `secondary_startup` 最早汇编路径加入 UART 路标；实机 `ABCDE` 证明 CPU1 已进入 `secondary_start_kernel()`
- [x] 在 Linux `secondary_start_kernel()` C 初始化路径加入 `F`–`N` UART 路标；实机完整输出至 `N`，CPU1 已 online 并完成唤醒 CPU0
- [x] 跟踪 CPU1 开启 IRQ/FIQ/abort 后进入 idle；实机完整输出至 `R`
- [x] 跟踪 CPU0 发起与返回 PSCI CPU_ON；`as ... tb` 实机证明开源 OP-TEE CPU_ON 已正常返回 CPU0
- [x] 以 polling completion 绕过首个 CPU0 次核启动等待；实机 `xop` 证明 completion 可见且 `__cpu_up()` 返回
- [x] 定位 CPU1 `cpuhp_online_idle()` 与 CPU0 第二个 hotplug completion；实机 `SyTUVz` 证明两端均完成
- [x] 用 GICv2 `TargetListFilter=1` 绕过 CPU1→CPU0 target map；实机 `D01fE` 仍无 CPU0 handler，排除 `gic_cpu_map[0]`/target-list 编码
- [x] 对照当前 OP-TEE 链 CPU0/CPU1 normal-world 可见 GIC 初始化前后寄存器；四组状态一致，仅 PMR 同步变为 `0xf0`
- [x] 对照 CPU0 在 PSCI_CPU_ON 前后及 kdevtmpfs completion 等待前的 GIC、HPPIR、RPR、CPSR；实机 IRQ unmasked 且 HPPIR 持续为 pending PPI 30，PSCI 调用前后不变
- [x] 在 SPL→TEE 前精确清理遗留 USB OTG INTID 55/APR0；实机 CPU0 RPR 恢复 `0xff`、PPI 30 不再积压，并跨过依赖 CPU1→CPU0 SGI 的 devtmpfs completion
- [x] 实机运行 secure SPL GICC_RPR/APR/ISACTIVER 只读探针；确认进入 TEE 前已存在 RPR=`0`、APR0=`1`、仅 INTID 55 active，且 cache cleanup 不改变状态
- [ ] 对照旧厂商链的同组 GIC 状态，并检查 normal world 不可见的 secure banked Group/IRQ 路由
- [x] 验证清理 INTID 55/APR0 后 CPU1→CPU0 reschedule/调用 IPI 与 completion 恢复，clean v8 双核 Linux 继续进入 shell并由用户确认 uptime 约 700 秒
- [x] 从同一 clean Linux 5.10.262 源码构建四核单变量 A/B；最终配置相对双核仅删除 `maxcpus=2` 并加入可辨识版本后缀
- [x] 修正一键启动器默认 loader：由旧 RX-fast 改为已验证的 INTID55/APR0 cleanup loader，并保留 SHA-256 强校验
- [x] RAM-only 实机验证 clean 四核 Linux 启动并稳定越过原约 30 秒边界；CPU0–CPU3 online，uptime 已到 72.92 秒且四核 IPI 均有增长
- [x] 从 `allnoconfig` 白名单构建 5.10 四核救援内核；主机侧确认 CAN/NFS/NTFS/PCI/MTD/图形/声音等未启用，zImage 由 10,158,592 B 降至 2,328,112 B
- [x] RAM-only 上板审计 v10；确认 `PROC_FS` 已编入但缺 `BINFMT_SCRIPT` 导致 `/init` `ENOEXEC`、虚拟文件系统未挂载，同时缺 `ARM_PSCI` 只启动 CPU0，因此 v10 判为无效基线
- [x] 构建补回 PSCI、脚本执行、time32/POSIX timers、futex/epoll 等基础 ABI 与诊断接口的救援 v11；仍保持 CAN/NFS/NTFS/PCI/MTD/图形/声音关闭
- [x] 将 R1 v9/v10/v11 实例整理为 Linux 内核裁剪方法论、实操流程和面试八股文档
- [x] 将 BootROM/471/SPL/OP-TEE/U-Boot proper、GICv2 基础、INTID55 定位过程和面试问答整理为专题文档
- [ ] RAM-only 实机验证 `5.10.262-phicomm-r1-rescue-v11` 四核、`/init`、proc/sys/devtmpfs、controlling TTY 和 uptime 超过 30 秒
- [x] 建立冻结 rescue 核心、叠加单个 peripheral fragment 和带 tag 产物的构建方式；生成首个 eMMC-only A/B 候选
- [x] 用 `-rescue-v11-emmc-a1` zImage + 最小 DT 完成 A 线；用户确认四核、虚拟文件系统、shell 和 uptime >30 秒均正常
- [x] eMMC B1 只读枚举成功：RK805、HS200、8GME4R、user/boot0/boot1/RPMB 均出现；但完整 DT 使 CPU1-CPU3 启动失败，不能作为最终基线
- [x] 保持 B1 zImage/initramfs/eMMC DT 不变完成 B2 arch-timer A/B；eMMC 与新 initramfs 正常，但 CPU1–CPU3 仍超时，timer 单变量假说被否定
- [x] 修复救援 initramfs 缺少 `echo/printf/test/[` applet 链接，并将 kernel/initramfs/B2 DTB 合成单文件 Linux FIT
- [x] 确认普通 SPL+FIT 拼接已经被 RK322x 0x472 有效窗口实机否定；构建保留 INTID55 cleanup 的直接 UART2 RX 无节流候选
- [ ] 实机用 direct-RX loader + `--tx-gap-us 0` 验证 FIT 传输速度和可靠性，再决定是否替换默认 loader
- [x] 构建并离线审计 U-Boot proper USB DFU RAM-only 候选；明确关闭 DFU/MMC、Fastboot、RockUSB 与 mass-storage 写入路径，实机已下载/校验 A1r9 FIT 并进入 shell
- [x] 通过单文件 RAM 启动路径运行 B2；用户确认 USB 下载已足够快，Linux 日志确认修正 initramfs 与 eMMC 生效（传输计时未留档）
- [x] 实机运行 C1：复用 multi_v7 clean v9 zImage且保持 B2 DT/initramfs；CPU0–CPU3 与 eMMC 同时通过（本轮文本只保存 uptime 13.56 秒，未保存 IPI）
- [x] 实机运行 C2：失败 v11 配置只补 Cortex-A7 `ARM_ERRATA_814220`；三个次核仍超时，实机否定该单变量，eMMC 继续正常
- [ ] （延后到最小化阶段）实机运行 B3：保持失败 B2 kernel/initramfs/完整 eMMC DT，只删除 CRU `assigned-clocks/rates`
- [ ] 以已验证 multi_v7 C1（四核 + 完整 eMMC DT）作为工作基线，继续逐项启用和验证板载外设
- [x] 在不覆盖 eMMC idb/U-Boot/trust 的前提下验证现代 U-Boot + 开源 OP-TEE + 双核 Linux RAM-only 启动链
- [x] 定位并修复当前 RAM-only 主线 SMP 启动冻结：MaskROM/RockUSB 遗留 USB OTG INTID 55/APR0 阻塞 CPU0 IRQ
- [x] 验证现代 U-Boot 的 eMMC 只读访问及 Rockchip `FwPartOffset=0x2000` 地址差异
- [x] 从纯 RAM 现代 U-Boot 启动单核主线 Linux 并进入救援 shell
- [x] eMMC 常驻 A1 零写入诊断：DDR/SPL 正常，但 SPL DT 裁掉 alias/eMMC 节点，启动列表只有两个 RAM loader；未访问 eMMC
- [x] 主机构建并逐字节审计 eMMC 常驻 A2：用 `&emmc` 路径并以 `bootph-all` 保留 eMMC/CRU；当时的 FIT `0x4000` 目标假设随后被 usbplug raw 双读否定
- [x] A2 零写入实机已进入 `Trying to boot from MMC1`；raw 失败后又回退到未实现的 FS loader，最终 `-38` 覆盖了 raw 返回码，旧日志不能证明实际读到的 header
- [x] A3 首读探针确认 mainline MMC LBA `0x4000` 返回 `LOADER  `；与既有 `FwPartOffset=0x2000` 实证吻合，否定 SPL 从 `0x4000` 加载 FIT
- [x] A4 零写入实机确认 mainline MMC LBA `0x6000` 返回原厂 trust 的 `TOS     `；usbplug raw `0x4000` 又返回 `LOADER`，故 A5 最终统一从 raw `0x6000` 写入和读取 FIT
- [x] 运行修正后的只读预检：真 MaskROM → RAM usbplug、A223/Samsung/容量、raw IDB 独立恢复片、raw `0x6000` trust 双读与备份比较全部通过
- [x] 生成并静态审计精确安装/恢复脚本：锁定 LocationID、容量、输入 SHA、raw `0x40`/`0x6000`，写后读回；安装异常会尝试现场回滚并读回验证，两个脚本均不自动 reset
- [x] 获得 raw eMMC `0x40` 与 `0x6000` 两个精确区间的明确授权，写入 A5 并对两个范围立即读回逐字节验证通过；未触发回滚，未自动复位
- [x] 首次 eMMC 冷启动通过：BootROM → DDR v1.06 → mainline MMC SPL → raw `0x6000` FIT → open OP-TEE → U-Boot proper，无 MaskROM/USB/YMODEM 依赖
- [ ] 从 eMMC 常驻 U-Boot 以 RAM-only Linux FIT 回归四核、uptime >30 s、eMMC、Wi-Fi 与 Bluetooth，确认常驻 SPL 与 RAM 调试 SPL 行为一致
- [ ] （降级/仅研究）USB Host / Device：R1 智能音箱没有对外 USB 外设接口；保留已构建候选用于 SoC/DFU 研究，不作为板载外设 bring-up 主线
- [ ] （后续）恢复 RK322x DDR DVFS：当前 `rk322x_ddr_300MHz_v1.06.bin` 只负责上电训练并以 300 MHz 初始化 DDR；需先确认 Rockchip TEE 的 DDR SMC ABI、RK322x DMC/devfreq 驱动以及板级 DDR timing/频点表，再做 RAM-only A/B，不能把更换 TEE 当作已经启用动态调频
- [ ] 连续冷启动测试

## P2：网络

- [x] Wi-Fi A1：`0x30010000` SDIO + GPIO2_D2 WL_REG_ON 实机通过；`mmc1` 枚举三个 Broadcom function（vendor `0x02d0`、device `0xa9bf`），四核保持 online
- [x] Wi-Fi A2：主线 brcmfmac 识别 BCM4345/6、启动 7.45.100.6 固件并创建 SDIO `wlan0`；四核长期运行及四核 IPI 活动复核通过
- [x] Wi-Fi A3：补齐可信 `regulatory.db`、国家码与 BCM43455 CLM blob；nl80211 实机扫描成功
  - [x] A3a 主机端：打包 Fedora `wireless-regdb 2026.05.30`、`linux-firmware 20260622` BCM43455 CLM，并设置初始监管域 `CN`
  - [x] A3a 实机：确认 regulatory/CLM 错误消失、`wlan0` 存在且四核 IPI 活动不回退
  - [x] A3b：BSSID-redacting nl80211 工具实机扫描到 2.4/5 GHz 共 9 个 BSS，退出码 0；后续构建已修正 executable-stack 标志
- [ ] 确认 NVRAM 文件名规则
- [x] 2.4 GHz 扫描
- [x] 5 GHz 扫描
- [ ] WPA2/WPA3 连接
- [ ] 配置文件自动联网
- [ ] 断线自动重连
- [ ] mDNS hostname

## P3：蓝牙

- [x] 找到蓝牙 UART
- [x] 找到 BT_REG_ON
- [x] 确认 RTS/CTS
- [x] Bluetooth UART A1r3：实机确认 UART1-1 pinmux、BT_REG_ON/BT_WAKE 高电平，HCI Reset 后 `tx:4 rx:0`
- [ ] Bluetooth UART A1r4：内建 RK805 clock provider 并以 always-on consumer 开启 CLK32KOUT2，验证 controller 低速时钟假说
- [x] Bluetooth UART A1r5：LPO=32768 Hz、BT_WAKE=low、BT_REG_ON=high 均实机成立，但 HCI 仍为 `tx:4 rx:0`
- [x] Bluetooth UART A1r6：`hci0` 创建，识别 BCM4345C0，UART 双向/RTS/CTS/LPO/GPIO 均通过；固件请求名为 `BCM4345C0.hcd`
- [x] Bluetooth UART A1r7：原厂 HCD 以 `BCM4345C0.hcd` 命中并完成 Patch，firmware build `0000` → `0124`
- [x] 提取正确的 `.hcd`
- [x] 创建 `hci0`
- [x] Bluetooth management A1r8：controller info、power on、BR/EDR inquiry、LE scan 及 Wi-Fi 5 GHz/四核共存已实机通过
- [x] Bluetooth management A1r9：`CONFIG_CRYPTO_AES=y` 实机消除 CMAC context 错误，power/LE scan/settings 均通过
- [ ] BlueZ 配对
- [ ] PipeWire A2DP Sink
- [ ] SBC 播放
- [ ] AAC 播放
- [ ] LDAC 播放
- [ ] 自动信任与重连

## P4：音频

- [x] 找到 RK3229 I2S 控制器
- [ ] 确认 MCLK/BCLK/LRCK
- [x] 确认 AK7755 I2C/SPI 地址
- [x] 主机构建 Audio I2C A1：确认完整型号 `AK7755EN`，保持 A1r9 kernel/initramfs，只启用 I2C1 100 kHz；DT 无 client，不操作 PDN/功放/总线 payload
- [x] 实机验证 Audio I2C A1：`/dev/i2c-1`、GPIO0_A2/A3 → `11060000.i2c`、PCLK_I2C1 均正常，Wi-Fi/BCM4345C0 HCD 保持正常
- [x] 主机构建 Audio A2：先固定 TPA3118D2 SDZ=low、MUTE=high，再释放 AK7755EN PDN；加入只执行 `0x60` repeated-start 身份读取的静态工具
- [x] 实机验证 Audio A2 安全状态与身份：GPIO35=high、GPIO111=low、GPIO113=high；`0x19` 的 `0x60` repeated-start 返回 AK7755EN ID `0x55`
- [x] 旧功能回归由后续 Audio A3 覆盖：四核 IPI、>30 s、2.4/5 GHz Wi-Fi 与 Bluetooth LE 均通过
- [x] 审计公开 AK7755 驱动/初始化样本：找到 AKM GPL Linux 3.10 ASoC driver，data2 命令头/CRC/DT/DAI 指纹与 R1 匹配；Ingenic/IPC-SDK 是无 firmware 的同源 OSS3 精简版；Ambarella bootloader 样本为明确专有代码
- [x] 从 GPL Kasa driver 提取并修正 I²C、PRAM/CRAM download、CRC/资源释放边界，移植最小 Linux 6.18 component；主机整核构建、DT/FIT 审计通过，暂不带 DAI/machine driver、不解除功放 shutdown/mute
- [x] RAM-only 实机验证 Linux 6.18 AK7755 A3：ID `0x55`、PRAM CRC `0x9916`、CRAM CRC `0x4453`；四核 IPI、>30 s、Wi-Fi/蓝牙回归通过
- [x] 设计并构建 Audio A4 主机候选：功放继续 shutdown+mute；AK7755 DAI、RK3229 I2S2 与最小 machine card 固定为 48 kHz/stereo/S16/32fs，只验证 ALSA 枚举和时钟，不播放音频
- [x] RAM-only 实机验证 Audio A4 核心链：`RK_AK7755` card/PCM、I2S2 四针 pinmux、12.288 MHz 内部 clock contract、功放 shutdown+mute 和 CPU0-3 online；未打开 PCM、禁止播放
- [ ] 补充 Audio A4 稳定性/无线回归证据：本版 `/proc/uptime` >30 s、Wi-Fi scan、Bluetooth LE scan 和四核 IPI 增长（A3 已通过这些项目，但不能替代 A4 证据）
- [x] 构建 Audio A5 主机候选：新增无 alsa-lib 的静态 `r1-pcm-clock-test`，仅允许 48 kHz/stereo/S16 全零 playback，保持 DSP stopped 和功放 shutdown+mute
- [x] RAM-only 实机验证 Audio A5：48 kHz/stereo/S16 全零 playback 连续 30 秒，`xruns=0`；运行中 `sclk_i2s2` gate 打开且 PL330 DMA IRQ 活动，结束后时钟回落，功放仍保持 shutdown+mute
- [x] 构建 Audio A6 主机候选：按 AKM GPL 状态机在 PCM prepare 时依次释放 CKRESETN、CRESETN/DSPRESETN，最后关闭 PCM 时回到 STANDBY；RUN/STANDBY 均读回 C1/CF，任一验证失败均断言 AK7755 reset，功放仍无解除路径
- [x] RAM-only 实机验证 Audio A6：功放前后均为 shutdown+mute；10 秒全零 PCM 得到 RUN `C1=0x21/CF=0x0c`、STANDBY `C1=0x21/CF=0x00`、`xruns=0` 和退出码 0
- [x] 构建 Audio A7 主机候选：新增不保存原始 PCM 的静态 capture 统计工具和单命令 60 秒并发验证链，覆盖零 playback、capture、DSP、PL330 DMA、四核、Wi-Fi/蓝牙共存及功放前后安全状态
- [x] RAM-only 实机运行 Audio A7r2 `/bin/r1-audio-soak 60`：playback/capture 均无 xrun，DMA IRQ `+5622`，DSP RUN/STANDBY 成对，CPU0-3 与四核 IPI 正常，Wi-Fi/BR-EDR/LE 分别扫描到 31/1/18 项，功放前后保持 shutdown+mute，最终 `AUDIO_SOAK_PASS`；capture 仅为两路 peak=1 LSB，麦克风/routing 仍未证明
- [ ] 提取 AK7755 原厂寄存器写序列
- [ ] 提取 PRAM/CRAM/OFREG/ACRAM 数据
- [ ] 移植 AK7755 ASoC driver
- [ ] 建立 machine driver
- [ ] 验证功放 mute GPIO
- [ ] 播放正弦波和扫频
- [ ] 记录声道映射
- [ ] 检查上电/关机爆音

## P5：服务与 DSP

- [ ] Shairport Sync
- [ ] AirPlay 发现
- [ ] AirPlay 2 / nqptp 评估
- [ ] 创建 `R1-FX` virtual sink
- [ ] 高通滤波
- [ ] 参数 EQ
- [ ] Limiter
- [ ] Convolver
- [ ] 配置预设切换
- [ ] Web 或 CLI 控制

## P6：稳定性

- [ ] 8 小时连续播放
- [ ] Wi-Fi AP 重启恢复
- [ ] 手机蓝牙关闭再打开恢复
- [ ] 多输入源切换
- [ ] 异常断电恢复
- [ ] watchdog
- [ ] 日志限额
- [ ] rootfs 只读化
- [ ] 升级和回滚机制
