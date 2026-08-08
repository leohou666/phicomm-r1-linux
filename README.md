# Phicomm R1 Linux Audio

面向斐讯 R1 智能音箱的自定义 Linux、无线音频接收与音频 DSP 实验项目，本质可能还是本人的学习项目。

过程用到了gpt5.6 sol + deepseek v4 flash ，大部分文档存在模型的口癖，后期跑通后再人为调整。

项目目标不是简单给原厂 Android 加功能，而是逐步建立一套可控、可裁剪、可复现的启动与音频系统：

- 插电自动启动；
- 按配置自动连接 Wi-Fi；
- 作为蓝牙 A2DP Sink 接收音频，优先支持 LDAC；
- 作为 AirPlay 接收端；
- 使用 PipeWire 统一管理音频路由；
- 使用 PipeWire filter-chain 或插件完成 EQ、Limiter、Convolver 等处理；
- 驱动并控制 AK7755 DSP；
- 在条件允许时，逐步替换原厂 U-Boot、内核和用户空间；
- 尽量缩短从上电到可播放状态的时间。

> 当前状态：方案设计与硬件资料收集阶段。本文档中的引脚、固件文件名、设备树节点和启动链均需要以 R1 实机日志、原厂 DTB 和原厂镜像为准。

## 1. 预期硬件结构

当前推定的核心硬件如下：

| 模块 | 预期器件或接口 | 用途 |
|---|---|---|
| SoC | Rockchip RK3229 | Linux、网络协议栈、蓝牙解码、PipeWire DSP |
| 内存 | 约 512 MiB DDR | 系统运行内存 |
| 存储 | 约 8 GiB eMMC | Bootloader、内核、rootfs、配置 |
| Wi-Fi / 蓝牙 | Broadcom/Cypress CYW43455 | Wi-Fi 通过 SDIO，蓝牙通过 UART HCI |
| 音频 DSP | AKM AK7755 | 音频路由、滤波、分频、AEC 等硬件 DSP 功能 |
| ADC | ES7243 系列 | 麦克风输入 |
| 功放 | TPA3118 系列 | 驱动扬声器 |

所有器件型号和连接方式应通过拆机、丝印、原厂设备树和内核日志再次确认。

## 2. 推荐启动链

第一阶段不要直接替换整套启动链。推荐保留原厂 DDR 初始化和前级 Loader：

```text
RK3229 BootROM
    ↓
原厂 DDR Loader / miniloader
    ↓
原厂或自定义二级 U-Boot
    ↓
自定义 Linux kernel + DTB
    ↓
Buildroot / 精简 Debian rootfs
    ↓
PipeWire + BlueZ + Shairport Sync
```

这样可以规避 DDR 参数不匹配导致的早期启动失败。等串口、eMMC、USB、Wi-Fi 和音频稳定后，再考虑完整替换 SPL/TPL。

## 3. 最终音频链路

### 3.1 蓝牙接收

```text
手机 / 电脑
    ↓ Bluetooth A2DP（LDAC / AAC / SBC）
CYW43455 Bluetooth UART HCI
    ↓
Linux BlueZ
    ↓
PipeWire A2DP Sink
    ↓
R1-FX 虚拟 Sink
    ↓
EQ / High-pass / Compressor / Limiter / Convolver
    ↓
ALSA SoC
    ↓
RK3229 I2S
    ↓
AK7755
    ↓
TPA3118
    ↓
扬声器
```

### 3.2 AirPlay 接收

```text
iPhone / Mac / iTunes
    ↓ Wi-Fi / AirPlay
Shairport Sync
    ↓ PipeWire
R1-FX 虚拟 Sink
    ↓
ALSA → I2S → AK7755 → 功放 → 扬声器
```

## 4. 软件组件建议

| 功能 | 推荐组件 |
|---|---|
| 基础系统 | Buildroot 或精简 Debian |
| 网络管理 | iwd + systemd-networkd，或 wpa_supplicant |
| 蓝牙协议栈 | BlueZ |
| 音频服务器 | PipeWire |
| Session Manager | WirePlumber |
| 蓝牙 A2DP Sink | PipeWire BlueZ monitor |
| AirPlay | Shairport Sync |
| AirPlay 2 时间同步 | nqptp，可作为后续阶段 |
| 无界面 DSP | PipeWire filter-chain / LADSPA / LV2 |
| 服务管理 | systemd 或 BusyBox init |

不建议把 EasyEffects 作为最终常驻服务。更适合在桌面环境中调试参数，再把最终参数转换为 PipeWire filter-chain 或 LV2/LADSPA 配置。

## 5. 开发阶段

### Phase 0：保护现场

- 焊接 UART；
- 记录串口电平和波特率；
- 完整保存启动日志；
- 进入 Loader 或 MaskROM；
- 完整备份 eMMC；
- 保存分区表、原厂 boot/recovery/system/vendor 镜像；
- 从原厂系统导出 DTB、内核配置、固件和 mixer 配置。

完成标准：可以在不依赖原厂网络服务的情况下恢复整机。

### Phase 1：自定义 Linux 启动

