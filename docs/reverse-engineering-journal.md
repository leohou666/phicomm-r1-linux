# 斐讯 R1 linux 记录


---

## 📋 当前结果

本轮工作的目标是先保护原厂系统，再为后续自定义 Linux 移植收集板级资料。目前已经完成：

- 连接并验证原厂 UART 控制台
- 保存完整冷启动日志
- 进入原厂 Android 串口 shell
- 确认串口 shell 的权限和 SELinux 限制
- 焊接并调通 USB 数据连接
- 进入 RK3229 MaskROM
- 修正本地主机端 `rkdeveloptool` 的 USB configuration 兼容问题
- 通过 MaskROM 直接读取完整 eMMC User Area
- 根据 Rockchip 固定分区布局提取关键分区
- 从原厂 `system.img` 导出 Wi-Fi、Bluetooth 和 AK7755 固件
- 用启动日志确认原厂实际加载的 Wi-Fi 和 AK7755 文件

本轮没有向 eMMC 写入任何数据。尝试下载 Loader 到 RAM 没有成功，但 MaskROM 已经支持直接读取 eMMC，因此没有阻塞备份。

## 🔧 串口接入

### 接线与终端参数

USB-TTL 只连接三根线：

```text
R1 TX  → USB-TTL RX
R1 RX  ← USB-TTL TX
R1 GND ↔ USB-TTL GND
```

没有连接 USB-TTL 的 VCC。实机串口参数为 `1500000 8N1`，关闭软硬件流控：

```bash
sudo picocom \
  --baud 1500000 \
  --databits 8 \
  --parity n \
  --flow n \
  /dev/ttyUSB0
```

冷启动日志保存为：

```text
backup/bringup_demsg.md
```


### 启动链观察

串口日志确认了以下启动链：

```text
RK3229 BootROM
DDR Loader
Boot1 2.37
Trust OS
U-Boot 2014.10
Linux 3.10.0
Android init
Android shell
```

观察到的关键版本：

```text
DDR Version V1.06 20171026
Boot1 version 2.37
U-Boot 2014.10-RK322X-06
Linux 3.10.0
Build: jenkins-R1-master-user-3448
```

硬件日志确认 SoC 为 RK3229、内存为 512 MiB DDR3、eMMC User Area 约为 7456 MiB。

## U-Boot 与串口 shell

### 无法中断 U-Boot

原厂 U-Boot 显示：

```text
Hit any key to stop autoboot:  0
```

这表示启动延时为 0。看到提示后再按键已经来不及，提前持续按空格也没有成功。项目中为此加入了：

```text
scripts/interrupt-uboot.py
```

脚本会在上电前持续发送空格并保存串口输出，但实机仍然直接进入内核。当前判断是原厂 U-Boot 没有启用零延时按键检测。

日志还会显示：

```text
<hit enter to activate fiq debugger>
```

因此抢占脚本刻意发送空格而不是回车，避免未被 U-Boot 消费的回车进入内核 FIQ debugger。

### Android 串口 shell

串口shell大概率是不能直接输入的，不知道为什么。

权限检查结果：

```text
uid=2000(shell) gid=2000(shell) groups=1007(log)
context=u:r:shell:s0
SELinux: Enforcing
ro.secure=1
ro.debuggable=1
ro.build.type=user
su: not found
```

访问 `/dev/block/mmcblk0` 时被 SELinux 拒绝：

```text
avc: denied { search }
scontext=u:r:shell:s0
tcontext=u:object_r:block_device:s0
```

结论是串口 shell 可用于只读收集普通系统信息，但不能直接读取整块 eMMC，也不能依靠 `su` 提权。

## ADB 与网络尝试

原厂系统的 ADB 配置其实比较开放：

```text
ro.adb.secure=0
init.svc.adbd=running
sys.usb.config=mass_storage,adb
persist.sys.usb.config=mass_storage,adb
service.adb.tcp.port=5555
```

进程列表显示 `/sbin/adbd` 以 `shell` 用户运行。理论上设备联网后可以尝试 TCP ADB，USB ADB 也不需要 RSA 屏幕确认。

实际过程中设备没有连上可用网络，当时也没有可靠的 USB 数据连接，因此没有通过 ADB 完成备份。即使普通 ADB 能连接，初始权限预计仍是 `uid=2000(shell)`；`ro.debuggable=1` 只代表 `adb root` 值得尝试，不保证原厂 `adbd` 一定允许 root。

## eMMC 信息与分区布局

### Android 侧信息

串口 shell 可以读取 `/proc/partitions`，确认：

```text
mmcblk0: 7634944 KiB
```

对应：

```text
15269888 个 512-byte 扇区
7818182656 字节
```

Android 内核暴露了 `mmcblk0p1` 到 `mmcblk0p16`，以及 `mmcblk0rpmb`。没有观察到可直接访问的 `mmcblk0boot0` 和 `mmcblk0boot1`。

### Rockchip 固定分区

分区布局来自内核命令行中的 `mtdparts`，不是标准 GPT：

| 名称 | 起始扇区 | 扇区数 | 大小 |
| --- | ---: | ---: | ---: |
| `parameter/idb` | `0x000000` | `0x002000` | 4 MiB |
| `uboot` | `0x002000` | `0x002000` | 4 MiB |
| `trust` | `0x004000` | `0x004000` | 8 MiB |
| `misc` | `0x008000` | `0x002000` | 4 MiB |
| `baseparamer` | `0x00A000` | `0x000800` | 1 MiB |
| `resource` | `0x00A800` | `0x007800` | 15 MiB |
| `kernel` | `0x012000` | `0x006000` | 12 MiB |
| `boot` | `0x018000` | `0x006000` | 12 MiB |
| `recovery` | `0x01E000` | `0x010000` | 32 MiB |
| `backup` | `0x02E000` | `0x020000` | 64 MiB |
| `cache` | `0x04E000` | `0x040000` | 128 MiB |
| `userdata` | `0x08E000` | `0x1FE000` | 1020 MiB |
| `metadata` | `0x28C000` | `0x008000` | 16 MiB |
| 保留区 | `0x294000` | `0x002000` | 4 MiB |
| `kpanic` | `0x296000` | `0x002000` | 4 MiB |
| `system` | `0x298000` | `0x180000` | 768 MiB |
| `private` | `0x418000` | `0x040000` | 128 MiB |
| `user` | `0x458000` | `0xA36000` | 约 5.1 GiB |
| 尾部保留区 | `0xE8E000` | `0x002000` | 4 MiB |

整盘第一个扇区以 `PARM` 开头，内容包括 `FIRMWARE_VER`、`MACHINE_MODEL` 和完整 `CMDLINE`。这也是 `file` 把整盘镜像误识别为 `Par archive data` 的原因。

`rkdeveloptool ppt` 没有识别出分区表，是因为该工具尝试在 `0x2000` 查找 `PARM`，而这台 R1 的 `PARM` 位于 LBA 0。该失败不代表镜像损坏。

## USB 与 MaskROM 调试

### USB 接口

为了使用 ADB 或 Rockchip MaskROM，需要把 USB OTG 数据线焊出。普通 USB Device 连接只需要正确连接：

```text
USB D+  → R1 USB_DP
USB D-  → R1 USB_DM
USB GND → R1 GND
```

ID 保持悬空。线色不作为依据，使用万用表通断档从 USB 插头引脚确认每根线。

实机可以由 USB VBUS 单独供电并启动 Android，因此没有额外连接原装电源。调试时避免双路 5 V 供电，以免倒灌。

## Loader 尝试

### 获取匹配版本

原厂日志显示 DDR `1.06` 和 Boot1 `2.37`。Rockchip 官方 `rkbin` 的历史提交 `972c281026c9e3ac969b24629df0b907227aa4d9` 中包含完全对应的三个组件：[^1]

```text
rk322x_ddr_300MHz_v1.06.bin
rk322x_miniloader_v2.37.bin
rk322x_usbplug_v2.37.bin
```

使用 `RKBOOT/RK322XMINIALL.ini` 和 `tools/boot_merger` 生成：

```text
firmware/rk322x_loader_v1.06.237.bin
```

历史配置原本使用 Rockchip SDK 内部路径 `tools/rk_tools/bin/`，在独立 `rkbin` 仓库中改为 `bin/` 后完成打包。

### 下载结果

尝试过以下 RAM 下载命令：

```bash
sudo ./rkdeveloptool/rkdeveloptool db \
  firmware/rk322x_loader_v1.06.237.bin
```

也尝试过：

```bash
sudo ./upgrade_tool DB \
  firmware/rk322x_loader_v1.06.237.bin
```

早期 USB 不稳定时，传输在 Rockchip vendor request `0x471` 阶段失败。USB 稳定后，工具返回：

```text
The device does not support this operation!
```

当次 Loader 没有下载执行；在后续修复 USB 物理链路并重新进入真 MaskROM 后，匹配 Loader 已成功下载，见本文后面的“真 MaskROM 纯 RAM Loader 下载成功”。当时的整盘备份则通过已有 Rockusb 会话直接完成，不依赖这次补做的纯 RAM 下载验证。

整个过程中没有执行 `UL`、`WL`、`EF`、`GPT`、`PRM` 等写入或擦除命令。

## 💾 MaskROM 整盘备份

### 只读探测

MaskROM 下成功执行：

```bash
sudo ./rkdeveloptool/rkdeveloptool rci
sudo ./rkdeveloptool/rkdeveloptool rid
sudo ./rkdeveloptool/rkdeveloptool rfi
```

输出确认：

```text
Flash ID: 45 4D 4D 43 20
Manufacturer: SAMSUNG
Flash Size: 7456 MB
Flash Size: 15269888 Sectors
Block Size: 512 KB
Page Size: 2 KB
```

在读取整盘前，连续读取了两次前 16 MiB，并确认两份内容一致。这一步用于验证手工 USB 链路在连续批量读取时保持稳定。

### 完整读取

完整 eMMC User Area 使用以下命令读取：

```bash
sudo ./rkdeveloptool/rkdeveloptool \
  rl 0x0 0xE90000 backup/r1-emmc-user.img
```

这是 eMMC User Area 的完整镜像。是否存在且能否单独读取 eMMC hardware boot0/boot1，当前仍未确认；Android 只显示了 `mmcblk0rpmb`。

## 📚 分区提取与文件系统识别

从整盘镜像中提取了以下关键镜像：

```text
backup/partitions/parameter-idb.img
backup/partitions/uboot.img
backup/partitions/trust.img
backup/partitions/resource.img
backup/partitions/kernel.img
backup/partitions/boot.img
backup/partitions/recovery.img
backup/partitions/system.img
```

识别结果：

| 文件 | 格式或文件头 |
| --- | --- |
| `parameter-idb.img` | `PARM` |
| `uboot.img` | `LOADER` |
| `trust.img` | `TOS` |
| `resource.img` | `RSCE` |
| `kernel.img` | `KRNL` |
| `boot.img` | Android boot image，`ANDROID!` |
| `recovery.img` | Android boot image，`ANDROID!` |
| `system.img` | ext4 |

其他文件系统探测结果：

- `cache`：ext4
- `userdata`：ext4
- `metadata`：ext4
- `private`：ext4
- `user`：FAT32，卷标 `ROCKCHIP`

使用 `debugfs` 可以在不挂载、不回放日志的情况下查看 `system.img`：

```bash
debugfs -R 'ls -l /' backup/partitions/system.img
```

根目录确认这是完整 Android `/system`，包括 `app`、`bin`、`etc`、`framework`、`lib`、`vendor` 和 `xbin`。

## 原厂固件导出

### 导出方式

通过 `debugfs rdump` 导出固件目录：

```bash
mkdir -p backup/extracted/system/etc
mkdir -p backup/extracted/system

sudo debugfs -R \
  'rdump /etc/firmware backup/extracted/system/etc' \
  backup/partitions/system.img

sudo debugfs -R \
  'rdump /vendor backup/extracted/system' \
  backup/partitions/system.img
```

普通用户运行 `rdump` 时出现过：

```text
Operation not permitted while changing ownership
```

这是 `debugfs` 尝试恢复 Android 原始 UID/GID 时缺少 `chown` 权限，不代表文件内容导出失败。改用 `sudo debugfs` 后可以保留 ownership，之后再把导出目录所有权交还当前用户。

### Wi-Fi

原厂 `/etc/firmware` 包含大量 Rockchip 通用 BSP 固件，不能把整个目录都当成 R1 实际所需文件。启动日志明确确认 R1 使用：

```text
/system/etc/firmware/fw_bcm43455c0_ag.bin
/system/etc/firmware/nvram_ap6255.txt
```

日志同时显示：

```text
Current WiFi chip is AP6335
chip: 0x4345 rev: 0x6
Firmware version 7.45.100.6
```

Wi-Fi 固件内部字符串也包含 `43455c0` 和版本 `7.45.100.6`。因此这两个文件是已经由运行日志闭环确认的目标文件：

- `fw_bcm43455c0_ag.bin` 是无线芯片实际执行的固件
- `nvram_ap6255.txt` 是板级射频、晶振、天线、PA 和 Bluetooth coexistence 参数

NVRAM 中确认晶振参数为 `37400 kHz`。MAC 地址不写入本学习记录。

### Bluetooth

原厂配置确认 Bluetooth 使用：

```text
UartPort = /dev/ttyS1
FwPatchFilePath = /vendor/firmware/
```

设备树运行日志还给出了：

```text
UART RTS GPIO = 102
BT power GPIO = 93
BT wake GPIO = 123
BT host wake IRQ/GPIO = 122
```

`/vendor/firmware/BCM4345.hcd` 已经导出。它是当前最可能的 Bluetooth patchram 文件，但本次启动日志没有记录 vendor library 最终选择的 HCD 文件名，因此这一项仍标记为待确认。

`libbt-vendor.so` 属于 Android 中间层：它负责打开 `/dev/ttyS1`、识别控制器版本、选择 `.hcd` 并下载。`.hcd` 本身才是 Bluetooth 控制器执行的固件。

### AK7755

原厂 `/vendor/firmware` 中存在两组 AK7755 数据：

```text
ak7755_pram_data2.bin
ak7755_cram_data2.bin
ak7755_ofreg_data2.bin
ak7755_pram_data3.bin
ak7755_cram_data3.bin
ak7755_ofreg_data3.bin
```

启动日志明确确认实际加载的是 `data2`：

```text
ak7755_pram_data2.bin size=5308
PRAM CRC success
ak7755_cram_data2.bin size=1113
CRAM CRC success
```

因此已经获得原厂实际运行的 AK7755 DSP 程序与参数。`data3` 是备用配置或另一工作模式，目前没有运行证据说明本次启动使用它。

这些 `.bin` 不是 Android 音频中间层。Android 或内核驱动只负责下载它们，AK7755 DSP 才是实际执行者。`audio_policy.conf`、`mixer_paths.xml`、`asound.conf` 等文件才属于 Android/ALSA 配置层，它们对还原路由和默认增益仍然有参考价值。

### boot.img

```bash
# boot.img
dd if=backup/partitions/boot.img \
  of=backup/unpacked/boot/kernel \
  bs=1M iflag=skip_bytes,count_bytes \
  skip=$((0x4000)) count=$((0x760a38)) \
  status=progress

dd if=backup/partitions/boot.img \
  of=backup/unpacked/boot/ramdisk.gz \
  bs=1M iflag=skip_bytes,count_bytes \
  skip=$((0x768000)) count=$((0x16c4ac)) \
  status=progress

dd if=backup/partitions/boot.img \
  of=backup/unpacked/boot/resource.img \
  bs=1M iflag=skip_bytes,count_bytes \
  skip=$((0x8d8000)) count=$((0x42400)) \
  status=progress
```

### recovery.img

```bash
# recovery.img
dd if=backup/partitions/recovery.img \
  of=backup/unpacked/recovery/kernel \
  bs=1M iflag=skip_bytes,count_bytes \
  skip=$((0x4000)) count=$((0x760a38)) \
  status=progress

dd if=backup/partitions/recovery.img \
  of=backup/unpacked/recovery/ramdisk.gz \
  bs=1M iflag=skip_bytes,count_bytes \
  skip=$((0x768000)) count=$((0x2d35e4)) \
  status=progress

dd if=backup/partitions/recovery.img \
  of=backup/unpacked/recovery/resource.img \
  bs=1M iflag=skip_bytes,count_bytes \
  skip=$((0xa3c000)) count=$((0x42400)) \
  status=progress
```

解ramdisk
```bash
(
  cd backup/unpacked/boot/ramdisk
  gzip -dc ../ramdisk.gz |
    cpio -idm \
      --no-absolute-filenames \
      --no-preserve-owner
)

(
  cd backup/unpacked/recovery/ramdisk
  gzip -dc ../ramdisk.gz |
    cpio -idm \
      --no-absolute-filenames \
      --no-preserve-owner
)
```

从 Rockchip resource image 中提取 DTB

```bash
dd if=backup/unpacked/boot/resource.img \
  of=backup/unpacked/boot/rk-kernel.dtb \
  bs=1M iflag=skip_bytes,count_bytes \
  skip=$((0x800)) count=$((0x135c2)) \
  status=progress

dd if=backup/unpacked/recovery/resource.img \
  of=backup/unpacked/recovery/rk-kernel.dtb \
  bs=1M iflag=skip_bytes,count_bytes \
  skip=$((0x800)) count=$((0x135c2)) \
  status=progress
```

反编译 DTB


```bash
file \
  backup/unpacked/boot/rk-kernel.dtb \
  backup/unpacked/recovery/rk-kernel.dtb

dtc -I dtb -O dts \
  -o backup/unpacked/boot/rk-kernel.dts \
  backup/unpacked/boot/rk-kernel.dtb \
  2>backup/unpacked/boot/dtc-warnings.log

dtc -I dtb -O dts \
  -o backup/unpacked/recovery/rk-kernel.dts \
  backup/unpacked/recovery/rk-kernel.dtb \
  2>backup/unpacked/recovery/dtc-warnings.log
```

```bash
backup/unpacked/boot/rk-kernel.dtb:     Device Tree Blob version 17, size=79298, boot CPU=0, string block size=3822, DT structure block size=75420
backup/unpacked/recovery/rk-kernel.dtb: Device Tree Blob version 17, size=79298, boot CPU=0, string block size=3822, DT structure block size=75420
```
需要分析的 ramdisk 文件：

```bash
find \
  backup/unpacked/boot/ramdisk \
  backup/unpacked/recovery/ramdisk \
  -type f \
  \( \
    -name 'init*.rc' -o \
    -name 'fstab*' -o \
    -name 'ueventd*.rc' -o \
    -name 'default.prop' -o \
    -name '*sepolicy*' -o \
    -name '*_contexts' -o \
    -name 'selinux_version' \
  \) \
  -print |
  sort |
  tee backup/unpacked/ramdisk-file-list.txt
```

```bash
backup/unpacked/boot/ramdisk/default.prop
backup/unpacked/boot/ramdisk/file_contexts
backup/unpacked/boot/ramdisk/fstab.rk30board.bootmode.emmc
backup/unpacked/boot/ramdisk/fstab.rk30board.bootmode.unknown
backup/unpacked/boot/ramdisk/init.box.rc
backup/unpacked/boot/ramdisk/init.box.samba.rc
backup/unpacked/boot/ramdisk/init.connectivity.rc
backup/unpacked/boot/ramdisk/init.environ.rc
backup/unpacked/boot/ramdisk/init.rc
backup/unpacked/boot/ramdisk/init.rk30board.bootmode.emmc.rc
backup/unpacked/boot/ramdisk/init.rk30board.bootmode.unknown.rc
backup/unpacked/boot/ramdisk/init.rk30board.environment.rc
backup/unpacked/boot/ramdisk/init.rk30board.rc
backup/unpacked/boot/ramdisk/init.rk30board.usb.rc
backup/unpacked/boot/ramdisk/init.rockchip.rc
backup/unpacked/boot/ramdisk/init.trace.rc
backup/unpacked/boot/ramdisk/init.usb.rc
backup/unpacked/boot/ramdisk/init.zygote32.rc
backup/unpacked/boot/ramdisk/property_contexts
backup/unpacked/boot/ramdisk/seapp_contexts
backup/unpacked/boot/ramdisk/selinux_version
backup/unpacked/boot/ramdisk/sepolicy
backup/unpacked/boot/ramdisk/service_contexts
backup/unpacked/boot/ramdisk/ueventd.rc
backup/unpacked/boot/ramdisk/ueventd.rk30board.rc
backup/unpacked/recovery/ramdisk/default.prop
backup/unpacked/recovery/ramdisk/file_contexts
backup/unpacked/recovery/ramdisk/fstab.rk30board.bootmode.emmc
backup/unpacked/recovery/ramdisk/fstab.rk30board.bootmode.unknown
backup/unpacked/recovery/ramdisk/init.bootmode.emmc.rc
backup/unpacked/recovery/ramdisk/init.bootmode.unknown.rc
backup/unpacked/recovery/ramdisk/init.rc
backup/unpacked/recovery/ramdisk/property_contexts
backup/unpacked/recovery/ramdisk/seapp_contexts
backup/unpacked/recovery/ramdisk/selinux_version
backup/unpacked/recovery/ramdisk/sepolicy
backup/unpacked/recovery/ramdisk/service_contexts
backup/unpacked/recovery/ramdisk/ueventd.rc
backup/unpacked/recovery/ramdisk/ueventd.rk30board.rc
```

