# Bring-up 手册

## 1. 串口


```bash
sudo picocom \
  --baud 1500000 \
  --databits 8 \
  --parity n \
  --flow n \
  /dev/ttyUSB0
```

### 1.1 零延时 U-Boot

原厂 U-Boot 可能显示：

```text
Hit any key to stop autoboot:  0
```

此时看到提示后再按键已经来不及。可以在主机上运行以下脚本，然后给开发板重新上电：

```sh
sudo python3 scripts/interrupt-uboot.py /dev/ttyUSB0
```

脚本以 1500000 8N1、无软硬件流控打开串口，在启动早期持续发送空格，并把原始输出保存到 `backup/uboot-interrupt.log`。如果实机使用其他波特率，通过 `--baud` 指定：

```sh
sudo python3 scripts/interrupt-uboot.py /dev/ttyUSB0 --baud 115200
```

脚本刻意不发送回车，以免未被 U-Boot 消费的回车进入内核 FIQ debugger。它不执行 U-Boot 命令，也不读写 eMMC。

如果脚本仍检测到 `Starting kernel`，说明该 U-Boot 很可能没有启用零延时按键检测。继续提高发送频率通常无效，应改查实体按键进入 recovery/loader，或使用 Rockchip Loader/MaskROM。未完成备份前，不应修改 U-Boot 或盲目使用 `fw_setenv`。

### MaskROM USB 写入前稳定性门槛

仅有 `rkdeveloptool ld` 显示 Maskrom 不代表链路足以安全读写；`ld` 主要依赖 USB 枚举，而 `rci`、`rid`、`rfi` 才会执行实际 vendor/bulk 通信。如果 `dmesg` 出现 `error -71`、`error -110`、`Device not responding to setup address` 或设备号持续变化，应立即停止所有写入计划。

实机还出现过一种更隐蔽的失败：设备在 xHCI root hub 上以 480 Mbps 直连，描述符、configuration 和两个 512-byte bulk endpoint 均可读取，设备也能持续出现在 `lsusb` 中，但 `rci`、`rid`、`rfi` 全部失败。`LIBUSB_DEBUG=4` 证明第一个 31-byte bulk OUT URB 就以 `status=-71`、`transferred=0` 完成；Rockchip `upgrade_tool` 同样报告 `RKU_Write failed`。USB device reset 不能恢复。这说明命令尚未送达 BootROM，更没有访问 eMMC；应按 High-Speed 信号完整性、接地/回路、VBUS 拓扑、PHY 状态或主机控制器兼容性排查，不能因 EP0 枚举成功而执行写入。

最终换到此前未用于 R1 的另一个物理 USB 口后，U-Boot 按键 Rockusb 在 `Loader` 模式下立即通过 `rci`、`rid` 和 `rfi`，芯片信息、SAMSUNG eMMC ID 和 15,269,888-sector 容量全部正确。主机拓扑显示成功口实际为同一个 ASM1074 Hub 的下游 Port 4；此前其他下游口及另一个 root port 均返回首包 `EPROTO`。因此板端 PHY 和 eMMC 并未损坏，故障与特定主机物理端口/接点的 High-Speed 链路裕量相关。后续救援固定使用已经通过真实 bulk 查询和双读验证的端口，不能只依据“直连”或 `lsusb` 枚举判断。

一次后续故障中，设备能以 High Speed 连续枚举、描述符和两个 512-byte bulk endpoint 也完整，但 `rci`、`rid`、`rfi` 的第一笔 31-byte bulk OUT 均由 xHCI 立即返回 `URB status=-71`、`transferred=0`。`usbreset` 后现象不变。这证明“设备持续出现在 `lsusb`”仍不足以说明数据链路可用；control/描述符阶段和 bulk 阶段必须分别验证。当拓扑显示 `root hub → ASM1074 hub → RK3229` 时，应优先换到真正直连 root hub 的端口，再检查 D+/D-、公共地和线缆。

R1 实测不稳定时最终仍可能完整枚举出接口和 endpoint，但这不能抹去此前的断连证据。写入前至少要求：原装电源独立供电、USB VBUS 断开、GND/D-/D+ 焊点和线长检查完成；`rci`、`rid`、`rfi` 连续多轮成功；目标分区连续读取结果逐字节一致。

