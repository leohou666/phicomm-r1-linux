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

用户拆机观察确认箱体声学单元为一只低音单元加一只高音单元，不是左右分离的两只全频扬声器。
这是 R1 实物观察；目前没有原理图或逐线测量证明两单元之间采用 AK7755 数字分频、模拟分频，
或 TPA3118D2 的哪种输出拓扑，因此应称为“二分频/双单元单声道候选结构”，不能再把 I2S
channel 0/1 直接写成物理左/右扬声器。

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

### Audio A6：fail-closed DSP RUN/STANDBY 主机候选

A6 只推进 AK7755 状态机，不解除 TPA3118D2。固定 commit `762398dc` 的 AKM GPL driver
在 RUN 路径先将 C1 bit 0 `CKRESETN` 置 1，等待 10 ms，再将 CF bits 3/2
`CRESETN|DSPRESETN` 置 1 并再等待 10 ms；STANDBY 保持 `CKRESETN=1`，清除 CF bits 3/2。
当前 Linux 6.18 driver 按相同顺序实现，但把生命周期收紧为 PCM `.prepare` 进入 RUN、最后
一个打开的 stream `.shutdown` 回到 STANDBY。两次转换都会通过器件 repeated-start read
读回 C1/CF；任一状态不符合预期都会拒绝/结束 stream 并重新断言 AK7755 reset。

本阶段没有加入 OFREG/ACRAM：R1 原厂启动日志只证明 data2 PRAM/CRAM 被加载，尚无本机证据
证明 data2 OFREG/ACRAM 是进入 RUN 的前置条件。也没有增加 mixer/sysfs 接口或功放控制；
GPIO/安全 regulator、A4 DT 和 A5 全零工具保持不变。因此即使 A6 上板 RUN 成功，也只证明
DSP reset/clock 状态与零 PCM 数字链可以协同工作，不能声称 DSP 算法 routing 或声音输出正确。

主机整核构建、config 审计和 FIT 三个 component 的解包逐字节比较均已通过：

```text
297f3705058c04f1222b5ae494ed4b91eccd03ef32d74512363bdb23bf53d50f  kernel/overlays/linux-6.18.42/sound/soc/codecs/ak7755.c
972f9de6dd15ca5d6a6ed199cec66cb09559ee1ad8f94f1fe319d7a2ace2ae4a  kernel-mainline-6.18-ak7755-dsp-run-a6.config
7d645658443dc64ee39cbb20266dc57494b79dd63d32983d1684b02c2e9bf606  zImage-mainline-6.18-ak7755-dsp-run-a6
d624e87edbd1a124283d7ba31169b2847f62cf924a719ac6a4129419560c82c3  r1-initramfs-mainline-6.18-ak7755-dsp-run-a6.cpio.gz
657b8f9fff815e5590abd5a63608f237e7790b005ef0019a305bcd57662533e4  rk3229-phicomm-r1-mainline-6.18-ak7755-dsp-run-a6.dtb
bf7ff93e05c5c36407b04ebdf4dfcb16a32c86ca00cbfd348f4d5638721733de  r1-linux-mainline-6.18-ak7755-dsp-run-a6.itb
```

FIT 为 14,345,092 bytes，低于 16 MiB DFU RAM alternate。

#### A6 RAM-only 实机通过

实机版本为 `6.18.42-phicomm-r1-ak7755-dsp-run-a6-dirty`，排除了误启动 A5。测试前 debugfs
显示 AK7755 `shutdown` high、功放 shutdown physical low/ACTIVE_LOW、功放 mute high。前台
执行 `/bin/r1-pcm-clock-test 10` 后，driver 与工具依次输出：

```text
DSP RUN armed: C1=0x21 CF=0xc; amplifier controls unchanged
zero_stream_seconds=10 state=running
DSP STANDBY verified: C1=0x21 CF=0x0; amplifier controls unchanged
zero_stream_complete xruns=0
pcm_rc=0
```

`C1=0x21` 同时包含已配置的 32fs bit 5 和 `CKRESETN` bit 0；RUN 时 CF bits 3/2 均为 1，
最后关闭 stream 后两位均回到 0。测试后同一 debugfs 查询再次得到功放 shutdown physical
low/ACTIVE_LOW、mute high，证明 driver 日志中的“amplifier controls unchanged”也有独立 GPIO
证据。启动时 PRAM CRC `0x9916`、CRAM CRC `0x4453`、ID `0x55`、ASoC card/DAI 均正常。

因此 A6 已验证 AK7755 firmware→DAI→DSP RUN→零 PCM→DSP STANDBY 的可回退链，且没有 xrun、
没有解除功放。它仍没有证明 data2 算法的输入输出 routing、DAC 模拟输出、外部波形或扬声器
声道；在建立受控 mute/unmute 与低幅测试前继续禁止非零 PCM。

### Audio A7：单命令并发 capture/无线 soak 主机候选

A7 不改变 A6 driver 或 A4 DT，也不增加任何功放解除路径。新增的 freestanding
`r1-pcm-capture-test` 直接使用 ARM ALSA PCM UAPI，以 48 kHz、stereo、S16_LE、1024-frame
period、4096-frame buffer 从 `pcmC0D0c` 读取。它不会把原始 PCM 写入文件或串口，只累计
每声道 frames、nonzero、peak 和近似 RMS；任一 xrun 或两个声道全程均为零时返回失败。
这只能验证 capture 数字链有活动，不能把统计值解释成校准后的声压、增益或声道映射。

`/bin/r1-audio-soak [30..120]` 把原先分散的手工步骤收敛成前台命令。默认 60 秒，内部并发运行
全零 playback 和上述 capture，并在 PCM 活动期间依次执行 Wi-Fi scan、Bluetooth BR/EDR 与
LE scan。命令前后都检查功放 shutdown physical low/ACTIVE_LOW 和 mute high，同时要求
CPU online 精确为 `0-3`、PL330 DMA IRQ 有增长、新增 DSP RUN/STANDBY 成对出现且没有状态
验证失败。各外部工具都有 timeout；Ctrl+C trap 会终止两个 PCM 子进程并打印诊断目录，避免
再次出现后台命令占住串口提示符的问题。成功只输出一个最终判据：

```text
AUDIO_SOAK_PASS seconds=60 full_duplex=1 wireless_coexist=1 amp_safe=1 ...
```

主机侧静态构建结果：capture ELF 是 ARM EABI5 static、GNU_STACK `RW` 且没有运行时 UND；
initramfs 清单包含 playback、capture、soak、Wi-Fi 和 Bluetooth 五个工具；FIT 三个 payload
均用 `dumpimage` 抽取并与输入逐字节比较一致。FIT 为 14,348,252 bytes，低于 16 MiB DFU
alternate。SHA-256：

```text
88aea676bc170409e6248c8ce6837a648568a3aececd6cd068c23cb904cfd464  kernel-mainline-6.18-ak7755-audio-soak-a7.config
15e904c9c5686d6e719660440dec9df1c11a39325b13177c0547357c96cdba22  zImage-mainline-6.18-ak7755-audio-soak-a7
657b8f9fff815e5590abd5a63608f237e7790b005ef0019a305bcd57662533e4  rk3229-phicomm-r1-mainline-6.18-ak7755-audio-soak-a7.dtb
c224590b98a3bf1246d513a733561f1944c04fef7830d427642c6b04fbb22e5f  r1-initramfs-mainline-6.18-ak7755-audio-soak-a7.cpio.gz
e9be41a37de7a39f66ac9669bdbcf431b77a41170f5bcf6f36d157929f57c6dd  r1-pcm-capture-test
571c8927c705dd87aab9d935a30d90ddbfc3e4b47e3ad6b7f2b945af5e7c719d  r1-linux-mainline-6.18-ak7755-audio-soak-a7.itb
```

当前只完成主机构建，不能据此声称 R1 capture 或并发共存已通过。下一步经 DFU RAM-only 启动
A7 后只运行 `/bin/r1-audio-soak 60`；失败时保留输出中给出的 `/tmp/r1-audio-soak.*` 日志，
不继续到功放 unmute 或非零 PCM。

首轮实机运行没有进入上述链路：设备内 BusyBox `timeout` 是旧语法，`timeout 75 PROG` 将
`75` 当成程序并返回 127，导致 playback、capture 和无线工具全部未启动。该次输出仍验证
CPU online 为 `0-3` 且功放前后保持安全，但 `DMA=0`、无 DSP 状态变化只是上游工具没执行的
连锁结果，不能当作硬件失败。A7r2 已把所有调用改成 `timeout -t SEC PROG`，FIT description
和脚本首行均标记 A7r2；上述 SHA-256 已更新为修正版，等待重新上板。

#### A7r2 RAM-only 实机通过

修正版 banner 为 `R1 AUDIO A7r2`。功放测试前后均为 shutdown physical low/ACTIVE_LOW、
mute high；CPU online 为 `0-3`。60 秒期间 playback 与 capture 并发运行，DSP 在 4.181 秒
进入 RUN（`C1=0x21/CF=0x0c`），在 65.186 秒最后关闭 stream 后回到 STANDBY
（`C1=0x21/CF=0x00`）。两方向均处理 2,880,000 frames、`xruns=0`，PL330 DMA IRQ 从 0
增长到 5,622。并发期间 Wi-Fi、BR/EDR、LE 分别报告 31、1、18 个 scan entries；结束时
uptime 为 65.32 秒，CPU0-3 四列均有 IPI2/IPI3 活动。最终统一结果为：

```text
AUDIO_SOAK_PASS seconds=60 full_duplex=1 wireless_coexist=1 amp_safe=1
```

