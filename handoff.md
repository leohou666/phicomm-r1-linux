# Phicomm R1 bring-up 交接记录

更新时间：2026-08-10（Asia/Shanghai）

本文用于把当前工作交给新的 Codex/开发者继续。它是工作现场摘要，不替代按时间保存全部证据的
[`docs/reverse-engineering-journal.md`](docs/reverse-engineering-journal.md)。开始操作前必须先读
[`AGENTS.md`](AGENTS.md)、[`docs/index.md`](docs/index.md) 和本文。

## 一句话状态

R1 已通过真 MaskROM 完成“原厂 DDR 471 + 主线 R1 SPL/U-Boot 472 + 开源 RK322x OP-TEE
3.7 + clean Linux 5.10.262”的纯 RAM 双核与四核启动，并进入 BusyBox shell；双核用户确认
约 700 秒，四核直接记录到 72.92 秒且四核 IPI 均有增长。冻结根因是 RockUSB 交接遗留 USB OTG INTID 55 active/APR0，板级 SPL 精确清理
后 CPU0 IRQ/SGI 恢复。未写 eMMC。

## 当前目标

在**不写 eMMC**的前提下完成：

```text
BootROM MaskROM
  -> 原厂 rk322x DDR 471（DDR3 300 MHz，已验证）
  -> 主线 U-Boot SPL 472（TARGET_PHICOMM_R1）
  -> FIT 将开源 RK322x OP-TEE 装到 0x68400000
  -> OP-TEE 提供 RK322x PSCI，并返回 0x61000000 的 U-Boot proper
  -> U-Boot proper
  -> 双核/四核主线 Linux
```

本阶段验收结果：

1. [x] 开源 OP-TEE 实际获得控制权并返回 U-Boot proper；
2. [x] 从该 RAM-only U-Boot 启动双核 clean Linux 到 shell；
3. [x] 用户确认 uptime 约 700 秒，远超原约 30 秒冻结边界；保存日志直接记录到 8.99 秒；
4. [x] 全程未执行存储写操作。

此前的四核单变量候选构建为
`build/artifacts/zImage-5.10-psci-v1-gic400-clean-4core-v9`。最终配置相对成功的双核 v8 只
增加可辨识的 `-4core` 版本后缀并删除强制命令行中的 `maxcpus=2`；DTB 逐字节相同。SHA-256
为 `5bc8624169e60ef4558cc78cb80ae887a2a4f798c2a824a9afecece29d3c2565`。
首次四核尝试误由一键脚本默认加载旧 RX-fast loader，因而在 CPU1 入口重现未清 GIC 的旧停止；
该结果无效。`scripts/boot-r1-optee-uboot.py` 默认 loader 现已改为实机成功的 INTID55/APR0
cleanup loader（SHA-256 `ff47e369...`）。
修正后重测已成功：CPU online/present 均为 `0-3`，uptime 到 72.92 秒，四颗 CPU 的
reschedule/function-call IPI 均非零。当前 initramfs 默认复用原厂 recovery 的静态 ARM
BusyBox 1.22.1；其 libc 是 Android bionic，所以 `uptime` 的时区初始化会打印 Android 环境变量
警告，这不表示当前内核或 rootfs 是 Android。

白名单 v10 实机失败：缺 `BINFMT_SCRIPT` 使 `/init` `ENOEXEC`，故 `/proc` 未挂载；缺
`ARM_PSCI` 使日志只有 CPU0。修正后的
`build/artifacts/zImage-5.10-r1-rescue-baseline-4core-v11` 为 2,721,856 B，SHA-256
`997fcf92c97a6f623650daea226df40179684827f1ce3bce89ef3b449037cb7b`。v11 补回 PSCI、脚本执行、
time32/POSIX timers、futex/epoll、RTC 与诊断接口，仍确认 CAN/NFS/NTFS/PCI/MTD/图形/声音
未启用；尚待 RAM-only 上板，不能替代已验证的 v9。

