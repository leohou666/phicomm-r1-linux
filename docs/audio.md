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