capture 的原始样本按设计未保存。统计为左右声道各 2,880,000 个 nonzero，但两路 peak 都只有
1 LSB，当前近似 RMS 因工具的低位缩放显示为 0。这足以证明 ALSA capture、I2S2/PL330 DMA
与并发 stream 生命周期确实活动，却不能证明麦克风模拟输入、ADC 增益或 data2 DSP routing；
更合理的当前解释是固定 1-LSB 量化偏置或数字底噪。下一步应在功放继续 shutdown+mute 时，
用受控近场声音/静音 A/B 和更精细的直流均值/极值/变化计数统计定位 capture 输入，不需要也
不允许先解除扬声器功放。

### Audio A8：保守实际外放实机通过

用户明确授权一次保守的真实扬声器验证。A8 不是开放通用 ALSA 播放，而是把真实出声窗口
限制在一个固定程序、一个独占内核安全门和一个短看门狗内。DT 删除 A4 的两个 always-on
安全 regulator，改为两个默认 disabled 的 fixed regulator：GPIO3_B7 的 `amp_output_enable`
禁用时为 physical low（TPA3118D2 SDZ/shutdown），GPIO3_C1 的 `amp_output_unmute` 禁用时为
physical high（MUTE）。最终 DTB 反编译确认两者分别为 active-high 与 active-low，machine
driver 是唯一 consumer。

`/dev/r1-audio-safety` 权限为 `0600`，还要求 `CAP_SYS_RAWIO`，并且只允许一个打开者。安全
状态机先开启 SDZ 但保持 MUTE，等待 20 ms 后才允许 unmute；回退顺序永远先 MUTE，等待
10 ms，再拉低 SDZ。ARM_MUTED 阶段最多 3 秒，unmute 后必须每 500 ms 内续期；文件关闭、
进程被杀、keepalive 超时、driver remove 或 system shutdown 都执行同一回退。即使 mute
regulator disable 报错，driver 仍继续尝试 shutdown，而不是停在可能出声状态。

`r1-audible-test` 是无 libc 的静态 ARM ELF，不接受幅度、频率或时长参数。它固定协商
48 kHz/stereo/S16_LE，先在 mute 状态预送 8 个全零 period，再短暂解除 MUTE；有效信号为
1 kHz、峰值 32/32767（约 -60.2 dBFS）、100 ms 阶梯淡入、100 ms 淡出、总长 1 秒。每个
PCM period 前续期内核看门狗，之后再送 4 个全零 period 并立即回到安全态。所有 ioctl、PCM
write、xrun 或退出路径都先请求 SAFE；若用户 Ctrl+C 或进程异常退出，内核 release/timeout
仍独立收口。这个测试可能因为 data2 DSP routing 未把 I2S 输入送到 DAC 而完全无声；无声
不能直接归因于功放或扬声器损坏。

主机侧已经完成 Linux 6.18.42 整核链接、DT 编译、ELF 静态/noexecstack/无运行时 UND 审计、
initramfs 清单检查，以及 FIT 三个 payload 的 `dumpimage` 抽取后逐字节比较。A8 FIT 为
13.7 MiB，低于 16 MiB DFU RAM alternate。SHA-256：

```text
9e828a366d46e316a095525798ff0a226c2cb27a03fb9e026b972a64aca1891e  kernel-mainline-6.18-ak7755-audible-a8.config
bf0b72a0ac4b1c1dfa39580b328a87d8676f36edebb39b383508630425825dfc  zImage-mainline-6.18-ak7755-audible-a8
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-audible-a8.dtb
191e3ecf9f2d0f990ccf7ddfeb0c5d23c9468cb3fec185dedecdeabf1fec4e28  r1-initramfs-mainline-6.18-ak7755-audible-a8.cpio.gz
2140038092475f22eb134d11a9c28e26114f12a494ebd6252d97f25edb0abc1b  r1-audible-test
8fd60b34bbb2de433ff58bd3553ad7bad7a1f85b25b2be4dd47cee56eb98ac1b  r1-linux-mainline-6.18-ak7755-audible-a8.itb
```

RAM-only 实机随后完成验证。用户明确报告听到“一段很小声”的短音，工具退出码为 0。串口
保留了两次完整触发序列：每次均先进入 safe，再 armed、DSP RUN、UNMUTED，约 1.1 秒后回到
safe，最后得到 DSP STANDBY；未出现 xrun/underrun。关键日志为：

```text
phicomm-r1-ak7755 sound: audible test armed: amplifier enabled but muted
ak7755 1-0019: DSP RUN armed: C1=0x21 CF=0xc; amplifier controls unchanged
phicomm-r1-ak7755 sound: audible test UNMUTED; 500 ms fail-safe active
phicomm-r1-ak7755 sound: audible test safe: mute+shutdown asserted
ak7755 1-0019: DSP STANDBY verified: C1=0x21 CF=0x0; amplifier controls unchanged
```

测试后 debugfs 显示 GPIO3_B7/`regulator-amp-output` 为 physical low，GPIO3_C1 对应 active-low
unmute gate 为 physical high，即 TPA3118D2 已重新 shutdown+mute。由此可验证的范围是：Linux
ALSA/I2S2/PL330 DMA、AK7755 data2 DSP routing、功放控制和扬声器至少存在一条实际可出声路径，
并且正常完成后的硬件状态安全收口。用户没有单独说明是否听到轻微 pop，因此不能把“无 pop”
写成已验证事实；也尚未验证左右声道、频响、失真、实际声压或较高音量。A8 固定工具继续保留，
不因此开放任意 PCM、任意幅度或长时间解除 MUTE。

### Audio A9/A9r2：固定低音量左右声道候选

A9 只回答 AK7755 原厂 data2 program 如何处理 Linux I2S stereo 输入，不改变 A8 的 machine
driver、安全 IOCTL、功放 GPIO 或看门狗。`r1-channel-test` 与已实机通过的 A8 工具共享同一
freestanding 源码，但由编译期宏固定进入声道模式，不接受任何参数。信号顺序为：

```text
左输入：1 kHz，约 -60 dBFS，750 ms，100 ms 淡入/淡出
全零间隔：A9 为 24 periods = 512 ms；A9r2 为 144 periods = 3072 ms
右输入：1 kHz，约 -60 dBFS，750 ms，100 ms 淡入/淡出
```

左段只写 interleaved channel 0，右段只写 channel 1；零间隔和每个 tone period 都继续发送
500 ms KEEPALIVE。开始前仍有 mute 状态下 8 个全零 period，结束后仍有 4 个全零 period，
随后立即 SAFE。整个 unmute 窗口内峰值不高于已经通过 A8 的 32/32767。若两个声音都来自同一
物理单元，或听起来没有方向差异，只能说明 data2 可能混合/重映射输入，不能直接判定某声道坏。

首轮 A9 上板后，用户报告 512 ms 间隔太短，无法明确听出间隔，并感觉左右声道可能被合并。
前半句是实机听感，后半句目前只是推断：R1 的物理声学结构、AK7755 data2 routing 或功放链
都可能让左右定位不明显。A9r2 因此只把全零间隔延长到 144 periods（3.072 秒），没有提高
峰值、延长单段 tone 或修改内核功放门。

主机已完成整核构建、静态 ARM ELF/noexecstack 检查、initramfs 清单、最终 DT GPIO 极性审计，
以及 FIT 三个 payload 抽取后逐字节比较。A9 DTB SHA-256 与 A8 完全相同，说明功放门的硬件
描述未漂移。A9r2 产物为 14,351,124 bytes，低于 DFU 16 MiB RAM window。SHA-256：

```text
563c4f252389e2a7b819086f9e88f1d0f0a5df19d66d2f1f3ebe115a58bf88c4  kernel-mainline-6.18-ak7755-channel-a9.config
7cb35a93a6956876c36866e6033d6ded77d41852569791add2e8fd1346270f62  zImage-mainline-6.18-ak7755-channel-a9
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-channel-a9.dtb
f574f78d6768844e2de47c13b71a71311ea94bee2be1d220cddb850adcaae435  r1-initramfs-mainline-6.18-ak7755-channel-a9.cpio.gz
a4d38aa741e1be7879b013f1b855d156d10820bf1fff727373123bd1023d3ca9  r1-channel-test
ea5948825cd359b44df9193e6984182d914bfc62281f8e322f0006fc4868ae36  r1-linux-mainline-6.18-ak7755-channel-a9.itb
```

当前 A9r2 证据类型仍是主机构建候选。实机验收需记录第一段与第二段分别从哪个物理扬声器发出、
3.072 秒中间是否确实静音、是否有 pop/持续噪声、工具退出码、DSP RUN/STANDBY 和最终 SDZ/MUTE。
未完成前不提高峰值。

A9r2 上板后，用户明确报告 3.072 秒“silence”阶段仍有声音。源码复核排除了测试工具忘记清零：
第一段结束后执行 `memset(pcm_period, 0, sizeof(pcm_period))`，随后连续写入 144 个完整 1024-frame
零 period。该窗口仍持续 KEEPALIVE，故 TPA3118D2 保持 UNMUTE；这里验证的是“数字输入为零”，
不是“硬件输出被静音”。可能原因按当前证据排序为：data2 DSP 仍输出内部状态/延迟尾音/麦克风
旁路或底噪；AK7755 DAC/模拟链及功放的可闻底噪；较低概率才是 MUTE/SDZ 电路或 DT 极性与
预期不同。若声音在最终 SAFE 后立即停止，则会进一步支持前两类并反对 GPIO 极性错误。