搜索 system 中额外的 SELinux 文件：

```bash
find backup/extracted/system \
  -type f \
  \( \
    -name '*sepolicy*' -o \
    -name '*_contexts' -o \
    -name 'selinux_version' \
  \) \
  -print |
  sort |
  tee backup/unpacked/system-selinux-file-list.txt
```

确认 boot/recovery 是否使用相同 kernel 和 DTB

```bash
cmp -s \
  backup/unpacked/boot/kernel \
  backup/unpacked/recovery/kernel \
  && echo 'boot/recovery kernel 相同' \
  || echo 'boot/recovery kernel 不同'

cmp -s \
  backup/unpacked/boot/rk-kernel.dtb \
  backup/unpacked/recovery/rk-kernel.dtb \
  && echo 'boot/recovery DTB 相同' \
  || echo 'boot/recovery DTB 不同'
```


## boot/recovery 拆解结果

`boot.img` 和 `recovery.img` 均为 Android boot image，page size 为 16384。两者都成功提取出 ARM zImage、gzip cpio ramdisk 和以 `RSCE` 开头的 Rockchip resource image。resource 内的 `rk-kernel.dtb` 已提取并用 `dtc` 反编译。

逐字节比较确认 boot 与 recovery 使用相同的 kernel 和 DTB。两者的主要差异在 ramdisk：boot ramdisk 包含完整 Android init 配置与服务，recovery ramdisk 使用精简 `init.rc`，运行 `/sbin/recovery`，并带有 BusyBox、恢复界面资源和 recovery ADB。

两套 ramdisk 均包含单体 `sepolicy`、`file_contexts`、`property_contexts`、`service_contexts`、`seapp_contexts` 和 `selinux_version`。从已导出的 system 文件树中没有额外找到同名 SELinux policy 文件。

原厂 zImage 没有被 `extract-ikconfig` 识别出内嵌配置。随后在运行中的原厂 Android 检查 `/proc/config.gz`，结果为 `No such file or directory`。因此当前镜像和运行系统都无法提供完整 `.config`，后续需要寻找匹配 BSP 的 defconfig，并结合 DTB、启动日志和已加载模块反推必要选项。

### DTB 中确认的关键连接

- eMMC：`rksdmmc@30020000`，8-bit，总线支持 HS200
- 外置 SD：`rksdmmc@30000000`
- Wi-Fi SDIO：`rksdmmc@30010000`
- Wi-Fi：AP6335，power GPIO2_D2，host wake GPIO0_D4
- Bluetooth：UART1/`ttyS1`，power GPIO2_D5，RTS GPIO3_A6，wake GPIO3_D3，host wake GPIO3_D2
- 音频控制：I2C1 `0x11060000`
- AK7755：I2C `0x19`，PDN GPIO1_A3
- AK7755 音频：I2S2 `0x100e0000`
- TPA3118D2：shutdown GPIO3_B7，mute GPIO3_C1
- 三个 ES7243 配置地址：`0x11`、`0x12`、`0x13`；但原厂启动日志显示访问失败，实际硬件状态待确认

DTB 同时保留了多种厂商参考板音频节点，不能把所有 `status = "okay"` 都视为 R1 实机证据。AK7755 路径有启动日志中的声卡注册和固件 CRC 成功作为交叉验证，可信度更高。

## 🎯 已确认事实与剩余问题

### 已确认

- SoC 是 RK3229，512 MiB DDR3
- 原厂 DDR Loader 是 `1.06`，Boot1 是 `2.37`
- UART 控制台是 `ttyFIQ0`，波特率为 1500000
- Bluetooth 控制 UART 是 `/dev/ttyS1`
- Wi-Fi/BT 组合模块被原厂识别为 AP6335，Wi-Fi 核心为 BCM43455C0
- Wi-Fi 实际加载 `fw_bcm43455c0_ag.bin` 和 `nvram_ap6255.txt`
- AK7755 实际加载 `data2` PRAM/CRAM，CRC 成功
- ASoC 注册了 `RK_AK7755`
- eMMC User Area 已完整只读备份
- 原厂 `system.img`、boot、recovery、kernel、resource、trust 和 U-Boot 已提取
- boot/recovery 的 kernel、ramdisk、DTB、init、fstab 和 SELinux policy 已提取
- boot/recovery 使用相同 kernel 和 DTB

### 待确认

- 继续将原厂 DTB 整理为可移植的 GPIO、pinctrl、regulator、SDIO、UART、I2C 和 I2S 映射
- 确认 Bluetooth 最终选择的 `.hcd` 文件和运行波特率
- 提取原厂内核配置
- 判断 AK7755 `OFREG` 和可能的 ACRAM 加载时序
- 实机验证功放 enable/mute GPIO 的有效电平与上电时序
- 确认 eMMC boot0/boot1 是否启用及如何只读保存
- 验证完整恢复路径，但在此之前不写正常启动分区

## 🔐 安全与许可

原厂镜像、Wi-Fi 固件、NVRAM、Bluetooth HCD、AK7755 程序和厂商库可能没有公开再分发许可。它们只应保存在本地备份中，不应直接提交公开仓库。

实机日志可能包含 MAC 地址、序列号、设备密钥或网络信息。公开分享前必须脱敏。建议将整个 `backup/` 目录加入 `.gitignore`，公开仓库只记录文件名、用途、提取过程和已脱敏的技术结论。

后续继续遵守以下原则：

- 完整备份前不写 Bootloader；本轮已满足备份条件，但仍不代表可以安全写入
- 恢复路径验证前不覆盖正常启动分区
- 优先对备份副本做分析，不直接修改唯一镜像
- 使用 MaskROM 工具时区分 `DB/RL` 与 `UL/WL/EF`
- 任何写操作都必须先明确起始扇区、长度和恢复方案

## 🔗 参考资料

[^1]: Rockchip Linux. “rkbin at commit 972c281026c9e3ac969b24629df0b907227aa4d9.” GitHub. https://github.com/rockchip-linux/rkbin/tree/972c281026c9e3ac969b24629df0b907227aa4d9

## Linux 6.18.42 主线构建基线

主线版本选择 Linux `6.18.42` LTS。项目没有切换用户已有的 `/home/pansy/repos/linux` 工作树，而是在 `build/kernel-src` 创建独立浅仓库：

```sh
scripts/prepare-kernel-source.sh

git -C build/kernel-src describe --exact-match --tags HEAD
git -C build/kernel-src status --short --branch
```

验证结果为精确标签 `v6.18.42` 和 detached HEAD。

最初尝试将已有 Linux Git 仓库作为 shared object cache。该仓库约有 1,175 万个 packed object，shared clone 长时间停留在对象连通性检查；改用 alternates 后又明确报告缺失对象：

```text
error: Could not read 5142c56651578abc346d6c17f3fb919b9ffbb317
error: Could not read e144887d3ae659dd3510bc177977e9864f964197
fatal: Failed to traverse parents of commit 37e2f878a7a660a216cc7a60459995fefd150f25
error: remote did not send all necessary objects
```

因此最终结论是：这个本地仓库可用于人工参考源码，但不能作为可复现构建的可靠对象缓存。准备脚本改为直接从 kernel.org stable 仓库浅抓取指定标签，避免修改或依赖已有工作树。

主机端构建命令：

```sh
scripts/build-kernel.sh
```

配置以 `multi_v7_defconfig` 为基础，再合并 `kernel/config/r1.fragment`。板级 DTS 保存在项目内，通过 C preprocessor 和内核构建出的 dtc 编译，不需要把文件复制进内核源码树。

构建输出：

```text
build/artifacts/zImage
build/artifacts/rk3229-phicomm-r1.dtb
build/artifacts/kernel.config
```

`file` 确认 zImage 是 ARM Linux boot executable，DTB 为 25,020 字节的 version 17 Device Tree Blob。将 DTB 反编译后检查，`Phicomm R1`、`memory@60000000`、UART2、RK805 `pmic@18`、eMMC `mmc@30020000` 和 USB peripheral 配置均存在。最终 `.config` 中也确认了强制串口命令行、RK322x clock、Rockchip pinctrl/GPIO、RK805 regulator、DW MMC、initrd 和 DWC2 等选项。

构建及 DTB round-trip 只有一条来自上游 `rk322x.dtsi` 的 `graph_child_address` 警告，位置是 VOP port；没有出现 R1 板级节点的 dtc 错误。主机没有安装 `dt-validate`，因此本阶段未执行 DT schema 验证。

以上是主机端静态验证，不代表内核已经在 R1 上运行。尤其是 PMIC IRQ GPIO、电源约束、eMMC HS200 和 USB 模式仍需串口实机日志确认。

## 最小救援 initramfs

检查发现原厂 recovery ramdisk 中的 `sbin/busybox` 是静态链接的 32 位 ARM EABI5 ELF；主机 `/usr/bin/busybox` 是 x86-64，已有 `/home/pansy/busybox` 构建则是 AArch64，都不能用于 RK3229 的 32 位用户空间。

项目新增 `initramfs/init` 和构建脚本：

```sh
scripts/build-initramfs.sh
```

默认 BusyBox 只从本地 recovery 提取目录复制到 `build/`。也可替换为自行构建的静态 32 位 ARM BusyBox：

```sh
BUSYBOX=/path/to/static-arm-busybox \
  scripts/build-initramfs.sh
```

产物为：

```text
build/artifacts/r1-initramfs.cpio.gz
```

解包列表确认归档内只有 BusyBox、必要 applet 链接、`/init`、`/dev`、`/proc` 和 `/sys`。`/init` 挂载虚拟文件系统，打印内核命令行、内存和块设备信息，然后在串口循环启动 shell；它不会自动挂载或修改 eMMC。

构建脚本将归档时间戳和属主固定，并使用 cpio reproducible 模式与 gzip 无时间戳模式。连续运行两次后用 `cmp` 验证产物逐字节一致。

当前已有 zImage、DTB 和 initramfs 三个首轮启动组件，但还没有生成或写入 Android boot image。下一步先确认原厂 U-Boot 支持的 RAM 加载方法、`bootz` 参数和内存地址，优先实现完全不写存储的启动。

## 原厂 U-Boot 能力与 RAM 启动候选镜像

对 `backup/partitions/uboot.img` 做字符串检查，并与冷启动日志交叉验证：

```sh
LC_ALL=C strings -a -n 4 backup/partitions/uboot.img |
  rg 'bootcmd=bootrk|bootm|bootrk|fastboot|rockusb|ums|bootz'
```

已验证结论：默认环境为 `bootcmd=bootrk`、`bootdelay=0`；原厂启动日志实际打印 `kernel`、`ramdisk` 和 `bootrk: do_bootm_linux...`；镜像中包含 `bootm`、`bootrk`、`fastboot`、`rockusb` 和 `ums`，没有找到 `bootz`。

Fastboot 字符串包含 `fbt_handle_boot`、`download:`、`do_bootrk() returned`、`FAILinvalid boot image` 和 `fastboot_unlocked`，说明这个 U-Boot 实现了下载 Android boot image 后直接从 RAM 启动的路径。它也会在锁定状态拒绝 download，因此必须实机读取 `unlocked`，不能根据 `SecureBootEn = 0` 推断 Fastboot 已解锁。

原厂 Android boot header 手工解码结果：

| 字段 | 值 |
|---|---:|
| kernel address | `0x60408000` |
| ramdisk address | `0x62000000` |
| second address | `0x60f00000` |
| tags address | `0x60088000` |
| page size | 16384 |

原厂 second-stage 是以 `RSCE` 开头的 Rockchip resource image，内部 DTB 路径为 `rk-kernel.dtb`。项目新增以下构建命令：

```sh
scripts/build-boot-image.sh
```

输出为：

```text
build/artifacts/r1-resource.img
build/artifacts/r1-mainline-boot.img
```

旧版 Rockchip `resource_tool` 会把 32-byte hash 数组中 SHA-1 后未使用的 12 字节留为未初始化内容。首次重复构建因此在 resource image 中出现一个随机字节，连带 Android boot header 的 image ID 变化。构建脚本现将这 12 个保留字节归零；随后连续构建两次，resource 和 boot image 均通过 `cmp`。

离线拆包验证：

```sh
unpack_bootimg \
  --boot_img build/artifacts/r1-mainline-boot.img \
  --out /tmp/r1-mainline-boot-inspect \
  --format=info

resource_tool \
  --image=build/artifacts/r1-resource.img \
  --unpack /tmp/r1-resource-unpack
```

标准工具识别出 12,005,888-byte kernel、626,712-byte ramdisk 和 26,112-byte second stage。resource 解包得到 25,020-byte `rk-kernel.dtb`，与构建输入逐字节一致。

候选镜像总大小为 12,697,600 字节，原厂 boot 分区镜像为 12,582,912 字节，候选大 114,688 字节。因此它不能写入 boot 分区。它当前只作为 Fastboot RAM download 候选；实机下一步先运行只读的：

```sh
scripts/check-fastboot.sh
```

该脚本只查询 `product`、`version-bootloader`、`secure`、`unlocked` 和 `max-download-size`，不会下载或写入。只有确认 Fastboot 解锁且下载上限足够后，才讨论执行 `fastboot boot`。尚未确认的 `upgrade_tool RUN` 不在本阶段试跑。

## Fastboot 实机锁状态

在原厂 Android 串口 shell 执行：

```sh
setprop sys.powerctl reboot,fastboot
```

设备没有修改分区，正常重启后由 U-Boot 明确识别 fastboot reboot reason：

```text
Restarting system with command 'fastboot'.
reboot fastboot.
SecureBootEn = 0, SecureBootLock = 0
```

此后 U-Boot 串口停止输出并等待 USB gadget。主机能够通过 `fastboot devices` 发现设备，运行只读检查：

```sh
scripts/check-fastboot.sh
```

结果：

```text
product: fastboot
version-bootloader: jenkins-R1-master-user-3448
secure: yes
unlocked: no
getvar:max-download-size FAILED (remote: 'unknown variable')
```

结论是 Fastboot 协议可用，但 download/boot 保护处于锁定状态。`SecureBootEn = 0` 描述 U-Boot 的签名验证配置，`secure: yes` 与 `unlocked: no` 描述 Fastboot lock 状态，它们不是同一开关。

原厂 U-Boot 镜像中的 Fastboot 字符串明确包含锁定时拒绝 download，以及 unlock 流程访问和擦除 userdata 的路径。因此本阶段不试跑 `fastboot boot`，也不执行 `fastboot oem unlock` 或任何 flash 命令。

继续静态检查 `upgrade_tool` 后发现 `RUN` 对应以下底层调用与错误信息：

```text
RKU_WriteSDRam
RKU_RunSDRam
RunSystem: RUN <uboot_addr> <trust_addr> <boot_addr> <uboot> <trust> <boot>
```

这证明 `RUN` 设计为向 SDRAM 发送 U-Boot、trust 和 boot 后从 RAM 运行，与 `RKU_WriteLBA` 不是同一条路径。但目前还没有确认三个地址所指的是镜像加载基址、入口地址还是厂商打包镜像内部地址，也没有确认 MaskROM/Loader 状态要求，所以仍不执行。下一步应先解析工具参数构造和原厂 loader 的对应地址。

### 对 `RUN` 的进一步反汇编与结论修正

由于 `upgrade_tool` 没有移除符号，可以定位并反汇编：

```sh
nm -C upgrade_tool | rg 'run_system|RKU_(Write|Run)SDRam'

objdump -d -C -Mintel \
  --start-address=0x40bbd0 \
  --stop-address=0x40bfd2 \
  upgrade_tool
```

确认函数签名：

```text
run_system(device, uboot_addr, trust_addr, boot_addr,
           uboot_file, trust_file, boot_file)
```

反汇编带来三个关键结论：

1. 三个文件路径只传给 `get_file_size()`；函数没有打开、读取或发送任何一个文件的内容。
2. 三个地址参数都执行 `<< 9` 后写入参数表，说明命令行地址以 512-byte sector 为单位，而不是直接的 SDRAM byte address。
3. 工具构造一个 56-byte 参数表，包含 magic、三个 image type、转换后的 offset 和本地文件 size；`RKU_WriteSDRam(56, table)` 只发送这张表，然后调用 `RKU_RunSDRam()`。

因此前一段“向 SDRAM 发送 U-Boot、trust 和 boot”的初步判断不完整。实际发送到 SDRAM 的只有运行描述表，设备再根据 offset 和 size 使用存储中已有的镜像。三个本地文件仅用于提供 size，不能通过 `RUN` 把项目生成的 boot image 上传到设备。

这条路径不能满足零写入的自定义主线启动目标。当前剩余方案是 Fastboot unlock 或 recovery 分区测试；两者都会改变设备状态，其中 unlock 还可能擦除 userdata，必须先验证恢复流程并取得明确授权。

## 评估恩山 R1 Root 教程

核对公开帖子《斐讯R1 root小白版教程 来填坑了》可打印正文：

```text
https://www.right.com.cn/forum/thread-1313117-1-1.html
```

帖子推荐的实体机步骤不是利用原厂 Android 中的提权漏洞，也不是只安装 `su`。其核心操作是先进入 Rockchip 低级 USB 模式，然后执行：

```sh
./rkdeveloptool rl 0x0 0x458000 out.img
./rkdeveloptool wl 0x0 r1_root_rush.img
```

帖子说明 `r1_root_rush.img` 是版本 3166 的预 Root 镜像。`wl 0x0` 从 eMMC User Area 的 LBA 0 开始写入，因此会覆盖开头的 parameter/idb、U-Boot、trust、boot/recovery 及随后落入镜像长度内的其他分区；它不是对当前系统的局部、可审计修改。帖子中的备份长度 `0x458000` sectors 也只有 2,334,064,640 bytes，不是本机已确认的 7,818,182,656-byte 完整 User Area。

由此得到以下结论：

- 不在本项目中直接执行该教程的 `wl 0x0`，也不使用来源和内容尚未审计的整盘 Root 镜像；
- Android root 与 U-Boot Fastboot lock 是两个层级。获得 Android UID 0 不会自动把 U-Boot 的 `fastboot_unlocked` 改为 unlocked；
- 即使 Android root 能直接访问块设备，其作用也只是提供另一条持久写入路径，并不比已经可用的 MaskROM 定址写入更接近“零写入 RAM 启动”；
- 更安全的下一项只读调查是启动原厂 recovery，检查 recovery ADB 是否枚举以及 `adbd` 是否已具有 root 权限。离线拆出的 recovery 已确认 `default.prop` 包含 `ro.debuggable=1` 和 ADB USB 配置，`init.rc` 会启动 `/sbin/adbd`，并支持 `service.adb.root=1` 触发 `adbd` 重启。静态配置表明 `adb root` 值得实测，但尚未验证 USB 枚举和最终 UID。该结果只能改善救援和定址访问能力，不能预先假定会解锁 Fastboot。

本轮只读取了公开网页和本地镜像字符串，没有对设备执行 Root、unlock、download、flash、LBA write 或 erase。

## 原厂 recovery ADB 首次枚举

设备进入原厂 recovery 后，主机实测：

```text
$ adb devices -l
List of devices attached
0123456789ABCDEF recovery usb:3-4 product:rk322x_echo model:rk322x_box device:rk322x_echo transport_id:3
```

这确认焊接 USB、recovery USB gadget 和 ADB transport 均已工作。直接执行 `adb shell id` 失败：

```text
- exec '/system/bin/sh' failed: No such file or directory (2) -
using port=5555
```

失败原因不是设备离线，也尚不能据此判断 adbd UID。该版 recovery 的 ramdisk 没有 `/system/bin/sh`，实际提供 `/sbin/sh` 和 `/sbin/busybox`。虽然 adbd 字符串包含 `exec:` 服务，但这个旧实现的 raw exec 仍可能通过默认 shell 执行，不能先假定 `adb exec-out` 一定绕过该路径。下一步先使用不依赖 shell 的 `adb root` 服务；若 daemon 成功以 root 重启，再用 ADB sync 将 `/sbin/sh` 的副本推到 recovery 的 RAM rootfs `/system/bin/sh`，然后执行 `adb shell id`。这一临时文件在重启后消失，不写 eMMC。

命令 `adb shell 0123456789ABCDEF` 不是选择序列号的语法，它会把序列号当作远端命令。多设备场景应使用 `adb -s 0123456789ABCDEF ...`。

## 修正主线启动镜像内存布局

准备 recovery 测试前重新核对 Android boot header 的实际加载区间，发现原先照搬厂商地址会发生两处冲突：