- 保留原厂 DDR Loader；
- 优先从 recovery 分区或外部介质启动测试系统；
- 串口可用；
- eMMC、USB、GPIO 基本可用；
- rootfs 能稳定进入 shell。

完成标准：连续冷启动 20 次无失败。

### Phase 2：Wi-Fi

- 确认 SDIO 控制器；
- 确认 WL_REG_ON 和 HOST_WAKE GPIO；
- 提取原厂 CYW43455 firmware、CLM 和 NVRAM；
- 加载 brcmfmac；
- 扫描 2.4 GHz / 5 GHz；
- 按配置自动联网；
- 配置断线重连。

完成标准：插电后自动联网，断开 AP 后能够自动恢复。

### Phase 3：蓝牙

- 确认 UART、RTS/CTS 和 BT_REG_ON；
- 加载 Broadcom HCD patch；
- 先以低速 UART bring-up；
- 创建 hci0；
- 启用 BlueZ；
- PipeWire 暴露 A2DP Sink；
- 验证 SBC，再验证 AAC 和 LDAC；
- 配置自动配对、可信设备和重连策略。

完成标准：手机重启蓝牙后能够自动重新连接并播放。

### Phase 4：音频输出

- 提取原厂 AK7755 驱动和 DSP 数据；
- 移植或重写现代 ASoC component driver；
- 建立 machine driver / audio graph；
- 首先实现 AK7755 直通或加载原厂程序；
- 验证采样率、通道数、MCLK/BCLK/LRCK；
- 验证功放静音和上电时序。

完成标准：无爆音、无明显底噪，连续播放 8 小时稳定。

### Phase 5：PipeWire DSP

- 创建 `R1-FX` 虚拟 Sink；
- 蓝牙和 AirPlay 都路由到 `R1-FX`；
- 加入高通、PEQ、Limiter；
- 保存多个调音预设；
- 提供脚本或 Web API 切换预设。

完成标准：所有输入源共用同一套处理链，切换输入源无需重启服务。

### Phase 6：启动优化和产品化

- 裁剪内核；
- 减少 rootfs 组件；
- 服务并行启动；
- 减少 DHCP、mDNS 和蓝牙初始化阻塞；
- 增加 watchdog；
- 只读根文件系统或 A/B 更新；
- 增加恢复模式和升级工具。

目标：

- 5～10 秒进入蓝牙可连接状态；
- 10～20 秒进入 Wi-Fi / AirPlay 可发现状态；
- 异常断电后不损坏系统。

## 6. 工期预估

按业余时间、已有嵌入式 Linux 和驱动经验估算：

| 工作项 | 预计投入 |
|---|---:|
| 串口、备份、启动链摸底 | 1～3 天 |
| 自定义 Linux 启动 | 2～5 天 |
| Wi-Fi 自动联网 | 1～3 天 |
| 蓝牙 HCI、SBC/AAC | 2～5 天 |
| LDAC 编译与协商 | 1～3 天 |
| AK7755 基础出声 | 3～10 天 |
| PipeWire + AirPlay 路由 | 2～4 天 |
| DSP 配置和调音 | 2～7 天 |
| 稳定性和启动优化 | 3～10 天 |

预期结果：

- 最小可播放版本：1～2 周；
- 满足主要目标：3～6 个周末；
- 达到长期稳定使用：1～2 个月业余时间；
- 完全自主的 AK7755 程序和工具链：额外数周到数月。

## 7. 当前最大风险

1. 原厂 DDR 初始化参数不公开，完整替换 SPL/TPL 有变砖风险。
2. AK7755 驱动可以移植，但原厂 DSP 程序和调音数据可能编译在内核中。
3. AK7755 自定义算法可能依赖 AKM 专用开发工具。
4. CYW43455 的板级 NVRAM、HCD 和 GPIO 定义需要从原厂系统提取。
5. 512 MiB RAM 不适合运行完整桌面环境和重型 EasyEffects GUI。
6. 功放 mute、reset 和电源时序错误可能造成上电爆音。
7. 蓝牙 LDAC 接收依赖 PipeWire 编译选项、codec 库和 BlueZ 配置。

## 8. 仓库目录建议

```text
phicomm-r1-linux/
├── README.md
├── TODO.md
├── docs/
│   ├── architecture.md
│   ├── bringup.md
│   ├── audio.md
│   └── reverse-engineering.md
├── config/
│   ├── network.example.toml
│   └── wireplumber.example.conf
├── firmware/
│   └── README.md
├── patches/
│   └── README.md
└── scripts/
    ├── collect-original-system.sh
    └── inspect-boot-log.sh
```

## 9. 原则

- 在完整备份前不写 Bootloader；
- 在 recovery 可启动前不覆盖正常启动分区；
- 第一版优先复用原厂 DSP 固件；
- 第一版 EQ 和动态处理优先放在 PipeWire CPU 侧；
- 所有 GPIO、时钟和 regulator 均以实机 DTB 为准；
- 每解决一个外设，记录设备树、日志、固件哈希和测试方法。

## 10. 许可证

项目代码后续可考虑使用 GPL-2.0-or-later。原厂固件、NVRAM、HCD、DSP 程序和厂商驱动可能具有不同许可证，不应直接提交到公开仓库，除非确认具备再分发权限。