不要使用 USB-TTL 模块的 VCC/5 V 引脚给 R1 整机供电。实机已验证这种接法会造成间歇性 USB 枚举成功、反复 `error -71` 以及设备号持续变化；即使换用较短的数据线也不能解决。R1 应使用原装电源从原生电源输入供电，USB-TTL 只连接 GND、TX、RX，VCC 保持断开；串口逻辑电平使用 3.3 V。独立供电时 USB 调试线断开 VBUS，只保留 GND、D-、D+，避免双路供电或倒灌。

Android 与 recovery 的 `mmcblk0pN` 编号可能不同。R1 recovery 模式会额外注册 parameter 分区，实测 `by-name/recovery` 为 `mmcblk0p9`；不能根据普通 Android 下的编号猜目标。块设备操作必须使用 `/dev/block/platform/30020000.rksdmmc/by-name/recovery`，并在写前用 `readlink` 与容量查询验证它确实对应 32 MiB recovery。

## 2. 原厂系统采集

在原厂 Android 或 Linux 中尽可能保存：

```sh
uname -a
cat /proc/cmdline
cat /proc/cpuinfo
cat /proc/partitions
cat /proc/mounts
cat /proc/interrupts
cat /proc/iomem
cat /proc/config.gz > /data/local/tmp/config.gz
getprop
ls -l /dev/block/by-name
ls -l /sys/class/mmc_host
ls -l /sys/class/tty
ls -l /sys/class/gpio
lsmod
dmesg
```

提取设备树：

```sh
find /sys/firmware/devicetree/base -type f
```

如果 `/sys/firmware/fdt` 存在：

```sh
cat /sys/firmware/fdt > /data/local/tmp/r1.dtb
```

## 3. eMMC 备份

先确认整盘节点，例如 `/dev/block/mmcblk0`。备份前必须核对容量，不要凭经验直接执行写命令。

示例：

```sh
dd if=/dev/block/mmcblk0 of=/data/local/tmp/r1-emmc.img bs=4M
sync
sha256sum /data/local/tmp/r1-emmc.img
```

完整镜像应复制到至少两个独立位置，并记录哈希。

## 4. 优先使用 recovery 测试

开发早期建议：

- 正常分区保持不动；
- 测试内核写入 recovery；
- U-Boot 通过按键、GPIO、串口命令或 bootcount 进入 recovery；
- recovery rootfs 提供 SSH、rkdeveloptool 辅助脚本和刷写工具。

## 5. 最小内核功能

```text
CONFIG_SERIAL_8250_DW
CONFIG_MMC
CONFIG_MMC_DW
CONFIG_MMC_DW_ROCKCHIP
CONFIG_USB_DWC2
CONFIG_GPIO_SYSFS               # 仅调试阶段
CONFIG_CFG80211
CONFIG_BRCMFMAC
CONFIG_BT
CONFIG_BT_HCIUART
CONFIG_BT_HCIUART_BCM
CONFIG_SND
CONFIG_SND_SOC
CONFIG_SND_SOC_ROCKCHIP
```

具体符号以目标内核版本为准。

当前 Linux 6.18.42 的配置、DTS、initramfs 构建方式和首次启动安全边界见[主线 Linux Bring-up](mainline-bringup.md)。现有产物尚未上板验证，不应直接写入 eMMC。

## 6. 验收日志

每个外设建立独立日志文件：

```text
logs/
├── boot/
├── emmc/
├── wifi/
├── bluetooth/
└── audio/
```

日志文件名建议包含：日期、Git commit、内核版本、DTB 版本和测试结果。

## 7. U-Boot Fastboot 只读检查

原厂 Android 串口 shell 可以请求重启到 U-Boot Fastboot：

```sh
setprop sys.powerctl reboot,fastboot
```

主机只读查询：

```sh
scripts/check-fastboot.sh
```

R1 实机结果为 `secure: yes`、`unlocked: no`，且不提供 `max-download-size`。这意味着当前不能用 `fastboot boot` 下载 RAM 镜像。不要执行 `fastboot flash` 或 `fastboot oem unlock`；后者可能擦除 userdata。

不继续实验时可让设备正常重启：

```sh
fastboot reboot
```
