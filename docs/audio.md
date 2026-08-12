# 音频子系统说明

## 1. 基本策略

先保证 PCM 能稳定从 RK3229 输出到扬声器，再处理高级 DSP。不要同时调试 I2S、AK7755 程序、PipeWire 和蓝牙 codec。

推荐顺序：

1. I2S 时钟；
2. AK7755 控制接口；
3. AK7755 直通；
4. 功放使能和 mute；
5. ALSA 播放；
6. PipeWire；
7. AirPlay；
8. 蓝牙；
9. EQ 和 limiter。

## 2. AK7755 驱动边界

驱动需要完成：

- reset 和电源时序；
- I2C/SPI 寄存器访问；
- DAI format；
- sysclk / PLL；
- 采样率和位宽；
- PRAM / CRAM / OFREG / ACRAM 下载；
- mute；
- mixer controls；
- suspend/resume。

驱动存在不等于 DSP 算法可重新编译。必须区分：

- Linux ASoC codec driver；
- AK7755 DSP program；
- 箱体调音参数；
- 麦克风阵列和 AEC 参数。

### 2.1 原厂 DTB 与启动日志确认的连接

原厂 `boot.img` 和 `recovery.img` 使用同一份 DTB。以下连接由反编译后的 DTB 得到，并与原厂启动日志交叉检查：

| 功能 | 原厂配置 |
|---|---|
| 音频控制总线 | I2C1，控制器地址 `0x11060000` |
| AK7755 | I2C 地址 `0x19` |
| AK7755 数字音频 | I2S2，控制器地址 `0x100e0000` |
| AK7755 PDN | GPIO1_A3，DTB 原始 flags 为 `0` |
| TPA3118D2 shutdown | GPIO3_B7，DTB 原始 flags 为 `0` |
| TPA3118D2 mute | GPIO3_C1，DTB 原始 flags 为 `0` |
| 麦克风 ADC 候选 | 三个 ES7243 节点，I2C 地址 `0x11`、`0x12`、`0x13` |

原厂启动日志确认 `rockchip-ak7755` 声卡将 `ak7755-AIF1` 映射到 I2S2，并成功注册为 `RK_AK7755`。随后加载 `ak7755_pram_data2.bin` 和 `ak7755_cram_data2.bin`，两次 CRC 均成功。

三颗 ES7243 节点虽然在 DTB 中标为启用，但启动时 I2C 访问持续返回错误。因此目前只能确认原厂配置尝试探测这些地址，不能据此认定三颗器件都实际存在或工作正常。

GPIO 的电气有效电平和真实上电时序尚未通过示波器或实机切换验证。移植时应先保持原厂驱动行为，不能只根据反编译后 flags 的数值推断功放安全时序。