```text
zImage:  [0x60408000, 0x60f7b200)
second:  0x60f00000
overlap: 0x7b200 bytes
```

同时，`build/kernel/vmlinux` 的 `_end` 为虚拟地址 `0xc20dfc10`，按本机 `PAGE_OFFSET=0xc0000000` 和 RAM base `0x60000000` 换算，运行时物理结尾为 `0x620dfc10`。原 ramdisk 地址 `0x62000000` 会落入内核运行范围约 `0xdfc10` 字节。此前的离线拆包检查只能证明容器格式正确，不能发现 U-Boot 加载后的地址重叠，因此旧候选不能上板。

构建脚本保留厂商 kernel 与 tags 地址，将其他载荷移到：

```text
kernel:  [0x60408000, 0x60f7b200)
ramdisk: [0x64000000, 0x64099018)
second:  [0x66000000, 0x66006600)
trust OS reserved base: 0x68400000
```

`scripts/build-boot-image.sh` 现在会输出上述区间并拒绝明显重叠的打包。重新构建后，`unpack_bootimg` 确认 header 地址为 kernel `0x60408000`、ramdisk `0x64000000`、second `0x66000000`、tags `0x60088000`。链接内核结尾、三个载荷和 Trust OS 保留区的边界检查通过，连续两次构建逐字节一致。

脚本同时新增精确的 recovery 分区候选：

```text
build/artifacts/r1-mainline-recovery.img  33554432 bytes
```

其前 12,697,600 bytes 与 `r1-mainline-boot.img` 完全一致，剩余空间为零；总长度严格等于 recovery 的 `0x10000` sectors。这样未来的测试写入和原厂 `backup/partitions/recovery.img` 回滚都可以使用相同的明确边界。本轮只生成和检查主机端文件，没有写设备。

## recovery 写入前的 MaskROM USB 不稳定

准备执行 recovery 只读回读时，`rkdeveloptool ld` 能看到 `2207:320b Maskrom`，但 `rci` 和 `rfi` 失败。主机内核日志给出连续多次枚举失败：

```text
Device not responding to setup address
device not accepting address ..., error -71
device descriptor read/64, error -110
Timeout while waiting for setup device command
```

`-71` 对应 USB protocol error，`-110` 对应 timeout。设备号从 52 连续变化到 70，说明主机在反复断开和重新枚举设备；这不是 `rkdeveloptool` lock 状态或分区问题。进程检查也确认没有遗留的 `rkdeveloptool` 或 `upgrade_tool` 占用设备。

后续 Device 72 能够完整枚举为 480 Mbps High Speed，configuration 包含一个 vendor-specific interface，以及 bulk IN `0x81`、bulk OUT `0x02` 两个 512-byte endpoint。`lsusb -t` 显示它直连 xHCI 的 USB 2.0 root hub port 3-4，没有经过外部 Hub。描述符已恢复正常，但之前的大量 `EPROTO`/`ETIMEDOUT` 足以证明当前链路不适合写入。

处理边界：优先检查和重焊 GND、D-、D+，保持 D+/D- 成对且尽量短；使用原装电源给板子供电并断开 USB VBUS，避免主机端 400 mA 供电余量不足或回灌。只有 `rci`、`rid`、`rfi` 连续多轮通过，并且同一 recovery 区间连续读取结果一致后，才重新讨论 `wl`。本轮没有执行任何写操作。

## 首次 recovery 写入、自动恢复与启动日志判定

修复 USB 连接后，`rci`、`rid`、`rfi` 连续三轮成功，芯片信息、eMMC ID 和 15,269,888-sector 容量完全一致。随后从 LBA `0x01e000` 连续读取两次 `0x010000` sectors；两份 33,554,432-byte 结果逐字节一致，并与 `backup/partitions/recovery.img` 一致。MaskROM 通信与 recovery 只读回滚基线因此通过。

用户明确授权只写 recovery 后，候选通过以下边界写入并立即读回：

```text
start: 0x01e000
length: 0x010000 sectors
source: build/artifacts/r1-mainline-recovery.img
```

主机保存的 `/tmp/r1-recovery-after-write.img` 与候选逐字节一致、与原厂 recovery 不同，证明本次 MaskROM 写入和读回本身成功。之后按原计划先正常启动 Android，再执行 `setprop sys.powerctl reboot,recovery`。串口日志保存为本地证据 `build/artifacts/bootlog`。

日志只包含一次启动，U-Boot 报告：

```text
kernel   @ 0x62000000 (0x00760a38)
ramdisk  @ 0x65bf0000 (0x002d35e4)
Linux version 3.10.0 (jenkins@phicomm)
```

这些地址、大小和版本均属于原厂 recovery，而项目候选应为 kernel `0x60408000`、ramdisk `0x64000000` 和 Linux 6.18.42。因此这不是主线启动失败日志，而是原厂 recovery 被恢复后正常启动的日志。

原因已由原厂镜像直接确认。boot ramdisk 的 `init.rc` 定义：

```text
service flash_recovery /system/bin/install-recovery.sh
    class main
    oneshot
```

`system.img` 中的脚本会校验 recovery；哈希不匹配时使用 `/system/recovery-from-boot.p` 和原厂 boot 镜像重建 recovery。也就是说，“写候选 → 正常启动 Android → 再请求 recovery”必然触发原厂自动恢复。

下一次测试不能经过 Android。原厂 recovery 的 BusyBox 已在主机通过 QEMU 查询 applet 列表，确认包含 `dd`、`cmp`、`sync`、`cat`、`mount` 和 `reboot`，仅缺少此前尝试的 `id`。新方案是在原厂 recovery 运行期间通过 ADB 将候选推入 `/tmp`，由串口 root shell 通过 `/dev/block/platform/30020000.rksdmmc/by-name/recovery` 定点写 recovery，在 RAM 中读回比较后直接执行 `adb reboot recovery`。这样运行中的 recovery 已全部加载到 RAM，且不会启动 `flash_recovery` 服务。

实际准备推送时还发现两个必须记录的安全细节：

1. recovery SELinux 处于 enforcing，`adbd` 域写 `/tmp` 会得到 `avc: denied { write }`，即使 Unix mode 已经放宽也不能推送。可由串口 root 临时向 `/sys/fs/selinux/enforce` 写 `0`，只改变当前 RAM 中 recovery 的 enforcement 状态；重启后恢复。
2. 当前 recovery 中 `by-name/recovery` 实测解析为 `/dev/block/mmcblk0p9`，不是先前根据普通 Android `/proc/partitions` 推断的 `p8`。recovery 启动日志会把 `parameter` 注册为 4 MiB 的首个分区，后续编号整体变化，并明确列出 recovery 范围 `0x03c00000 -- 0x05c00000 (32 MB)`。因此不能跨启动模式硬编码 `mmcblk0pN`；写前必须同时验证 by-name 解析结果和 33,554,432-byte 容量，实际 `dd` 也应使用 by-name 路径。

先前文档中的 `mmcblk0p8` 目标已撤销。没有对 `p8` 或 `p9` 执行本轮写入。

## USB-TTL 5 V 供电导致 USB 间歇枚举失败

再次准备使用 recovery ADB 时，主机始终无法稳定发现设备。更换较短的 USB 数据线后问题仍会复现，但偶尔能够成功枚举。主机内核日志显示同一端口反复分配新的设备号，并出现：

```text
device descriptor read/64, error -71
Device not responding to setup address
device not accepting address ..., error -71
unable to enumerate USB device
```

链路偶尔能够以 High Speed 完整枚举为 Rockchip `2207:320b`，随后又断开并在 Full Speed 枚举阶段失败。D+/D- 极性错误因此可以排除；仅用差分对几毫米的长度偏差也不足以解释这种现象。

本轮最终确认，故障复现时 R1 不是由原装电源供电，而是连接了 USB-TTL 模块的 5 V 引脚并由其给整机供电。改回原装电源后 USB 恢复可用，验证供电方式是本次间歇枚举故障的原因。USB-TTL 的 VCC 输出不能作为 R1 的整机电源：启动和 USB PHY 建链期间的瞬态负载可能造成电压跌落，而普通万用表未必能够捕获这种短时变化。

后续固定接线边界：R1 使用原装电源从原生电源输入供电；USB-TTL 只连接 GND、TX 和 RX，不连接 VCC，并使用与 R1 UART 匹配的 3.3 V 逻辑电平；USB 调试线连接 GND、D- 和 D+，在独立供电场景下断开 USB VBUS，避免双路供电或倒灌。遇到 `-71`、设备号连续变化或偶发枚举时，先核对供电拓扑，再调查线长、焊点、xHCI 或软件状态。本轮没有执行设备写入。

## recovery ADB 中转受阻与 UART XMODEM 传输

原厂 recovery 的 adbd 可枚举，但向 mode `0777` 的 `/cache/recovery` 和 `/tmp` 推送仍返回 `remote Permission denied`。AVC 分别确认 adbd 为 `u:r:adbd:s0`，且不允许写对应的 `cache_file`/`tmpfs`。串口 shell 虽为 UID 0 和 `u:r:init:s0`，但写 `/sys/fs/selinux/enforce` 被拒绝 `{ setenforce }`；尝试以 `context=u:object_r:shell_data_file:s0` 挂载专用 tmpfs 又被拒绝 `{ relabelto }`。`adb root` 在主机端等待重连超时，重启后的 adbd 实测仍为 UID 2000。因此此前“临时切 permissive 后使用 ADB sync”的推断已被实机否定。

离线查询 recovery BusyBox applet，确认包含 `rx`；主机同时安装了 `sx`。使用 XMODEM-1K 和 gzip 压缩后，候选能够通过 1500000-baud UART 传入 recovery 的 `/tmp`，解压为精确 33,554,432 bytes。该路径完全使用 RAM 中转，不需要挂载 userdata；传输后仍需与主机原文件比较 SHA-256。

## 首次直接 recovery 启动被 Rockchip SHA 拒绝

候选经 `/dev/block/platform/30020000.rksdmmc/by-name/recovery` 写入并读回确认后，直接执行 `adb reboot recovery`，没有经过 Android。U-Boot 读取到 recovery 启动原因，但在跳转内核前报告：

```text
boot or recovery image sha mismatch!
Unable to boot:recovery
try to start backup
bad image magic.
Unable to boot:backup
try to start rockusb
```

因此主线内核没有获得执行权；这不是内核早期日志丢失。设备最终进入 U-Boot `rockusb`，恢复入口仍可用。

原厂 boot 与 recovery 的 Android header 在标准 `id` 之外还包含 Rockchip 扩展：`0x26c` 为 SHA flag `256`，`0x270` 起为 32-byte SHA-256。对同代 Rockchip U-Boot `SecureNSModeBootImageShaCheck()` 的源码复核确认，厂商 SHA 输入除 kernel、ramdisk、second 和各自 size 外，还追加 `tags_addr`、`page_size`、两个 `unused`、16-byte name 和 512-byte cmdline。用该算法同时计算原厂 boot/recovery，SHA-1 和 SHA-256 均与头部逐字节一致；标准 AOSP `mkbootimg` 的算法则不一致。

新增 `scripts/add-rockchip-boot-hashes.py`，构建时自动注入并复核两种摘要。修正后的 `build/artifacts/r1-mainline-recovery.img` 仍为 33,554,432 bytes，SHA-256 为：

```text
5ed1b7772931fc34eb89ff46c85f305e4a38bba526cfdd55765f82b758ce97c2
```

完整构建连续执行两次，boot 和 recovery 产物均逐字节一致。新候选尚未写入设备；当前设备保持在 `rockusb`，可以直接测试修正候选或回滚已验证的原厂 recovery。本次修复只改变主机端构建产物和脚本，没有再次执行设备写入。

## 修正 SHA 后到达 zImage，停在解压或内核最早期

完整 eMMC User Area 备份已再次核对为 7,818,182,656 bytes，SHA-256 为 `eb4dbb57a7a78ea2604121fd699ba11bf951a970c1c0c5c165b78a2bd7c19cf7`。在已有完整回滚证据、明确的 recovery `0x01e000 + 0x010000` sectors 边界和用户针对该目标的写入授权下，修正候选写入 recovery 并立即读回验证；同时把 misc 内 `bootloader_message`（绝对 LBA `0x008020`，3 sectors）设为 `boot-recovery`，避免经过 Android 自动恢复服务。

复位后的串口日志包含：

```text
got recovery cmd from misc.
Secure Boot state: 0
kernel   @ 0x62000000 (0x00b73200)
ramdisk  @ 0x65bf0000 (0x00099018)
bootrk: do_bootm_linux...
Loading Device Tree to 65600000, end 656091bb ... OK
Starting kernel ...
```

此后无输出。U-Boot 不再报告 `boot or recovery image sha mismatch!`，所以 Rockchip SHA 修复和 BCB 路径均已通过实机验证。主线代码已获得执行入口，但尚无证据证明解压完成或进入 `start_kernel()`。

新的关键观察是厂商 `bootrk` 没有采用 Android header 中的 `0x60408000`、`0x64000000` 和 `0x66000000`，而是把 zImage、ramdisk 和 DTB 分别放到固定的 `0x62000000`、`0x65bf0000` 和 `0x65600000`。压缩输入范围为 `0x62000000–0x62b73200`；由 `vmlinux` 符号换算出的解压输出范围约为 `0x60208000–0x620dfc10`，尾部重叠约 `0xdfc10` bytes。解压器应有自搬移逻辑，因此暂记为重点嫌疑，不能直接认定为根因。

为区分“未进入解压器”“解压中停机”和“解压完成后早期内核停机”，新增板级 decompressor UART 诊断：

```text
CONFIG_DEBUG_LL=y
CONFIG_DEBUG_LL_UART_8250=y
CONFIG_DEBUG_UART_PHYS=0x11030000
CONFIG_DEBUG_UART_8250_SHIFT=2
CONFIG_DEBUG_UART_8250_WORD=y
CONFIG_DEBUG_UNCOMPRESS=y
```

Linux ARM Kconfig 默认禁止 multiplatform 固定 decompressor UART；项目新增 `patches/linux-6.18.42/0001-arm-allow-board-specific-decompressor-debug.patch`，并由 `scripts/prepare-kernel-source.sh` 自动应用。诊断内核和 boot image 构建成功，Rockchip SHA 复核通过，32 MiB recovery 连续两次打包逐字节一致，SHA-256 为 `a006e755cf444b4601d4cfb3282faa344267680061f20046effc525ca5dc868b`。本轮只生成了下一版主机端产物，尚未再次写设备。

## MaskROM 可枚举但首个 bulk OUT 返回 EPROTO

为恢复 misc BCB，设备重新进入 MaskROM。原装电源供电且 USB 改为主机 xHCI root hub 的 `3-4` 直连后，`lsusb -t` 持续显示 480 Mbps，configuration 完整包含 bulk IN `0x81` 和 bulk OUT `0x02`，runtime power 为 `active/on`。因此外部 ASM1074 Hub、USB autosuspend 和普通枚举断线均已排除。

设备节点 ACL 放开后，`rkdeveloptool rci`、`rid`、`rfi` 仍全部失败。启用 `LIBUSB_DEBUG=4` 后，三条命令都在同一位置失败：interface 0 claim 成功，但第一个 31-byte bulk OUT URB 立即返回 `status=-71`，`transferred=0`。Rockchip `upgrade_tool` 交叉验证报告 `RKU_Write failed,err=-1`；执行 USB device reset 后错误不变。`lsusb -v` 仍能读取缓存/标准描述符，但最后的 device status 请求一度返回 `Resource temporarily unavailable`。

这证明故障发生在主机向 BootROM 发送首个 bulk 命令时，命令没有到达 Rockchip 协议层，也没有访问 eMMC。当前禁止 BCB 或 recovery 写入。下一步依次排除 USB-TTL 与调试 USB 形成的接地回路、焊接引线/屏蔽/阻抗突变、VBUS 拓扑、RK3229 USB PHY 冷复位状态和当前 xHCI 控制器；若使用 USB 1.1 Hub 强制 12 Mbps 后查询成功，可直接确认是 High-Speed 信号完整性问题。

随后完全移除 CH340 USB-TTL，断开原装电源和调试 USB 后冷复位，再次进入 MaskROM。设备以新地址 `3-4` 直连 xHCI root hub，CH340 已不在 USB 拓扑中，错误仍然存在。连续执行 20 次只读 `rci`，结果为 0 次成功、20 次失败；每次都是 interface claim 成功后，第一个 31-byte bulk OUT 在零数据传输时返回 `EPROTO -71`。因此 USB-TTL 接地回路不是唯一原因，当前也不是偶发重试可绕过的问题。继续排查前必须确认 MaskROM 短接已释放，并改用不同 USB 主控制器或强制 Full Speed 的链路作判别。

主机随后完整重启，RK3229 再次接到同一个 xHCI root hub 的 `Bus 003 Port 4`，设备号从低位重新开始，排除了旧 xHCI transfer ring 或连续枚举残留状态。新设备节点 ACL 正常，interface claim 成功，但首个 31-byte bulk OUT 仍以 `transferred=0`、`status=-71` 失败；`rid` 和 `rfi` 同样失败。至此主机重启、外部 Hub、权限和 runtime power 均已排除，下一项判别必须改变物理链路或 USB 主控制器，而不是继续重复相同软件查询。

PCB 上的实体按键随后被确认可触发厂商 U-Boot Rockusb。按住按键上电时串口报告 `rockusb key pressed.`；若主机未及时建立有效会话，U-Boot 报告 `Usb Timeout, Return for boot recovery!` 并继续启动 recovery。新增只读 `scripts/catch-rockusb.sh`，以 root 权限轮询 USB 窗口并立即执行 `ld`、`rci`、`rid`、`rfi`，不包含存储写入。

把调试 USB 换到此前未连接过 R1 的另一个物理口后，脚本捕获到 `LocationID=504 Loader`，三项查询全部成功：chip info 为 `41 32 32 33`，Flash ID 为 eMMC 标识 `45 4d 4d 43 20`，SAMSUNG eMMC 容量为 15,269,888 sectors。主机拓扑进一步显示成功位置是 ASM1074 Hub 的下游 Port 4（`5-1.4`）；这推翻了板端 USB PHY 已损坏的推断，也说明“外部 Hub/直连”不是唯一判据。当前证据指向特定主机物理端口、接触或每端口 High-Speed 裕量差异。成功端口应保持不动，并在任何写入前继续完成目标区间双读比较。

## MaskROM 枚举正常但第一笔 bulk OUT 返回 EPROTO

设备因 misc 中 `boot-recovery` 仍然有效而持续进入当前主线 recovery 候选；正常 Android boot 分区尚无损坏证据。重新进入 MaskROM 后，主机能够连续看到 `2207:320b`，但 `rci`、`rid`、`rfi` 全部失败。

主机侧检查确认设备位于 `/sys/bus/usb/devices/5-1.2`，物理拓扑为 xHCI root hub 下的 ASM1074 High-Speed hub，再到 RK3229。设备协商为 480 Mbps，configuration、vendor interface、bulk IN `0x81` 和 bulk OUT `0x02` 描述符完整，runtime power 为 `active`、control 为 `on`。

打开 `LIBUSB_DEBUG=4` 后，三个只读 Rockchip 命令均成功打开 `/dev/bus/usb/005/048` 并 claim interface 0；失败发生在发送第一笔 31-byte bulk OUT：

```text
submit_bulk_transfer: length 31
urb type=3 status=-71 transferred=0
low-level bus error -71
```

主机执行 `usbreset 2207:320b` 成功，设备保持枚举，但复位后的首笔 bulk OUT 仍以相同方式失败。由此排除普通权限、命令解析、eMMC 内容和一次性 endpoint halt；当前故障位于 Rockchip 命令到达设备解析前的 USB bulk 传输层。与此前成功备份时的直连 `usb 3-4` 不同，本次经过 ASM1074 hub，下一步优先改用真正直连 root hub 的端口并重新验证 `rci/rid/rfi`，在此之前禁止任何写入。

_最后更新：2026-08-05_

## 恢复 misc BCB 并完成 Android 回滚闭环

更换到已验证可用的 USB 物理端口后，PCB 按键可稳定进入 U-Boot Loader，`rci`/`rid`/`rfi` 均成功。写入前对当前 misc `bootloader_message` 区间作两次只读采样：

```sh
sudo ./rkdeveloptool/rkdeveloptool rl 0x008020 0x3 /tmp/r1-bcb-current-1.img
sudo ./rkdeveloptool/rkdeveloptool rl 0x008020 0x3 /tmp/r1-bcb-current-2.img
sha256sum /tmp/r1-bcb-current-1.img /tmp/r1-bcb-current-2.img
cmp /tmp/r1-bcb-current-1.img /tmp/r1-bcb-current-2.img
```