外设完善从冻结救援核心开始，不直接恢复完整多平台配置。首个候选
`build/artifacts/zImage-rescue-v11-emmc-a1`（2,721,840 B，SHA-256
`5013b84d149e431f0c2886be8b4d0f8e0aea2e96acdd0de08b058332cb23861b`）叠加独立 eMMC
fragment；其最终配置相对 v11 只有版本后缀变化。配套
`build/artifacts/rk3229-phicomm-r1-rescue-v11-emmc-a1.dtb`（22,629 B，SHA-256
`7d343820991e8bc7592114c303e7a663b719e5c1015417bd1e044038d30635bc`）启用 eMMC/RK805、采用
PSCI v1.0/v0.2，并显式禁用 USB。该候选只完成主机构建与静态 DT 审计；上板顺序必须是同一
zImage 先配最小 v11 DT，再仅换 eMMC DT，且只读检查存储。A 线现已由用户确认四核、虚拟
文件系统、shell 和 uptime >30 秒正常。B1 已只读枚举 RK805、HS200 `8GME4R`、user/boot0/
boot1/RPMB，但 CPU1–CPU3 在 eMMC probe 前未 online。B2 只恢复最小 timer 属性/两路 PPI，
DTB 为 `rk3229-phicomm-r1-rescue-v11-emmc-a2-timer-minimal.dtb`，SHA-256
`4800850ad50e8109ccd763a245128b4b3f08477cff583e77c932e5dc90625c6b`。B2 已上板：新
initramfs 与 eMMC 枚举正常，但三个次核仍依次超时，因此 timer 单变量假说已被否定。

B1 完整日志还显示 `/init` 的 `echo` 未安装；构建脚本已补齐基础 BusyBox applet 链接，新
initramfs SHA-256 为 `31508a74...`。kernel、该 initramfs 与 B2 DTB 已合成
`r1-linux-rescue-v11-emmc-a2.itb`（SHA-256 `22555b88...`）。新的 U-Boot proper 候选通过
同一 OTG 口提供 `linux-fit` DFU RAM 下载，并明确关闭所有 storage-backed DFU、Fastboot、
RockUSB 和 mass-storage 路径。用户确认当前 USB 下载已足够快并进入 B2 Linux；现有保存文件
只有 Linux 段，未保留 `dfu-util`/U-Boot 传输计时，因此只把功能路径而非具体速度作为证据。

config A/B C1 不再继续裁剪；它直接复用实机四核稳定的 multi_v7
clean v9 zImage，保持 B2 DTB 与修正 initramfs 不变：
`build/artifacts/r1-linux-multiv7-v9-emmc-c1.itb`，10,809,808 B，SHA-256
`ed8c5b932c3853242e0bb0476cbfa17d0d2f2e70379bbd23f85d2006531a24b5`。C1 实机已使
CPU0–CPU3 在 35 ms 内全部 online，`online/present=0-3`，RK805 与 HS200 eMMC 同时正常，因而
锁定为 v11/multi_v7 config 差异；本轮文本只保存 uptime 13.56 秒，未保存 IPI。

上一候选 C2 在失败 v11 最终配置只补 `CONFIG_ARM_ERRATA_814220=y`（另改可见版本后缀），
继续用 B2 DT/initramfs。FIT 为
`build/artifacts/r1-linux-rescue-v11-emmc-c2-a7-814220.itb`，3,372,920 B，SHA-256
`e69b7839cc2900dd9d8c912189d1e460e706cfbd99654ceea537baad186113f6`。C2 已实机失败：FIT 与
版本正确，但 CPU1–CPU3 仍依次超时，故 814220 单变量已否定；eMMC/RK805/RTC 正常。

后续裁剪诊断候选 B3 使用与失败 B2 逐字节相同的 v11 A1 zImage/initramfs，完整 DT 只删除
CRU 的 `assigned-clocks` 和 `assigned-clock-rates`；排序反编译 diff 已确认无其他变化。FIT 为
`build/artifacts/r1-linux-rescue-v11-emmc-b3-clock-inherit.itb`，3,372,932 B，SHA-256
`9e2ff4a214c29fcc2a06ba1445528209ba54d4093e5ad6256a8b4abed5b215d6`。选择依据是完整 DT 在
SMP 前重编 PLL/ARM/CPU/PERI clocks，而最小成功 DT 不会；B3 尚未上板。

按用户当前优先级，外设 bring-up 直接使用已验证 C1，不等待 v11 根因二分：
`build/artifacts/r1-linux-multiv7-v9-emmc-c1.itb`（10,809,808 B，SHA-256
`ed8c5b932c3853242e0bb0476cbfa17d0d2f2e70379bbd23f85d2006531a24b5`）。
C1 仍是已验证回退件；当前 `scripts/usb-dfu-r1-linux.py` 默认值与强制哈希已前移到待验证的
Wi-Fi A3a，执行时会明确打印所选 FIT。