结合用户确认的一只低音单元加一只高音单元，左右声道定位测试现已暂停。下一项应改成单声道
Audio A10：在保持 PCM 连续全零和 DSP RUN 的条件下，比较功放 UNMUTE 与硬件 MUTE 两个窗口。
只有硬件 MUTE 也仍出声，才应优先怀疑 GPIO/电路拓扑差异；若硬件 MUTE 立即无声，则 GPIO
路径正确，后续应定位 AK7755 data2 routing、内部 mixer/旁路或模拟噪声。

进一步复核 hello/kasa 固定提交中的自动初始化路径后，`probe`/`resume` 只自动下载 PRAM 和
CRAM；OFREG/ACRAM 由独立 mixer control 或 ioctl 显式触发。R1 原厂启动日志也只记录 PRAM/CRAM。
因此 `ak7755_ofreg_data2.bin` 的存在只能证明原厂文件系统提供了该可选数据，不能证明当前
RUN 前漏加载 OFREG；A10 不引入这一额外变量。

A10 主机候选已构建，复用 A9 的 kernel/DTB，只在 initramfs 新增
`/bin/r1-audio-mute-ab`。工具先播放 1 秒、约 -60 dBFS 的 1 kHz 双输入参考音，然后在同一个
PCM stream 中持续写数字零，依次给出约 2 秒 `zero_unmuted_1`、约 2 秒
`zero_hardware_muted`、约 2 秒 `zero_unmuted_2`。中间硬 MUTE 通过已验证的安全 ioctl 完成：
先 mute、再 shutdown、以 muted 状态重新 enable；最终总会 SAFE。稳定音高若只在 UNMUTE
窗口出现，应称为窄带音/振荡而不是先验认定为 1 kHz；仅凭耳听不能确定频率。

A10 随后实机通过状态机与安全收口：1 kHz 参考段后，第一段 UNMUTE 数字零仍可闻；日志在
`37.918078` 秒进入 ARM_MUTED，用户确认随后无声；`39.883097` 秒重新 UNMUTE 后声音恢复，
第二段数字零结束后 SAFE 和 DSP STANDBY 均成功。硬件静音窗口约 1.965 秒。这验证了
GPIO3_C1 极性、regulator 控制和 TPA3118D2 MUTE 功能有效，排除了“所谓静音脚根本没有控制
功放”的解释。它只把边界收窄到功放开放时的音频链，仍不能单凭该测试区分 AK7755
DSP/DAC/模拟输入、板级反馈与 TPA3118D2 自身未静音噪声。若耳听为稳定音高，则 DSP/routing
或反馈的优先级高于普通宽带底噪，但频率仍未测量。

在 A10 验证 MUTE 后，A11 主机候选加入 `/bin/r1-melody-test`，仍复用同一 kernel/DTB 与安全门。
它不读取任意歌曲文件，而是本地合成 4.8 秒 C 大调短句 `C C G G A A G / F F E E D D C`；
两路输入相同，峰值仍为 32/32767（约 -60.2 dBFS），每个音符有短淡入淡出和尾部零间隔。
这个信号的用途不是评价音质，而是判断扬声器输出能否跟随多个已知音高，以及固定异常声是否
独立叠加。FIT 三个 payload 已抽取逐字节比较，尚无实机听感。

用户随后给出的 A11 主观听感是：音量很小，更像“收音机搜不到频”的噪声，未确认听到短句
音高变化。由于没有同步提交退出码、SAFE/STANDBY 或录音，这只能说明低电平听辨失败，不能
区分约 -60 dBFS 下只剩数十个 S16 量化级造成的失真、已知数字零底噪，或 DSP/routing 问题。

A12 因此改成可辨识的分级扫频：同一工具依次合成三次 300 Hz→2 kHz 线性扫频，每段 65536
frames（约 1.365 秒），peak 为 32/64/128，即约 -60.2/-54.2/-48.2 dBFS；每段前后约
43 ms 淡入淡出，段间 8192 frames（约 171 ms）数字零。最高档只是 A11 电压幅度的 4 倍，
远低于满幅，且继续由同一个 500 ms keepalive 和 SAFE 收口。该候选只用于判断扫频是否能压过
底噪并连续升调，不授权普通歌曲或任意 PCM。FIT payload 已逐字节比较，尚待实机。

用户随后确认三档峰值听感几乎不变，仍主要是小声“收音机失台”噪声。该结果没有附退出码和
SAFE 日志，但足以否定“只因 -60 dBFS 太低”的单一解释。重新对照原厂 DT、当前驱动、AKM
GPL 驱动和 AK7755EN 数据手册后发现更直接的代码缺口：原厂 DT 只给 I2S2、CPU bit/frame
clock master 和三个 GPIO，不配置 codec 内部路由；这些寄存器由原厂 codec driver 写入。

当前 6.18 驱动设置了 C0/C1/C2/C3/C6/C7 并启动 DSP，却从未写 `CE`。AKM 数据手册
`014006643-E-01` 的 CONT0E 表明确给出 `CE=0x00` 为复位默认，且 PMDAL/PMDAR/PMLO1 为 0
时 DAC 与 Lineout1 均 power-down/Hi-Z。与此相对，最接近 R1 的 AKM GPL driver
`hello/kasa@762398dc` 默认 `aec=1`，初始化时选择 `C8[7:6]=00`（DSP DOUT4）、写
`CE[7:6,2:0]=0xC7`、`D4=0xFF`，同时设置 CC/CD/DA/E6/EA。因此现状很可能是“数字 DSP 和
I2S/DMA 在运行，但 AK7755 DAC/Lineout 模拟输出没有建立，外部 TPA3118D2 放大了 Hi-Z/底噪”。
这也解释了 DMA、DSP RUN 和功放 MUTE 都能分别通过，而 PCM 幅度变化听不出来。