两份均为 1536 bytes，SHA-256 均为 `648519893c9687c33869fed944175b5e8fc6b26fc86162b040e06cdbc8ab67bf`，且逐字节相同；其内容以 `boot-recovery` 开头。已有的原始备份 `backup/r1-misc-bcb-before-mainline.img` 为 1536-byte 全零镜像，SHA-256 为 `80422bc3d307b4a25bdafcc84ac7fb01cb55a09810e8b0f37bb12e0edb5c48ca`。

在 Loader 仍在线、目标绝对 LBA `0x008020`、长度 3 sectors 和回滚源均已核对后，只将原始 BCB 写回 misc，立即重读同一区间并与源文件比较，然后复位设备。Android 已正常启动。

已验证事实：正常 Android 无法启动的直接原因是 misc BCB 仍请求 `boot-recovery`，不是 boot 分区或正常 Android 内核被破坏。“PCB 按键 → eMMC 中存活的 U-Boot Loader → Rockchip 存储访问 → misc BCB 回滚 → Android 正常启动”已完成实机闭环。

安全边界修正：这不是“引导区全损坏后仅靠 SoC MaskROM 复原”的完整灾难恢复验证。当前整盘镜像仅覆盖 15,269,888 sectors 的 eMMC User Area；hardware boot0/boot1 是否使用及如何单独备份仍未确认。而且匹配版本 Loader 在 MaskROM 下执行 `db` 曾返回 `The device does not support this operation!`，尚未建立不依赖 eMMC U-Boot 的可写恢复会话。因此可以放开 recovery、misc，在逐分区备份和边界核对后也可扩展到 boot/system；但目前不擦除 parameter/idb、U-Boot、trust 或 eMMC hardware boot 区。

## U-Boot env 与 `bootrk` 装载地址

原厂 `backup/partitions/uboot.img` 中的默认环境只包含 `bootcmd=bootrk`、`bootdelay=0`、`baudrate=1500000`、`preboot=` 和 `verify=n` 等项，没有 `kernel_addr_r`、`ramdisk_addr_r` 或 `fdt_addr_r`。启动日志每次还报告 `Using default environment`，持久 env 的位置和可靠性尚未验证。

与该厂商 U-Boot 同代且已用于复现 Rockchip boot SHA 的 FriendlyARM `uboot-rockchip` commit `62645e6fefc9294f60befbb2e8032a35f67b1145` 中，`common/cmd_bootrk.c` 在从 RAM 或分区读镜像时都把 `hdr->kernel_addr` 改写为编译期 `CONFIG_KERNEL_RUNNING_ADDR` 或 `CONFIG_KERNEL_LOAD_ADDR`，并把 ramdisk 放到 `gd->arch.rk_boot_buf_addr`。它不查询 `kernel_addr_r`。这与 R1 实机日志中 header 地址被忽略、kernel 固定落在 `0x62000000` 完全一致。

结论：env 中的 `bootcmd` 可用 `bootrk recovery` 选择 recovery 分区，但不能用 `kernel_addr_r` 修改该厂商 `bootrk` 的 kernel 装载地址。修改编译期地址需要重建或二进制修补 U-Boot，风险明显高于在已验证可回滚的 recovery 分区测试 decompressor-debug 候选。

## decompressor 完成，故障缩小到解压后内核最早期

在 `LocationID=504` 的已验证 U-Boot Loader 链路上，`scripts/flash-diagnostic-recovery.sh --confirm-recovery-write` 依次核对镜像和 eMMC 身份、备份当前 recovery、定点写入 LBA `0x01e000`、完整读回比较、写入 LBA `0x008020` 的 3-sector `boot-recovery` BCB，再读回比较并直接复位。写入与读回均通过后，串口在 U-Boot `Starting kernel ...` 之后输出：

```text
C:0x620010A0-0x62B73200->0x6207E800-0x62BF0960
Uncompressing Linux... done, booting the kernel.
```

已验证结论：zImage 输入与解压输出的重叠被正确检测，解压器把自身搬到 `0x6207e800–0x62bf0960`，解压完成并执行了跳入解压后内核的路径。因此不再把 U-Boot 固定 `0x62000000` 装载、zImage 自搬移或解压作为当前根因。尚未看到解压后内核输出，故障范围为 ARM kernel `head.S`/页表/DTB 早期处理到串口控制台注册之间。

配置复核发现 `CONFIG_DEBUG_LL=y`、`CONFIG_DEBUG_UNCOMPRESS=y` 和强制 `earlycon` 已存在，但 `CONFIG_EARLY_PRINTK` 未启用。新候选在 `kernel/config/r1.fragment` 中加入 `CONFIG_EARLY_PRINTK=y`，命令行加入 `earlyprintk`。内核构建成功，Rockchip SHA-1/SHA-256 复核通过，32 MiB recovery 连续两次打包一致，新镜像 SHA-256 为 `3252e7263ae30e315c3ee52e33bfd83242a9540fc60d89f131f2076b66881501`。该镜像尚未上板。

## `earlyprintk` 进入 `start_kernel()`，显式 earlycon 注册后静默

启用 `CONFIG_EARLY_PRINTK=y` 并加入 `earlyprintk` 后，实机在 decompressor 完成后继续输出：

```text
[    0.000000] Booting Linux on physical CPU 0xf00
[    0.000000] Linux version 6.18.42-phicomm-r1-dirty ...
[    0.000000] CPU: ARMv7 Processor [410fc075] revision 5 (ARMv7), cr=10c5387d
[    0.000000] OF: fdt: Machine model: Phicomm R1
[    0.000000] printk: legacy bootconsole [earlycon0] enabled
```

此后再次静默。已验证结论：解压后内核已执行，MMU/基本 CPU 初始化、DTB 可读性、machine model 匹配和 `start_kernel()` 早期 printk 均已通过。该输出始终可以在 1500000 baud 正确解码；强制命令行中的显式 earlycon 和后续 `console=ttyS2` 也均指定 1500000，无直接证据表明此处切换了波特率。

源码复核确认，`mmio32` 会设置 `regshift=2`，所以“完全因为寄存器步长错误”不能成立。但显式 8250 earlycon 仍会重新写 LCR/IER/FCR/MCR，并与已工作的 DEBUG_LL `earlyprintk` 同时挂在同一 UART 上。为作最小判别，下一候选移除 `earlycon=uart8250,...`，只保留 `earlyprintk console=ttyS2,1500000n8 loglevel=8 ignore_loglevel rdinit=/init`。新 recovery 镜像 Rockchip SHA 复核通过，连续两次打包一致，SHA-256 为 `8b74dca655323921edc8a05c35251867025626c3611f070ed4e4def3199a5762`。该镜像尚未上板。

## 定位 DEBUG_LL UART 虚拟地址映射错误

移除显式 8250 earlycon 后，实机日志继续越过 EFI 和 CMA 初始化，最后输出：

```text
[    0.000000] printk: debug: ignoring loglevel setting.
[    0.000000] Memory policy: Data cache writealloc
[    0.000000] efi: UEFI not found.
[    0.000000] cma: Reserved 64 MiB at 0x7c000000
[    0.000000] BUG: not creating mapping for 0x11030000 at 0x11030000 in user region
```

已验证事实：内核此前把 `CONFIG_DEBUG_UART_PHYS` 和 `CONFIG_DEBUG_UART_VIRT` 都配置成了 `0x11030000`；在 ARM 3G/1G 地址划分中，后者属于用户虚拟地址区，`debug_ll_io_init()` 明确拒绝创建映射。日志始终以 1500000 baud 正确解码，故这不是启动过程中切换波特率。

推断：映射被拒后，下一次 DEBUG_LL `earlyprintk` 访问未映射的 UART 虚拟地址，是当前静默的直接原因。为验证该推断，新候选保留物理地址 `0x11030000`，把 `CONFIG_DEBUG_UART_VIRT` 改为内核虚拟区 `0xfed00000`。增量重编和两次 recovery 打包均成功且逐字节一致；镜像为 33,554,432 bytes，文件 SHA-256 为 `69be4baa4e0e0e7a447fb1a84de9958bce4e77bb9a83ee6739caa543842060d0`，内置 Rockchip SHA-1 为 `0004bb551f5dcfeea75fb4d1eaedc8bbf67d319f`、Rockchip SHA-256 为 `b46c8904e04c85d14a1499becede34cfc5921f770e6e8d2dd6b166939f60e879`。该修复尚未上板验证。

`0xfed00000` 候选上板后只输出 decompressor 的完成信息，不再出现解压后内核的第一条日志。源码复核给出确定解释：ARM `head.S` 在初始页表中按 1 MiB section 将 `CONFIG_DEBUG_UART_PHYS` 所在 section 映射到 `CONFIG_DEBUG_UART_VIRT` 所在 section。物理 UART `0x11030000` 的 section 内偏移是 `0x30000`，因此其初始虚拟地址实际为 `0xfed30000`；DEBUG_LL 却按配置访问 `0xfed00000`，二者不一致。

修正候选现使用 `CONFIG_DEBUG_UART_VIRT=0xfed30000`，既处于内核虚拟区，也保留与物理地址相同的低 20-bit section 内偏移。连续两次打包一致；33,554,432-byte recovery 文件 SHA-256 为 `046b7a060d646e7d1ef79abb98606714f63820930c927fa42d9d97331803de05`，Rockchip SHA-1 为 `8c56cc98e529333997ba6e43786b67aeed5fa4cb`，Rockchip SHA-256 为 `2b29e61f5c63b8e29c3565ed9a686d502d5bc432b78dca90111a1368135b6556`。

## 主线 Linux 首次进入救援 shell

`0xfed30000` 候选实机启动成功。日志从 decompressor 连续进入 `start_kernel()`，此前的 UART mapping BUG 消失；四个 CPU 全部启动，8250 驱动在 `0.340059` 秒识别 `11030000.serial` 为 `ttyS2` 并从 DEBUG_LL early console 平滑接管。内核随后执行 `/init` 并在 `/dev/console` 上给出交互式 `#` shell。

eMMC 控制器使用内部 DMA，以约 37.125 MHz 总线时钟完成 HS200 初始化，识别 Samsung `8GME4R`：User Area 为 7.28 GiB，boot0/boot1 各 4 MiB，RPMB 为 512 KiB。`/proc/partitions` 当前只有 `mmcblk0`、`mmcblk0boot0` 和 `mmcblk0boot1`，未出现原厂逻辑分区；因此已验证的是 eMMC 控制器和介质枚举，不是 Rockchip parameter 分区解析或文件系统挂载。initramfs 没有自动挂载或写入存储。

完整实机日志保存为 `build/artifacts/mainline-first-shell-20260805.log`，17,787 bytes，SHA-256 为 `4b12c13810a35bcef4b98141ee13cea1eccebf6cce961980ec834e48477cd095`。日志中的待办包括：RK805 regulator 不接受 408/600 MHz CPU OPP，以及 BusyBox 缺少 `echo` applet 导致 `/init` 的说明文字报 `echo: not found`；二者均未阻止进入 shell。

进入 shell 后，串口交互在约 11.36 秒完全停止，回车、命令和 Ctrl-C 均无响应。最后输出依次是 USB supplier 的 `deferred probe pending`，以及 Rockchip power-domain 对 video-codec/RGA consumer 的三条 `sync_state() pending`。源码确认启用模块时 `driver_deferred_probe_timeout` 默认为 10 秒；上述信息来自该延迟工作中的 `deferred_probe_timeout_work_func()` → `fw_devlink_probing_done()` 路径。时间和代码位置高度相关，但尚不能把相关性写成已确认根因。

为作单变量 A/B，新候选只在强制命令行加入 `deferred_probe_timeout=0`，其余 DTS、驱动、DEBUG_LL 和 initramfs 均不变。33,554,432-byte recovery 连续两次打包一致，文件 SHA-256 为 `0eb4a049093d214140e42ccac31885bf6138f88256670e86ce501bd67ad43a99`，Rockchip SHA-1 为 `0baac1bbbc072522badf95fbf8b2cc68d5ffcc0e`，Rockchip SHA-256 为 `3198c2a61d82a05c7a5561760424832dee401dbdf865a1e25197f0b6a022e828`。该候选尚未上板。

随后在原成功内核中启动每 2 秒读取 `/proc/uptime` 的后台心跳。心跳明确越过 11.36 秒 deferred-probe 信息，持续输出到 `28.41 111.60` 后才停止；串口输入也同时失效。因此 deferred-probe timeout 与冻结的直接因果被推翻，`deferred_probe_timeout=0` 候选停止使用。

约 30 秒的边界与 watchdog 更吻合。原厂 ramdisk 的 `init.rockchip.rc` 明确写有“watchdog timer to 30 seconds and pet it every 10 seconds”；SoC watchdog 位于 `0x110a0000`。主线 `rk322x.dtsi` 虽有兼容 `snps,dw-wdt` 的节点，板级 DTS 此前未启用它，因而无法接管 bootloader 可能遗留的运行中计数器。新候选恢复原命令行，只加入 `&wdt { status = "okay"; };`。内核已有 `CONFIG_DW_WATCHDOG=y`、`CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED=y` 和无限 userspace-open deadline；若探测到 watchdog 已运行，watchdog core 会在 userspace 打开设备前自动续喂。

该 33,554,432-byte recovery 连续两次打包一致，文件 SHA-256 为 `3ae236c9d3ec6b12e16884eb12a569de4189852c3d3872320588f360532bfd95`，Rockchip SHA-1 为 `7d6a0788ac98a1f1332b5a370d1b64c2750f74e4`，Rockchip SHA-256 为 `321dd4f1ed04422a1b4fbde71d1f768c0566e8407b073edf3c6a7201890575d0`。watchdog 根因和该候选均尚待上板验证。

watchdog 节点候选上板后，uptime 心跳仍从 `3.22` 秒持续到 `27.32` 秒后停止，约 30 秒冻结现象不变。主机侧现场检查显示 ASM1074 Hub 和 `5-1.2` CH341 仍在线，CH341 没有 disconnect；因此本次不是主机 Hub 或 USB-TTL 消失。仅凭启用节点未修复不能完全排除 watchdog，因为尚未确认 `dw_wdt` 是否成功 probe、硬件 control bit 是否为 enabled、current-count 是否递减以及 kernel worker 是否实际 reload。下一步在冻结前通过 BusyBox `devmem` 只读采样 `0x110a0000` 的 WDT control、timeout-range、current-count 和 interrupt-status 寄存器。

实机随后确认 `dw_wdt` 已绑定并创建 `/dev/watchdog`、`/dev/watchdog0`，但只读寄存器连续采样始终为 `CR=0x00000008`、`TORR=0x0000000f`、`CCVR=0x0000ffff`、`STAT=0`。DesignWare WDT 的 CR bit 0 为 enable；该位为 0，CCVR 也没有倒计时，故硬件 watchdog 没有运行，约 30 秒冻结不是 watchdog 到期。板级 DTS 已撤销 WDT enable。

启动日志中的 PSCI 版本为异常的 `v65535.65535`，但随后又拉起四个 CPU；旧 Trust OS 的 PSCI/SMP 兼容性因此成为下一候选。新镜像只在已成功进入 shell 的基线上加入 `maxcpus=1`，不启用 WDT，也不改 deferred-probe。33,554,432-byte recovery 连续两次打包一致，文件 SHA-256 为 `ece57f96a42f5796e9b6d7f9729a2726a3e0fac0a1fd54af68bfd979cbeebdaf`，Rockchip SHA-1 为 `48dfc050c06c4e4becd948b3a540c2f1ac0c3dde`，Rockchip SHA-256 为 `56a1230318b40c1adf58363fe6d245c2c1f5ce7bfb464bf1f2ef8658f32899f5`。该单核候选尚未上板。

单核候选实机心跳已稳定运行到至少 uptime `135.68` 秒，远超四核候选稳定复现的约 30 秒边界。已验证结论：冻结依赖 secondary CPU/SMP 路径，不是一个与 CPU 数量无关的固定 30 秒定时器。尚未区分 PSCI firmware、secondary CPU bring-up/idle、RCU 或特定核心数量。下一步只把命令行改为 `maxcpus=2` 作二分。

双核候选构建完成，除 `maxcpus=2` 外不引入其他诊断变量。33,554,432-byte recovery 连续两次打包一致，文件 SHA-256 为 `173fd98bd4457dd26bb253b38ff4f2342a394c8ba4006e42bd705a5e99fd59f7`，Rockchip SHA-1 为 `ab7a17ec48beeed315c3f61802e27792670495f6`，Rockchip SHA-256 为 `fc63b26ee9e95220fe15634ed57001aab8348686dc08a260296da3f7d14019f8`。该候选尚未上板。

双核候选实机同样在 30 秒前停止，而单核稳定超过 135 秒。由此排除“只有第三/第四核或四核规模才触发”，确认任意 secondary CPU 在线即可复现。源码确认通用 `nohlt` 参数会强制 idle polling；下一候选保持 `maxcpus=2` 并只加入 `nohlt`，判别次核 WFI/idle 路径。该模式空闲功耗较高，只用于短时诊断。

`maxcpus=2 nohlt` 候选构建完成。33,554,432-byte recovery 连续两次打包一致，文件 SHA-256 为 `5e6031888cfdfb82caa2bc941c98ebdce53d1005e7261ecc3bfeac1e9fe959cc`，Rockchip SHA-1 为 `2f17ed4ecdf3a54c185d271dca88a15c8f33b36c`，Rockchip SHA-256 为 `fd2487943dd46ddee958c3b6e2607b09daf64a12f09d855b47eefaaec884917b`。该候选尚未上板。

该候选实机仍在约 30 秒停止，故次核 WFI/idle 路径被排除。当前内核的 `CONFIG_RCU_CPU_STALL_TIMEOUT=21`，而约 30 秒边界可能对应启动后某个 grace period 加 21 秒 stall 检测窗口。`rcupdate.rcu_cpu_stall_timeout` 以 0644 module parameter 暴露，可在当前 recovery 重启后用 BusyBox `printf` 将 RAM 中的值临时改为 5 秒，使潜在 RCU stall 在系统仍可输出时提前报告；该测试不写存储。

实机确认运行时参数已变为 5；uptime 心跳从 `8.73` 持续到 `28.81` 秒，期间没有任何 RCU stall、CPU 或 stack 报告，随后仍按原边界停止。因此默认 21 秒 RCU stall 报告窗口不是停止原因。下一步复用当前镜像，连续采样 `/proc/interrupts` 与 `/proc/softirqs`，检查 CPU1 的 arch timer、IPI、TIMER/SCHED/RCU 计数是否持续推进。

per-CPU 采样显示 CPU0/CPU1 的 arch timer 约每两秒各增长 200，CPU1 的 reschedule/function-call/IRQ-work IPI 以及 TIMER、SCHED、RCU softirq 均持续增长到 uptime `28.82` 秒。次核在停止前没有失联，GIC、timer、IPI 和 RCU 都能工作。

原厂 boot/recovery DT 提供了关键差异：其 PSCI 节点是 `compatible = "arm,psci"` 的 0.1 binding，并显式给出 `cpu_suspend=0x84000001`、`cpu_off=0x84000002`、`cpu_on=0x84000003`。上游 `rk322x.dtsi` 则声明 `arm,psci-1.0`, `arm,psci-0.2`，导致主线调用旧 Trust OS 不支持的 PSCI_VERSION 并显示 `v65535.65535`。下一候选恢复普通 `maxcpus=2`、撤销 `nohlt`，只按原厂证据覆盖 PSCI 为 0.1 binding，避免 0.2+ capability/version 初始化。

PSCI 0.1 双核候选连续两次打包逐字节一致。33,554,432-byte recovery 的文件 SHA-256 为 `608faafbfc2bd5faad646d30ece72ff7d0987227935b97f65a41a6139f65839b`；`scripts/add-rockchip-boot-hashes.py --verify` 得到 Rockchip SHA-1 `68651ac1abb67eb8d7e4c9a9cfad4d646e2af47b`、Rockchip SHA-256 `c40a8fe440c80320b931fb0f8437f93f2d60ec82e7adb6c47d3501e021ea7619`。刷写脚本的允许哈希已同步。该候选尚未上板；下一步经已验证的 Loader/recovery/BCB 受控路径写入，检查启动日志是否消除异常 PSCI 版本，并运行至少 60 秒双核心跳。

PSCI 0.1 双核候选实机心跳仍只输出到 uptime `28.71` 秒，约 30 秒全局停止没有变化。编译后 DTB 已反编译确认确实包含 `compatible = "arm,psci"` 和三个原厂 function ID，因此不是候选未生效；PSCI binding 根因被排除。

首次完整日志中，RK805 `vdd_arm` 的板级约束为 1,000,000–1,450,000 uV，而上游 408/600 MHz OPP 分别要求 950,000/975,000 uV；OPP core 在 0.48 秒明确删除了这两个频点。于是可用最低 OPP 变成 816 MHz/1.0 V，而 U-Boot 明确把 ARM PLL 初始化为 600 MHz。单核稳定、双核失稳可能来自 cpufreq 注册后升频、错误 OPP 电压或双核负载下的电源完整性，但目前仍是推断。下一候选保持 PSCI 0.1 与 `maxcpus=2`，只加入文档化启动参数 `cpufreq.off=1`，让 CPU 保持 bootloader 频率作 A/B。