2026-08-11 已生成下一单变量 `r1-linux-multiv7-v9-emmc-usb-host-a1.itb`（SHA-256
`3f6533ff47091f878e3c94cceab45df779f5005b84e8b9971eaa52b45fc45e07`）：kernel/initramfs 与 C1
逐字节相同，只在 DT 开启原厂日志已验证能注册的三组 EHCI/OHCI fixed-host；DWC2 OTG 继续
禁用以避开 INTID 55。该候选仅完成离线审计，必须显式 `--fit` 上板；默认 C1 不变，便于回退。
DDR DVFS 已延后：DDR 471 的 300 MHz 只是初始化频率，当前 5.10 缺 RK322x DMC/devfreq 调用方，
以后还需确认 Rockchip TEE DDR SMC ABI 和板级 timing/频点表。

用户随后确认 R1 成品没有 USB 外设接口和 SD 卡槽；USB Host A1 已降级为研究产物，不再安排
外设实机测试。当前顺序是板载 SDIO Wi-Fi、UART1 Bluetooth、I2C/SPI/I2S/AK7755 音频。
Wi-Fi A1 已构建为 `build/artifacts/r1-linux-multiv7-v9-emmc-wifi-sdio-a1.itb`（SHA-256
`0139f8f7153caccf27bcc674be70f47c1070352db4c46c92e40ad1ed5d10a0d7`）：只启用
`0x30010000` SDIO 与 GPIO2_D2 WL_REG_ON，不含 brcmfmac/firmware；下一实机成功标准仅为
SDIO card/function 枚举。

Wi-Fi A1 已实机通过：`mmc1` 约 35.7 MHz 枚举三个 Broadcom SDIO function
（vendor `0x02d0`、device `0xa9bf`），CPU online 仍为 `0-3`，eMMC 正常。下一 A2 保持该
硬件 DT 不变，只增加 cfg80211/brcmfmac 和固件/NVRAM，目标为 `wlan0`。

Wi-Fi A2 已构建：`build/artifacts/r1-linux-multiv7-v9-emmc-wifi-brcmfmac-a2.itb`，
11,468,836 B，SHA-256 `7fdc5cbbc1816e23ffdefa9fabb6f8addcee5f4fdc2b2cba35dcaa160f86328d`。
它复用 A1 DT，内建 brcmfmac SDIO，并在 initramfs 携带 BCM43455/AP6255 firmware/NVRAM；
尚待实机，只验收 `wlan0`，不扫描/联网。

A2 已实机通过：brcmfmac 识别 BCM4345/6、运行 7.45.100.6 firmware，`wlan0` 绑定
`mmc1:0001:1`。不要记录用户日志中的真实 MAC。当前缺 `regulatory.db` 与 CLM blob，下一 A3
先补法规数据/国家码/CLM 后扫描，尚未联网。用户已确认 A2 长期运行稳定；CPU0–CPU3 均在线，
四列 IPI2/IPI3 都有非零活动，因此四核稳定性回归也已闭环。

Wi-Fi A3a 已完成主机端构建：
`build/artifacts/r1-linux-multiv7-v9-emmc-wifi-regulatory-a3.itb`，SHA-256
`0b8ec5eebdda65a981d985b10b5408b3edbf1ce7346b07afd0002abe1bc06b1c`。它保持 A1 DT/A2 驱动与
原厂 AP6255 NVRAM不变，加入 Fedora `wireless-regdb 2026.05.30`、`linux-firmware 20260622`
BCM43455 CLM，并设置 `cfg80211.ieee80211_regdom=CN`。所有 payload 已逐字节核对。A3a 已
实机通过：CN cmdline 生效，regdb/CLM 缺失警告消失，`wlan0` 和四核 IPI 活动正常。

A3b 已构建 `build/artifacts/r1-linux-multiv7-v9-emmc-wifi-scan-a3b.itb`（SHA-256
首版 SHA-256 `931b616af63bdd24525fd983b65fb1985bfa61a7239e64cce0e817b8f6c64201`，内含 freestanding
`/bin/r1-wifi-scan`，只输出 SSID/频率/信号并隐藏 BSSID。实机已扫描到 2.4/5 GHz 共 9 个 BSS，
`scan_entries=9`、退出码 0。首版有 executable-stack 警告；新增可复现构建脚本并以
`-z noexecstack` 修正后，当前 FIT SHA-256 为
`9e0f11ed93ca8ce8ff8c2c96193c9d8f8918d43877c52386f51207b86aae6d3c`，静态审计通过但修正版
尚未重复上板。USB DFU 下载脚本默认值与强制哈希已切到该修正版。

Bluetooth UART A1 已构建：
`build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1.itb`（SHA-256
`60b8cc08672d46c6702a268d2a2d4133d74ca5a96b70ead7e39d33bad99fc9b0`）。kernel 与 A3 逐字节
相同；DT 启用 UART1 原厂 alternate GPIO3_B6/B5/A7/A6，并用 GPIO2_D5 hog 拉高 BT_REG_ON；
没有 serdev child/HCD。下一步在 `/dev/ttyS1` 115200+RTS/CTS 做原始 HCI Reset。USB DFU
脚本默认 FIT/哈希已切到该 A1。

