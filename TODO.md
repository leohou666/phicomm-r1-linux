# TODO

## P0：必须先完成

- [x] 保存完整冷启动日志
- [x] 完整备份 eMMC
- [x] 验证 U-Boot Loader 仍存活时的 recovery/BCB 回滚流程
- [ ] 验证真 MaskROM 状态下不依赖 eMMC U-Boot 的恢复流程
- [ ] 确认并备份 eMMC hardware boot0/boot1（若启用）
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
- [ ] 验证 RAM-only 启动路径
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
- [ ] 实机 `db` 运行 `r1-phicomm-r1-uboot-optee-jump-trace-loader.bin`（SHA-256 `b41a2955...`）并按最后字符定位停止点
- [ ] 在不覆盖 eMMC idb/U-Boot/trust 的前提下验证现代 U-Boot + OP-TEE 启动链
- [ ] 定位并修复主线 SMP 启动后约 30 秒全局停止的问题
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