`maxcpus=2 cpufreq.off=1` 候选连续两次打包逐字节一致。33,554,432-byte recovery 的文件 SHA-256 为 `5748bf2ff17fe75fd48b30ad21b0bddad1ed7716a7f2c14da8fad1ad1e49a0ab`，Rockchip SHA-1 为 `767fba0292a157ac4b9d477c84c828cf0f834f57`，Rockchip SHA-256 为 `6c8d9d2236577b2b4d014274e28f8e796b8ba610a73d174373fdfeebe7f1f0c4`。刷写脚本允许哈希已同步；该候选尚未上板。

实机 `/proc/cmdline` 确认 `cpufreq.off=1` 已生效，cpufreq sysfs 目录也不存在；双核心跳仍只到 uptime `27.68` 秒，冻结边界不变。因此 cpufreq 注册、OPP 升频和动态电压转换被排除。

静态主线 DTB 没有 `reserved-memory` 节点且 memory node 声明完整 512 MiB，表面上可能覆盖 `0x68400000–0x684fffff` Trust OS 区域；但内核首次完整日志的 `Early memory node ranges` 已明确分成 `0x60000000–0x683fffff` 和 `0x68500000–0x7fffffff`。这证明原厂 U-Boot 在启动前修正了传入 FDT memory ranges，Trust OS 内存没有进入 Linux 页分配器，该假设也被排除。下一步不刷写，在当前双核镜像启动后立即通过 CPU hotplug offline CPU1；若超过 60 秒，再 online CPU1 并记录从 online 到冻结的间隔。

实机启动后立即向 `/sys/devices/system/cpu/cpu1/online` 写 0，随后单核心跳稳定超过 60 秒，远超双核稳定复现的约 29 秒边界。已验证结论是冻结需要 secondary CPU 持续在线，而不是次核启动阶段一次性破坏状态。当前 `/bin/sh` 报告 `can't access tty; job control turned off`，没有 controlling TTY，所以前台无限循环不能通过串口 `Ctrl-C`/`Ctrl-Z`终止；后续改为启动时安排后台定时任务在 70 秒后重新 online CPU1，前台心跳保持不变。

后台任务在 uptime `74.33` 秒打印 online 开始标记，`74.63` 秒确认 CPU mask 为 `0-1`；心跳随后持续到 `98.73` 秒，下一次预期输出前系统停止。由此确认 PSCI `CPU_ON` 成功返回，冻结发生在 CPU1 重新上线约 24–26 秒后，计时会随 secondary CPU online 重新开始。配置核对显示 `CONFIG_NO_HZ_IDLE=y`，SMP timer migration 默认启用，而 `CONFIG_SOFTLOCKUP_DETECTOR`、`CONFIG_HARDLOCKUP_DETECTOR`、`CONFIG_WQ_WATCHDOG` 均未启用。下一步仍复用当前镜像，启动后立即将 `/proc/sys/kernel/timer_migration` 写为 0，再运行双核心跳。

运行时测试确认 `/proc/sys/kernel/timer_migration` 为 0、online mask 为 `0-1`；心跳从 uptime `5.28` 持续到 `27.37` 后仍按原边界停止。因此 SMP timer migration 被排除。下一单变量候选在现有 PSCI 0.1、`maxcpus=2 cpufreq.off=1` 基线上只加入文档化参数 `nohz=off`，关闭 dynamic tick。内核已启用 `CONFIG_MAGIC_SYSRQ=y`；若仍停止，将在 shell 中预先安排后台定时任务，在预期边界前向 `/proc/sysrq-trigger` 写 `l`，采集全 CPU backtrace，不依赖 controlling TTY。

`nohz=off` 候选连续两次打包逐字节一致。33,554,432-byte recovery 的文件 SHA-256 为 `e377631aaff17d2f2c37dcddea464d830ba9223d9bb26b75059f4e671ba93be4`，Rockchip SHA-1 为 `65293581e60d3a6f34e04b5e420b019f666911b4`，Rockchip SHA-256 为 `d02d7b79f52c3cc184c77dbdae4f5ed84e3e3a5bd318e17fe6105e8dcc073259`。刷写脚本允许哈希已同步；该候选尚未上板。

实机命令行确认 `nohz=off` 生效，心跳仍只到 uptime `27.94` 秒。后台任务在 uptime `21.38` 和 `27.41` 两次触发 SysRq `l`：CPU0 正在执行 sysrq 写入，CPU1 两次都被报告为 `idling at default_idle_call`，且跨核 backtrace 请求成功完成。故 CPU1 在停止前不到两秒仍能响应，NO_HZ dynamic tick 被排除；第三次计划触发前系统已经停止。

进一步核对发现此前“`cpufreq.off=1` 保持 U-Boot 600 MHz”的推断不成立。上游 `rk322x.dtsi` 的 CRU 同时声明 `assigned-clocks = <PLL_GPLL>, <ARMCLK>, ...` 与第二项 `assigned-clock-rates = 816000000`；该 assigned rate 由时钟框架处理，不依赖 cpufreq。RK3228 clock driver 的 CPU rate table明确支持 600 MHz。下一候选保持 PSCI 0.1、双核、`cpufreq.off=1 nohz=off`，只在板级 DTS 把 ARMCLK assigned rate 改为 600 MHz，首次真正验证低频双核稳定性。

编译后 DTB 反编译确认 CRU assigned rates 的第二项为 `0x23c34600`，即 ARMCLK 600,000,000 Hz。33,554,432-byte recovery 连续两次打包逐字节一致，文件 SHA-256 为 `d0f18fd80c02027b3f199a817f69ca7ad9a19f7fa3e369f7067cec602b2f00f9`，Rockchip SHA-1 为 `e2a02390d780b2e6a320bbdcd1ae50b6ed543a76`，Rockchip SHA-256 为 `047aa2bc159afe5627fbb24fd61254cd91fae2102c469ed1f066efaf97cee316`。刷写脚本允许哈希已同步；该候选尚未上板。

600 MHz 双核候选实机仍只输出到 uptime `28.95` 秒，冻结边界不变；上游默认 816 MHz ARMCLK 不是根因。用户提出改用逆向得到的原厂 DT。原厂 3.10 DT 含有大量主线不兼容的私有 clock、reset、DVFS、serial 和 peripheral binding，不能整体直接替换。为区分上游 DTS 外设描述冲突与核心 SMP/firmware 问题，新增独立的 `rk3229-phicomm-r1-minimal.dts`：只保留原厂证据中的四个 Cortex-A7 CPU、PSCI 0.1 function ID、两路 arch timer interrupt、两区 Cortex-A15 GIC、UART2 和 RAM；不 include `rk322x.dtsi`，不描述 CRU、PMIC、eMMC、USB、IOMMU、power domain 或任何存储。

构建脚本新增可选 `BOARD_DTS`，默认仍使用完整 `rk3229-phicomm-r1.dts`；以 `BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal.dts` 构建得到的 DTB 为 1,599 bytes，反编译后逐项核对通过。对应 33,554,432-byte recovery 连续两次打包一致，文件 SHA-256 为 `e70a392e6e3cc30853d23aa5dbcc2c3ee52b86b873a5d7808c9fc39e0d6a87c8`，Rockchip SHA-1 为 `8dbca049e9fc6247899b3bbdbd7b021a6cc485a0`，Rockchip SHA-256 为 `00be6be1206db60cebed32109fa331443abff5a59c5560cccb5fc0f6a8fb9879`。刷写脚本允许哈希已同步；该候选尚未上板，且内核侧没有 eMMC 节点，不会访问存储。

最小原厂同构 DT 实机仍在约 30 秒内停止，故完整上游 `rk322x.dtsi` 的外设、时钟、regulator、IOMMU 和 power-domain 节点冲突被排除。问题范围收敛到现代 ARM SMP、GIC/arch timer 或旧 Trust OS 交互。源码显示 6.18 的 `CONFIG_ARM_ARCH_TIMER_EVTSTREAM=y` 会在每颗 CPU online 时修改 `CNTKCTL` 并启用 virtual event stream，而原厂 3.10 DT/日志没有对应行为。下一候选仍用最小 DT，只加入早期参数 `clocksource.arm_arch_timer.evtstrm=0`；若失败，则开始用较老主线 LTS 作版本二分。

关闭 event stream 的最小-DT recovery 连续两次打包一致。33,554,432-byte 文件 SHA-256 为 `fb410dca52963a69ebf5a2de262306a9da7deb9cf5372cbb50facb2caf4146dc`，Rockchip SHA-1 为 `de69b99516e8c4bb753ff52042b296487becfb31`，Rockchip SHA-256 为 `4c62b4ef3be0eeeb510016e1c9a7f6b4317fbc90b8bc221833916d66db320b29`。刷写脚本允许哈希已同步；该候选尚未上板。

为获得硬冻结前的执行证据，选择主线原生 persistent function tracing，而不是依赖冻结后仍能运行的 printk/SysRq。内核现启用 `CONFIG_FUNCTION_TRACER` 和 `CONFIG_PSTORE_FTRACE`；最小 DT 把 `0x7bf00000–0x7bffffff` 保留为 1 MiB ramoops，其中 ftrace 512 KiB、console 128 KiB、kmsg record 128 KiB，并以 `flags=1` 创建 per-CPU ftrace zones。地址低于 CMA 起点 `0x7c000000`，且不与 kernel、ramdisk、resource、Trust OS 相交。测试必须由硬件 watchdog 暖复位；断电会清除 DRAM，无法恢复 ftrace。记录默认关闭，进入 shell 后经 debugfs 显式开启，以免第二次启动在读取旧记录前无意覆盖。

持久化 trace 候选完成构建。最终 `kernel.config` 已核对 `CONFIG_FUNCTION_TRACER=y`、`CONFIG_PSTORE_FTRACE=y`、`CONFIG_PSTORE_RAM=y` 和 `CONFIG_PSTORE_CONSOLE=y`；DTB 反编译确认 `ramoops@7bf00000` 的 1 MiB 保留区、512 KiB ftrace、128 KiB console、128 KiB record 及 `flags=1` 均生效。recovery 连续两次打包逐字节一致，大小为 33,554,432 bytes，文件 SHA-256 为 `bd4fe88dd09a55e11779172c9f9e8ec3a3c51d3b1caac25e01f758847a34c849`，Rockchip SHA-1 为 `31e60e779ee3fbfc504f2966a9816f9ec90500e4`，Rockchip SHA-256 为 `17233851b4ddc66302614515bf90316046831d8042085e2040f88df3029914f4`。刷写脚本允许哈希已同步；本轮只生成并验证主机端产物，尚未执行设备写入。

可复现的主机构建与验证命令为：

```sh
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal.dts rtk scripts/build-kernel.sh
rtk scripts/build-boot-image.sh
cp build/artifacts/r1-mainline-recovery.img /tmp/r1-mainline-recovery-pstore-first.img
rtk scripts/build-boot-image.sh
cmp /tmp/r1-mainline-recovery-pstore-first.img build/artifacts/r1-mainline-recovery.img
sha256sum build/artifacts/r1-mainline-recovery.img
python3 scripts/add-rockchip-boot-hashes.py --verify build/artifacts/r1-mainline-recovery.img
```

随后主机 ASM1074 Hub `5-1` 出现 `hub_ext_port_status failed (err = -110)`；`lsusb -t` 超时且普通 `lsusb` 无法初始化 libusb。断开并恢复 Hub 后拓扑重新枚举，Rockchip 从原来的下游 Port 4 移到 `5-1.3`，因此旧脚本硬编码的 `LocationID=504` 不再成立。

`scripts/flash-diagnostic-recovery.sh` 已改为只接受当前唯一一个 VID `2207`、PID `320b`、模式 `Loader` 的设备；若同时出现多个匹配设备则立即拒绝。写入前还新增对 chip info `41 32 32 33`、eMMC Flash ID `45 4d 4d 43 20` 和 15,269,888-sector 容量的强制核对。需要固定物理位置时，可通过环境变量 `R1_LOCATION_ID` 显式指定，不再把一次枚举产生的 LocationID 当作永久硬件身份。

## Linux 5.10.262 最小-DT SMP 对照镜像

先前的 tracefs 命令依赖 `[ -e ... ]`，而极简 initramfs 没有 `[` applet。串口上的四条 `/bin/sh: [: not found` 说明没有任何目标 event 被启用；最后的 `cat trace_pipe` 是等待事件的阻塞读取，不是新的内核冻结证据。因此放弃解释该次 trace，改用较老 LTS 做版本对照。

从 kernel.org 稳定标签准备了独立 `v5.10.262` 源码树 `build/kernel-src-5.10`，保留原 `build/kernel-src` 的 6.18 源码。首次命令给 `KERNEL_BUILD` 传相对路径时发现构建脚本语义不一致：`make -C` 把相对 `O=` 放到内核源码树下，而 config merge 写到项目根下的另一个目录，导致禁用 GCC plugin 的配置没有参与实际编译。脚本现统一把自定义源码、输出、DTS 和额外 fragment 相对项目根解析。

5.10 `multi_v7_defconfig` 的另两个宿主依赖也被明确处理：ARM per-task stack-canary GCC plugin 需要当前主机缺少的 `gmp.h`；`CFG80211` 的隐藏 signed-regdb 选项又会选择系统证书 keyring，使 `extract-cert` 需要缺少的 OpenSSL 开发头文件。新增 `kernel/config/r1-5.10.fragment` 只关闭这两个与最小硬件诊断无关的路径，普通 `CONFIG_STACKPROTECTOR_STRONG=y` 仍保留。可复现构建命令为：

```sh
KERNEL_SRC=build/kernel-src-5.10 \
KERNEL_BUILD=build/kernel-5.10 \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal.dts \
rtk scripts/build-kernel.sh
```

已验证内核 release 为 `5.10.262-phicomm-r1`，`CONFIG_SMP=y`、`CONFIG_STACKPROTECTOR_STRONG=y`、`CONFIG_FUNCTION_TRACER=y`、`CONFIG_PSTORE_FTRACE=y` 均进入最终配置；强制命令行继续为双核、禁用 cpufreq/NO_HZ/arch timer event stream 的最小诊断基线。zImage 为 10,166,784 bytes，SHA-256 为 `fdb8d3b0e3d3674541216633fa6d9faa1557d62397bbbd22d205f53b29b937a6`；1,849-byte DTB SHA-256 为 `69266c1d52ded5c3abe1c7efce84169a641caa34404a9bf1b4f8b3a519939839`。

旧 Rockchip `resource_tool` 在受限系统调用沙箱中会以 `Bad system call` 退出，获准在沙箱外运行后只写 `build/artifacts` 并成功封装。两次 recovery 产物逐字节一致：大小 33,554,432 bytes，文件 SHA-256 `1bbd729a09cf1e81f8d2ebd5d8226e2ac33e78cb0252eb3df5c6820ed7e555f2`，Rockchip SHA-1 `bb200b2a19412e6a5ef4dd7471f7a22da1242b43`，Rockchip SHA-256 `bb40520dc8c10a54de9505c32f9e43aec49b4fd6994c580c2bad28ee2e58e844`。离线 hash verification 已通过，刷写 helper 的允许哈希已同步。

该 Linux 5.10.262 最小-DT 双核候选随后已上板。用户实测报告其仍出现与 6.18 相同的约 30 秒全局停止；本轮没有另存完整串口日志或精确的最后 uptime，因此只把“同类现象复现”记为用户报告，不补造更细的时间和栈证据。由于 5.10 与 6.18 共用同一份最小原厂同构 DT、PSCI 0.1、双核和诊断命令行，版本对照已经排除“6.18 特有回归”，不再继续在 5.10–6.18 之间二分。

下一步把公共变量收敛到旧 U-Boot/Trust OS 与现代内核的 SMP/PSCI 交互，以及 secondary CPU 持续在线后的板级供电裕量。Armbian 的通用 RK322x 目标虽然把 current 内核设为 6.18，但同时使用现代 U-Boot、独立 DDR TPL 和 OP-TEE；其板级 DTS 也明确依赖 U-Boot/OP-TEE 自动配置安全保留内存。因此它不能作为“只换 DTS 即可解决”的证据，却适合作为现代固件启动链的 A/B 参考。

随后纠正硬件前提：R1 PCB 没有 SD 卡槽，通用 RK322x 盒子的 SD 启动方案不适用于本板。现代固件的非破坏性入口只剩真 MaskROM RAM 下载。此前 `rkdeveloptool db` 的最终 “The device does not support this operation!” 很可能来自 PCB 按键进入的 U-Boot `Loader`，而工具源码规定 `db` 只接受 `Maskrom`；早期真 MaskROM 下的 vendor request `0x471` 失败又发生在尚未修复的 USB 物理链路上。因此匹配的 `rk322x_loader_v1.06.237.bin` 仍需在“真 MaskROM + 已验证 USB 端口”组合下重新测试。该命令只把 DDR/usbplug 下载到 RAM，不写 eMMC；只有它成功后才研究如何从 RAM 进入现代 U-Boot/OP-TEE。

## 真 MaskROM 纯 RAM Loader 下载成功

在已验证稳定的 USB 物理端口上进入真正的 `Maskrom`，执行：

```sh
sudo env LIBUSB_DEBUG=4 \
  ./rkdeveloptool/rkdeveloptool db \
  firmware/rk322x_loader_v1.06.237.bin \
  2>&1 | tee /tmp/r1-maskrom-db.log
```

主机日志最终明确报告 `Downloading bootloader succeeded.`。所有 control transfer 都以 `status=0` 完成，设备在约 0.706 秒发生预期的 USB remove/re-enumeration。串口同时输出 DDR `V1.06`、300 MHz DDR3、512 MiB、Boot1 `2.37`、eMMC `UserCapSize=7456MB`，最后进入 `UsbHook`：

```text
DDR Version V1.06 20171026
300MHz
DDR3
Bus Width=16 ... Size=512MB
Boot1 Release Time: 2017-06-12, version: 2.37
UserCapSize=7456MB
UsbHook 820715
powerOn 820757
```

这完成了不依赖 eMMC 中 U-Boot 的“BootROM MaskROM → vendor request 0x471 DDR 初始化 → 0x472 usbplug”纯 RAM 闭环，且没有执行任何存储写入。此前 `db` 的失败由模式或当时的 USB 链路解释，不能再写成 loader 不匹配。

RAM usbplug 随后的只读复核全部通过：chip info 为 `41 32 32 33 ff ...`，Flash ID 为 `45 4d 4d 43 20`，SAMSUNG eMMC 为 7456 MB、15,269,888 sectors。`rkdeveloptool ld` 仍打印 `Maskrom`，但这不是代码回到了 BootROM。工具源码 `RKScan.cpp` 只以 USB device descriptor 的 `bcdUSB & 1` 分类：最低位 0 就标成 Maskrom，1 才标成 Loader。当前重新枚举设备的原始描述符开头为 `12 01 00 02 ...`，即 `bcdUSB=0x0200`，所以 RAM usbplug 被该启发式误标；成功的 eMMC 协议查询和串口 `UsbHook` 才是实际执行状态的证据。

下一步研究把现代 U-Boot/OP-TEE 作为 RAM 第二阶段执行的方法；尚未因此获得现代 U-Boot shell，也尚未验证引导区损坏后的写回恢复。

## 现代 U-Boot MaskROM RAM 候选完成离线验证

核对 U-Boot 官方源码 commit `baa64b2f892890f00a377eac4a3e685472bb56b5` 后确认，`CONFIG_ROCKCHIP_MASKROM_IMAGE` 会由 binman 生成两个专用产物：`u-boot-rockchip-usb471.bin` 包含 TPL，`u-boot-rockchip-usb472.bin` 包含 SPL 以及内嵌的 U-Boot payload。SPL 在 BootROM 启动源为 USB 时优先选择 `BOOT_DEVICE_RAM`，因此 472 payload 不需要从 eMMC 读取。RK3229 默认地址也与实机链路吻合：SPL 位于 `0x60000000`，U-Boot proper 位于 `0x61000000`。

正式 RK322x FIT 配置要求 OP-TEE；本轮先制作只验证现代 U-Boot 提示符的诊断候选。临时把 RK322x 对 `SPL_OPTEE_IMAGE` 的强制 `select` 改为可关闭的 `imply`，关闭 OP-TEE 后让 usb472 使用 legacy `u-boot.img` payload。同时设置 `CONFIG_BOOTDELAY=-1`、`CONFIG_ENV_IS_NOWHERE=y`，关闭 `ENV_IS_IN_MMC` 和 `USE_PREBOOT`，防止诊断 U-Boot 读取原厂环境或自动启动。该候选不能用于验证 Linux SMP，也不能替代后续正式 OP-TEE 链。