A1 实机在 init 前失败，日志明确为 BT_REG_ON gpio-hog 在 gpio2 自注册期间返回 `-517`
（`EPROBE_DEFER`），使 gpio2/pinctrl 注册失败并连带阻塞 eMMC、UART 和 I2C。A1r2 已用根节点
always-on fixed regulator 替换 hog，最终 FIT
`build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1r2.itb`，SHA-256
`1688800ce0f91de1475ba2cca0562b6a3e78bb14b2fa2b18723f74faa6acf677`；payload/DT 静态审计
通过，USB DFU 默认已切到 A1r2，待重新上板。

A1r2 已实机进入 shell：`ttyS1` 正常，GPIO93 BT_REG_ON 为 high；无/有 RTS/CTS 以及 regulator
低→高复位后，115200 HCI Reset 均无响应。主线 pinctrl 已确认会为 GPIO3_B5 自动设置 UART1-1
GRF route，不交换 TX/RX。A1r3 新增原厂 GPIO3_D3 BT_WAKE output-high，FIT
`build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1r3.itb`，SHA-256
`da312e92a29096e0d741dd85c23cfa5ec7654b2e04b546beecfda6de93a8153a`；静态审计通过，USB DFU
曾切到 A1r3。A1r3 实机确认 alternate pinmux、BT_REG_ON/BT_WAKE 均正确，HCI Reset 后 UART1
统计为 `tx:4 rx:0`。当前 A1r4 内建 RK805 clock provider，并用 always-on consumer 开启
CLK32KOUT2；FIT `build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1r4.itb`，SHA-256
`3bc999abb1ba313d369afa2d9dde0f5e7d41cb1aee2299145f553435001a89d1`。USB DFU 默认已切到 A1r4，
实机已确认 CLK32KOUT2 为 32768 Hz/enable count 1，但 HCI 仍为 `tx:4 rx:0`。模组丝印确定为
AW-CM256SM；其启动资料要求 BT_WAKE 在 BT_REG_ON 上升前为低。A1r5 已改为有序的
`CLK32KOUT2 → BT_WAKE low → BT_REG_ON high`，FIT SHA-256
`3ac5b522c6512b4ba008f4838417fa636c61e14291b1c85d33dc690a69212555`，USB DFU 默认已切到 A1r5。
手册确认 WL_REG_ON/BT_REG_ON 内部 OR，之前仅切 BT_REG_ON 不是真正 POR；现有 Wi-Fi pwrseq
200 ms 已满足其 150 ms 要求。当前默认已更新为标准 `hci_bcm` serdev A1r6，FIT SHA-256
`ccc6f63461bd903b7d366dc710a3b8d03215fd423d8cefbe09550b02ed13cfe3`，待实机检查驱动日志与 `hci0`。
A1r6 已实机创建 `hci0` 并识别 BCM4345C0；唯一失败是请求名 `brcm/BCM4345C0.hcd` 未命中。
A1r7 只增加指向原厂 HCD 的精确别名，FIT SHA-256
`80f914a9c2100abe19b43774781d2295ffb4e7da05607f488032a4aee7a26552`；USB DFU 默认已切换，待上板。
A1r7 已实机通过：原厂 HCD Patch 命中，controller `build 0000` → `0124`，`hci0` 存在，UART
双向 patchram 数据成立。Bluetooth 内核阶段完成；下一步做最小 management/扫描工具，尚未进入配对。

重复启动的主要耗时仍是 822,318-byte FIT 经 50 us/byte 节流走 UART。不能再尝试普通 SPL+ITB
拼接：当前 USB472 已是 SPL 后接 FIT，历史 15 地址探针证明 `0x472` 运行时有效窗口只覆盖约
36–40 KiB SPL。新候选
`r1-phicomm-r1-uboot-spl-ymodem-gic-int55-cleanup-directrx-loader.bin` 保留 cleanup，仅让 SPL
直接轮询 UART2 LSR/RBR，SHA-256
`ee8466d7217c5b9d52990d6c4393d32c4aeb8c63f92cdddfc6b02eb4e06e3015`；用显式 loader 和
`--tx-gap-us 0` 做 RAM-only A/B，未通过前不得改默认值。

## 已验证事实

### 设备与恢复路径

