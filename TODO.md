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
- [x] 在该 U-Boot 提示符只读加载 zImage、recovery initramfs 与 DTB，启动 clean v8 双核 Linux 进入 shell；用户实机确认 uptime 超过 30 秒
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
- [x] 验证清理 INTID 55/APR0 后 CPU1→CPU0 reschedule/调用 IPI 与 completion 恢复，clean v8 双核 Linux 继续进入 shell并由用户确认超过 30 秒
- [x] 在不覆盖 eMMC idb/U-Boot/trust 的前提下验证现代 U-Boot + 开源 OP-TEE + 双核 Linux RAM-only 启动链
- [x] 定位并修复当前 RAM-only 主线 SMP 启动冻结：MaskROM/RockUSB 遗留 USB OTG INTID 55/APR0 阻塞 CPU0 IRQ
- [x] 验证现代 U-Boot 的 eMMC 只读访问及 Rockchip `FwPartOffset=0x2000` 地址差异
- [x] 从纯 RAM 现代 U-Boot 启动单核主线 Linux 并进入救援 shell
- [ ] 验证 USB Host / Device
- [ ] 连续冷启动测试

## P2：网络

- [ ] brcmfmac 枚举成功
- [ ] 确认 NVRAM 文件名规则
- [ ] 2.4 GHz 扫描
- [ ] WPA2/WPA3 连接
- [ ] 配置文件自动联网
- [ ] 断线自动重连
- [ ] mDNS hostname

## P3：蓝牙

- [x] 找到蓝牙 UART
- [x] 找到 BT_REG_ON
- [x] 确认 RTS/CTS
- [ ] 提取正确的 `.hcd`
- [ ] 创建 `hci0`
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