为了不在首次测试中同时更换 DDR training，最终首测 Loader 使用已实机验证的原厂 `rk322x_ddr_300MHz_v1.06.bin` 作为 471，仅把 472 换成主线 SPL + U-Boot。开源 `rkdeveloptool pack` 生成的容器为：

```text
build/artifacts/r1-hybrid-mainline-uboot-ram-prompt.bin
size:   551189 bytes
sha256: 5478af8147c75a7bbe6603b1ef0ccec06e78a39a7f6d8b0dc2086e804f4bb195
```

离线验证结果：解包后的 7,196-byte 471 前缀与原厂 DDR blob 逐字节一致，尾部填充全零；533,648-byte 472 前缀与 binman 产物逐字节一致，尾部填充全零；容器保存的 Rockchip CRC `0x21218e2f` 与按项目 `CRC_32()` 算法独立重算结果一致。构建配置保存为 `build/artifacts/r1-mainline-uboot-ram-prompt.config`。

旧专有 `boot_merger` 在约 524 KiB 的 472 条目上发生自身 buffer overflow，产生的 24 KiB 截断文件已判无效；最终候选只由开源 merger 生成并解包复核。首次实机测试冷启动进入真正的 BootROM MaskROM 后执行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-hybrid-mainline-uboot-ram-prompt.bin
```

该命令只发送 471/472 到 RAM；不得把这个诊断容器用于 `ul`、`wl` 或其他存储写入命令。串口保持 `1500000 8N1`。实机结果为原厂 471 正常完成 DDR3 300 MHz、512 MiB training 并打印到 `OUT`，但此后没有主线 SPL/U-Boot 输出：

```text
DDR Version V1.06 20171026
In
300MHz
DDR3
Bus Width=16 Col=10 Bank=8 Row=15 CS=1 Die Bus-Width=16 Size=512MB
mach:9
OUT
```

因此已验证 471 下载和执行成功；故障边界位于 BootROM 向 472 载荷切换之后。当前证据尚不能区分“472 未获得控制权”和“主线 SPL 在首条串口输出前停止”。离线核对确认主线 SPL 已启用 `CONFIG_SPL_SERIAL`、`CONFIG_SPL_BANNER_PRINT` 和 DEBUG UART，UART2 base 为 `0x11030000`、波特率为 1500000，SPL 链接/入口地址为 `0x60000000`，所以不能简单解释为忘记打开串口。

## 制作最小 472 入口串口探针

为把 472 传输/入口与主线 SPL 初始化彻底分开，新增 `scripts/rk322x-uart472-probe.S`。该 68-byte ARM 探针链接到 `0x60000000`，不初始化 DDR、时钟或存储，只复用 471 已配置的 UART2，轮询 LSR THRE 后打印 `R1 472 ENTRY OK`，随后原地循环。开源 `rkdeveloptool pack` 将已验证原厂 DDR 作为 471、该探针作为 472，生成：

```text
build/artifacts/r1-uart472-entry-probe-loader.bin
size:   18709 bytes
sha256: c3776642f2557aba21ab6a4305431f56eeb2db4a4ced74457d74d51396c2e516
pack CRC: 0xc2ab9aed
```

离线 `unpack` 后，7,196-byte 471、68-byte 472 和 loader 前缀都与各自输入逐字节一致，三个尾部 padding 也全零。探针本身 SHA-256 为 `bd6fc974bb401877d5d2bf174e7bcd31e5e721604355a21da8a8c6d3e818ae15`。待执行命令如下，仍然只下载到 RAM：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-uart472-entry-probe-loader.bin
```

实机在 471 的 `OUT` 后立即打印：

```text
R1 472 ENTRY OK
```

这验证了 BootROM 的 472 下载、`0x60000000` 装载地址、ARM 状态入口和既有 UART2 配置全部成立。完整候选无输出的原因因此已收窄为主线 SPL 自身在最早期启动路径停止，不能再归因于 USB、供电、MaskROM 模式或 471/472 协议交接。

## 主线 SPL 最早期 UART 路标候选

为定位主线 SPL 的具体停止边界，在 U-Boot `arch/arm/cpu/armv7/start.S` 与 `arch/arm/lib/crt0.S` 加入不调用 C 的 UART2 路标；补丁保存为 `patches/u-boot-rk322x-spl-uart-breadcrumbs.patch`。字符含义如下：

```text
S  reset
R  save_boot_params() returned
M  entering _main
0  _main entry
1  initial stack installed
2  global data initialized
3  before debug_uart_init
4  after debug_uart_init
5  before board_init_f
6  after board_init_f
```

构建后的 SPL 反汇编确认 `S` 位于 `0x60000064` 的 `reset` 首部，所有路标都直接轮询 UART2 `0x11030000 + 0x14` 的 THRE bit 并写 THR。使用同一原厂 471 封装后得到：

```text
build/artifacts/r1-spl-uart-breadcrumbs-loader.bin
size:   551189 bytes
sha256: 23dcd93bd1983664ba28523019555a9cb219c376e99f9d8e8af95b65d34042b1
pack CRC: 0x178ea464
```

离线解包后的 7,196-byte 471、534,160-byte 472 和 loader 前缀与输入逐字节一致，三个 padding 区全零。待实机执行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-spl-uart-breadcrumbs-loader.bin
```

该候选仍只运行于 RAM。实机只打印 `S`，没有 `R`，因此已验证停止发生在 `reset` 跳入 Rockchip `save_boot_params()` 之后、返回 `save_boot_params_ret` 之前。

反汇编进一步显示强实现位于 Thumb 地址 `0x60000360`，入口立即 `push {r3, lr}`，随后调用 `setjmp()` 把 BootROM 上下文保存到 `brom_ctx`。与此同时，本次 SPL 配置明确为 `# CONFIG_SPL_ROCKCHIP_BACK_TO_BROM is not set`，正常语义应使用 `start.S` 中不碰栈、直接跳往 `save_boot_params_ret` 的 weak stub。强实现仍进入 SPL 的原因是 TPL 启用的全局 `ROCKCHIP_BROM_HELPER` 使 `bootrom.c` 同时被编入 SPL，进而错误覆盖 weak stub。

“vendor 471 返回后遗留栈不满足该 C 实现”是基于首条 `push` 和实机停止边界的推断；尚未直接读取入口 SP。为做最小 A/B，新增 `patches/u-boot-rockchip-phase-aware-save-boot-params.patch`：只在当前构建阶段实际启用 `ROCKCHIP_BACK_TO_BROM` 时编译强 `save_boot_params()`。TPL 的返回 BootROM 逻辑保持不变；未启用 back-to-BROM 的 SPL 改用无栈 weak stub。修正后 ELF 只剩：

```text
60000080 T save_boot_params_ret
600000f4 W save_boot_params
```

带原有 UART 路标的 A/B Loader 为：

```text
build/artifacts/r1-spl-phase-fix-loader.bin
size:   551189 bytes
sha256: c62e3ac92f129104e0f0a9d92f53dfecbdf17bebc9429b1efb426f86fa514b90
pack CRC: 0x67055c5e
```