- SoC 为 RK3229/RK322x，BootROM chip info 为 `41 32 32 33`（ASCII `A223`）。
- DRAM 为 512 MiB DDR3，原厂训练频率 300 MHz。
- eMMC 为 Samsung，User Area 为 15,269,888 个 512-byte sector，共 7,818,182,656 bytes。
- 完整 eMMC User Area 已只读备份为 `backup/r1-emmc-user.img`：

  ```text
  SHA-256 eb4dbb57a7a78ea2604121fd699ba11bf951a970c1c0c5c165b78a2bd7c19cf7
  ```

- PCB 按键可以进入 Rockchip Loader/MaskROM。原 U-Boot 尚存活时，恢复全零 misc BCB 后
  Android 已实机正常启动。
- 真 MaskROM 已成功 `db` 下载匹配的 DDR/usbplug loader，并能只读查询芯片和 eMMC。
- `rkdeveloptool ld` 有时把 RAM usbplug 显示为 `Maskrom`，原因是该 usbplug 的
  `bcdUSB=0x0200` 触发工具的错误启发式分类；应以串口 `UsbHook` 和 `rci/rid/rfi` 是否成功判断。
- USB 对物理主机端口非常敏感：此前某个端口始终失败，换到未使用过的另一个直连端口后稳定。
  使用原装电源，USB-TTL 只接 GND/TX/RX，不能用 TTL 的 5 V 给整机供电。

### Linux 与 30 秒冻结

- Linux 6.18.42 和 5.10.262 都能启动；6.18 已进入 BusyBox shell。
- 原厂 Trust OS 路线下，双核和四核都会在约 30 秒全局停止；单核曾稳定超过 135 秒。
- 已排除 Linux 6.18 特有回归、DesignWare watchdog 到期、次核普通 idle/WFI、NO_HZ、
  timer migration、默认 RCU stall 窗口、PSCI 0.1/0.2 DT binding 和完整上游外设 DT 冲突。
- 无 Trust 的现代 U-Boot 路线若保留次核节点，会在第一个 `PSCI_CPU_ON` SMC panic；删除
  `cpu@f01`、`cpu@f02`、`cpu@f03` 后，单核 Linux 能进入 shell。这证明当前缺的是有效的
  secure monitor/PSCI，而不是 kernel、ramdisk 或控制台交接。
