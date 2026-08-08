# 系统架构

## 1. 设计目标

系统需要同时满足四个条件：启动快、无线连接可靠、音频处理统一、底层可调试。

因此将系统分为五层：

```text
Boot Layer
Kernel / BSP Layer
Connectivity Layer
Audio Routing Layer
DSP / Application Layer
```

## 2. Boot Layer

第一阶段保留原厂 DDR Loader，只替换可控的二级引导和后续系统。原因是 DDR training 参数与板级走线、内存颗粒高度相关，直接使用通用 RK3229 SPL 风险较高。

建议提供三个启动目标：

- `normal`：正常系统；
- `recovery`：维护和升级；
- `factory`：串口、网络和音频测试环境。

## 3. Kernel / BSP Layer

需要支持：

- UART console；
- eMMC；
- USB；
- GPIO / regulator / PWM；
- SDIO Wi-Fi；
- UART Bluetooth；
- I2C 或 SPI 控制 AK7755；
- I2S 音频；
- 功放静音和电源控制。

建议先在主线或较新 LTS 内核上建立最小 DTS，再按外设逐项补齐。不要一开始复制整个原厂 3.10 BSP。

## 4. Connectivity Layer

网络配置由单一配置文件生成 iwd 或 wpa_supplicant 配置。蓝牙由 BlueZ 管理，配对策略应明确区分首次配置模式和正常使用模式。

建议状态机：

```text
BOOT
 ↓
LOAD_CONFIG
 ↓
WIFI_CONNECTING ──失败──> RETRY / AP_CONFIG_MODE
 ↓
ONLINE
 ↓
START_MDNS + AIRPLAY

BLUETOOTH_POWER_ON
 ↓
HCI_READY
 ↓
PAIRABLE_WINDOW
 ↓
TRUSTED_RECONNECT_ONLY
```

## 5. Audio Routing Layer

PipeWire 是音频系统的中心。蓝牙、AirPlay、本地文件播放都输出到同一个虚拟 Sink，随后进入统一 DSP 链。

```text
Bluetooth ─┐
AirPlay ────┼──> R1-FX Sink ─> ALSA Sink ─> AK7755
Local PCM ──┘
```

这样可以保证所有音源共享同一套 EQ、音量、Limiter 和延迟配置。

## 6. DSP Layer

DSP 分成两层：

- CPU DSP：PipeWire filter-chain、LADSPA、LV2；
- Hardware DSP：AK7755 PRAM/CRAM。

第一版策略：

1. AK7755 加载原厂程序或直通程序；
2. 自定义 EQ 和保护逻辑放在 CPU；
3. 稳定后分析 AK7755 参数格式；
4. 将固定滤波、分频等功能逐步下沉到 AK7755。

这样可以减少对专有工具链的依赖。

## 7. 配置模型

建议使用单个 TOML 或 YAML 文件描述：

- Wi-Fi SSID 和认证方式；
- 蓝牙设备名、配对窗口；
- AirPlay 名称；
- 默认音量；
- DSP preset；
- 日志等级；
- 启动模式。

配置生成器负责转换为各软件的原生配置，避免用户直接维护多份配置文件。