离线解包后的 7,196-byte 471、533,904-byte 472 和 loader 前缀均与输入逐字节一致，padding 全零。该候选仍只在 RAM 中执行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-spl-phase-fix-loader.bin
```

实机输出为 `SRM0123`，验证 phase 泄漏确为第一个阻塞点且修正有效。`R/M/0/1/2` 还证明 SPL 已继续完成启动模式切换、进入 `_main`、建立 `0x61100000` 初始栈并初始化 global data。最后字符 `3` 位于调用 `debug_uart_init()` 之前，缺少 `4`，新边界因此位于该函数内部。

反汇编显示 `debug_uart_init()` 先调用 `board_debug_uart_init()` 写 UART2 IOMUX，随后 NS16550 `_debug_uart_init()` 在任何 divisor 写入之前无限等待 UART LSR `TEMT`（bit 6）：

```text
60006f50: ldr  r2, [r3, #20]
60006f52: lsls r2, r2, #25
60006f54: bpl  60006f50
```

现有全部 UART 路标使用 `THRE`（bit 5）并持续成功，故已验证 CPU 能访问 UART、THR 可写；当前证据支持“卡在 TEMT 等待”，同时保留“UART 被重配后输出不可见”这一替代解释。因为原厂 471 已把 UART2 配成实测的 1500000 8N1，正确策略是跳过 debug UART 的重复初始化。

U-Boot 已有 `CONFIG_DEBUG_UART_SKIP_INIT`，但 NS16550 `_debug_uart_init()` 原实现未检查该选项。新增 `patches/u-boot-ns16550-honor-debug-uart-skip-init.patch`，在该配置启用时直接返回；板级 IOMUX 设置仍执行。构建配置保存为 `build/artifacts/r1-spl-phase-fix-skip-debug-uart.config`。修正后的反汇编中 `debug_uart_init()` 仅尾调用 `board_debug_uart_init()`，不再包含 TEMT 循环。新候选为：

```text
build/artifacts/r1-spl-uart-skip-init-loader.bin
size:   551189 bytes
sha256: add95d06ab4872b0b2c61798db2a1bf08719ed4af3ee8c75b80828cb82e5fbb6
pack CRC: 0x4e32c5ca
```

离线解包后的 7,196-byte 471、533,864-byte 472 和 loader 前缀逐字节一致，padding 全零。纯 RAM A/B 命令：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-spl-uart-skip-init-loader.bin
```

预期至少越过 `3` 输出 `4`；后续 `5/6` 分别界定 `board_init_f()` 的入口和返回。

## 纠正 EVB 配置来源并建立 Phicomm R1 U-Boot 目标

用户在运行下一候选前追问板级配置来源。复核确认此前完整 472 虽使用实机验证的 vendor DDR 471、UART2 `0x11030000` 和 1500000 baud，但 U-Boot 构建骨架仍来自上游 `evb-rk3229_defconfig`，其有效 DT 明确包含：

```text
CONFIG_TARGET_EVB_RK3229=y
CONFIG_DEFAULT_DEVICE_TREE="rockchip/rk3229-evb"
model = "Rockchip RK3229 Evaluation board"
memory = <0x60000000 0x40000000>
```

该 EVB DT 还描述了 R1 未验证的 12 V 输入、PWM regulator、GMAC 和 USB VBUS GPIO。虽然 `SRM0123` 的早期定位发生在解析板级 DT 之前，仍然有效，但任何 EVB 派生候选都不得继续越过 `board_init_f()` 用作 R1 bring-up；上一节的 `r1-spl-uart-skip-init-loader.bin` 因此只保留为历史诊断产物，不再上板。

同时复核原厂 DTB：UART2 `0x11030000` 的 `pinctrl-0` 指向 `uart21-xfer`，具体为 GPIO1_B2 input 与 GPIO1_B1 output，和上游 RK322x 的 `uart21_xfer` 定义一致。因此“channel 1”本身有原厂证据，错误在于沿用了整个 EVB board target，而不是这一个 pinctrl 结论。

新增 `patches/u-boot-phicomm-r1-board.patch`，建立独立的 `TARGET_PHICOMM_R1`、`phicomm-r1_defconfig`、R1 DTS 与 U-Boot phase DTS。R1 DT 只启用当前有设备证据且诊断需要的 512 MiB RAM、UART2/`uart21_xfer` 和 8-bit eMMC；不引入 EVB regulator、GMAC 或 USB VBUS 配置。干净 defconfig 复核为：

```text
CONFIG_TARGET_PHICOMM_R1=y
# CONFIG_TARGET_EVB_RK3229 is not set
CONFIG_DEFAULT_DEVICE_TREE="rockchip/rk3229-phicomm-r1"
CONFIG_SYS_BOARD="phicomm_r1"
CONFIG_SYS_VENDOR="phicomm"
CONFIG_BOOTDELAY=-1
CONFIG_ENV_IS_NOWHERE=y
# CONFIG_ENV_IS_IN_MMC is not set
# CONFIG_MMC_WRITE is not set
# CONFIG_NET is not set
# CONFIG_I2C is not set
```

反编译最终 U-Boot DT 确认 `model = "Phicomm R1"`、compatible 为 `phicomm,r1`、memory 为 `0x60000000 + 0x20000000`；SPL DT 只保留 UART2、CRU、GRF 和读取 DRAM 信息所需的 DMC 节点。源码复核还确认 RK322x DMC 驱动只有在 `CONFIG_TPL_BUILD` 下调用 `sdram_init()` 重新训练 DDR；当前打包使用 vendor 471 而非主线 TPL，472 中的 SPL 只从 GRF `os_reg[2]` 读取已训练容量，不会套用 EVB timing 重新训练。最终 472 中没有 `Evaluation board` 或 `rk3229-evb` 字符串。

R1 专用诊断构建继续包含已验证的 phase-aware `save_boot_params` 修正、UART 路标以及完整保留 vendor 471 UART 配置的 skip-init 修正。写能力进一步收紧：无持久化 environment、无 MMC write、GPT、SPI flash、fastboot flash、USB mass-storage 或 FAT write。新 Loader 为：

```text
build/artifacts/r1-phicomm-r1-uboot-ram-debug-loader.bin
size:   432405 bytes
sha256: 2662e687fc6c6ff9abf69c52f1e3a1d070aed728fd50b52ae31b75e46c9c2b9a
pack CRC: 0x22f30d74
config: build/artifacts/r1-phicomm-r1-uboot-ram-debug.config
```

离线解包后的 7,196-byte vendor 471、415,080-byte R1 472 和 loader 前缀均与输入逐字节一致，padding 全零。只有该候选可用于下一次纯 RAM 测试：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-ram-debug-loader.bin
```

该修正是对板级配置来源的必要纠偏，不将通用 RK322x box 或 EVB 参数冒充为 R1 实机事实。

## R1 专用候选仍停在 `3`，完全绕过 debug UART 初始化调用

R1 专用 `r1-phicomm-r1-uboot-ram-debug-loader.bin` 实机仍只输出 `SRM0123`。因此“EVB pinmux 写入导致串口消失”这一解释被实机否定；板级来源纠正仍然必要，但没有改变这个更早的停止点。

再次核对最终封装的 472：解包前缀与 `/tmp/r1-u-boot-build/u-boot-rockchip-usb472.bin` 逐字节一致。其 SPL 反汇编显示，`debug_uart_init()` 已被优化为从 ARM `_main` 经 `blx` 进入 Thumb 函数，再尾跳到只含 `bx lr` 的 `board_debug_uart_init()`；函数不访问 UART、GRF 或其他硬件。按代码语义它应立即返回，但实机没有到达紧随调用后的路标 `4`。当前边界因此进一步收窄到该 ARM/Thumb 跨状态调用/返回本身，不能再归因于 NS16550 TEMT 等待或板级 DT。

新增 `patches/u-boot-crt0-honor-debug-uart-skip-init.patch`：当 `CONFIG_DEBUG_UART_SKIP_INIT=y` 时，`crt0.S` 连 `debug_uart_init()` 调用本身也不生成，直接从路标 `3` 执行路标 `4`。新反汇编确认两次 UART 路标写之间不存在 `bl`/`blx`。纯 RAM A/B 候选为：

```text
build/artifacts/r1-phicomm-r1-uboot-bypass-debug-init-loader.bin
size:   432405 bytes
sha256: 99cfcd0839267e550538398126a3d61a21590e9d9e2e11fb7e9e9f73cdee3174
pack CRC: 0x3daa3341
472 sha256: 0cb8c1609c5a545bbe37def9354f38b5814052037aa48b7e1c3322d4398f4608
```

离线解包确认 7,196-byte vendor 471、415,080-byte 472 和 7,196-byte loader 前缀分别与输入逐字节一致，三段 padding 全零。该镜像不增加存储写能力；下一次只执行 RAM 下载：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-bypass-debug-init-loader.bin
```

预期首先看到 `SRM012345`。若仍只有 `3`，则“执行流卡在 debug UART C 调用”也被否定，应转而检查路标宏在该地址附近的 UART 状态或指令/缓存一致性；若出现 `4/5`，则该跨状态空调用就是已隔离变量。

实机得到：

```text
OUT
SRM012345
```

这验证了汇编层绕过有效：`debug_uart_init()` 的 ARM/Thumb 调用路径就是 `3` 后的阻塞变量。执行流现已进入 `board_init_f()` 调用点，边界从 `3` 推进到 `5`；UART、DDR、472 交接和 EVB pinmux 均不是该阻塞的原因。

为继续定位 `board_init_f()` 内部，新增 `patches/u-boot-phicomm-r1-spl-board-init-breadcrumbs.patch`。路标直接内联轮询原厂 UART 的 THRE，不调用 debug UART/console 框架：`A` 为函数入口，`B` 为 `board_early_init_f()` 后，`C` 为 `spl_early_init()` 后，`D/E/F` 分别为 CPU、secure timer、arch timer 后，`G/H/I` 分别为 `dram_init()`、有效内存大小和可用 RAM top 后，`J/K` 为 `preloader_console_init()` 前后；`X/Y` 表示相应初始化返回错误。

```text
build/artifacts/r1-phicomm-r1-uboot-board-init-trace-loader.bin
size:   432405 bytes
sha256: 8b16e031802dc8de9c2e0924f563ebfcab03dae9019afbf626e37574c5fa8192
pack CRC: 0xb8640d50
472 size: 415208 bytes
472 sha256: 594c6fba1e018b9bec418c80da052daa3e7f907abc194f43c89a21587a0b5e8f
```

解包后 471、472、loader 前缀分别与输入逐字节一致，所有 padding 全零。下一次仍只执行 RAM 下载：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-board-init-trace-loader.bin
```

实机输出停在 `B`。这证明 `board_init_f()` 已进入，且 `board_early_init_f()` 已正常返回；缺少 `C` 将停止边界明确限定在 `spl_early_init()` 内。

当前 SPL 配置关闭 bootstage/log 后，`spl_early_init()` 的有效路径为：设置 2 KiB early malloc 状态、`fdtdec_setup()` 检查内嵌 SPL DTB、`dm_init_and_scan()` 扫描 driver model、`dm_autoprobe()` 自动 probe。新增 `patches/u-boot-phicomm-r1-spl-common-init-breadcrumbs.patch`，用同一直接 UART 写法加入小写路标：

```text
a  spl_common_init() 入口
b  early malloc 状态设置完成
c  fdtdec_setup() 完成
d  dm_init_and_scan() 返回
e  dm_autoprobe() 返回
f  spl_common_init() 成功返回
p  fdtdec_setup() 返回错误
q  dm_init_and_scan() 返回错误
r  dm_autoprobe() 返回错误
```

新候选为：

```text
build/artifacts/r1-phicomm-r1-uboot-spl-common-trace-loader.bin
size:   432405 bytes
sha256: e67700fe55594d5b91fc604b0756ad6f8c873f8b308afc1c5c78031bde7306af
pack CRC: 0x59c2c470
472 size: 415336 bytes
472 sha256: fe4ccaa808f5a25fd69f8a8517458d1354f404cbe41402da6e47a4767b012d2b
```

解包后的 471、472、loader 前缀分别与输入逐字节一致，三段 padding 全零。测试仍只下载到 RAM：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-spl-common-trace-loader.bin
```

实机输出为 `SRM012345ABab`，所以停止发生在 `fdtdec_setup()` 返回之前；early malloc 已明确正常，尚未进入 driver model 扫描。

离线检查当前 SPL 的 `CONFIG_OF_SEPARATE` 布局：`__bss_end` 与 DTB 起始地址一致，当前构建为 `0x600095e8`；文件中该处 DTB magic 为合法的 `d0 0d fe ed`，DTB 可由 `fdtdump` 完整解析。需要确认的不是磁盘文件，而是 BootROM 运行时该地址是否仍含相同内容。新增累计补丁 `patches/u-boot-phicomm-r1-spl-dtb-memory-probe.patch`：在 `fdtdec_setup()` 前输出 `s`，直接从 `__bss_end` 读取四字节，匹配 CPU 小端值 `0xedfe0dd0` 输出 `m`，否则输出 `n`。其中停在 `s` 表示读取该地址即异常；`sm` 后再停表示运行时 DTB 存在而问题在 `fdtdec_setup()` 调用路径；`sn` 表示运行时 DTB 内容没有正确到达。

```text
build/artifacts/r1-phicomm-r1-uboot-spl-dtb-memory-probe-loader.bin
size:   432405 bytes
sha256: eeb7579f0a65e5db3533d23742092f9d6dfaa022a1ba0d34a6f8b4f1c2943677
pack CRC: 0x65fe2c9d
472 size: 415400 bytes
472 sha256: 5c64cf4e8672ea5a83e1be2aa391c1a69d236866d9dd0c4bc0ae948a5a577265
```

471、472 和 loader 解包前缀均与输入逐字节一致，padding 全零。运行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-spl-dtb-memory-probe-loader.bin
```

实机得到 `...absn`，确认运行时 DTB magic 不匹配。下一版在 `n` 后直接以 8 个十六进制字符打印该地址实际读出的 32 位值：

```text
build/artifacts/r1-phicomm-r1-uboot-spl-dtb-hex-loader.bin
size:   432405 bytes
sha256: a269ad47e2073147ce016c951ede49610e3c2af38af4c56e9984e49a3b9a57e1
pack CRC: 0x52542047
472 size: 415464 bytes
472 sha256: a48134a756adc411c5f98783fe7762ceff25dd1ee989bb9224d345466006dfdd
```

运行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
build/artifacts/r1-phicomm-r1-uboot-spl-dtb-hex-loader.bin
```

实机进一步得到 `SRM012345ABabsmMissing DTB`。其中 `sm` 证明正确封装后运行时
`__bss_end` 已包含合法 DTB magic；随后失败来自实验性 `+0x3c4` 指针，而不是
原生 `__bss_end` 定位。因此否定硬编码偏移方案，恢复 U-Boot 原生定位，并统一使用
Rockchip `boot_merger` 封装及解包逐字节复核。新的纯 RAM 候选为
`build/artifacts/r1-phicomm-r1-uboot-spl-dtb-generic-loader.bin`。

恢复原生定位后的候选 SHA-256 为
`3b8959a491c1fead6409e4eaf4cb1598dac5ab9b9e3e171f824ccb7f3746c70d`，大小
432,405 字节。实机纯 RAM 下载成功并完整输出：

```text
SRM012345ABabsmcdefCDEFGHIJ
U-Boot SPL 2026.10-rc1-gbaa64b2f8928-dirty
Trying to boot from RAM
SRM012345
U-Boot 2026.10-rc1-gbaa64b2f8928-dirty
Model: Phicomm R1
DRAM:  512 MiB
Core:  103 devices, 14 uclasses, devicetree: separate
MMC:   mmc@30020000: 0
=>
```

由此验证原厂 DDR 471、主线 R1 SPL、运行时独立 DTB、driver model、512 MiB DRAM、
eMMC 枚举、RAM payload 交接和 U-Boot proper 交互控制台全部贯通。此前的
`n18a7fdf5` 不能作为 DTB 装载失败证据：根因是手工重封装没有忠实复现 Rockchip
Loader 编码；官方 `boot_merger` 封装后原生 `__bss_end` 定位正确。

现代 U-Boot 的 `mmc read` 重复读取同一扇区逐字节一致，确认 eMMC 只读链路稳定。
进一步把实机在主线 U-Boot 的 LBA `0x1e000` 读出的首字节
`c8 81 96 7a b4 ea e8 52` 在完整 User Area 备份中定位到 LBA `0x1c000`，两种视图
固定相差 `0x2000` sectors，恰与原厂 Loader 的 `FwPartOffset=2000` 一致。因此主线
U-Boot 读取 recovery 使用原 Rockchip 地址 `0x1e000 + 0x2000 = 0x20000`。

从 LBA `0x20000` 读取 32 MiB recovery 到 `0x68000000` 后，Android v0 header 中的
zImage 位于 `0x68004000`、ramdisk 位于 `0x689b8000:0x99018`，Rockchip
`resource.img` 内 DTB 位于 `0x68a54400`。首次显式 `bootz` 已进入 Linux，但由于纯
RAM 链没有 Trust OS，DT 中 PSCI v0.1 的首个 `CPU_ON` SMC 进入无处理程序的 monitor
模式并 panic。Linux 5.10 ARM32 的早期 CPU map 不忽略 `status = "disabled"`，因此
仅设置 status 仍会复现；在运行时 DT 中彻底删除 `cpu@f01`、`cpu@f02`、`cpu@f03`
后，日志确认 `pcpu-alloc: [0] 0`、`Total of 1 processors activated`，随后成功执行
`/init` 并进入 BusyBox shell。由此验证纯 RAM 主线 U-Boot 到单核主线 Linux 的
kernel、ramdisk、DTB 和控制台交接全部贯通；恢复 SMP 需要先提供 PSCI secure monitor。

## 核对 Armbian RK322x box 的 Trust/OP-TEE 实现

为回答通用 `rk322x-box` 为何能运行现代 SMP 内核，核对了 Armbian 当前板级配置、
Paolo Sabatino 的维护分支及 U-Boot 的 Rockchip FIT 模板。公开资料的入口为：

- [Armbian `rk322x-box.tvb`](https://github.com/armbian/build/blob/main/config/boards/rk322x-box.tvb)
  只选择 `rk322x-box_defconfig` 和 `rk322x-box.dtb`；
- 真正的固件链位于 `config/sources/families/rockchip.conf`；
- [维护者分支](https://github.com/paolosabatino/armbian-build/tree/rk322x-opensource-tee)
  的提交 `d80ff015a83b0cf9a2500a2312a31d42931a6da4`（`provide opensource TEE for rk322x`）
  把 `TEE=.../rk322x_tee.bin` 切换成 `TEE=.../rk322x_tee_os.bin`。

复核命令：

```sh
git ls-remote --heads https://github.com/paolosabatino/armbian-build.git
git clone --depth 1 --filter=blob:none --sparse \
  --branch rk322x-opensource-tee \
  https://github.com/paolosabatino/armbian-build.git \
  /tmp/r1-armbian-optee-audit
git -C /tmp/r1-armbian-optee-audit show --stat d80ff01
sha256sum /tmp/r1-armbian-optee-audit/packages/blobs/rockchip/rk322x_tee_os.bin
strings /tmp/r1-armbian-optee-audit/packages/blobs/rockchip/rk322x_tee_os.bin | \
  grep -E 'OP-TEE version|psci_rk322x|plat-rockchip'
```

`rk322x_tee_os.bin` 为 423,248 bytes，SHA-256 为
`ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`。其字符串明确包含
`core/arch/arm/plat-rockchip/psci_rk322x.c`、`psci_cpu_on`、`psci_cpu_off`，版本字符串为
`3.7.0-1-ga34a269b7-dev ... #2 Thu Apr 4 16:39:43 UTC 2024 arm`。这验证它是带 RK322x
PSCI 后端的 32-bit 开源 OP-TEE，而不是只改名的原厂 Trust OS。

U-Boot `arch/arm/dts/rockchip-u-boot.dtsi` 在 ARM32 + `CONFIG_SPL_OPTEE_IMAGE` 下生成
`u-boot.itb`，把 OP-TEE 作为 FIT firmware 装入
`CFG_SYS_SDRAM_BASE + 0x08400000 = 0x68400000`，然后由 OP-TEE 返回位于
`CONFIG_TEXT_BASE` 的 U-Boot proper。该地址与 R1 原厂 Trust OS 的实测装载/保留地址完全
一致。Armbian 还带有为 Rockchip 专有 Trust OS 设置 `r1 = CONFIG_TEXT_BASE` 的 U-Boot
补丁；不能因为这个兼容补丁存在就把开源与专有两个 TEE 混为一谈。

Armbian 维护者 jock 在 [2025-10-26 的论坛回复](https://forum.armbian.com/topic/34923-csc-armbian-for-rk322x-tv-box-boards/?comment=227602&do=findComment)
中同时记录：专有 RK322x Trust OS 的 watchdog 会在不同板上造成约 30 秒、60 秒或
30 分钟冻结，而开源 OP-TEE 路线不受该问题影响，但会失去 DDR 动态缩放和
“virtual power off”等原厂特性。这与 R1 上“原厂 Trust + SMP 约 30 秒冻结、无 Trust
单核稳定”的现象高度吻合，但在 R1 实机运行开源 OP-TEE 前仍属于跨设备证据支持的推断，
不能提前记为已修复。

因此下一候选不自行重写 PSCI，也不照搬 box 的 DDR/DT：继续使用已经实机验证的 R1
vendor DDR 471 与独立 `TARGET_PHICOMM_R1`，只恢复 `CONFIG_SPL_OPTEE_IMAGE`，把上述
`rk322x_tee_os.bin` 放入 RAM-only 472 FIT。先离线核对 FIT 的 load/entry、U-Boot proper、
DTB 和地址不重叠，再通过 MaskROM `db` 测试；不写 eMMC。

## 构建 R1 开源 OP-TEE RAM-only Loader

在此前已经进入 U-Boot 提示符的 R1 专用源码上，仅把 `CONFIG_SPL_OPTEE_IMAGE` 打开，
保留 `CONFIG_TARGET_PHICOMM_R1`、原厂 DDR 471、`CONFIG_ENV_IS_NOWHERE`/禁 MMC 写入和
所有 R1 UART/DT 修正。宿主机只补充用户级 `pyelftools==0.31`；pylibfdt 复用已有构建
产物，避免修改系统工具链。开源 TEE 来源仍为 Armbian 分支中的
`rk322x_tee_os.bin`，SHA-256 为
`ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`。

为匹配 Armbian 的 Rockchip 兼容路径，`common/spl/spl_optee.S` 同时把
`CONFIG_TEXT_BASE` 传入 `r1`；反汇编确认 handoff 为：

```text
mov lr, #0x61000000
mov r1, #0x61000000
mov pc, r3
```

U-Boot/binman 产物 `u-boot.itb` 的离线 FIT 检查结果：

```text
U-Boot load/entry: 0x61000000
OP-TEE load/entry: 0x68400000
OP-TEE data size: 423248 bytes
FDT compatible: phicomm,r1, rockchip,rk3229
configuration: firmware=op-tee, loadables=u-boot
```

最终使用已实机验证的 7,196-byte vendor DDR 471 和新的 843,136-byte 472，经过
`rkdeveloptool pack` 生成：

```text
build/artifacts/r1-phicomm-r1-uboot-optee-os-loader.bin
size:   860433 bytes
sha256: 7f617c52269e9fe4f29f6bcfa7716460e970363b646d70ac4021caf275174f0b
pack CRC: 0x84db0dbc
```

`unpack` 后 471/472/loader 三个条目的有效字节分别与输入逐字节一致，所有条目只在
RAM 容器中；尚未上板，不能把该构建记为“已恢复 SMP”。下一步只用 MaskROM `db` 下载，
观察 471 的 `OUT` 后是否出现 U-Boot/OP-TEE 输出或直接进入 `=>`，随后再从 U-Boot
RAM-only 启动双核 Linux。任何异常都通过复位回到原厂 eMMC，不执行 `ul/wl`。

## 首次 OP-TEE FIT 实机失败与 FDT 诊断候选

首次纯 RAM 下载通过了全部 SPL 路标并解析到 FIT，但在跳入 OP-TEE 之前输出：

```text
K6Trying to boot from RAM
fdt_record_loadable: FDT_ERR_BADMAGIC
fdt_find_or_add_subnode: chosen: <unknown error>
spl_perform_arch_fixups: could not find/create '/chosen'
```

因此已验证故障不在 471、SPL 早期初始化或 eMMC，也尚未进入 OP-TEE；停止点位于 SPL
加载 U-Boot proper 的 FIT loadable 后、更新传递给下一阶段的 FDT 时。离线检查实际 FIT
确认 `/images/u-boot` 含 `os = "u-boot"`，FDT 数据大小为 25,752 bytes，故不能把失败简单
归因于缺少 OS 属性。当前开放问题是 `spl_fit_append_fdt()` 返回失败，还是其目标内存在
加载后被覆盖。

新增 `patches/u-boot-spl-fit-fdt-debug.patch`：每次 loadable 先清零 `image_info`，检查并传播
`spl_fit_append_fdt()` 错误，并在记录 loadable 前打印 OS、返回值、FDT 地址及首字 magic。
对应纯 RAM 候选为：

```text
build/artifacts/r1-phicomm-r1-uboot-optee-fdt-debug-loader.bin
final size: 860437 bytes
sha256: c32bba2ae19ce0c359972ec7caa1a3631e0c99355279fc87e36ceaa07998d3d3
472 size: 843200 bytes
472 sha256: 611244dc17a3d7a01937aefe537ec92e9ab9a1ef2a530d732bbcc9c52935ebc0
pack CRC: 0x86068011
```

解包后 471、472 与 loader 的有效前缀均与输入逐字节一致，尾部 padding 全零。下一步只
执行 MaskROM `db`，收集新增的 `FITF` 一行；该候选不写 eMMC，也不能在获得实机输出前
记为修复。

实机运行上一版只到 `K6Trying to boot from RAM`，连 `FITF` 都没有出现，故继续把路标
前移到 Rockchip `ramboot_load_image()`：新候选额外输出 `n/o/p/P/Q`，分别覆盖 RAM
入口、binman payload 定位、FIT 搬移前后以及进入 `spl_load_simple_fit()`。最终产物已
重新打包并离线核对：

```text
build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-loader.bin
final size: 860437 bytes
sha256: 5c71d5ca7bf0d9c72b43b0654760a79bff1385581c6f97e381b540a290189a10
472 size: 843392 bytes
pack CRC: 0xfd9b59e4
```

下一次只需运行该候选；根据最后一个字符即可判断是否卡在 FIT 搬移之前、之后，还是
`spl_load_simple_fit()` 内部。

随后实机已得到 `nopPQqrstuvwxy`，最后停在 `y`：已进入 FDT append，但尚未完成 FDT
load 或 shrink。当前同名候选已再加入 `1/2/3/4` 路标并重新打包，最新 SHA-256 为
`9a09c66cac68dc5e3367eb30a05e84aa3b2e93a6a4e8809bb124d6c53d4e17d9`，472 大小为
843456 bytes，pack CRC 为 `0x8b78c1fe`。

实机随后得到 `...y123`，确认卡在 `fdt_shrink_to_minimum()`。该操作只是为 `/fit-images`
元数据扩容，不是 OP-TEE handoff 的必要条件；因此下一候选启用 `CONFIG_SPL_FIT_IMAGE_TINY=y`
跳过 shrink 和 loadable 记录。最新产物 SHA-256 为
`fa78050823e35ce0ba9635da5ceb0efdb901275ee96de151245ec353c83dc05c`，472 大小为
839552 bytes，pack CRC 为 `0x78a47b1a`。

该候选实机继续到 `FITF ... magic=61055d50`；该值正好是 FDT 目标地址，离线 FIT 检查显示
当前外置数据布局下 SPL 读取了错误的 data-offset。最终改为 `CONFIG_SPL_LOAD_FIT_FULL=y`
生成内嵌 FIT 数据。最新纯 RAM loader SHA-256 为
`8008082c9c173368a009dfe40d9c4f19407b44758c2f8ad57d9b737e4c6253e2`，472 大小为
862912 bytes，pack CRC 为 `0x0235e7a6`。

## 内嵌 FIT 候选推进至 OP-TEE 跳转边界及重启后交接

内嵌 FIT 候选继续增加固定字符路标后，最后一次实机使用的文件为
`build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-loader.bin`，大小 856,341 bytes，
mtime 为 2026-08-06 14:58:53 +0800，当前文件 SHA-256 为
`169ead28ed3f8a7e658557a834ea102d62d6857d390808d8c8c417a398bcba12`。串口构建时间与
文件 mtime 对应，实机输出：

```text
OUT
SRM012345ABabsmcdefCDEFGHIJ
U-Boot SPL 2026.10-rc1-gbaa64b2f8928-dirty (Aug 06 2026 - 14:58:43 +0800)
K6Trying to boot from RAM
nopPQrstuvwxyRzFITF os=17 ret=0 addr=? magic=68500000
spl_perform_arch_fixups: could not map boot_device to ofpath: -19
spl_perform_arch_fixups: could not map BootROM boot device to ofpath
```

`FITF os=17 ret=0` 证明本轮 FDT append 已返回成功；此前的 FDT shrink 和外置 FIT
data-offset 已不再是当前停止点。两条 boot-device/ofpath 信息来自架构 fixup，现有证据不足以
把它们判为致命错误。`addr=? magic=68500000` 的格式化结果可疑，不能直接把 `68500000`
解释为 magic。下一轮应使用固定字符，依次覆盖 `spl_perform_arch_fixups()` 返回、
`spl_board_prepare_for_optee()` 前后、`cleanup_before_linux()` 前后和 `spl_optee_entry()` 的
`mov pc, r3` 前，以确认控制权是否真正进入 OP-TEE。

主机随后重启。原 U-Boot 临时工作树和 OP-TEE blob 位于 `/tmp`，已经丢失；重新拉取的
`build/u-boot` 是 commit `baa64b2f892890f00a377eac4a3e685472bb56b5` 的干净源码树，
没有 `.config`。大部分阶段性修改仍保存在 `patches/u-boot-*.patch`，初始 OP-TEE 配置保存在
`build/artifacts/r1-phicomm-r1-uboot-optee-os.config`，但最后的路标和
`CONFIG_SPL_LOAD_FIT_FULL=y` 状态需要重建。完整恢复顺序、安全边界和下一步记录于
[`handoff.md`](../handoff.md)。

## 重启后重建最小 R1 U-Boot 源码状态并生成 OP-TEE 跳转路标候选（2026-08-07）

交接后第一轮主机侧工作：把重启后丢失的 `/tmp` U-Boot 工作树状态在干净的
`build/u-boot`（commit `baa64b2f892890f00a377eac4a3e685472bb56b5`，无 `.config`）中
重建，并生成两个新的、已离线校验的 RAM-only loader，供下一次真 MaskROM `db` 使用。

### 补丁重放顺序

按依赖顺序应用（顺序验证过，前一个为后一个提供上下文）：

```sh
cd build/u-boot
for p in \
  u-boot-phicomm-r1-board \
  u-boot-rockchip-phase-aware-save-boot-params \
  u-boot-rk322x-preserve-preconfigured-uart-mux \
  u-boot-rk322x-spl-uart-breadcrumbs \
  u-boot-ns16550-honor-debug-uart-skip-init \
  u-boot-crt0-honor-debug-uart-skip-init \
  u-boot-phicomm-r1-spl-board-init-breadcrumbs \
  u-boot-phicomm-r1-spl-common-init-breadcrumbs ; do
  git apply ../../patches/$p.patch
done
```

`u-boot-spl-optee-return-address.patch` 原文件因尾部 `-- /Armbian/R1 bring-up`
不是合法 git 签名而被 `git apply` 判为损坏，已用真实 blob 哈希重写为合法
format-patch 并覆盖原文件。改动本身只有一行：`spl_optee_entry()` 在
`mov pc, r3` 前把 `CONFIG_TEXT_BASE` 写入 `r1`。

新增 `patches/u-boot-phicomm-r1-optee-jump-breadcrumbs.patch`：固定单字符路标
`L/M/N/O/P/Q/R/T`，覆盖交接记录要求的全部 OP-TEE 跳转边界：

| 字符 | 位置 |
|---|---|
| `L` | `common/spl/spl.c` `spl_perform_arch_fixups()` 返回后 |
| `M`/`N` | `spl_board_prepare_for_optee()` 调用前后 |
| `O` | `spl_board_prepare_for_boot()` 返回后 |
| `P` | `jumper(&spl_image)` 调用前 |
| `Q`/`R` | `arch/arm/lib/spl.c` `jump_to_image_optee()` 中 `cleanup_before_linux()` 前后 |
| `T` | `common/spl/spl_optee.S` 写入 `r1=CONFIG_TEXT_BASE` 后、`mov pc, r3` 前 |

`T` 的反汇编确认（`spl/common/spl/spl_optee.o`）：

```text
mov lr, #0x61000000
mov r1, #0x61000000
ldr ip, [pc, #20]      ; UART2 0x11030000
ldr fp, [ip, #20]      ; LSR
tst fp, #32             ; THRE
beq ...                 ; 等待
mov fp, #0x54           ; 'T'
str fp, [ip]            ; 打印 T
mov pc, r3              ; 进入 OP-TEE
```

若实机最后字符是 `T`，说明控制权已写入 OP-TEE（之后的失败要查 OP-TEE 入口
约定、`r1` 返回地址和 FDT 参数），而不是 FIT parser。

### 配置恢复

`build/artifacts/r1-phicomm-r1-uboot-optee-os.config`（初始 OP-TEE 候选）复制为
`.config` 后 `make olddefconfig`，再补交接记录指出的最后两个变化：

```sh
./scripts/config --enable CONFIG_SPL_LOAD_FIT_FULL --enable CONFIG_SPL_FIT_IMAGE_TINY
make olddefconfig
```

`SPL_FIT_IMAGE_TINY` 仍保留（上一轮为绕过 `fdt_shrink_to_minimum()` 停止点使用，
重建后未再验证是否仍需要；FIT 结构相同，实机行为应一致）。最终配置保存为
`build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace.config`。

### OP-TEE blob 重新获取

按固定 commit 从 Armbian 维护分支重新下载并验证：

```sh
mkdir -p build/tee
curl -fsSL -o build/tee/rk322x_tee_os.bin \
  https://raw.githubusercontent.com/paolosabatino/armbian-build/d80ff015a83b0cf9a2500a2312a31d42931a6da4/packages/blobs/rockchip/rk322x_tee_os.bin
wc -c build/tee/rk322x_tee_os.bin            # 423248
sha256sum build/tee/rk322x_tee_os.bin        # ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c
```

不再放在 `/tmp`。构建时复制为 `build/u-boot/tee.bin`（binman `tee-os` 条目读取）。

### pylibfdt 构建阻塞与绕过

干净树首次构建在 `scripts/dtc/pylibfdt` 处失败：需要 `swig`（未安装）。交接前的
"pylibfdt 复用已有构建产物"在 `/tmp`，已丢失。按 U-Boot Makefile 语义，
提供系统 DTC 可跳过 in-tree pylibfdt 构建：

```sh
pip3 install --user pylibfdt      # 用户级，1.7.2.post1，不改系统
make CROSS_COMPILE=arm-none-eabi- DTC=/usr/bin/dtc TEE=tee.bin -j8
```

binman 运行时通过用户 site-packages 的 `libfdt` 工作。系统 dtc 1.7.2 满足
`DTC_MIN_VERSION`（1.4.6）。

### 关键修复：TEE 变量为空导致 OP-TEE 数据缺失

首次构建的 `u-boot.itb` 中 `/images/op-tee` 的 `data-size=0`、`load/entry` 被
写成字符串字节（`"h@"` 实为 `0x68400000` 的字节），configuration 也退化为
`firmware="u-boot"`。根因：binman 命令把 `-a tee-os-path=${TEE}` 传入，而 Makefile
没有定义 `TEE`，binman 的 `tee-os` 条目找不到数据、把 op-tee 判为 missing，
FIT 生成器随后回退。构建必须显式传 `TEE=tee.bin`。修复后离线核对：

```text
/images/op-tee  load/entry 0x68400000, data 423248 B, 与 rk322x_tee_os.bin 逐字节一致
                （FIT data-offset 相对 FIT 结构末尾 0x600）
/images/u-boot  load/entry 0x61000000, data 与 u-boot-nodtb.bin 逐字节一致
/images/fdt-1   data-size 0x6498 (25752)
configuration:  firmware=op-tee, loadables=u-boot
```

### 两个新 loader 与逐字节解包验证

`rkdeveloptool pack` 需要 `config.ini`（写入了 `rkdeveloptool/config.ini`，注意
`parseLoader` 的索引是 0 基，必须写 `LOADER0=FlashData`，`LOADERCOUNT=1` 才会成功）。
条目与前一轮一致：471=原厂 DDR、472=`u-boot-rockchip-usb472.bin`、
loader/FlashData=原厂 DDR。

1. 复现版（无新逻辑，仅重建）：

```text
build/artifacts/r1-phicomm-r1-uboot-optee-fit-repro-loader.bin
size:    880917 bytes
sha256:  3b45373aa824c7cd5a42c1c5ae2610b690980639e8e8833605582a19a100c707
pack CRC: 0x68da28d4
472: u-boot-rockchip-usb472.bin 862528 B, sha256 015eb05c67309485d7a9c2dfb35ed146f4e118735be8c351f4b0d10247b17393
```

2. 跳转路标候选（新）：

```text
build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-loader.bin
size:    880917 bytes
sha256:  b41a2955fa41035cb164733d68d0cdaeb0480b5046adb94b5a43ba8cdb2297f7
pack CRC: 0x1fd332e2
472: u-boot-rockchip-usb472.bin 862592 B, sha256 e9f2998baf572274ecdc7828a97a576acb04baf2c47ad9bcfbae106d22fd8b60
```

两者解包后 471/472/FlashData 有效字节均与输入逐字节一致、尾部 padding 全零；
`u-boot.itb` 中 OP-TEE 数据与已验证 blob 逐字节一致。重新构建产生的 472 与原
封装条目仅有 8 个字节差异（内嵌构建时间戳），确认重建与封装对应同一源码状态；
最终跳转路标 loader 已用重建后的 472 重新封装，SHA 以上述为准。U-Boot 源码已
提交为本地 commit `b0531571496`（build/u-boot 独立仓库），补丁链可完整重放。

### 下一步（实机）

真 MaskROM + 串口就绪后，只执行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-loader.bin
```

预期输出在既有 `SRM012345ABabsmcdefCDEFGHIJ ... K6Trying to boot from RAM`
之后出现 `LMNOPQR` 与 `T`。若停止在 `Q` 说明卡在 `cleanup_before_linux()`，
`R` 之后无 `T` 说明卡在 `spl_optee_entry()` 调用前，`T` 之后无输出说明已进入
OP-TEE（转而检查 OP-TEE 入口约定与 FDT 参数）。全程不执行 `UL/WL/EF/GPT/PRM`。

### 重建后实机 `ab` 停止与探针补丁恢复（2026-08-07 晚）

重建候选 `r1-phicomm-r1-uboot-optee-jump-trace-loader.bin` 首次实机只输出：

```text
OUT
SRM012345ABab
```

与历史 `r1-phicomm-r1-uboot-spl-common-trace-loader.bin` 的停止点完全一致
（`ab` = early malloc 设置完成、`fdtdec_setup()` 之前），而不是此前最后候选的
`SRM012345ABabsmcdefCDEFGHIJ ... FITF os=17 ret=0`。对照证据：

- 唯一一次历史 `ab` 停止发生在**没有** `u-boot-phicomm-r1-spl-dtb-memory-probe.patch`
  的构建；此后 memory-probe/hex/location/generic 以及全部 OP-TEE 链构建都带该补丁
  且全部通过 `ab`（最后候选实机日志的 `absmcdef` 中 `s/m` 即探针路标）；
- 本次重建按交接记录 10 补丁清单省略了该探针补丁（清单遗漏），复现 `ab` 停止；
- 配置核对：`olddefconfig` 后与保存配置仅差 `SPL_LOAD_FIT_FULL` 与
  `SPL_FIT_IMAGE_TINY` 两项，排除配置回归。

探针补丁与已应用的 common-init 路标补丁重叠（宏重复定义），按交接警告手工合并：
只新增 `R1_SPL_COMMON_HEX32` 宏、`<asm/sections.h>` include 和 OF_REAL 块内的
`s/m/n+hex` 探针（`ab`/`p` 路标已存在）。重建后 SPL 为 862,720 B（+128 B），
反汇编确认 `0xedfe0dd0` 比较与 UART 打印路径已编入，FIT 数据与 OP-TEE blob
逐字节一致。新候选：

```text
build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-dtbprobe-loader.bin
size:    880917 bytes
sha256:  ec7f650d300de1b093406734003ef77a8a2af8d720d831e23d18fb1758693f56
pack CRC: 0x03d4cad5
472: u-boot-rockchip-usb472.bin 862720 B, sha256 433d3dfb4b40f4ada179e8d7364603308443d9257913462adf0232729883bb93
```

解包后 471/472/FlashData 有效字节均与输入逐字节一致。源码已提交为本地 commit
`78472b20c9e`。失败候选 `r1-phicomm-r1-uboot-optee-jump-trace-loader.bin`
（SHA-256 `b41a2955...`）保留为 `ab` 停止证据，不再使用。

下次上板：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-dtbprobe-loader.bin
```

预期与判读：`ab` 后应出现 `s`；`sm` 表示运行时 `__bss_end` DTB magic 合法，
继续 `cdefCDEFGHIJ` 与 `LMNOPQRT`；`sn<8位hex>` 表示运行时 DTB 内容未正确到达
（记录该地址值）；若仍停在 `ab`，则探针假说不成立，需在 `bootstage_init()` 与
`fdtdec_setup()` 之间细分。

## R1 原厂 Trust OS 版本鉴定（2026-08-08）

用户询问原厂 TEE 版本并对照手册（手册提到 RK 的 BL32/BL33 与 "2.0" 版本）。主机侧
从本地证据直接鉴定，不依赖手册。

### R1 实机原厂 Trust OS（最强证据）

从 `backup/partitions/trust.img`（`TOS` 容器，见分区提取一节）提取的
`build/artifacts/r1-vendor-tee.bin`：

```text
size:     332232 bytes
sha256:   aecdf2b717ce3aeecd5ae0bb1b151ed0e54b1ea3d3d27924913752b9607243b7
版本串:   1.0.1-54-g0d46013
构建行:   51.0.1-54-g0d46013 #4 Thu Sep 29 01:09:49 UTC 2016 arm
```

strings 含 `OPTEE`、`tee_fs_*`、`tee_pager_*`、`tee_mmu_*` 等 OP-TEE 内核符号，
判定为 OP-TEE 派生的 Rockchip 专有 Trust OS，32-bit，无独立 ATF。该构建日期
2016-09-29 早于 rkbin 本地 v1.90（2017-03-24）与官方 v2.00（2019-01-31）。

### 与 rkbin 发布版对照（均为 OP-TEE 1.0.1 分支家族）

| 来源 | 文件 | 内部版本串 | 构建日期 | 大小 |
|---|---|---|---|---|
| R1 原厂 trust 分区 | `r1-vendor-tee.bin` | `1.0.1-54-g0d46013` | 2016-09-29 | 332,232 |
| 本地 rkbin | `rk322x_tee_v1.90.bin` | `1.0.1-65-gf1567d3-dev` | 2017-03-24 | 346,352 |
| 官方 rkbin master | `rk322x_tee_v2.00.bin` | `1.0.1-86-g31e775b` | 2019-01-31 | 333,896 |
| 本地 rkbin | `rk322x_tee_ta_v1.91.bin` | `1.1.0-250-g85c621f` | 2017-11-29 | 669,620 |

官方 `rk322x_tee_v2.00.bin` 来源为
<https://github.com/rockchip-linux/rkbin/blob/master/bin/rk32/rk322x_tee_v2.00.bin>
（master 为移动分支，本记录下载时的 SHA-256 为
`a568cba05d073fc6192bcbf43b8128f2601e34fbc310e94eb85c32294b6c36f6`）。

结论：手册中的 "2.0" 对应 rkbin 发布包 `rk322x_tee_v2.00.bin`，但其内部 OP-TEE
版本串仍是 `1.0.1-86-g31e775b`——"2.00" 是 Rockchip 发布编号，不是 OP-TEE 大版本。
R1 原厂的是同分支最旧一档（`1.0.1-54`，2016-09-29），比 v1.90/v2.00 都早。

### BL31/BL32/BL33 编号澄清

ARM Trusted Firmware 标准约定（TF-A 文档）：
`BL31` = EL3 运行时固件 = ARM Trusted Firmware (ATF)；`BL32` = Secure-EL1
payload = Trusted OS（OP-TEE）；`BL33` = 非安全世界引导器（U-Boot）。因此
"BL32 是 ATF、BL33 是 OP-TEE" 的说法与 ARM 约定相反。且 RK3229 这类 32 位
SoC 没有独立 ATF：trust 分区仅含该 OP-TEE 派生的 Trust OS，PSCI/secure monitor
都由它提供（这也是 RAM 链必须换成开源 OP-TEE 提供 PSCI 的原因）。

## B 线：官方 rk322x_tee_v2.00 的 RAM-only 候选（2026-08-08）

按用户建议建立 A/B 双线：A 线 = 开源 OP-TEE（既有），B 线 = Rockchip 官方
`rk322x_tee_v2.00.bin`（2019-01-31，内部 `1.0.1-86-g31e775b`）。目的：用一条
便宜的纯 RAM 实验判断"30 秒冻结是否专有 Trust OS 通病"，同时给 SMP 链一条
备选路径。

### 依据与兼容性

- 用户提供的官方配置 `RKTRUST/RK322XTOS.ini`（master）指名 `TOS=bin/rk32/rk322x_tee_v2.00.bin`、
  `TOSTA=bin/rk32/rk322x_tee_ta_v2.12.bin`；本地旧 rkbin 对应 v1.90/v1.91。
- `rk322x_tee_v2.00.bin`：333,896 B，SHA-256
  `a568cba05d073fc6192bcbf43b8128f2601e34fbc310e94eb85c32294b6c36f6`，
  入口为裸 ARM 代码（`b +0x0b` + 自旋），无独立头，装载地址由容器决定；
- 现有 SPL 的 `spl_optee_entry` 已按 Armbian 的专有 Trust 兼容约定写
  `r1=CONFIG_TEXT_BASE`，故 v2.00 与现有链天然兼容；FIT load/entry 仍用
  `0x68400000`（与 R1 原厂 trust.img 头一致）。

### 构建与验证

```sh
cp /tmp/opencode/tees/rk322x_tee_v2.00.bin build/tee/rk322x_tee_v2.00.bin
cd build/u-boot
make CROSS_COMPILE=arm-none-eabi- DTC=/usr/bin/dtc TEE=../tee/rk322x_tee_v2.00.bin
```

离线核对：`u-boot.itb` 中 op-tee 数据（333,896 B）与 v2.00 blob 逐字节一致、
load `0x68400000`、firmware=op-tee。经 `rkdeveloptool pack` 生成：

```text
build/artifacts/r1-phicomm-r1-uboot-vendor-tee-v2.00-loader.bin
size:    790805 bytes
sha256:  a4e3c7f681eca7ea8c4f5d19417dfd62d8a87f3c27a0dbe0b2264d258f0eee20
pack CRC: 0x8a18dbdf
472: u-boot-rockchip-usb472.bin 772352 B（有效），sha256 663450d00ba8f6f88caf92a466cc753839ac9336cbf7c1da0ac0ba0298da5804
```

解包后 471/472/FlashData 前缀逐字节一致、padding 全零。配置副本保存为
`build/artifacts/r1-phicomm-r1-uboot-vendor-tee-v2.00.config`。v2.00 为专有
二进制，置于 `build/tee/`，不提交仓库。

### 观察：U-Boot 重建不可复现性（-4 布局差异）

恢复 A 线现场（`TEE=tee.bin` 重建）后，新 472（SHA `6e132ca6...`）与已打包的
dtbprobe A 线 472（SHA `433d3dfb...`）对比：代码与字符串位置完全一致，但全部
字面量池中的 data/bss 地址整体 -4（1177 个差异点，均为低字节减 4）。判定为
同源同配置重建时一处 4 字节数据布局差异（疑似时间戳相关符号），两个构建各自
自洽（探针读各自链接期 `__bss_end`），不影响行为。因此**已打包 loader 的 SHA
是权威身份**；当前树的 472 与它仅差这一布局。已记录的 loader SHA 不可用
"重建验证"方式复现，只能靠解包比对。

### 上板计划（一次开机测两条链）

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-dtbprobe-loader.bin   # A 线
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-vendor-tee-v2.00-loader.bin            # B 线
```

判读（两条链串口应相同直到 `LMNOPQRT` 附近）：A 线若停在 `ab` 之前 → 探针假说
需复查；若到 `T` → 进入 OP-TEE，改查入口约定。B 线若进入 U-Boot proper 提示符，
则下一步从该 RAM-only U-Boot 测双核 Linux 与 30 秒边界；若同样在 OP-TEE 跳转
边界停止或 30 秒冻结，则坐实专有 Trust OS 通病，开源 OP-TEE 为唯一路线。
全程不执行 `UL/WL/EF/GPT/PRM`。

### 实机 `sn782e54f1` 根因：原厂 DDR 471 训练暂存区覆盖 DTB（2026-08-08）

A/B 双线首测均失败，输出：

```text
OUT
SRM012345ABabsn782e54f1   ← A 线到此为止（B 线后续带乱码打出 pCX + spl_early_init() failed: -2）
```

`sn782e54f1`：探针在 `__bss_end`（0x6000e500）读到 0xf1542e78（字节序换算），
不是 DTB magic。诊断链：

1. 反汇编打包 472 确认探针读取地址就是 `0x6000e500`（字面量池 0x10a4）；
2. 该值 `f1 54 2e 78` 在 471/472/tee 等所有文件里都不存在 → 运行时生成的数据；
3. 原厂 DDR 471 二进制（`rk322x_ddr_300MHz_v1.06.bin`）内含固定地址字面量
   `0x6000e9cd`、`0x6000f8cc`、`0x6000f8dc` → 训练在 DRAM `0xE000-0xF900`
   一带留暂存数据；
4. 历史可工作构建（实机 `sm`）的 DTB 在 `0x8aa0`、FIT 结构 `0x8fc0`，
   完全避开该窗口；本次重建的 SPL 比历史大 ~23 KB，把 DTB 推到 `0xe500`、
   FIT 结构 `0xea00`、FIT 数据 `0xf000`，全部落入暂存区，运行时被覆盖。

结论：471 是先装载 472、后跑 DDR 训练，训练写暂存区时把已就位的 472 内容
覆盖。历史候选的 DTB 位置（0x8aa0）只是侥幸避开——它的 FIT 数据同样穿过
`0xE000-0xF900`，这甚至可能是它实机卡在 OP-TEE 跳转边界的隐患之一。

修复（commit `79643482a60`）：`common/spl/spl.c` 增加
`#if defined(CONFIG_SPL_BUILD) && !defined(CONFIG_TPL_BUILD)` 守卫的
`0x2000` 字节 `.rodata` 填充（`r1_ddr471_scratch_pad`），把 SPL 尾部抬高到
暂存区之上。重建后 `__bss_end` = `0x60010500`，DTB `0x10500`、FIT 结构
`0x10a00`、FIT 数据 `0x10d00+`，全部避开 `0xE000-0xF940`。

坑：填充数组直接 `__attribute__((used))` 会被链接脚本的
`SORT_BY_ALIGNMENT(.rodata*)` 当成孤儿段丢掉（`-fdata-sections` 下段名带
后缀），必须显式 `__attribute__((section(".rodata"), used))`；且数组会被
同时编入 TPL，超限报 `SPL image too big`（此前 rtk 包装把错误吞了，退出码
误导为 0），需要 SPL-only 守卫。

两个修复后的候选（均解包逐字节验证）：

```text
A 线：build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-ddrpad-loader.bin
  size:    889109   sha256: c1b48444c43352ee44108f9d138ec3531bb5f2810b047cd9d69b438c9b9d049e
  pack CRC: 0xb8e6d568
B 线：build/artifacts/r1-phicomm-r1-uboot-vendor-tee-v2.00-ddrpad-loader.bin
  size:    798997   sha256: a67497ab50efeb2ed76072d38280a998e4b3c4ec34399dc3063f7197b5a3ae93
  pack CRC: 0xf1133ca4
```

下一轮上板预期：`ab` 后出现 `s`；若 DTB 正确到达应为 `sm`（不再 `n+hex`），
继续 `cdefCDEFGHIJ` 与 `LMNOPQRT`。若仍 `sn`，则假说需复查（例如暂存区上限
高于 0xF940，需要加大填充量）。

附带修正：B 线首测的"乱码"是同一故障的次要现象（SPL 在 DTB 区被覆盖后，
控制台/错误路径在坏状态下打印），不是 TEE 本身的问题。

### ddrpad 修复未生效：运行时无效区比预想更高（2026-08-08 续）

8 KB `.rodata` 填充后的 B 线实机仍失败：

```text
OUT
SRM012345ABabsn8f6642af<乱码>pCXspl_early_init() failed: -2<乱码>
```

探针值从 `782e54f1`（0xe500）变为 `8f6642af`（0x10500）——探针按新 `__bss_end`
读取，但新地址仍是运行时垃圾。471 二进制的全部固定引用（0x6000e9cd、0x6000f8cc、
0x6000f8dc、0x600160c3、0x600202f0、0x600202c0、0x600243d4、0x6002431a、
0x60027280）表明其数据区至少延伸到 `0x60027280`——0x2000 填充远不够。

结论：往上垫不可行（区带 >160 KB）。下一步改为精确制导：新增固定地址内存地图
探针（commit `adcad3c2083`），在 `fdtdec_setup()` 前打印 14 个地址的 32 位值
（0x60008a90/0x6000a000/0x6000c000/0x6000d000/0x6000e000/0x6000e500/
0x6000f900/0x60010500/0x600160c3/0x600202f0/0x600243d4/0x60027280/
0x60030000/0x60050000/0x60080000），一次上板即可区分"有效装载区"与"运行时垃圾区"
边界，据此把 DTB/FIT 结构放到确定可用的区域。

```text
build/artifacts/r1-phicomm-r1-uboot-optee-memorymap-loader.bin
size:    889109   sha256: 2e7919130d152f8c7cc501bd1047abbacc1921e559b48dc0cf678aab18c03b2c
pack CRC: 0xe2ac585c
```

注意：B 线 ddrpad loader 在打包操作中被误覆盖过一次，已重建，新身份为
SHA-256 `5e4eb78fb9e101552971a6b3ef9afa6a2852e22fe97e574267b19d6fa4167075`
（CRC `0x31009e66`），旧 SHA `a67497ab...` 作废。

判读预期：若 0x60008a90 处为 `d00dfeed`（历史 DTB 位置）而 0x6000e000 起为垃圾，
则边界在两者之间；若 0x60008a90 也是垃圾，则整个高地址区在运行时都不可用，
需要重新审视 472 装载流程（例如 BootROM 仅装载前 ~0x9000 字节）。