- Armbian 维护者 jock 记录，专有 RK322x Trust OS 的 watchdog 会因板子不同造成约
  30 秒、60 秒或 30 分钟冻结；开源 OP-TEE 不受该问题影响，但失去 DDR 动态缩放和
  “virtual power off”等原厂特性。直接来源：
  [2025-10-26 论坛回复](https://forum.armbian.com/topic/34923-csc-armbian-for-rk322x-tv-box-boards/?comment=227602&do=findComment)。
  这只是与 R1 证据高度吻合的跨设备推断，尚未在 R1 上验证为修复。

### 现代 U-Boot RAM 链

- 独立 `TARGET_PHICOMM_R1` 已实机进入 U-Boot 2026.10-rc1 提示符。
- 已验证 512 MiB DRAM、driver model、UART2 `1500000 8N1` 和 eMMC 只读枚举。
- 现代 U-Boot 的原始 eMMC LBA 视图相对 Rockchip Loader 逻辑分区地址多
  `FwPartOffset=0x2000` sectors。例如 recovery 的 Rockchip LBA `0x1e000` 在现代
  U-Boot 中应从 `0x20000` 读取。
- 已从 RAM-only U-Boot 只读加载 recovery 中的 zImage、ramdisk 和 DTB，并以单核进入 shell。

## OP-TEE 来源与固定身份

参考链不是通用 RK322x box 的板级 DTS，而只是其开源 TEE/PSCI 实现：

- Armbian 板入口：
  <https://github.com/armbian/build/blob/main/config/boards/rk322x-box.tvb>
- 维护者分支：
  <https://github.com/paolosabatino/armbian-build/tree/rk322x-opensource-tee>
- 固定提交：`d80ff015a83b0cf9a2500a2312a31d42931a6da4`
  （`provide opensource TEE for rk322x`）
- 文件：`packages/blobs/rockchip/rk322x_tee_os.bin`
- 文件大小：423,248 bytes
- SHA-256：`ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`
- 字符串证据包含 `core/arch/arm/plat-rockchip/psci_rk322x.c`、`psci_cpu_on`、
  `psci_cpu_off`，版本为 OP-TEE 3.7.0 派生版本。

不要把该开源 OP-TEE 与 `build/artifacts/r1-vendor-tee.bin` 或原厂 trust 分区混为一谈。

## 最后一次实机结果

最后上板的是：

```text
build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-loader.bin
size:    856341 bytes
mtime:   2026-08-06 14:58:53 +0800
sha256:  169ead28ed3f8a7e658557a834ea102d62d6857d390808d8c8c417a398bcba12
```

对应串口时间戳和文件 mtime 一致，输出为：

```text
OUT
SRM012345ABabsmcdefCDEFGHIJ
U-Boot SPL 2026.10-rc1-gbaa64b2f8928-dirty (Aug 06 2026 - 14:58:43 +0800)
K6Trying to boot from RAM
nopPQrstuvwxyRzFITF os=17 ret=0 addr=? magic=68500000
spl_perform_arch_fixups: could not map boot_device to ofpath: -19
spl_perform_arch_fixups: could not map BootROM boot device to ofpath
```

重要解释：

- 之前的 `FDT_ERR_BADMAGIC`、`fdt_shrink_to_minimum()` 停止和外置 FIT `data-offset`
  错误已经依次绕过或修正。
- 最后一版启用了 `CONFIG_SPL_LOAD_FIT_FULL=y`，使用内嵌 FIT 数据；`FITF ... ret=0`
  表明本次 FDT append 已成功返回。
- 两条 `could not map ... to ofpath` 是 `spl_perform_arch_fixups()` 的警告，现有证据不能把它们
  当成致命错误；输出停止点已接近 `spl_board_prepare_for_optee()`、cache cleanup 和
  `spl_optee_entry()`。
- `addr=? magic=68500000` 的格式输出有可疑之处，不应把 `68500000` 未经复核地解释为
  FDT magic。下一版优先用固定字符路标，不依赖 SPL 的 `%p` 格式化。

## 重启后的工作区状态

```text
U-Boot source: /home/pansy/phicomm-r1-linux/build/u-boot
commit:        b0531571496 (本地 bring-up 提交，基于 baa64b2f892890f00a377eac4a3e685472bb56b5)
branch:        游离 HEAD
status:        clean
.config:       存在（跳转路标候选，同 r1-phicomm-r1-uboot-optee-jump-trace.config）
```

2026-08-07 已按本文"建议的下一步"完成重建（详见 journal"重启后重建最小 R1 U-Boot
源码状态"一节）：

- 8 个基础补丁按依赖顺序全部 `git apply` 成功并提交为 `b0531571496`；
- `patches/u-boot-spl-optee-return-address.patch` 已用真实 blob 哈希重写为合法
  format-patch（原文件尾部签名非法）；
- 新增 `patches/u-boot-phicomm-r1-optee-jump-breadcrumbs.patch`（`L/M/N/O/P/Q/R/T`）；
- OP-TEE blob 按固定 commit 重新下载并验证，位于 `build/tee/rk322x_tee_os.bin`
  （423,248 B，SHA-256 `ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`），
  不再只在 `/tmp`；构建时复制为 `build/u-boot/tee.bin`；
- 修复 binman 数据缺失：构建必须传 `TEE=tee.bin`（Makefile 的 `-a tee-os-path=${TEE}`
  为空时 op-tee 被 binman 判为 missing，FIT 退化为 `firmware=u-boot`、`data-size=0`）；
- 配置 = `r1-phicomm-r1-uboot-optee-os.config` + `CONFIG_SPL_LOAD_FIT_FULL=y` +
  `CONFIG_SPL_FIT_IMAGE_TINY=y`（IMAGE_TINY 是否仍需要未验证）；
- 干净树构建 pylibfdt 需要 `swig`：改用 `pip3 install --user pylibfdt` +
  `DTC=/usr/bin/dtc` 跳过 in-tree 构建，不动系统工具链。

```text
原始（重启前）U-Boot 工作树位于 /tmp，含未固化的 FIT_FULL 最终状态，已丢失
```

重启前的 `/tmp` 工作树和其中尚未固化的改动（最终 `n...z/R` 路标、`FITF` 打印
补丁）仍然丢失，不必再追；当前源码状态是可重放、可构建的最小集合。

### 当前产物（2026-08-07 重建后）

```text
build/artifacts/r1-phicomm-r1-uboot-optee-fit-repro-loader.bin     复现版（无新逻辑）
  size:    880917   sha256: 3b45373aa824c7cd5a42c1c5ae2610b690980639e8e8833605582a19a100c707
  pack CRC: 0x68da28d4   472: 862528 B, sha256 015eb05c67309485d7a9c2dfb35ed146f4e118735be8c351f4b0d10247b17393
build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-loader.bin   已废弃：实机复现历史 SRM012345ABab 停止（缺 DTB 探针补丁）
build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-dtbprobe-loader.bin   下次上板
  size:    880917   sha256: ec7f650d300de1b093406734003ef77a8a2af8d720d831e23d18fb1758693f56
  pack CRC: 0x03d4cad5   472: 862720 B, sha256 433d3dfb4b40f4ada179e8d7364603308443d9257913462adf0232729883bb93
build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-169ead28.bin  旧最后候选的冻结副本
build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace.config       跳转路标候选配置
build/tee/rk322x_tee_os.bin                                       已验证 OP-TEE blob
```

两者解包后 471/472/FlashData 有效字节与输入逐字节一致、padding 全零；`u-boot.itb`
中 OP-TEE 数据与已验证 blob 逐字节一致（load/entry `0x68400000`，firmware=op-tee、
loadables=u-boot）。`rkdeveloptool/config.ini` 已写好当前 pack 配置（注意
`parseLoader` 索引是 0 基：`LOADER0=FlashData` 才能通过）。

### 跳转路标含义（下一次串口判读）

```text
L   spl_perform_arch_fixups() 返回后
M/N spl_board_prepare_for_optee() 前后
O   spl_board_prepare_for_boot() 返回后
P   jumper(&spl_image) 调用前
Q/R jump_to_image_optee() 中 cleanup_before_linux() 前后
T   spl_optee_entry() 中 mov pc, r3 前（进入 OP-TEE 前的最后输出）
```

若最后字符是 `T`：控制权已进入 OP-TEE，停止排查 FIT parser，改查 OP-TEE 入口
约定、`r1` 返回地址、FDT 参数和该 blob 是否期望额外平台参数。

## 关键文件

| 路径 | 含义 |
|---|---|
| `docs/reverse-engineering-journal.md` | 完整时间线、命令、失败与证据 |
| `docs/index.md` | 当前项目入口和阶段摘要 |
| `docs/acknowledge/uboot.md` | R1 U-Boot、内存和 OP-TEE 链说明 |
| `TODO.md` | 阶段清单 |
| `build/u-boot/` | 重启后重新拉取的干净 U-Boot 源码 |
| `patches/u-boot-*.patch` | 之前逐阶段固化的 U-Boot 补丁；`u-boot-spl-optee-return-address.patch` 已重写为合法格式，另新增 `u-boot-phicomm-r1-optee-jump-breadcrumbs.patch`；`u-boot-spl-fit-fdt-debug.patch` 仍是损坏的历史手工会并件，不再使用 |
| `rkdeveloptool/rkbin/bin/rk32/rk322x_ddr_300MHz_v1.06.bin` | 已验证的原厂 DDR 471，7196 bytes，SHA-256 `cab11c3a...` |
| `build/artifacts/r1-phicomm-r1-uboot-spl-dtb-generic-loader.bin` | 不含 OP-TEE、已实机进入现代 U-Boot 提示符的可靠对照 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-169ead28.bin` | 旧最后实机候选的冻结副本（原 `r1-phicomm-r1-uboot-optee-fit-trace-loader.bin` 已不再更新） |
| `build/artifacts/r1-phicomm-r1-uboot-optee-fit-repro-loader.bin` | 重建复现版（无新逻辑），SHA-256 `3b45373a...`，pack CRC `0x68da28d4` |
| `build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-loader.bin` | 新跳转路标候选（`L/M/N/O/P/Q/R/T`），SHA-256 `b41a2955fa41035cb164733d68d0cdaeb0480b5046adb94b5a43ba8cdb2297f7`，pack CRC `0x1fd332e2`；下次上板文件 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace.config` | 跳转路标候选的完整 U-Boot 配置 |
| `build/tee/rk322x_tee_os.bin` | 重新下载并验证的开源 OP-TEE blob，423,248 B，SHA-256 `ff56bb3b...`；构建时复制为 `build/u-boot/tee.bin` |
| `backup/r1-emmc-user.img` | 完整 eMMC User Area 备份，禁止公开提交 |

DDR 471 的完整 SHA-256：

```text
cab11c3a081d2a67a2f07a3387a8bf25889c0356a5bed6b5b5dd373026186cd2
```

可靠无 OP-TEE 对照 loader：

```text
build/artifacts/r1-phicomm-r1-uboot-spl-dtb-generic-loader.bin
sha256 3b8959a491c1fead6409e4eaf4cb1598dac5ab9b9e3e171f824ccb7f3746c70d
```

## 建议的下一步

### 1. ~~先冻结现场，不覆盖最后候选~~（已完成）

旧最后候选已冻结为 `build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-169ead28.bin`。

### 2. ~~在 `build/u-boot` 重建最小 R1 源码状态~~（已完成）

8 个基础补丁按序重放成功、`spl-optee-return-address` 补丁已修复格式、新增
`u-boot-phicomm-r1-optee-jump-breadcrumbs.patch`，全部提交为本地 commit
`b0531571496`，补丁链在干净 worktree 上完整重放验证一致。诊断路标只保留了
`L/M/N/O/P/Q/R/T` 新集合；不要再套用 `u-boot-spl-fit-fdt-debug.patch` 等历史重叠
版本（该文件仍是手工合并的损坏补丁，仅作历史证据）。

### 3. ~~重新取得并验证 OP-TEE blob~~（已完成）

位于 `build/tee/rk322x_tee_os.bin`，大小 423,248、SHA-256
`ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`，已验证匹配。
不要提交到公开仓库。

### 4. ~~先重现最后候选，再增加跳转路标~~（已完成，含离线验证）

复现版 `r1-phicomm-r1-uboot-optee-fit-repro-loader.bin` 与跳转路标版均已生成并解包逐字节验证。
**注意**：重建必须包含 `patches/u-boot-phicomm-r1-spl-dtb-memory-probe.patch` 的探针代码
（`s/m/n+hex` 路标，位于 `common/spl/spl.c` OF_REAL 块）。交接时 10 补丁清单遗漏了它；
没有它的构建在实机复现 `SRM012345ABab` 停止（历史 spl-common-trace 阶段同款），带它的构建
全部通过。该补丁与 common-init 路标补丁重叠，需手工合并（只加 `R1_SPL_COMMON_HEX32` 宏 +
探针块，当前树已合并并提交为 `78472b20c9e`）。

### 5. 上板命令只允许 RAM 下载

设备进入真 MaskROM 且串口已打开后，只运行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-dtbprobe-loader.bin
```

串口判读：`ab` 后应出现 `s`；`sm` = 运行时 `__bss_end` DTB magic 合法，继续
`cdefCDEFGHIJ` 与 `LMNOPQRT`；`sn<8位hex>` = 运行时 DTB 未正确到达（记录该值）；
若仍停在 `ab`，探针假说不成立，需细分 `bootstage_init()`/`fdtdec_setup()`。

不要把 `scripts/catch-rockusb.sh` 当下载脚本；它只等待设备并运行 `rci/rid/rfi` 只读探针。
不要执行 `UL`、`WL`、`EF`、`GPT`、`PRM`，也不要写 parameter/idb/U-Boot/trust/recovery。

## 内存与地址速查

```text
0x60000000  SPL 起始地址
0x61000000  U-Boot proper / CONFIG_TEXT_BASE
0x68400000  原厂 Trust OS 保留位置；当前开源 OP-TEE load/entry
0x68500000  Trust 保留区之后；最后日志中的 FDT 地址/格式仍需复核
0x60000000-0x7fffffff  512 MiB DRAM
```

不要让 kernel、ramdisk、FDT、U-Boot proper 或 SPL 工作区覆盖 `0x68400000` 的 OP-TEE。

## 安全边界

- 当前阶段只授权只读检查、主机端构建和 RAM-only `db` 实验。
- recovery/BCB 回滚已经验证，但"引导区全损坏后从真 MaskROM 完整恢复"仍未实测。
- eMMC hardware boot0/boot1 已于 2026-08-08 备份：`backup/boot/r1-emmc-boot0.img`、
  `r1-emmc-boot1.img`（4 MiB 各，逐字节相同，SHA-256
  `70b4abfd87fa2e201ce17ddbf6886009ac9e70c64d2cc09880f61bef5604fdb9`），
  内含 Rockchip loader（"RK32" 段 ~59 KB）；BootROM 是否实际从 boot 分区启动
  未直接验证。
- 任何设备写入都必须重新核对 USB 设备、目标介质、offset、length、源镜像、备份和恢复流程，
  并取得针对具体命令和目标的明确授权。
- 不公开提交 `backup/`、整盘/分区镜像、设备唯一数据、原厂 loader 或 OP-TEE binary。

## 文档纪律

每完成一个有实机证据或确定产物的阶段，同一轮更新：

1. `docs/reverse-engineering-journal.md`：原始命令、输出、失败、结论和教训；
2. `docs/index.md`：当前状态和下一步；
3. `TODO.md`：对应复选框；
4. 对应专题文档。

外部结论必须链接具体原帖、固定 commit/blob、原始补丁或数据手册；明确区分 R1 已验证事实、
基于 R1 的推断和来自其他 RK322x 板子的旁证。