数据手册来源：Asahi Kasei Microdevices，*AK7755 DSP with Mono ADC Stereo CODEC + Mic/Lineout
Amp*，文档号 `014006643-E-01`，2018-08，可从
[AKM 产品页](https://www.akm.com/jp/ja/products/audio-voice-dsp/lineup-audio-voice-processor/ak7755en/)
及[原始数据手册镜像](https://www.mouser.com/datasheet/2/1431/ak7755en_en_datasheet-3515126.pdf)
复核 CONT08/CONT0E。这里的 R1 根因仍是结合本机源码与听感得出的高可信推断，尚待寄存器读回
和 A13 直通实机闭环。

下一步不直接照抄 `CE=0xC7` 或提高音量。A13 先在 MUTE 下读回关键寄存器；A 线只启用
SDIN1→DAC→Lineout1 直通，关闭 ADC/模拟输入并绕过 data2 DSP，以 -60 dBFS 扫频验证
I2S→DAC→功放；A 线清晰后，B 线再恢复原厂 DSP DOUT4、逐项加入原厂寄存器，定位问题究竟在
codec power/routing 还是 data2 DSP program。

### 2.8 Audio A13a：SDIN1 直通主机候选

A13a 已实现上述 A 线，但当前只有主机构建证据。driver 在 DAI 配置前读回并记录
C0/C1/C2/C3/C6/C7/C8/CE/D4/CF；然后保持已验证的 I²S/32fs `C6[5:4,2:0]=0x33`，
仅把 `C8[7:6]` 设为 `11` 选择 SDIN1，并把 D4 Lineout1 低四位设为 `0xF`
（0 dB）。PCM prepare 时才设置 `CE[2:0]=111`，只为 DAC L/R 与 Lineout1 上电；
CE 高位保持 0，因此 ADC/数字麦克风不上电。CF 只释放 CRESETN，DSPRESETN 保持 0。

RUN 后必须读回 `C6[5:4,2:0]=0x33`、`C8[7:6]=11`、`CE[2:0]=111`、
`D4[3:0]=F`、`C1.CKRESETN=1` 且 `CF[3:2]=10`；任何一项不符都拒绝 PCM prepare，
清 CE/CF 并重新断言 AK7755 PDN。最后一个 stream 关闭时先清 CF，再清 `CE[2:0]`，
并读回两者。外部功放仍只能由 A8 已验证的 root-only、exclusive、500 ms keepalive 安全门控制。

这里没有把 C6 清零。固定 AKM GPL driver 在 I²S/32fs 分支明确写
`C6 mask 0x37, value 0x33`；直通变量由 C8 选路实现。因此 A13a 不把串行数据格式也
变成未验证变量。第一次实机仍只运行原 A8 固定 1 kHz、-60.2 dBFS、1 s 工具，
不运行 A12 的 -54/-48 dBFS 档位。

A13a 实机已通过寄存器和安全状态机验证。配置前基线为 C0/C1/C2/C3/C6/C7/C8/CE/D4/CF
全 0；PCM RUN 读回为 `C6=0x33 C8=0xc0 CE=0x07 D4=0x0f C1=0x21 CF=0x08`，正好匹配
直通合同且 DSPRESETN 仍为 0。正常关闭后读回 `CE=0 CF=0`，工具输出 PASS，功放
GPIO 前后都是 SDZ physical low、MUTE physical high。用户的听感是“好像有点不一样，
噪声中混有一点点嘟声”。这证明直通修改让 Linux PCM 开始影响扬声器输出，支持先前
“DAC/Lineout 未上电”的根因推断；但信号仍被噪声淹没，不能声称已获得干净 1 kHz 或
完成音质适配。

A13b 随后在同一 A13a kernel 上运行了已审计的三档扫频。用户能听到 300 Hz→2 kHz 的
连续升调，-60/-54/-48 dBFS 三档音量也逐档增加，主观增幅类似 10%→30%→50%；每档仍有
固定底噪。工具输出 PASS，DIRECT RUN/STANDBY 与最终功放 shutdown+mute 均完整通过。
这证明 I2S 数字幅度可以穿过 SDIN1→DAC→Lineout1 路径影响输出，排除了“数据完全错乱”以及
D8/D9 固定衰减导致幅度不响应作为首要解释。每档 6 dB 对应电压约 2 倍，主观响度本就不会
线性翻倍，因此不能仅凭听感比例认定增益异常。

下一步 A13c 仍不改 codec 寄存器，只把同一扫频提高为 -48/-42/-36 dBFS；最高 peak=512，
约为 S16 full scale 的 1.6%。若固定底噪被更高信号明显压过，优先将现象归为测试幅度过低加
尚未优化的模拟噪声底；只有信噪比仍不改善，才单独设计 codec powered/Hi-Z/硬件 MUTE 噪声
A/B，并逐项审计 CC/CD/DA，而不是整组照抄 vendor 初始化。

### 2.9 Audio A14：受控公版音乐候选

用户反馈提高幅度后仍能听到底噪，并选择先进行受控音乐播放。该主观反馈没有附 A13c 的
工具 PASS、DIRECT STANDBY 或最终 GPIO，因此不把 A13c 标记为完整实机回归；它只说明固定
底噪尚未消失。

A14 不开放任意 WAV、歌曲文件或长时间播放，而是在静态工具内合成公版《欢乐颂》开头 16 个
音符。内容为单声道并复制到两个 I2S slot，避免把箱体的低音/高音双单元误当成立体声；基音
加入 1/3 权重的二次谐波，使旋律比纯正弦更易听辨。全程约 10.9 秒，peak=512，即约
-36 dBFS；每个音符有约 43 ms 淡入、约 43 ms 淡出和约 85 ms 数字零尾部。

内核、DTB、AK7755 SDIN1→DAC→Lineout1 寄存器和外部功放安全门均逐字节复用 A13a。
工具仍先 SAFE、ARM_MUTED、打开并 prepare PCM、写零 preroll，随后才 UNMUTE；播放期间每个
period 都刷新 500 ms keepalive，正常退出、错误、关闭或被杀均由现有内核安全门回到硬件
MUTE+shutdown。该候选用于判断旋律是否可辨，不用于音质或最大音量测试。

用户随后确认 A14 播放期间底噪始终以近似固定响度叠加。由于 A13a 已将 DAC 输入明确切到
SDIN1、保持 DSP reset，且 A13b 已验证扫频音高和数字幅度响应，PRAM/CRAM/DSP routing
不再是这轮底噪的第一嫌疑；固定噪声更像位于 DAC 数字输入之后的固定 noise floor 或增益链。

### 2.10 Audio A15：AK7755 DAC soft-mute 边界 A/B

[AK7755EN 数据手册 `014006643-E-01`（AKM，2018-08）](https://www.mouser.com/datasheet/3/5939/1/ak7755en_en_datasheet.pdf)
规定 CONT1A（write `0xDA`）D5 为 DAC digital soft mute，D4 在 system reset 期间必须写 1。
现有 A13a/A14 基线读回 `DA=0x00`，而参考驱动写 `0x10`，因此这里确有一个初始化合同缺口。
A15 在 reset 阶段写 `DA=0x30`，外部功放仍硬件静音时释放 DAC mute 到 `DA=0x10` 并等待
25 ms；停机和启动失败路径都先恢复 `DA=0x30`、等待 25 ms，再 reset/power-down。

新增 `/bin/r1-dac-mute-ab` 保持同一个 zero-PCM stream 和 TPA3118D2 enable+unmute 状态，依次
输出 `zero_unmuted_1`、`zero_dac_muted`、`zero_unmuted_2` 三个 2 秒窗口。中间只切换 DA.D5，
每次写后等待并读回 `DA=0x30/0x10`；现有 500 ms keepalive、独占门和 close-to-SAFE 不变。
它用于定位噪声边界，不是普通播放：若中间窗口明显安静，噪声来自 DAC mute 之前或 DAC 数字
路径；若完全不变，优先检查 AK7755 模拟 lineout、TPA3118D2 与板级供电/增益；部分降低则表示
至少存在两个噪声源。

A15 已在 R1 连续运行两次。每次都读回 `DA=0x10→0x30→0x10`，随后完成硬件 SAFE 和
`DIRECT STANDBY`；用户确认三个窗口直到结束都保持同样的固定底噪。RUN 同时读回
`C8=0xc0 CE=0x07 CF=0x08`。按数据手册字段解释，C8 选择 SDIN1 而不是 DSP DOUT，CE 的
ADC L/R、ADC2 和 Lineout2/3 位全部为 0，CF 的 line-in 与 DSP release 位也为 0。新增的
C0/C9/D3 强制配置又分别关闭 analog input、OUT3 模拟 mixer 和 OUT3 gain。因此当前 OUT1
路径没有 ADC、LIN、DSP 或 OUT3 mixer 可供“误混”；而 DAC 数字软静音不改变噪声，把边界
进一步推到 DAC 数字静音之后。C0/C9/D3 的显式清零和读回是下面 A16 新增的防御性合同，
不是对 A15 实机日志的追记。

### 2.11 Audio A16：DAC 模拟核、Lineout1 与功放边界 A/B

AK7755EN 数据手册的 CONT0E 表将 DAC L/R 与 Lineout1 分成独立电源位，并说明
`PMLO1=1, PMDAL=0` 时 OUT1 由 Lineout1 驱动到 AVDD/2；`PMLO1=0` 时 OUT1 为 Hi-Z。
A16 保持同一个 zero-PCM stream、TPA3118D2 enable+unmute 和外部 GPIO 不变，依次进入三个
2 秒窗口：

1. `zero_dac_muted`：`DA=0x30, CE=0x07`，DAC 数字静音，DAC 与 Lineout1 仍上电；
2. `zero_dac_off_lineout_vmid`：先静音，再关 DAC L/R，读回 `CE=0x04`，Lineout1 仍以
   AVDD/2 驱动 OUT1；
3. `zero_lineout_hiz`：再关 Lineout1，读回 `CE=0x00`，OUT1 置为 Hi-Z。

每次边界切换还强制读回 `C0/C8/C9/CE/CF/D3/D4/DA`，要求 analog input、全部 mixer、ADC、
LIN、DSP 均保持关闭。关闭、错误、进程死亡和 500 ms keepalive 超时仍由内核回到功放
MUTE+shutdown。结果解释必须考虑 Hi-Z 会让 TPA3118 输入悬空：第一、二窗口有明显差异指向
AK7755 DAC 模拟核；前两者相同而第三个变化指向 Lineout1/功放输入交界；三者近似相同则
TPA3118D2 自噪声、增益或供电成为首要嫌疑。Hi-Z 反而更响不能解释成 AK7755 产生更多噪声。

A16 主机构建已通过，kernel 编译日志确认重新编译 codec 与 machine driver；FIT 为
14,362,256 bytes，低于 16 MiB DFU RAM alternate。`dumpimage` 抽取的 kernel/initramfs/DTB
与输入逐字节一致，initramfs 中的 `/bin/r1-analog-boundary-ab` 也与静态 ARM 输入 ELF 一致。
默认 DFU 脚本已锁定该 FIT。

A16 随后在 R1 实机退出 0；日志中五次完整运行都重复得到 `CE=0x07→0x04→0x00`，每次
`C0=0x35 C8=0xc0 C9=0 D3=0 CF=0x08 DA=0x30` 合同成立，并在最后进入
`DIRECT STANDBY CE=0 CF=0` 与功放 SAFE。用户确认第一段 DAC digital mute 与第二段
DAC-off/Lineout1-AVDD/2 的底噪相同，只有第三段 Lineout1 Hi-Z 后底噪性质改变。因此 DAC
模拟核不再是主要噪声源；变化边界位于 Lineout1 输出阻抗与 TPA3118 输入交界。仅凭“不同”
仍不能区分 Lineout1 buffer 自噪声与 Hi-Z 后功放输入悬空拾噪。

为核对是否遗漏外围 GPIO，重新逐项审计原厂 boot/recovery DTB（两份 DTS SHA-256 均为
`12ca8dd93e06d1618c359bb69f8167643213687e7c0f943684c6d8cd00216fb`）和原厂启动日志。真实
`ak7755@19` 节点只声明 GPIO1_A3 PDN、GPIO3_B7 TPA3118 SDZ、GPIO3_C1 TPA3118 MUTE；原厂
日志也只打印 `pdn_gpio=35`、`sdz_gpio=111`、`mute_gpio=113`，随后成功注册 `RK_AK7755`。
A16 最终 DTB 的三根物理 pin 完全相同，且 A10 已独立证明 GPIO3_C1 hardware MUTE 能立即
消音。因此没有“漏掉第四根 AK7755/TPA3118 GPIO”的 DT 证据。

原厂万能板 DT 另有 `es8323@11` 的 GPIO1_A0/A1 `pa-en1/pa-en2`，但同地址的 ES7243 先被
实例化并持续 I2C 失败；原厂日志没有 ES8323 probe/card，ALSA 列表只有 HDMI、MA4、AK7755、
SPDIF。这两个 GPIO 属于备用 codec 节点，不能移植到 R1 AK7755 路径。板上仍可能存在由电阻
固定的 TPA3118 gain、耦合电容或未被 DT 描述的纯模拟网络，但那不是漏配 GPIO。

### Audio A17：Lineout1 模拟音量边界

AKM GPL 参考驱动在 `Line Out Volume 1` 控件中把 D4 低四位定义为从 -30 dB 起、每级 2 dB；
其紧邻注释同时写明最低端是 mute，而不是普通 -30 dB，初始化则明确写 `0xF` 为 0 dB。
A17 当时把 raw 0 标成了 -30 dB，现更正为 mute endpoint。A17 不再改变 CE 或输出阻抗，而在同一个 DAC-muted
zero-PCM stream 中依次保持：

1. D4=`0xF`，Lineout1 0 dB；
2. D4=`0x8`，Lineout1 -14 dB；
3. D4=`0x0`，Lineout1 mute endpoint（A17 工具旧标签误写为 -30 dB）。

三个约 2 秒窗口中 `DA=0x30`、`CE=0x07`、Lineout1 low-Z、TPA3118 enable+unmute 均不变。
内核只接受严格的 `0xF→0x8→0x0` 顺序，每步读回 C0/C8/C9/CE/CF/D3/D4/DA；进程退出、错误
或 500 ms keepalive 超时仍由硬件 MUTE+shutdown 收口。若底噪随 D4 明显衰减，噪声位于
Lineout1 volume 之前；若近似不变，优先级转向 Lineout buffer 之后、耦合网络或 TPA3118。
该解释不依赖第三段 Hi-Z 的悬空输入听感。实机上用户报告三段底噪基本不变，但 A17 只有
第一段之前的一次参考音，没有证明同一可听信号在每个 D4 档位均按预期衰减。因此当前只能把
“固定噪声不响应 A17 写值”记为用户听感，不能仅凭它排除 D4 控制未落到板上实际输出的
软件路由/通道错误。下一步应使用 `F/8/1`（0/-14/-28 dB）逐档重复完全相同的参考音并穿插
zero window；避开 raw 0 的特殊 mute 语义。

A17 主机构建、整核链接和 FIT 静态验证已通过。最终 FIT 为 14,363,456 bytes，低于 16 MiB
DFU RAM alternate；三个 FIT payload 与 ITS 输入逐字节一致，initramfs 中的工具与输入 ELF
逐字节一致。R1 已完成听感比较并得到上述“基本不变”的用户报告；因缺少逐档参考音自校验，
仍不将它写成最终噪声定位结论。

```text
5cc747f499b80f550bb431c20f13a90990f9a22cfca8a65aff94f9eaf14d055a  ak7755.c
9342f8316545c0c4e7fd452f4c1e42ccfce31b3bed57b20906fd63374abf97d4  phicomm_r1_ak7755.c
c93d1354afdeaf13569c6221bef52b28bfa41740f83d30b8eb25a87d50a7340c  r1-lineout-volume-ab
51c35ddd8e6c2c0e784637defbf818fb6308cfa2f574c24b172c1fc94f5ed07a  zImage-mainline-6.18-ak7755-lineout-volume-a17
e3f486ccdc699fb99c85fb5542c661e0483211fe83df9642ac086cb55c962f4c  r1-initramfs-mainline-6.18-ak7755-lineout-volume-a17.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-volume-a17.dtb
f37cb463682ea5b4acf1baff8b71a9fbdf5acc9a5d38a1336f83a6866d10aa2b  r1-linux-mainline-6.18-ak7755-lineout-volume-a17.itb
```

### Audio A18：D4 可听增益自校验

A18 补上 A17 缺失的正向控制。它避开 raw 0 的特殊 mute endpoint，使用 D4 低四位
`F/8/1`（0/-14/-28 dB）；每一档都播放完全相同的一秒 1 kHz stereo、约 -36 dBFS 参考音，
随后保持 DAC unmuted 写约两秒 zero PCM。只有切换 D4 的短窗口先置 DAC soft-mute，写入并
读回 D4 后再解除 DAC mute。Lineout1 始终 low-Z，TPA3118 仍由 root-only 独占门、500 ms
keepalive、close/error 自动 SAFE 控制。

因此实机解释分成两个独立观察：若三段 tone 不按 0/-14/-28 dB 明显衰减，A17 的 D4 噪声
定位无效，应继续检查实际输出通道/寄存器；若 tone 明显衰减而三段 zero 固定底噪不变，
则 PCM、SDIN1 routing 与 D4 Lineout1 控制均已形成正向闭环，固定噪声位于该音量控制之后。
后者仍可能是软件控制的 I2S clock 耦合，下一单变量应比较 PCM/I2S clocks 运行与停止，而不是
直接宣判 PCB 或 TPA3118 损坏。

A18 已完成整核链接和主机静态验证。FIT 三个 payload 解包后与输入逐字节一致；initramfs 内
`/bin/r1-lineout-selfcheck` 与输入 ELF 一致，静态 ARM EABI5、GNU_STACK RW、无 GLOBAL UND；
System.map 包含 `ak7755_component_set_lineout_volume`，最终 localversion 为
`-phicomm-r1-ak7755-lineout-selfcheck-a18`。默认 DFU 脚本已 hash-pinned 到 A18。

```text
749e32ab12d67d6e2f8ad486e7e78006efa692af484ff32ace38a66c319f546d  ak7755.c
51efdf7c923efd9c2826a694b517cda3fa425cd94781ebfee93b0f2ead1b54e1  phicomm_r1_ak7755.c
dc72960a9fc0a30840f4801911a629be2ca6dbf241a468c69524fc3fff58c455  r1-lineout-selfcheck
7cfeffca1d3c2cb2825486736ea2a012ad541cc2bdd04865cd332f4469bf64ac  zImage-mainline-6.18-ak7755-lineout-selfcheck-a18
fde9534b2145cbea56a6e9ab116c98e1b015906ac4f956a8f85fc444bcd7c7d2  r1-initramfs-mainline-6.18-ak7755-lineout-selfcheck-a18.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-selfcheck-a18.dtb
c8402952e3a9f7ced94d5fd5793c4885f5d4c4d3ae4f8bd8f51a96f0295d823f  r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18.itb
```

A18 首次实机没有进入可听窗口：`snd_soc_dai_prepare()` 的 direct-output RUN 回读合同返回
`-EIO`。machine gate 随即恢复功放 mute+shutdown，codec 内部失败路径也先 soft-mute 并清除
CE/CF，因此没有误放声音；但 codec DAI 外层又把 PDN/reset 断言，令同一次启动无法重试。
这不是总线死锁，也不是功放被锁开，而是错误收口过度。

A18r2 删除了该多余的 PDN 断言。原有 fail-closed 顺序不变，失败时新增一行
`DIRECT RUN contract mismatch`，逐项输出 C6/C8/CE/D4/DA/C1/CF 的 masked actual/expected；
随后仍保持 codec soft-muted/powered-down 和功放 SAFE，但允许再次打开 PCM 重试。主机构建明确
重编译 `sound/soc/codecs/ak7755.o`，最终 FIT 为 14,363,832 bytes，三个 payload 解包后与输入
逐字节一致。默认 DFU 已改为 A18r2 并锁定下列 SHA-256；实机根因仍待回读值，不能把一次
`-EIO` 推断成器件损坏或固定时序问题。

```text
9020a39d4ddbe21613712560595b860e9c2d3f1f5e9be831d75d8c959ed74c55  ak7755.c
a5f0d660102ddd2146477e560ec009f493c65c41a3c15d9dbe03666749ca5421  zImage-mainline-6.18-ak7755-lineout-selfcheck-a18r2
fde9534b2145cbea56a6e9ab116c98e1b015906ac4f956a8f85fc444bcd7c7d2  r1-initramfs-mainline-6.18-ak7755-lineout-selfcheck-a18r2.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-selfcheck-a18r2.dtb
f2dc1713fdaec1e73632c33787582c56deb311dc63a8f22ce50ca744463a0240  r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18r2.itb
```

A18r2 实机把失败拆成了两个确定的软件状态问题。第一次 RUN 的七项合同全部匹配，随后完整播放
0 dB tone 和 zero；用户仍听到噪声。切换到 D4=`0x08` 时，DAC mute、D4 写入和 DAC unmute
读回均成功，但累计约 70 ms 的控制停顿使 4096-frame/48 kHz 缓冲 underrun。工具因 xrun
fail-closed，standby 清了 CE/CF，却没有把 D4 从 `0x08` 恢复到 `0x0f`。后续两次 prepare
的 actual/expected 证明唯一差异就是 `D4=0x8/0xf`，其余 C6/C8/CE/DA/C1/CF 全部一致。

因此 A18r3 在 DAC 尚处于 soft-mute 时为每个新 PCM stream 显式恢复 D4=`0x0f`，让测试可以
确定性重入；仅 `R1_LINEOUT_SELFCHECK_TEST` 把 ALSA buffer 从 4096 增到 16384 frames，覆盖
切档控制窗口而不改变信号幅度、D4 值、功放门或 500 ms fail-safe。当前噪声证据仍只覆盖
0 dB zero 窗口，不能提前声称三档噪声不变。

```text
2bea30dfc109de403c2f87f362266e8f6033d734dae07def7defaba23a2a7f89  ak7755.c
d9f0eb88b42dfb3114859cf401524a1881629251dc8da15fd2d83727c78ad4c0  r1-lineout-selfcheck
f36747959c06b39d72dae30b27de3ccec4ea75300edbb202a4a5654d726ab718  zImage-mainline-6.18-ak7755-lineout-selfcheck-a18r3
377b8b290f551330e965e95a346faf315de5f91159d384a2acbd35199e64abb5  r1-initramfs-mainline-6.18-ak7755-lineout-selfcheck-a18r3.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-selfcheck-a18r3.dtb
7d2672ffc49c5e0becfcd466e249a4272d6307fb7795086fc4f5a487b7b9eee5  r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18r3.itb
```

A18r3 实机听感闭环完成：用户确认 0/-14/-28 dB 三段 1 kHz tone 明显逐档变小，而紧随各档的
zero 底噪音量不变。这证明 D4 确实作用于板上可听信号，固定噪声位于 D4 之后；PCM 数据、
SDIN1 直通选择和 D4 Lineout1 音量寄存器不再是首要嫌疑。

A19 逐字节复用 A18r3 kernel 与 DTB，只在 initramfs 新增 `/bin/r1-i2s-clock-ab`。工具先持续
写 zero PCM 两秒，再保持 codec fd、功放 enable+unmute 与安全门不变而执行 ALSA DROP，
每 100 ms 刷新 keepalive 并观察两秒，之后 PREPARE 并恢复 zero PCM 两秒。DROP 应触发 CPU
DAI 停止 BICK/LRCK/SDIN，而不会调用 codec DAI shutdown；因此该实验把数字时钟耦合从模拟
后端中单独切出。异常、close 或 500 ms 超时仍自动 mute+shutdown。

```text
95ef1ee5b8b92d566ed2764a58f95cc452fe64a5bbad435291db42acb7f3803a  r1-i2s-clock-ab
855f92832e9de5ae0c182c19a07ad66e7f0892efc6a9f5933f3c32c8336d5859  r1-initramfs-mainline-6.18-ak7755-i2s-clock-a19.cpio.gz
f36747959c06b39d72dae30b27de3ccec4ea75300edbb202a4a5654d726ab718  zImage-mainline-6.18-ak7755-lineout-selfcheck-a18r3
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-selfcheck-a18r3.dtb
1bba7b088f3d6ba40c1bce737ead66266786facd4489e513a19c6565f72b7c54  r1-linux-mainline-6.18-ak7755-i2s-clock-a19.itb
```

A19 已由用户连续运行三次。每轮约 6 秒，日志均显示 DIRECT RUN 成功、结束后 DIRECT STANDBY
读回 `CE=0/CF=0` 且功放 SAFE；过滤日志没有 xrun/underrun。用户确认 running、DROP、running
三个窗口的底噪全程相同。这强烈反对 PCM 数据活动是噪声源。窗口内随后实测
`i2s2_src/i2s2_frac/i2s2_pre/sclk_i2s2` 三段均为 enable/prepare `1/1`。
Linux 6.18 `rockchip_i2s.c` 源码说明这不是 DROP 失败：trigger STOP 会清 `I2S_DMACR.TDE`、
把 `I2S_XFER.TXS/RXS` 置 STOP 并清 FIFO；mclk 则只在 runtime suspend 中
`clk_disable_unprepare()`。由于实验故意保持 PCM fd 打开，CCF 引用不归零是预期行为。
随后三个听感窗口内的 MMIO 采样给出：

```text
              running 1    DROP/stopped   running 2
DMACR 0x10    000f0110     000f0010       000f0110
XFER  0x1c    00000003     00000000       00000003
```

因此 DROP 确实清除了 TX DMA enable，并把 TX/RX serial engine 从 START 切到 STOP；恢复后两者
又回到原值。用户同时确认三段底噪完全不变。结合 A18r3 中 D4 会衰减参考音但不衰减底噪，
当前可把 PCM DMA、SDIN1 数据和 I2S serial switching 从固定底噪的首要嫌疑中排除。边界位于
D4 之后，更应检查 AK7755 Lineout1 输出级之后的板级模拟链、TPA3118 输入/增益/电源，以及
原厂是否有尚未复刻的模拟初始化。下一步优先做原厂 Android 同硬件 idle-noise A/B，并提取
原厂 kernel/module/用户态寄存器序列，而不是继续盲调 PCM 格式。

原厂 boot/recovery 共用的 7.4 MiB zImage 随后被定位到第二个 LZO stream，解出 14.6 MiB ARM
raw kernel；从中恢复的 90,294 个 kallsyms 包含 `ak7755_init_reg()` 等完整局部符号。定点
反汇编确认原厂初始化在 codec/DSP system reset 下设置 `CD.D6=1`、`DA.D4=1`、`E6.D0=1`、
`EA.D7=1`。这与 AKM 数据手册 014006643-E-01 的 reset contract 完全一致；当前驱动此前只
设置了 DA.D4。A20 因此只补 CD/E6/EA 三个缺项并立即读回，保持 SDIN1 直通、D4、PCM、DT
和功放安全门不变，避免把原厂整张寄存器表未经语义审计地照抄。该候选已完成整核重编、FIT
payload 逐字节核验。实机启动读回 `CD=0xc0 DA=0x10 E6=0x1 EA=0x80`，之后多次
`/bin/r1-audible-test` 均完成 DIRECT RUN、可听窗口、DIRECT STANDBY 和最终功放 SAFE；用户
确认固定底噪与 A19/A18r3 相同。由此可把这些 reset-contract 位归类为必须保留的正确性修复，
但不能再把它们当作固定底噪解释。

```text
9c76a5d3f0a84f2496ab582b44d6f9d65bb3c57d1df376b6e9ded8e8d9d86db9  zImage-mainline-6.18-ak7755-reset-contract-a20
855f92832e9de5ae0c182c19a07ad66e7f0892efc6a9f5933f3c32c8336d5859  r1-initramfs-mainline-6.18-ak7755-i2s-clock-a19.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-reset-contract-a20.dtb
ca9ed449b8bc4422bfd4192e07e5349466babc49182b163b74e5ac4b2bc32aa7  r1-linux-mainline-6.18-ak7755-reset-contract-a20.itb
```

### Audio A21：原厂 kernel/codec driver 的 RAM-only 噪声 A/B

A20 否定了目前唯一从原厂反汇编得到、且有 datasheet reset contract 支撑的明显寄存器缺项。
继续从原厂大表中逐项猜值的区分力很低，所以下一实验把整个 kernel codec/machine driver 一次
替换为同一台 R1 保存的原厂 3.10 实现。A21 不启动 Android：FIT 使用本地
`backup/unpacked/boot/kernel` 和 `rk-kernel.dtb`，ramdisk 则复用 A19 的 rescue initramfs；
`/init` 只挂 proc/sysfs/devtmpfs，不挂载或修改 eMMC。

对原厂 DTB 的 overlay 只有两类更改：保留 open OP-TEE 使用的
`0x68400000..0x686fffff`，以及把 cmdline 换成 `rdinit=/init maxcpus=1` 的串口救援配置。
原厂 3.10 kallsyms 已证实存在 `early_init_fdt_scan_reserved_mem`；单核则故意移除旧 SMP/PSCI
与音频噪声无关的变量。原厂 DT 音频节点和 GPIO 没有被改写。A21 首轮只运行五秒数字零 PCM，
由原厂 ASoC mute/status 路径打开后级；不播放 tone 或音乐。进程正常结束会 DROP/close PCM，
首次测试仍应准备通过串口中断进程并远离扬声器。

A21 首次实机没有到达该步骤。原厂 eMMC 枚举 `8GME4R` 后，`kmmcd/mmc_rescan` 调入厂商
`rkpart_setup_real()`；寄存器与反汇编共同证明它以 `r0=0x9` 调用 `strchr(..., ':')`，产生
低地址 Oops，随后 workqueue 二次崩溃。当前救援 cmdline 刻意没有原厂
`mtdparts=rk29xxnand:...`，而这份 3.10 厂商 parser 没有安全处理该状态。该失败发生在音频测试
之前，与 AK7755/底噪无关，也不能算原厂驱动 A/B 结果。

A21r2 保持原厂 kernel 和所有音频节点不变，仅在项目 overlay 把
`/rksdmmc@30000000`、`30010000`、`30020000` 全部设为 `disabled`。本实验的 kernel、ramdisk、
DTB 本来就来自 DFU RAM，不需要 eMMC、SD 或 SDIO；禁用它们比恢复一整条 Android 分区 cmdline
更小、更安全。构建脚本现会强制核对三个 status 后再做 FIT payload 比较，专用 DFU 下载器也
曾改为只接受 A21r2 hash。A21r2 实机已经越过分区 parser、进入 rescue shell，且
`/proc/partitions` 为空；原厂 AK7755 driver/machine card 注册成功。但原厂 DT 同时注册
HDMI、MA4、AK7755 和 SPDIF，顺序分别为 card 0/1/2/3。现有 zero-PCM 工具固定打开
`/dev/snd/pcmC0D0p`，所以 `pcm_rc=0` 和“完全无声”实际来自 HDMI stream，并没有经过
I2S2、AK7755 或 TPA3118，不能用于比较底噪。

A21r3 继续使用同一原厂 kernel、AK7755 节点与 A19 initramfs，但在 overlay 中禁用 HDMI、
MA4、SPDIF 和其余无关 machine-card 节点，使 `RK_AK7755` 成为唯一声卡/card 0；同时禁用
仍会等待约 90 秒失败的 wireless platform glue。AK7755 节点本身仍为 `okay` 且内容未修改。
构建脚本新增相关 status 断言，专用 DFU 下载器现只接受 A21r3。

A21r3 随后完成了 PCM/GPIO 实机闭环，但后续证明它不是有效播放正向控制。`/proc/asound/cards`
只有 card 0 `RK_AK7755`，PCM 也只有
`00-00 AK7755 PCM ak7755-AIF1-0`。十秒 zero PCM 返回 `xruns=0`、退出码 0；原厂内核在 stream
边界明确打印 `ak7755_set_dai_mute: unmute` 和 `mute`。运行中 GPIO35=high、GPIO111=high、
GPIO113=low；结合本项目 A10 已验证 GPIO113 high 是硬件 mute 窗口，可确认原厂测试期间
AK7755 上电、TPA3118 enable 且 mute 已解除。用户报告整个 stream 完全无声、没有当前 6.18
链中的固定底噪；但 A21r4 的非零 tone 随后也完全无声，因此这里不能再称为安静的有效音频链。

因此当时关于硬件边界的结论需要撤回：PA GPIO 解除静音并不等于 AK7755 DSP→DAC→Lineout
已建立有效路径。A18r3/A19/A20 仍证明当前 6.18 链中的噪声不跟 D4、PCM DMA、I2S serial
activity 或 reset-contract 三位变化；A21r3 只提供未完成 Android 路由时的寄存器对照。下一步先使用原厂
3.10 已存在的 ASoC `codec_reg` debugfs，在 idle、zero-PCM running、close-after 三个状态读取
硬件寄存器，再对 A20 做相同只读快照；没有寄存器差分前不继续写新值。

原厂三态快照现已完成。idle→running 时 C1 `00→01`、CE `00→0f`、CF `00→0c`、DA
`30→10`；close-after 中 C1/CE/CF 保持 running 值，只有 DA 回到 `30` soft-mute。其余
C0..DE 均稳定：

```text
       idle  running  after
C0      0d      0d      0d
C1      00      01      01
C2      10      10      10
C6      00      00      00
C8      00      00      00
CA      60      60      60
CD      40      40      40
CE      00      0f      0f
CF      00      0c      0c
D0      40      40      40
D3      0f      0f      0f
D4      ff      ff      ff
D5/D6/D7 30     30      30
D8/D9   18      18      18
DA      30      10      30
DD      30      30      30
```

与当前 A20 已打印的 DIRECT RUN 相比，至少 C0/C1/C2/C6/C8/CE/CF/D4 都不同。C8=`00`
对应原厂 data2 DSP 输出路径，而当前排障链用 C8=`c0` 强制 SDIN1 直通；D4=`ff` 与 CE=`0f`
也表明原厂两组 Lineout/相关 power 状态均配置，当前只有 D4=`0f`、CE=`07`。这些事实能解释
为什么 A20 的三项 reset 补写没有改变噪声，但尚不能证明应把整张原厂表照抄到 6.18。

A22 因此不写任何新寄存器。它逐字节复用 A20 kernel 和 DT，只在 A19 initramfs 加入约 8 KiB
的 freestanding `/bin/r1-ak7755-regdump`；工具使用与 driver 相同的 command-byte + repeated-start
I²C 协议读取 C0..EA，逐项打印，不包含 write ioctl。下一实机步骤是在 A19
`r1-i2s-clock-ab` 已知会产生底噪的 unmute/running 窗口抓快照，再与上表逐项对比。

```text
cbe75c041e6242eaab82b6605c197cf1b8d92815cab6d8702ee31de0542cab83  r1-ak7755-regdump
416af28683590255c2cf7a53ea3c5934769bf7e97a971f719f6b545e46483691  A22 initramfs
0d76869bb4aa67b776ad2e9fafb53cfc43cd4ef8a93dfd6610cd3264b950e400  A22 FIT
```

A22 实机 running 快照与内核 DIRECT RUN 合同完全一致：

```text
c0=35 c1=21 c2=10 c3=f0 c4=00 c5=00 c6=33 c7=f3
c8=c0 c9=00 ca=00 cb=00 cc=00 cd=40 ce=07 cf=08
d0=40 d1=00 d2=00 d3=00 d4=0f d5=30 d6=30 d7=30
d8=18 d9=18 da=10 db=00 dc=00 dd=30 de=00
df=00 e0=55 e1=00 e2=00 e3=00 e4=00 e5=00 e6=01
e7=00 e8=00 e9=42 ea=80
```

idle 只在 C1/CE/CF/DA 为 `20/00/00/30`；工具 close 后 driver 断言 PDN，用户的 after 文件为空，
这不影响 running 对比。相对 factory quiet-running，公共项包括 C2、CD、D0、D5..D9、DA、DD；
差异则集中在 C0/C1/C3/C6/C7/C8/CA/CE/CF/D3/D4。原厂 DT 的 AK7755 dai link 还明确设置
`bitclock-master` 和 `frame-master`，原厂 CA=`60` 而当前 CA=`00`。这至少证明此前以“板上没有
独立 codec MCLK”为由直接选择当前 clock topology 的依据不足；CA 与 DT 的精确主从语义仍需
结合原厂 parser/driver 审计，不能只靠寄存器名下结论。

更直接的证据缺口是：factory C8=`00` 使用 data2 DSP output，而当前 C8=`c0` 是 SDIN1 direct。
factory zero 安静可能意味着路径正确且无噪，也可能意味着 DSP path 根本没有传入 PCM。A21r4
因此保持 A21r3 的原厂 kernel/DT，新增一个不使用 6.18 私有 gate 的固定 1 kHz stereo、峰值约
32/32767（约 -60 dBFS）工具。原厂 machine/codec driver 仍自行执行 PA unmute/mute；先重复
zero，再播放两秒 tone。只有“zero 安静 + tone 可辨”才构成原厂数据路径完整正向控制。

```text
95ded47c48d8325bae9083a3bc5b9f905e1b00d3ed56937f6b2f08e135afa373  r1-factory-tone-test
b88e684001a88f64eb2c3c8f4b18c39ac0558abacd12a3b02545999c0863fa88  A21r4 initramfs
4b1804055568fa080737cab08b9155744f4e7a08bc86ee2865d0bad0d457e24d  A21r4 FIT
```

A21r4 实机中 zero 与 tone 都完全无声。由此确认原厂 kernel/codec/machine driver 并不会仅凭
PCM open 自动建立 Android media route，A21r3 的“quiet-running”是缺少有效信号路径的假阴性。
其寄存器表仍可描述硬件状态，但不能作为 A23 降噪目标，也不能据此排除 TPA3118 模拟后端。

只读提取 `system.img` 后得到两份相互独立的原厂用户态证据。未剥离动态符号的
`/system/lib/hw/audio.primary.rk30board.so` 暴露 `ak7755_speaker_normal_controls`；解码其
11 个 16-byte route entry 得到：DRAM Size=`1`、DLRAM Mode=`2`、POMODE Pointer=`1`、
PRAM/CRAM/OFREG=`data2`、DAC MUX=`DSP`、Line Out Volume 1/2=`15`、LineOut Amp1/2=`On`。
原厂 `/system/bin/echo_test` 的 `speaker mid-low/high` 分支又以明文 `tinymix -D 2` 执行同一
DRAM/DLRAM/POMODE 和三份 firmware 下载合同，再分别打开 Amp1 或 Amp2。这证明救援环境缺少的
不是 PCM 数据，而是 Android HAL/factory script 主动触发的 mixer 与 OFREG 初始化。

A21r5 加入 freestanding `/bin/r1-factory-ak7755-route`，只允许按上述顺序写这 11 个已恢复 ALSA
control；前三项按原厂 tinymix 的数字 enum index 设置，其余 enum 按名称查找，不直接写 I2C
寄存器。initramfs 同时携带 PRAM、CRAM 和此前缺失的 39-byte OFREG data2，以及完全相同的
zero/tone 工具。原厂 kernel/DT、单核、no-MMC 和无无线安全边界不变。工具无动态依赖，FIT
payload、OFREG 与输入逐字节一致，连续构建哈希稳定：

```text
388dc260c86230b16cf20b82f5c1d51823df1917066a8f9d78dc0ce06b73c65b  r1-factory-ak7755-route
82e8e1d9e8381ad3e3b47aa95374b1fff489af393ef30962b0c2beddfd1a244b  A21r5 initramfs
334f75358c9f109ba3115c268d11cd6b7d638f86cb90af8f61b8694f4438003c  A21r5 FIT
```

下一步必须先看到 route 工具 11 项全部成功，再跑 zero 5 秒和 tone 2 秒。只有 zero 安静且 tone
可闻，原厂路径才成为有效 A/B；若 route 工具在任一 control/firmware 上失败，停止测试并保留
输出，不绕过失败项、不提高音量。

A21r5 随后获得完整正向证据：11 项 route 全部 `applied`、`route_rc=0`，PRAM/CRAM/OFREG
CRC 为 `9916/4453/96c1`；10 秒 1 kHz tone `xruns=0`、`tone_rc=0`，固定底噪基本不存在，
用户形容音色有“震动感”。route 后 idle/running/close 的关键状态分别是
`0d/00/10/02/48/60/00/00/0f/ff/30`、`0d/01/10/02/48/60/0f/0c/0f/ff/10`、
`0d/01/10/02/48/60/0f/0c/0f/ff/30`（字段依次 C0/C1/C2/C3/C4/CA/CE/CF/D3/D4/DA）。
因此原厂 data2 PRAM/CRAM/OFREG 与 DSP DAC 路由确实能把 PCM 送到扬声器；A21r3/A21r4
完全无声已确定是缺少用户态 route。1 kHz 的震动感没有测量证据，仍不写成故障结论。

A23 已把这套合同迁入 Linux 6.18：AK7755 为 12.288 MHz XTI 的 BCLK/LRCK provider，
RK I2S2 为 consumer，64fs；新增 OFREG 严格 size/command/CRC 校验，并在 PCM prepare 验证
完整 factory DSP running 寄存器。首个 A23 FIT 的构建脚本误选 A4 DT，因而虽然声卡可以存在，
machine driver 取不到 `amp-enable-supply`/`amp-unmute-supply`，不会注册 `/dev/r1-audio-safety`。
实机工具以 `open ... errno=0x02` 退出是该打包错误的直接证据；它没有打开功放，不能算音频测试。
修正版 A23 已改用 A8 DT，最终 DTB 反编译确认两个 supply 和可控 regulator 均存在，外部 PA 的
默认 shutdown+mute 与 500 ms fail-safe 恢复。整核、DT、含三份 data2 的 initramfs、FIT 解包
均已通过主机验证。修正版 A23 随后已在 R1 播放低电平测试音，用户确认“很干净”，即此前
A22 的固定明显底噪在 factory DSP 路由下没有复现。这是同一硬件上的有效主观 A/B；由于本轮
尚未贴出退出码、FACTORY DSP RUN/STANDBY readback 与最终功放 SAFE 日志，安全状态机和完整
回归仍保持为待补证据，不能把主观听感扩大成信噪比或失真测量结论。

用户随后按给定顺序完成 60 秒 zero PCM、CPU0-3、Wi-Fi、Bluetooth LE 和最终功放状态检查，
并确认回归均无问题；本轮没有保留逐项输出，因此这里把结果标为用户确认，不记录虚构的 xrun、
扫描计数或寄存器值。固定旋律不再提供新的诊断边界，后续直接进入普通用户态音频。

普通 BlueZ 并不自行把 A2DP payload 播放到本地 ALSA card。BlueALSA 上游将自身定位为 BlueZ
与 ALSA 之间的轻量后端，并明确提供 `bluealsa-aplay` 将蓝牙流送往本地 PCM；因此第一版完整
用户态选择 `D-Bus + BlueZ + BlueALSA + alsa-utils`，先于 PipeWire。来源为 Arkadiusz Bokowy
等维护的 [BlueALSA README](https://github.com/arkq/bluez-alsa)（访问于 2026-08-12，moving
`master`，实际构建版本仍须由 Buildroot 锁定）。BlueZ 上游 README 说明 A2DP/AVRCP 是
`bluetoothd` 的内建 profile、可在构建时禁用；Buildroot 配置必须明确保持两者开启。来源为
Linux Bluetooth maintainers 的 [BlueZ tree](https://github.com/bluez/bluez)（访问于
2026-08-12，moving `master`，仅用于能力边界，不作为版本固定证据）。

用户态不继续塞进当前 freestanding rescue initramfs。Buildroot 2026.05 官方手册 revision
`313414b92c` 明确支持独立生成交叉工具链和 root filesystem，因此用项目外部配置固定 ARMv7、
libc、软件包、overlay 和 legal-info。来源为 Buildroot developers，2026-06-08 生成的
[Buildroot 2026.05 manual](https://buildroot.org/downloads/manual/manual.html)。在普通播放器上板前，
machine driver 还必须从 root-only 诊断 misc gate 过渡为 PCM START/STOP/close 驱动的 PA 时序；
否则 `aplay`、BlueALSA 或未来 PipeWire 无法独立、安全地驱动扬声器。

```text
9ae541809bf9f05ae00145876814fbc4d049e19801bf15a23c6a579b0d5d40a8  factory kernel
ac5f7f3b6a4612486ab348a3bdb6aabb41439b9999115dc540720f76e0f44993  factory DTB
855f92832e9de5ae0c182c19a07ad66e7f0892efc6a9f5933f3c32c8336d5859  A19 rescue initramfs
38a7679fb4629456cc7a77d5261cd814688173c07f14d997dd20128239b0e14a  A21 overlaid DTB
3cbaa1abdf016ca5e4a780576ea4c132df34461cbd1bb11d015949d27224b0b5  A21 FIT
2543d382529729e32750ab6b22636bc487372ffd4204c677e15a817774a8e63c  A21r2 no-MMC DTB
4acefadf00012638de85ac2d237eb3028b42112a4026d942630f0c19a3e34545  A21r2 FIT
3651c63aae60a20dddd702c0ed38dfd589ec24c1e09c5d31407ef15460b4d519  A21r3 audio-only DTB
daeb0fe41c5e701b2b0747b789e307778081fd4a30fb31cd66b5d327c1a1cf20  A21r3 FIT
```

```text
60b779677845d3b6fe810c9f9ccbdf36faa38a12e32925e3bdaee932319545f4  ak7755.c
e3bfd76b6ded8681dca29988eaf506cd4d4f3c40d6e7ba98f4ca63df11f895cd  phicomm_r1_ak7755.c
864f1a56ef40a957709e85e622e4b3d92f8a901d6bb3952f335062da1b662fe0  r1-analog-boundary-ab
52b4ea166e7ff85dec5f79ecb9e60d1b4b786deaceb21f976a9870f49e54ff8e  zImage-mainline-6.18-ak7755-analog-boundary-a16
0db5011c377ffc2f8339d5b987aa00510af17f32533fa3ec907c3b77fe0d7540  r1-initramfs-mainline-6.18-ak7755-analog-boundary-a16.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-analog-boundary-a16.dtb
9ce2ed99b338223529761f0039420fd7c5b050710e43f13dbcf2f212752efd2b  r1-linux-mainline-6.18-ak7755-analog-boundary-a16.itb
```

### Audio A24/A25：普通 PCM 自动功放与最小 BlueALSA 用户态

A24 把 A23 的 root-only 实验 gate 留作诊断兼容，同时让普通 ALSA playback 自己拥有完整 PA
生命周期。首次 R1 实机 zero-PCM 验证确认 factory DSP RUN、PA enable/unmute 和最终 SAFE 都发生，
且 D-Bus、bluetoothd、bluealsa、bluealsa-aplay 均存活；播放全零数据没有声音是预期结果，不是
音频链路失败。但 DMA drain 在 PL330 tasklet 中调用 STOP，旧实现直接从 trigger 进入 `msleep()`，
触发 `BUG: scheduling while atomic`。这证明 `link.nonatomic=1` 不能保证所有 STOP 来源都可睡眠，
原 A24/A25 因而降级为失败证据。

A24r2 的 trigger 只原子更新目标状态并排入 `system_highpri_wq`，不再直接碰 regulator、mutex 或
sleep。worker 在进程上下文执行 safe→enable→等待 20 ms→unmute 或 mute→等待 10 ms→shutdown；
settle 结束前会再次检查目标状态，STOP 若在等待期间到达就保持 SAFE，不允许晚到 unmute。
hw_free、close、诊断 gate、remove 和 shutdown 会同步取消 worker 后强制 SAFE。capture 仍不控制 PA，
普通 PCM 与 misc 诊断 gate 仍严格互斥。该 r2 修复已完成 `-j16` 整核构建和 FIT 解包，尚待实机
确认不再出现 atomic-sleep 警告。

A25 不再把完整用户态塞入救援 initramfs。`buildroot-external/r1/` 固定 Buildroot 2026.05.1，
以 Linux 6.18.34 UAPI headers 自建 Cortex-A7 EABI hard-float/musl 工具链，生成 BusyBox SysV
rootfs，并包含 D-Bus、BlueZ 5.79、BlueALSA 4.3.1、SBC、alsa-lib、alsa-utils 和
libsamplerate。启动顺序为 D-Bus、bluetoothd、`bluealsa -p a2dp-sink`、
`bluealsa-aplay -D r1-output 00:00:00:00:00:00`；`r1-output` 把输入统一转换为
48 kHz/stereo/S16_LE 后送到 `hw:0,0`。第一阶段明确不带 PipeWire、AAC、aptX、LDAC、Opus
或 HFP。

专有 CYW43455、板级 NVRAM、BCM HCD 与 AK7755 data2 文件没有加入仓库。post-build hook 只接受
九个白名单目标，并要求外部 manifest 为每个非 symlink 输入提供精确 SHA-256。主机构建已验证
`/init`、四个服务、三个 BlueZ/BlueALSA 程序、九个 firmware target、ARM32 hard-float ELF，
并生成 Buildroot `legal-info`。第一次完整构建误用 Buildroot 默认 7.0 headers，虽然能生成 cpio，
但它比目标 Linux 6.18 更新，不作为候选；改为 `BR2_KERNEL_HEADERS_6_18=y` 后重新从独立 output
目录构建。该失败说明 rootfs “能编译”不等于 kernel userspace ABI 选择合理。

```text
d52b3d7ebc0fa37e414b4bcc3e34d3a4b64a325f9685c28211d071491ddef115  A24r2 zImage
4078b6aa84190948f9ffc289c6762645effc68c776e233664055c04be3cae2e7  A24 DTB
fdd0a96307a81c14d93b523b9b1060296ceba2855f3fb358ce4cbc4afed7ec2c  A24r2 rescue FIT
f78f5d12eac1c4524e4deb49b1ab280227415a9034e8be0681f6a48a2b0c7315  A25 rootfs.cpio.gz
92539648aaed0fc136221960750b5c9432a3f094f5e8ea4da0fcf3b574aadf55  A25r2 FIT
```

A25r2 FIT 为 20,281,340 B，超过旧 16 MiB DFU alternate，因此只扩大 RAM transfer ceiling 为
64 MiB；这不写 eMMC，也不改变常驻 OP-TEE/U-Boot。下一步先 RAM-only 验证普通 ALSA 自动 PA，
再运行配对 agent 和 SBC A2DP Sink；配对、暂停、断连、播放器崩溃、重连和无线共存通过前，
不把 rootfs 写入 eMMC。

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