用户实物确认芯片完整型号为 `AK7755EN`。AKM 官方产品页说明该型号为
36-pin HVQFN 的 RAM-based DSP，集成 mono ADC、stereo codec、mic/line-out amplifier，
支持 I²C/SPI 控制和最高 96 kHz AD/DA。这是器件级事实；R1 上 I2C1 `0x19`、
I2S2 和控制 GPIO 仍来自原厂 DT/日志与实物交叉证据。来源：AKM，
[`AK7755EN` product page](https://www.akm.com/global/en/products/audio-voice-dsp/lineup-audio-voice-processor/ak7755en/)，
访问于 2026-08-12。

当前 Audio A1 只启用 RK3229 I2C1 controller 与 GPIO0_A2/A3 pinmux，不声明
codec child，不操作 AK7755EN PDN、TPA3118D2 shutdown/mute，也不发送任何
I²C payload。首轮只验证 controller/clock/pinmux/设备节点，再根据准确数据手册
设计最小无副作用的 `0x19` 身份读取。

Audio A2 根据 AKM AK7755EN 数据手册 revision `014006643-E-01` 的 Device
Identification 流程，只写 read command `0x60`，随后以 repeated-start 读取一个字节；
期望值为 `0x55`。来源：AKM，
[`AK7755EN datasheet`](https://www.mouser.com/datasheet/3/5939/1/ak7755en_en_datasheet.pdf)，
revision `014006643-E-01`，2018-08。功放安全状态依据 TI 官方
[`TPA3118D2 datasheet`](https://www.ti.com/lit/ds/symlink/tpa3118d2.pdf)：SDZ low 与
MUTE high 均使输出进入 Hi-Z。A2 因而用 fixed-regulator supply dependency 明确建立
GPIO3_B7 low → GPIO3_C1 high → GPIO1_A3 high 的顺序，并在 PDN 后声明 2 ms delay。
它仍不启用 I2S/ASoC、不加载 AK7755 DSP RAM，也不解除功放 shutdown/mute。

Audio A2 已于 R1 实机验证。debugfs 报告 GPIO1_A3/global 35 为 output-high、
GPIO3_B7/global 111 为 output-low active-low、GPIO3_C1/global 113 为 output-high；
GPIO EXT_PORT 原始值 `GPIO1=0xFFFE9F3E`、`GPIO3=0x128261FE` 也分别确认 bit3=1、
bit15=0、bit17=1。保持这些安全状态时，`r1-ak7755-id` 对 I2C1 `0x19` 执行
`0x60` combined transaction，返回 `0x55` 且退出码 0。这是 AK7755EN 在 R1 上的
器件身份实证；尚未验证的是 I2S 时钟、ASoC、DSP firmware 下载和音频输出。

### 2.2 公开驱动源码审计

截至 2026-08-12，当前 Linux 5.10 源码只有 RK3229 I2S2 CPU-DAI/DMA 驱动，
没有 AK7755 codec driver。对 Rockchip 官方 BSP 的 `release-3.10`
(`6c04d006ae88...`)、`release-3.14` (`a5bb31b08ba5...`)、`release-4.4`
(`27f039b43ada...`)、`develop-4.4` (`9ead5f3cbd6e...`) 以及 `others/kylin/brillo`、
`others/miniarm`、`others/multi-os` 的递归文件树检索均未出现 `ak7755`。因此 R1 原厂
zImage 中的 `akm,ak7755` 和 `rockchip,ak7755-audio` 属于未进入这些公版分支的厂商代码；
不能把 Rockchip 公版驱动当作已经找到。

用户随后找到了目前最关键的公开实现：hello/kasa 固定 commit
[`762398dc`](https://github.com/hello/kasa/blob/762398dc7ceff508a4ac834ff93b14955d802328/ambarella/kernel/linux-3.10/sound/soc/codecs/ak7755.c)
中的 Linux 3.10 ASoC driver。该文件有 Asahi Kasei Microdevices 2014–2016 版权头，
声明 GPL-2.0-or-later，并实现 I²C/SPI transport、`snd_soc_codec_driver`、
`ak7755-AIF1` DAI、DAPM/controls、`request_firmware()`、PRAM/CRAM/OFREG/ACRAM
下载和 CRC16-CCITT (`0x1021`) 读回。它不是 Rockchip machine driver，但已经足以作为
Linux 5.10 codec component 的主要 GPL 移植来源。

它与 R1 原厂二进制链的同源证据很强，而不只是寄存器相似：源码使用
`akm,ak7755`、`ak7755,pdn-gpio`、`ak7755-AIF1`，并构造
`ak7755_{pram,cram,ofreg}_data2.bin`；这些字符串均在 R1 DT、ASoC 日志或原厂文件中
独立出现。R1 data2 文件首命令分别为 PRAM `0xB8`、CRAM `0xB4`、OFREG `0xB2`，
也与源码相同。以该源码的 CRC16 算法重算本机原厂 PRAM/CRAM，分别得到 `0x9916`、
`0x4453`，逐字匹配原厂启动日志。因此该实现很可能是 R1 厂商驱动的 AKM 上游祖先。

它仍是需要审计的旧 vendor code，不能整文件机械搬入 5.10：使用已移除的旧 ASoC codec
API 和全局单实例状态；成功请求 firmware 后没有 `release_firmware()`；CRC mismatch 返回
正数 `1`，上层却用 `ret >= 0` 当成功；部分错误路径会泄漏 firmware/buffer；OFREG/ACRAM
文件名误用了 CRAM mode 数组。移植时应只保留 I²C、component/DAI 和 firmware protocol，
改用 regmap/gpiod、严格 CRC/资源释放，并删除 SPI、misc ioctl 与内嵌示例 DSP program。
`ak7755_dsp_code.h` 没有独立许可头，也不应复制；R1 的 data2 二进制继续只作为本地专有
测试证据，不提交公开仓库。

另一份可读 Linux 实现位于 themactep/ingenic-sdk commit
[`8addc4a9`](https://github.com/themactep/ingenic-sdk/blob/8addc4a9acc93a4547dbd2a937f30a8c9745520a/4.4.94/audio/a1/oss3/ex_codecs/ak7755_codec.c)
及其[寄存器头文件](https://github.com/themactep/ingenic-sdk/blob/8addc4a9acc93a4547dbd2a937f30a8c9745520a/4.4.94/audio/a1/oss3/ex_codecs/ak7755_codec.h)。
它验证了 repeated-start I2C、`0xC0..0xEA` 控制寄存器、采样率、ADC/DAC mute、
PDN 和 reset/run 状态机等基本结构。但该实现依赖 Ingenic `codec-common`/OSS3 私有接口，
不是 ALSA ASoC；虽然头文件声明 PRAM/CRAM/OFREG/ACRAM 类型，源文件没有实际 firmware
请求、RAM 下载或 CRC 验证，也与 R1 原厂 data2 文件名无关。

用户给出的 iesah/IPC-SDK 固定 commit
[`1986333e`](https://github.com/iesah/IPC-SDK/blob/1986333e26bd50a453edca5749433071cf88b390/opensource/drivers/audio/oss3/ex_codecs/ak7755_codec.c#L231)
属于同一 Ingenic OSS3 源码家族，并非第三套完整驱动：它的头文件与上述 themactep 版本
SHA-256 完全相同，C 文件仅有 5 个 diff hunk，主要是 reset/speaker GPIO、双声道增益和
I²S mode 板级差异；仍没有 DSP firmware download 或 CRC。

许可证也不能草率处理：该文件自身无 SPDX/版权头，仅声明 `MODULE_LICENSE("GPL v2")`；
仓库根 [LICENSE](https://github.com/themactep/ingenic-sdk/blob/8addc4a9acc93a4547dbd2a937f30a8c9745520a/LICENSE)
则是 2024 thingino 的 MIT 文本。因文件来源与许可覆盖范围不够清晰，本项目不复制该实现，
只把它作为公开行为参考。Microchip 官方 Harmony v1.11 文档也确认存在 production 级
`framework/driver/codec/ak7755/src/dynamic/drv_ak7755.c`，但它属于 PIC32 Harmony 的
I2C/I2S 框架而非 Linux ASoC，见 Microchip
[`Driver Libraries`](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/UserGuides/DriverLibraries_v111.pdf)
第 188–199 页。

用户提供的 `nrnjnkr/dvt_factory_ak7755_HwCodec` 也含一份可读的 AK7755 实现：固定提交
[`cacdbbc9`](https://github.com/nrnjnkr/dvt_factory_ak7755_HwCodec/blob/cacdbbc9b9620ecc31e8b461758223e29fd55411/Ooma-Butterfleye-Gen2FW/source/s2l_linux_sdk/ambarella/boards/btfl/bsp/iav/codec_ak7755.c)
中的 Ambarella S2L bootloader 板级 `codec_ak7755.c`。它展示了 SPI mode 3、1 MHz、
PDN low 2 ms 后拉高、`0xC0..0xEA` 控制寄存器、8/16/48 kHz 选择以及一个明确标为
`Bypass mode` 的配置序列。这对核对 AK7755 通用寄存器含义和后续构造“无 DSP 算法的
安全直通”实验有参考价值，但它走 SPI，而 R1 已验证走 I2C；它也没有 Linux ASoC、
PRAM/CRAM/OFREG/ACRAM 下载、firmware parser 或 CRC。

该文件头还明确声明 Ambarella confidential/proprietary，只有签署许可协议或 NDA 才允许
使用、复制或制作衍生作品；GitHub 仓库也没有声明开源许可证。因此本项目不能复制或改写
其中的寄存器序列，只能记录它存在并把可由 AKM 数据手册独立验证的行为当作交叉线索。
其 SHA-256 为 `e802cdaa19980789a2e1547d3f76c4d47ae37474850ba9b05ad49e57c99a2848`。

因此后续不再需要完全从二进制猜测协议：以 AKM 数据手册和 GPL Kasa driver 为规范来源，
以 R1 的 GPIO、I2S2、data2 firmware 与成功 CRC 为实机证据，编写精简且修正缺陷的 Linux
5.10 codec component。第一版只实现 I²C probe/ID、受控 reset、PRAM/CRAM 下载和严格 CRC，
且始终保持外部功放 shutdown+mute；通过后再注册 PCM DAI/machine driver并解除功放。

### Linux 6.18 AK7755 安全 firmware verifier A3（2026-08-12）

canonical 实现目标从临时 Linux 5.10 调整为 Linux 6.18.42，便于后续进入 Armbian/主线
环境。第一阶段仍严格限制在 control path：驱动只注册一个没有 DAI 的 ASoC component，
通过 I²C command `0x60` 检查 ID `0x55`，依次下载 data2 PRAM/CRAM，并用器件 command
`0x72` 读取硬件 CRC。任何 size、命令头、I²C 或 CRC 错误都会使 probe 失败并重新断言
PDN；不会创建声卡，也不会解除功放 shutdown/mute。

实现位于 `kernel/overlays/linux-6.18.42/sound/soc/codecs/ak7755.c`，由
`scripts/prepare-kernel-source.sh` 在应用 Kconfig/Kbuild patch 前复制到固定 kernel tree。
相比旧 Kasa driver，A3 修正了 firmware 引用泄漏、CRC mismatch 返回成功、全局单例和错误
路径问题；没有复制无独立许可头的示例 DSP program。R1 data2 仍是本地证据，不进入源码树。

A3 DTS 继续维持已验证的安全电平：GPIO111 low 令 TPA3118D2 shutdown，GPIO113 high 令其
mute，之后才释放 GPIO35 的 AK7755 PDN。I2S2 显式 disabled。最终 config 中四核 PSCI、
brcmfmac SDIO、BCM HCI UART、AES/CMAC、I2C RK3X、ASoC 和 `SND_SOC_AK7755` 均为 built-in。

主机验证产物如下；这些只证明构建与封装正确，不等同于 R1 实机 probe 成功：

```text
63ee4975c90c244fce4b6cf44c1a9522cd1f03b5ed6444d042a01ff8b7e16c69  zImage-mainline-6.18-ak7755-fw-a3
34ace61e54e991a9fd259909accfd49af0abc1b128fa7bc068f7f62778c731c4  r1-initramfs-mainline-6.18-ak7755-fw-a3.cpio.gz
f3c76a6cc8a0abece8aa4f170075803d0d43975da8dcb788a08725814bda6b92  rk3229-phicomm-r1-mainline-6.18-ak7755-fw-a3.dtb
3b5a4d788f7f66ab57c5dfc62d554b89754594c5a8473be2aff82a63a8e4679f  r1-linux-mainline-6.18-ak7755-fw-a3.itb
```

`dumpimage` 抽取的 kernel/initramfs/DTB 已分别与输入 `cmp` 一致。当前主机没有安装
`dtschema` 的 `dt-doc-validate`，所以 binding 只完成源码审阅和 DT 编译，尚未执行正式
`dt_binding_check`；这是待补的主机工具验证，不阻塞 RAM-only A3。

#### A3 RAM-only 实机通过

实机启动为 `Linux 6.18.42-phicomm-r1-4core-wifi-a3-dirty`。驱动在 1.83 秒内完成三项
关键验证：PRAM 5308 bytes 的硬件 CRC 为 `0x9916`，CRAM 1113 bytes 的硬件 CRC 为
`0x4453`，最终 ID 为 `0x55`，随后明确报告 DSP intentionally stopped。三项值均与原厂
日志和主机独立重算一致，故 A3 control/firmware 链已验证，不再只是主机候选。

旧功能回归也通过：Wi-Fi 扫描返回 28 个 BSS，覆盖 2.4 GHz 和 5 GHz；BCM4345C0
controller power-on 成功，LE 10 秒扫描返回 20 个 report；`/proc/interrupts` 显示
CPU0–CPU3 四列且四核 IPI2/IPI3 均增长。`Bluetooth: MGMT ver 1.23` 出现在 31.867 秒，
随后又完整运行 10 秒 LE 扫描，因此系统至少稳定运行约 42 秒，越过本项目 >30 秒门槛。

A3 仍没有 PCM DAI 或 sound card，这是设计结果而非缺陷。A4 才注册 codec DAI、I2S2 和
minimal machine card；A4 首轮继续保持功放 shutdown+mute，只以 ALSA card/PCM 枚举、
DAI format 和 clock tree 为验收项，不进行扬声器播放。

### Linux 6.18 AK7755 安全 DAI A4 主机候选

原厂 DT 和启动日志共同表明 R1 的 I2S2 只有 RX、TX、BCLK、LRCK 四个 PCM pin，且原厂
明确打印 `i2s2 has no mclk`。因此 A4 不虚构物理 MCLK：RK3229 I2S2 是 BCLK/LRCK provider，
AK7755 是 clock consumer 并从 BICK 派生内部时钟。参考 AKM GPL driver 的 R1 匹配 data2
路径，首版只开放 48 kHz、双声道、S16、32fs；machine driver 把 I2S controller 的内部
clock 设为 12.288 MHz，以得到 1.536 MHz BCLK。DSP 仍停止，GPIO111/113 继续让 TPA3118D2
保持 shutdown+mute，本阶段禁止执行播放或 capture。

新增的 codec DAI 名为 `ak7755-AIF1`，专用 machine card 名为 `RK_AK7755`。完整 Linux
6.18.42 构建已通过，两个新对象 `ak7755.o`、`phicomm_r1_ak7755.o` 均进入 built-in；DTB
反编译确认 sound card、codec/CPU phandle、I2S2 pinctrl 和两个 `#sound-dai-cells = <0>`。
FIT 总大小 14,339,592 bytes，低于 16 MiB DFU RAM alternate 上限。`dumpimage` 抽取的三个
payload 均与输入逐字节一致：

```text
80ccc4c77348fa3da8ff4c6fe9af4bdee3edb2aed3364f12682728c9bcd75936  kernel-mainline-6.18-ak7755-dai-a4.config
843fdecc6c383bfbeaba77ff68a11bbcf7426ef7277524e75c2da89b294f5cd7  zImage-mainline-6.18-ak7755-dai-a4
34ace61e54e991a9fd259909accfd49af0abc1b128fa7bc068f7f62778c731c4  r1-initramfs-mainline-6.18-ak7755-dai-a4.cpio.gz
657b8f9fff815e5590abd5a63608f237e7790b005ef0019a305bcd57662533e4  rk3229-phicomm-r1-mainline-6.18-ak7755-dai-a4.dtb
245e705f07ad5d0ed585ad91e2a3b9c3379e199ca54205cba7bc7aa75ef32ba5  r1-linux-mainline-6.18-ak7755-dai-a4.itb
```

当前主机仍没有 `dtschema`，所以新增 binding 未执行正式 `dt_binding_check`；DT 编译和整核
链接已经通过。以下实机结果已经把 A4 核心链从主机候选提升为 R1 验证事实，但仍不写 eMMC、
不解除功放、不播放声音。

#### A4 RAM-only 实机核心链通过

实机版本为 `6.18.42-phicomm-r1-ak7755-dai-a4-dirty`，排除了误启动旧 FIT。AK7755 再次得到
PRAM CRC `0x9916`、CRAM CRC `0x4453` 和 ID `0x55`，随后 machine/codec 两端分别报告：

```text
AK7755-I2S2: safe card ready: 48 kHz stereo S16, CPU clock provider, 32fs
ak7755 1-0019: DAI prepared: I2S 48 kHz stereo S16, codec slave, 32fs; DSP stopped
```

`/proc/asound/cards` 枚举 card 0 `RK_AK7755`；`/proc/asound/pcm` 枚举一个 playback 与一个
capture endpoint，link/DAI 为 `AK7755 PCM ak7755-AIF1-0`。GPIO0_D2/D3 和 GPIO3_B3/B4 已由
`100e0000.i2s2` 以 RX/TX/CLK/SYNC 四组占用。clock summary 显示 `i2s2_frac`、`i2s2_pre`、
`sclk_i2s2` rate 均为 12.288 MHz；因为没有打开 PCM stream，`sclk_i2s2` prepare/enable
计数为 0，而 bus clock `hclk_i2s2_2ch` 为 1，这是预期 idle gating。

安全链也由 debugfs 验证：AK7755 `shutdown` 为 high，功放 shutdown consumer 为 physical
low (`ACTIVE LOW`)，功放 mute consumer 为 high；regulator summary 显示
`amp_shutdown_safe -> amp_mute_safe -> 1-0019-safe` 依赖完整。Linux 6.18 debugfs 显示的是
gpiochip 动态局部编号 `gpio-29/15/17`，不能与旧内核的全局 `35/111/113` 数字直接比较，
应以 consumer 名、原始 DT pin 和实际电平为准。CPU online 为 `0-3`。

A4 核心验收已通过。由于本轮没有提供 A4 自身的 `/proc/uptime`、Wi-Fi scan、Bluetooth LE
scan 或四核 IPI 增长输出，稳定性与无线回归仍保持为显式待办；A3 的成功不能代替 A4 证据。
继续禁止打开 PCM 或进行播放/capture，下一音频阶段应先设计无声时钟/数据线观测方案。

### Audio A5：全零 PCM 时钟/DMA 验证

用户决定暂不补 A4 的稳定性/无线回归，先进入不出声的 PCM 链路验证。新增
`tools/r1-pcm-clock-test.c`：这是 7,468-byte ARM EABI5 freestanding static ELF，不依赖
alsa-lib 或动态加载器。工具在编译期断言 ARM `snd_pcm_hw_params` ABI 为 604 bytes，只打开
`/dev/snd/pcmC0D0p`，将硬件参数限制为 RW_INTERLEAVED、48 kHz、stereo、S16_LE、1024-frame
period 和 4096-frame buffer，然后持续写全零。默认 20 秒，可指定 1–120 秒；xrun 会恢复、
计数并最终返回失败，其他 ioctl/write 错误立即停止，正常或错误退出都会 drop/close stream。

A5 没有修改 A4 kernel、DT、codec firmware、DSP 状态或功放 GPIO。主机侧确认工具是 ELF32
little-endian ARM static executable、无运行时未解析符号；initramfs 同时包含该工具、Wi-Fi/BT
工具和两份 AK7755 firmware。FIT 三个 component 抽出后与 A4 kernel、A5 initramfs、A4 DTB
逐字节一致：

```text
f36d959d82dab252a7ad9d1e415b015e77b6b3eb37256e6cfc9bc50028b4cd91  r1-pcm-clock-test
d624e87edbd1a124283d7ba31169b2847f62cf924a719ac6a4129419560c82c3  r1-initramfs-mainline-6.18-ak7755-pcm-clock-a5.cpio.gz
bb59d10590d9c61add007a34c55c275766d8ea199df80759df4aea79305771f1  r1-linux-mainline-6.18-ak7755-pcm-clock-a5.itb
```

A5 已在 RAM-only 实机通过。`/bin/r1-pcm-clock-test 30` 以 48 kHz/stereo/S16_LE、1024-frame
period、4096-frame buffer 连续写零 30 秒，最后输出 `zero_stream_complete xruns=0`。运行期间
`i2s2_src`、`i2s2_frac`、`i2s2_pre` 和 `sclk_i2s2` 的 enable/prepare 计数均为 1，后三级
rate 为 12.288 MHz；PL330 `110f0000.dma-controller` IRQ 32 观察到 901 次。结束后四级
stream clock 的 enable/prepare 均回到 0，说明 runtime gate 能正确打开和关闭；
`hclk_i2s2_2ch` 继续保持 1 是 controller bus clock 的预期状态。

测试后的 debugfs 仍显示 AK7755 `shutdown` high、功放 shutdown physical low/ACTIVE_LOW、
功放 mute high；regulator summary 仍为
`amp_shutdown_safe -> amp_mute_safe -> 1-0019-safe`。内核日志没有 PCM/I2S/DMA xrun、
underrun、timeout 或新错误。这里“playback”仅指 ALSA→I2S2→DMA 的数据方向；全零样本、
DSP stopped 和功放 shutdown+mute 使本次不是扬声器声音验证，也没有证明外部 BCLK/LRCK
波形或 AK7755 DSP routing。下一阶段仍应保持功放关闭，先建立 DSP start/routing 的可回退边界。

## 3. PipeWire DSP

建议创建一个虚拟输出节点：

```text
R1-FX
```

第一版处理链：

```text
Input Gain
 → DC Block / High-pass
 → Parametric EQ
 → Optional Bass Shelf
 → Limiter
 → Output Gain
```

音箱保护建议至少包含：

- 低频高通，避免小尺寸扬声器过冲；
- 峰值 limiter；
- 功放启动时淡入；
- 输入切换时短暂 mute。

## 4. 延迟预算

蓝牙接收链路本身存在 codec 和缓冲延迟。PipeWire DSP 不应配置过大的 quantum。

调试目标：

- 不出现 xruns；
- 蓝牙播放稳定优先于极限低延迟；
- AirPlay 以同步稳定为优先；
- filter-chain 的卷积长度应匹配 RK3229 性能。

## 5. 测试音源

至少准备：

- 1 kHz 正弦波；
- 20 Hz～20 kHz sweep；
- 左右声道 identification；
- pink noise；
- 0 dBFS limiter 测试；
- 静音底噪录音。

每次修改时钟、DAI format 或声道映射后都重新执行基础测试。
