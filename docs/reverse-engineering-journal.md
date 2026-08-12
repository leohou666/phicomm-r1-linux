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

## eMMC hardware boot0/boot1 备份完成（2026-08-08）

补齐交接记录中的安全缺口：eMMC hardware boot0/boot1 已确认并完整备份。

### 方法（纯只读）

设备在 RAM-only U-Boot 提示符下（`r1-phicomm-r1-uboot-spl-dtb-generic-loader.bin`
`db` 进入，不写盘），用 `mmc dev` 切硬件分区、`mmc read` 读到 RAM、`md.b` 经串口
dump（该 U-Boot 构建没有 `mmc partconf`/`extcsd` 子命令，直接读内容判断）：

```text
mmc dev 0 1                          # boot0
mmc read 0x61000000 0 0x2000         # 4 MiB 到 RAM
md.b 0x61000000 0x40000              # 256 KiB/块 × 16 块，串口日志落盘
mmc dev 0 2                          # boot1（新日志文件，地址范围相同避免混淆）
```

主机端重建（串口日志 `build/artifacts/r1-boot-backup.log` /
`r1-boot-backup-b.log`）：

```python
# 提取所有 "61000000: aa bb ..." 行 → 按地址连续拼接 → 无缺口即成功
# 16,384 行/块 × 16 块 = 4 MiB，缺口 0
```

### 结果

```text
backup/boot/r1-emmc-boot0.img   4 MiB
backup/boot/r1-emmc-boot1.img   4 MiB
两者 sha256 相同：70b4abfd87fa2e201ce17ddbf6886009ac9e70c64d2cc09880f61bef5604fdb9
（SHA256SUMS 见 backup/boot/*.sha256）
```

### 内容观察

- boot0/boot1 **逐字节相同**：同一个 Rockchip loader 写了两份（冗余）；
- 结构：0x000-0x2173 加密/签名数据块（0x020A 处 ASCII "RK28"），0x0800 起
  "RK32" 标记 + ARM 启动代码（主代码段 0x2A08-0x11510 ≈ 59 KB），非零内容共
  66,411 B，其余全零；
- 该内容与本地 rkbin 的 `rk322x_miniloader_v2.37.bin`（69,876 B，头为
  "RSAK"）**不是同一格式**，未做进一步归属鉴定（备份目的已达成）；
- 注意：boot 分区**有内容**，但 BootROM 是否实际从 boot0/boot1 启动仍未直接
  验证——R1 正常启动路径走 user area IDB（sector 0x40 一带）；该 loader 的存在
  可能是出厂编程的冗余/回退路径。

### SPL 瘦身 + UART YMODEM 交付链（2026-08-08）

按既定路线（A 线 FIT 交付）落地：SPL 瘦身到 db 窗口附近 + YMODEM 从串口收 FIT。
commit `3ceba8432be`。

#### 瘦身结果（36 KB 窗口约束）

| 措施 | 收益 |
|---|---|
| 关 `SPL_SHA1`/`SPL_SHA256`（FIT 只用 crc32） | **13.6 KB**（sha1_process 4.2K + sha256_process 8.2K，经 common/hash.c 的 algo 表引用） |
| 关 `SPL_PARTITIONS`/`SPL_EFI_PARTITION`/`SPL_MMC` | ~6.9 KB |
| 移除 0x20000 暂存区填充 + 15 地址地图探针 | ~1.5 KB |
| 关 `ANDROID_BOOT_IMAGE`（影响 proper，记录在案） | ~0.7 KB（其余早被 gc-sections 剔除） |

`__bss_end`: 0x2E7B0 → **0x9200**（SPL+DTB 结束 0x96FE）。教训：**用 map 统计对象大小
不可靠**（包含已丢弃段），`nm --size-sort -S` 的最终符号尺寸才是真实贡献；
`command.o`/`dump.o` 等看似可砍的 obj-y 实际已被 --gc-sections 剔除，改 Makefile
门槛无收益（已保留改动，属无害清理）。

坑：`SPL_SHA1/SHA256` 有 `default y if SHA1/SHA256`，`scripts/config --disable` 后
`olddefconfig` 会因 mbedtls Kconfig 的 `select SPL_SHA1_LEGACY if SPL_SHA1` 链被
拉回——直接对 `SPL_SHA1/SPL_SHA256` 本体 disable 即可（LEGACY 的 select 带
`if SPL_SHA1` 条件，本体关掉后不再触发）。

#### YMODEM 链路

- `CONFIG_SPL_YMODEM_SUPPORT=y`；`spl_ymodem.c:188` 的
  `SPL_LOAD_IMAGE_METHOD("UART", ...)` 自动注册 BOOT_DEVICE_UART；
- 当前工作树随后将 `board_boot_order()` 调整为首先尝试 BOOT_DEVICE_UART；
  这是为了避免 MaskROM RAM payload 的越窗垃圾路径阻塞在串口交付之前；
- `spl_ymodem.c` 的 `CONFIG_SPL_LOAD_FIT_FULL` 分支：YMODEM 整包收进
  `CONFIG_SYS_LOAD_ADDR`(0x61800800)，FIT 就地解析（op-tee 数据已离线验证完整）；
- 收完跳转沿用既有 `L/M/N/O/P/Q/R/T` 路标。

#### 产物

```text
初版产物曾使用上述路径和 SHA-256 `ca01385e...` / `4f2dec72...` 记录；其后为
首传诊断重建并覆盖同名文件，不能再把初版身份当作当前文件校验值。当前身份、FIT
组件及首传结论见下节“YMODEM 首传失败的主机侧复核”。
```

#### 上板流程（下一步）

1. 真 MaskROM + 串口（1500000 8N1）就绪后：
   `sudo ./rkdeveloptool/rkdeveloptool db build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-loader.bin`
2. 串口预期：`SRM012345ABabsm...` → `Trying to boot from UART` → 输出单个 `C`，
   后者是接收端请求 CRC16 的 YMODEM 握手；
3. 主机端必须从**拥有同一串口文件描述符的终端程序本地命令功能**启动真正的
   YMODEM sender：`sb -k -vv build/artifacts/r1-ymodem-fit.itb`。不要用
   `sz -Y`：`-Y` 是 ZMODEM 的“overwrite-or-skip”，不选择 YMODEM。实机失败的
   `sz -Y -vv` 已在主机伪串口复现：收到 `C` 后发出 ZMODEM 起始串
   `72 7a 0d 2a 2a 18 42 30 ...`（ZMODEM `rz` 前导），所以 SPL 发送 NAK；相同
   `C` 输入下，`sb -k` 正确发出 YMODEM block 0
   `01 00 ff r1-ymodem-fit.itb 00 796672 ...`。终端必须保持 1500000 8N1、无流控、
   原始二进制模式且没有第二个进程读取该串口；
4. 判读：若 DTB 落在 0x9200 不可用（`sn<hex>`）→ 需再砍 ~1.9 KB 把 DTB 压回
   0x8aa0 已验证区；若 `sm` 且出现 `LMNOPQRT` 则进入 OP-TEE 交接验证。

### YMODEM 首传失败的主机侧复核（2026-08-08）

实机日志已确认当前 loader (`U-Boot SPL 2026.10-rc1-00121-g3ceba8432bef-dirty`)
到达 `Trying to boot from UART` 并发出 `C`；因此 RAM `db`、UART2 RX/TX、SPL 启动顺序
和 `CONFIG_SPL_YMODEM_SUPPORT` 均已通过此次最小验证。发送端使用的是：

```text
sz -Y -vv build/artifacts/r1-ymodem-fit.itb
```

其输出为多次 `Retry 0: NAK on sector`，没有传出任何 FIT 数据。这个结果不能用于判断
OP-TEE 或 FIT，因为协议在文件块 0 之前已经不匹配。

本机以一对 raw pseudo-TTY 向 sender 注入单个 `C`，并抓取首个输出帧。`sz -Y` 发出
ZMODEM `rz` 前导握手；`sb -k` 则发出 CRC 正确的 YMODEM 文件头。结论是 `sz -Y`
的选项含义被误解：它不是 YMODEM 模式。下次只按上述 `sb -k -vv` 操作；首个成功标志
应为 SPL 输出 `Loaded 796672 bytes`。当前 FIT 经 `dumpimage -l` 离线核对，确含
345,128-byte U-Boot、423,248-byte 开源 OP-TEE（load/entry `0x68400000`）和
25,752-byte R1 FDT；接收缓冲区 `0x61800800-0x618c3000` 不与 OP-TEE 保留区重叠。

当前文件身份：

```text
build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-loader.bin
  852245 B  SHA-256 8031e57044ae0f2506bb77d5c022534adf989d23e85da373918a4469cfc08434
build/artifacts/r1-ymodem-fit.itb
  796672 B  SHA-256 eb848a90f15d7fa29854d635916478801193b1b5827f293f8157d831bc368493
```

### 直接串口桥接发送器（主机侧，待实机验证）

picocom 的外置文件传输会把串口交给子进程；在 `sb` 首包仍超时的情况下，下一步
不再让终端模拟器参与二进制路径。新增
`scripts/ymodem-serial-bridge.py`：它独占 USB-TTL、以 raw 1500000 8N1 打开物理
串口，用 pseudo-TTY 连接 `sb -k -vv`，把所有板端字节保存到 raw log；`sb` 结束后
继续在同一终端显示板端输出 45 秒。这样既不需要两个进程竞争串口，也能保留 OP-TEE
与 Linux 的启动日志。命令只向 RAM-only SPL 发送 FIT，不写 eMMC：

```sh
sudo python3 scripts/ymodem-serial-bridge.py \
  /dev/ttyUSB0 build/artifacts/r1-ymodem-fit.itb
```

开始前以 picocom 的 `Ctrl-A`、`Ctrl-Q` 退出，避免它仍占用 `/dev/ttyUSB0`；板端可
继续停在 YMODEM 等待状态，bridge 会等下一个 `C`。成功的第一个设备标志仍是
`Loaded 796672 bytes`。脚本已通过 Python 编译和 `--help` 参数检查，尚未连接 R1
实机执行。随后用一对 raw pseudo-TTY 模拟板端：bridge 收到单个 `C` 后，捕获到
`sb` 发出的完整 133-byte YMODEM block 0，首字节为
`01 00 ff 72 31 2d 79 6d 6f 64 65 6d ...`，文件名和 `796672` 长度字段均正确；
这验证了 bridge 的 PTY 转发不会重现 picocom 路径中的“无完整首包”现象。

### YMODEM 双向日志与 SPL RX 阶段诊断（2026-08-08）

第一次实机运行 bridge 后保留的 `build/artifacts/r1-ymodem-serial.log` 由连续 9 个 `C`（`0x43`）开始，随后为 9 个 NAK（`0x15`）以及：

```text
spl: ymodem err - Timed out
Error: -1
SPL: Unsupported Boot Device!
```

日志内没有 SOH/STX、YMODEM 文件名或任何来自主机的文件头字节。因此这次失败发生在接收 FIT 之前，不能归因于 FIT、开源 OP-TEE 或 Linux。旧 bridge 仅记录 board→host，不能区分“`sb` 没输出”和“bridge 没写入 USB-TTL”；脚本现增加 `--tx-log`（默认 `build/artifacts/r1-ymodem-serial-tx.log`），写入物理串口成功后同步保存 host→board 原始字节。`python3 -m py_compile scripts/ymodem-serial-bridge.py` 已通过。

为让设备端独立说明超时位置，`common/xyzModem.c` 增加诊断输出：`R1XM timeout stage=0 count=<n>` 表示等待 SOH/STX 时收到的非帧首字节数；stage 1–5 依次表示已收到帧首字节后等待 block、反码、payload、CRC；完整帧还会报告 frame/CRC 错误。

```sh
make -C build/u-boot CROSS_COMPILE=arm-none-eabi- DTC=/usr/bin/dtc TEE=tee.bin -j8
./rkdeveloptool/rkdeveloptool pack
sha256sum build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxtrace-loader.bin
```

生成 RAM-only 诊断 loader：

```text
build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxtrace-loader.bin
  size:    854289 bytes
  sha256:  c78ed30e668d3085c415f04d66373929f7f938934fcaa698b9cb60878df3c3e6
  472:     u-boot-rockchip-usb472.bin, 835712 bytes,
           sha256 9601ef156d613a20b9189c83e929d271405f7770f67ace4b81f05909620b5aa6
```

打包输出确认 471 为 `rk322x_ddr_300MHz_v1.06.bin`、472 为刚构建的文件；对 loader 运行 `strings` 也确认所有 `R1XM timeout stage=0..5` 字符串在包内。下一次仅用 `db` 加载这个诊断镜像，不写 eMMC，再执行一次修正后的 bridge。若得到 `stage=0 count=0`，优先检查 SPL/UART RX 初始化或物理主机→R1 路径；stage 1–5 或 CRC/frame 错误则说明字节已到达设备，可以定位帧级问题。

### 双向日志定位 UART RX 初始化并生成修正版（2026-08-08）

实机 bridge 的双向日志完成闭环。host→board 日志每次都是完整、正确的 133-byte YMODEM block 0，开头为：

```text
01 00 ff 72 31 2d 79 6d 6f 64 65 6d 2d 66 69 74
         r  1  -  y  m  o  d  e  m  -  f  i  t
```

但 board→host 每轮均为 `C R1XM timeout stage=0 count=0`。因此 USB-TTL 与 bridge 已成功把文件头写出；SPL UART RX 在等待 SOH/STX 时没有收到一个字节。这排除 FIT、OP-TEE、YMODEM sender 参数和桥接转发。

检查 SPL 生成 DTB 发现 `CONFIG_OF_SPL_REMOVE_PROPS` 已移除 UART 的 `pinctrl-0`/`pinctrl-names`。同时 R1 的 `CONFIG_DEBUG_UART_SKIP_INIT=y` 使 `board_debug_uart_init()` 与 `debug_uart_init()` 都跳过，完全继承 471 留下的 UART 状态。该状态的 TX 可用（所有路标和 `C` 正常），但 UART2-1 RX 不可用。这是本次故障的固件侧根因。

修正为删除 `CONFIG_DEBUG_UART_SKIP_INIT`，保留 R1 路标并使 SPL 早期显式执行 RK322x UART2-1 mux（GPIO1B2 RX/GPIO1B1 TX）、NS16550 FIFO 与 1,500,000 baud 8N1 初始化。重建命令：

```sh
cd build/u-boot
./scripts/config --disable DEBUG_UART_SKIP_INIT
make olddefconfig
make CROSS_COMPILE=arm-none-eabi- DTC=/usr/bin/dtc TEE=tee.bin -j8
```

新的 RAM-only 候选：

```text
build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxfix-loader.bin
  size:    854289 bytes
  sha256:  d23b86fd5e2d54e808b99880b1ede71917e6f9237e1270903bd9d93262fcab98
  472:     836288 bytes
           sha256 c91ad0897f0a8d8f01d35f988d354834d26245cf89c9cac970035fdbf9e4e954
```

离线检查确认 `.config` 中 `CONFIG_DEBUG_UART_SKIP_INIT` 已关闭、`CONFIG_DEBUG_UART_BOARD_INIT=y` 与 `CONFIG_SPL_YMODEM_SUPPORT=y` 仍在，且镜像保留 stage 0–5 诊断字符串。下一步仅执行 `rkdeveloptool db` 加载此文件，再运行 bridge；不执行任何 eMMC 写命令。

### RX-fix 实机结果与 1.5M 轮询优化（2026-08-08）

RX-fix 不是原问题的重复：这次 board→host 日志每轮均为 `R1XM timeout stage=3 count=53`（少数为 `52`）。stage 3 表示 SPL 已收到 SOH、block number、block complement 和 payload 的 `0x53`（83）或 `0x52`（82）字节，随后才发生字符超时。相比上一版的 `stage=0 count=0`，UART2-1 RX mux/初始化修正已经被实机验证。

`xyzModem.c` 的 `CYGACC_COMM_IF_GETC_TIMEOUT()` 原先在每一次获取字符前无条件调用 `schedule()`；1,500,000 baud 下每字节仅约 6.7 µs，连续 133-byte 文件头期间该额外调度足以让轮询接收落后并造成 UART FIFO 丢失。修正为仅在 `while (!tstc())` 的空闲等待路径调用 `schedule()`：连续可读数据不调度，包间和超时等待仍可调度。

```diff
-  schedule();
  while (!tstc ()) {
 +  schedule();
     ...
  }
```

重建、打包及 SHA-256：

```text
build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxfast-loader.bin
  size:    854289 bytes
  sha256:  7220f8b7508cca136d87ba33098b8bf2851d9a1343a5442568ce2a8414aeda3f
  472:     836288 bytes
           sha256 7321bea3c4e705422e81dd30537c47ff07ce3b744885f8e5098b02a862172da1
```

该产物同时含 UART2-1 显式初始化和 stage 0–5 诊断。下一次仍只用 `db` 下载它到 RAM 后运行 bridge；成功门槛为完整 block 0 的 ACK 和 `Loaded 796672 bytes`，不执行 eMMC 写入。

### 主机端 YMODEM 逐字节节流（2026-08-08）

RX-fast 实机仍稳定报告 `R1XM timeout stage=3 count=52`，而其 host→board raw log 仍是完整的 133-byte block 0。移动 `schedule()` 没有改变该上限，故不能再把故障归为该调度调用；已验证事实是 1,500,000 baud 的连续 USB-TTL burst 超过当前 SPL 轮询接收链的可靠吞吐。

`scripts/ymodem-serial-bridge.py` 新增 `--tx-gap-us`。值非零时，bridge 对 `sb` 输出的每一个字节执行物理串口写入、`flush()` 和指定微秒间隔，同时完整保存未修改的 tx raw log；这不改变 YMODEM 帧、FIT 或板端配置。Python 编译与 `--help` 参数检查通过。下一次采用：

```sh
sudo python3 scripts/ymodem-serial-bridge.py \
  --tx-gap-us 50 \
  /dev/ttyUSB0 build/artifacts/r1-ymodem-fit.itb
```

50 µs 是可靠起始值，总代价约为 1–2 分钟传输时间；全程只向 RAM 中的 SPL 发送 FIT，不写 eMMC。若 block 0 成功但 1 KiB data packet 仍丢失，保持该模式即可继续节流发送，而不改变 OP-TEE/FIT 内容。

### 首次实机进入开源 OP-TEE 与 U-Boot proper DTB 缺失（2026-08-08）

以 RX-fast loader 和 `--tx-gap-us 50` 发送后，`sb` 报告 `Transfer complete`，SPL 报 `Loaded 796672 bytes`，随后依次加载 OP-TEE、FIT FDT 与 U-Boot loadable，并输出 `LMNOPQRT`。开源 OP-TEE 实机日志确认：

```text
I/TC: OP-TEE version: 3.7.0-1-ga34a269b7-dev ... #2 Thu Apr  4 16:39:43 UTC 2024 arm
I/TC: Initialized
D/TC:0 0 init_primary_helper:1109 Primary CPU switching to normal world boot
```

这首次在 R1 上验证了开源 OP-TEE 的真实执行，而不只是 FIT 离线检查。`spl_perform_arch_fixups` 关于 MaskROM boot device 无 ofpath 的警告不阻止 OP-TEE 初始化，属于 RAM `db` 路径没有 DT ofpath 映射。

切回 normal world 后 U-Boot proper 立即报：

```text
No valid device tree binary found at 61054428
initcall_run_f(): initcall fdtdec_setup() failed
### ERROR ### Please RESET the board ###
```

地址 `0x61054428` 恰为旧 FIT `u-boot` 子镜像的终点：其 345,128-byte payload 被离线确认是 `u-boot-nodtb.bin`。U-Boot proper 采用 separate DTB 配置，启动时要求 DTB 紧接自身 binary；FIT 的独立 `fdt-1` 供 SPL/OP-TEE 使用，并不会自动拼接到 loadable 尾部。因此此错误是 FIT 组成错误，不是 OP-TEE、PSCI 或 Linux 失败。

新增 `scripts/r1-ymodem-fit-dtb.its`，其 `u-boot` 使用 `u-boot-dtb.bin`，同时保留独立 `fdt-1`：

```sh
mkimage -f scripts/r1-ymodem-fit-dtb.its build/artifacts/r1-ymodem-fit-dtb.itb
```

通过 `dumpimage -p 0/1/2` 解出三项并逐字节 `cmp`，结果分别等于 `u-boot-dtb.bin`、`rk322x_tee_os.bin`、`u-boot.dtb`。U-Boot 子镜像 371,360 B，末尾偏移 `0x54608` 出现 `d00dfeed` DTB magic；新 FIT 身份为：

```text
build/artifacts/r1-ymodem-fit-dtb.itb
  size:    822318 bytes
  sha256:  5687f549a82d3f2e0b51fe064df05a4b623ba180872c581132e6ecbe6a49cd84
  u-boot:  371360 bytes (u-boot-dtb.bin)
  op-tee:  423248 bytes (matches rk322x_tee_os.bin)
  fdt-1:   25752 bytes (matches u-boot.dtb)
```

下一次在真 MaskROM 下仅 `db` RX-fast loader 后，以 `--tx-gap-us 50` 发送这个新 FIT；全程不执行 eMMC 写命令。

## 开源 OP-TEE 后进入 U-Boot proper 提示符（2026-08-08）

按上述流程发送 `r1-ymodem-fit-dtb.itb` 后，YMODEM 完成，SPL 装入 OP-TEE、其后
返回 normal world 的 U-Boot proper。关键实机输出为：

```text
I/TC: OP-TEE version: 3.7.0-1-ga34a269b7-dev ... arm
I/TC: Initialized
D/TC:0 0 init_primary_helper:1109 Primary CPU switching to normal world boot

U-Boot 2026.10-rc1-00121-g3ceba8432bef-dirty
Model: Phicomm R1
DRAM:  512 MiB (total 480 MiB)
MMC:   mmc@30020000: 0
=>
```

这完成了不写 eMMC 的开源 OP-TEE → 现代 U-Boot proper 实机交接。`total 480 MiB`
与 OP-TEE 在 `0x68400000` 起保留 TEE RAM、TA RAM 和 shared memory 的布局一致；它不是
DRAM 探测失败。下一步只从 U-Boot 对 eMMC 执行 `mmc read`，把 recovery 的 zImage、
gzip initramfs 和 resource 内的 DTB 逐项放在 `0x68400000` 以下，再以 `bootz` 启动。
当前 recovery 的 Linux DTB 仍故意保留先前针对原厂 Trust OS 的 PSCI 0.1 binding，先作为
开源 OP-TEE 能否带起同一 SMP 基线的 A/B；随后再构建 PSCI 1.0/0.2 Linux DTB 对照。

### 首次双核 Linux 交接观察

在 U-Boot 提示符下没有写 eMMC。先从原始 LBA `0x20000` 读一 sector，Android header
确认 kernel size `0x009b2200`、ramdisk size `0x00099018`、second size `0x00000c00`；
随后分别读取 kernel、ramdisk 和 resource 内偏移 `0x400` 的 1,849-byte DTB，并执行：

```text
bootz 0x62000000 0x64000000:0x99018 0x65000000
```

Linux 5.10.262 在 CPU0 正常进入 SMP bring-up，明确打印 `psci: Using PSCI v0.1
Function IDs from DT`。紧接着的 secure-world 输出是：

```text
smp: Bringing up secondary CPUs ...
D/TC:0   psci_cpu_on:278 core_id: 1
D/TC:1   init_secondary_helper:1133 Secondary CPU Switching to normal world boot
```

此后没有 Linux CPU1 的启动行或 panic。已验证的范围是：旧 DTB 确实使用
`compatible = "arm,psci"` 和 `cpu_on = <0x84000003>`；OP-TEE 确实接收并处理
CPU1 的 CPU_ON，且执行切回 normal world。PSCI 0.1 与 0.2/1.0 的 ARM32 CPU_ON
调用号同为 `0x84000003`，因此不能把停止直接归因为“没有进入 OP-TEE”；但 Linux DT
仍应以 mainline PSCI 1.0/0.2 binding 再做单变量 A/B，检查 PSCI_VERSION/feature
协商及次核入口路径是否不同。

已新增 `kernel/dts/rk3229-phicomm-r1-minimal-psci-v1.dts`。它 include 原最小 DT，
仅把 `/psci/compatible` 改为 `"arm,psci-1.0", "arm,psci-0.2"`，并删除仅属于 0.1
binding 的三个 function-ID 属性。离线编译和验证：

```sh
cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
  -I build/kernel-src-5.10/arch/arm/boot/dts/rockchip \
  -I build/kernel-src-5.10/scripts/dtc/include-prefixes \
  kernel/dts/rk3229-phicomm-r1-minimal-psci-v1.dts > /tmp/r1-minimal-psci-v1.dts
build/kernel-5.10/scripts/dtc/dtc -I dts -O dtb -Wno-unit_address_vs_reg \
  -o build/artifacts/rk3229-phicomm-r1-minimal-psci-v1.dtb \
  /tmp/r1-minimal-psci-v1.dts
fdtget -t s build/artifacts/rk3229-phicomm-r1-minimal-psci-v1.dtb /psci compatible
fdtget -t x build/artifacts/rk3229-phicomm-r1-minimal-psci-v1.dtb /psci cpu_on
```

输出 compatibility 为 `arm,psci-1.0 arm,psci-0.2`，读取 `cpu_on` 正确返回
`FDT_ERR_NOTFOUND`。产物 1,790 B，SHA-256 为
`85605813b11b6b8744de044f3e954809c0b1baf93eb764acd016830cd4653436`。它需要通过
U-Boot proper 的 `loady` 放入 RAM，不能混入 SPL 的 OP-TEE FIT；kernel 和 ramdisk
仍直接从 eMMC 只读加载。

首次发送该 DTB 时，板子已不在 U-Boot proper `loady` 接收状态；`sb` 虽显示
`Transfer complete`，后续板端却打印 `Bad FIT kernel image format! (err=-42)` 和
`SPL: Unsupported Boot Device!`。这说明 1,792-byte DTB 被错误交给 SPL 在
`0x61800800` 按 FIT 解析，并非 U-Boot proper 成功收到 DTB；没有 eMMC 写入。为避免
串口终端关闭/打开与发送器之间的时序竞争，`scripts/ymodem-serial-bridge.py` 新增
`--command 'loady 0x65000000'`：bridge 独占串口，先发 U-Boot 命令，确认收到 YMODEM
CRC 请求 `C` 后才启动 `sb`。只有看到 `## Ready for binary (ymodem)` 和该 `C`，才可
继续发送 Linux DTB。

### PSCI 1.0/0.2 单变量 A/B 结果

使用 bridge 的 `--command 'loady 0x65000000'` 将 1,790-byte PSCI v1 DTB 放入 RAM；
U-Boot `fdt print /psci` 确认节点只含：

```text
compatible = "arm,psci-1.0", "arm,psci-0.2";
method = "smc";
```

同一份 eMMC kernel 与 ramdisk 经相同 `bootz` 命令启动。Linux 明确打印：

```text
psci: PSCIv1.0 detected in firmware.
psci: Using standard PSCI v0.2 function IDs
psci: SMC Calling Convention v1.0
smp: Bringing up secondary CPUs ...
D/TC:0   psci_cpu_on:278 core_id: 1
D/TC:1   init_secondary_helper:1133 Secondary CPU Switching to normal world boot
```

之后依旧没有 Linux CPU1 输出，现象与 PSCI 0.1 DTB 相同。此为实机结论：OP-TEE 的
PSCI_VERSION、标准 v0.2 CPU_ON 和 SMC convention 都已可用；Linux 的 PSCI binding/
function-ID 选择不是 CPU1 立即停止的根因。故障范围缩窄到 OP-TEE secondary normal-world
return 的目标入口/CPU 状态，或 Linux `secondary_startup` 的首段（SVC mode、processor
lookup、`secondary_data`/MMU 切换）。下一项最小诊断是在该汇编路径分段写 UART 路标，
不改变 PSCI、DT、kernel command line 或 eMMC 内容。

### Linux `secondary_startup` UART 路标候选

用户授权后，新增 `patches/linux-5.10.262/0001-arm-phicomm-r1-secondary-uart-trace.patch`，
并在 `kernel/config/r1-5.10.fragment` 启用
`CONFIG_PHICOMM_R1_SECONDARY_UART_TRACE=y`。路标语义为：

```text
A  OP-TEE 已跳入 Linux secondary_startup（MMU 关闭，物理 UART）
B  safe_svcmode_maskall 完成
C  Cortex-A7 processor type lookup 成功
D  secondary_data 已读出，准备执行 processor init / MMU enable
E  MMU 已切换，进入 __secondary_switched、即将跳 secondary_start_kernel
```

`A`–`D` 直接写 `DEBUG_UART_PHYS=0x11030000`，`E` 改写
`DEBUG_UART_VIRT=0xfed30000`，避免在 MMU 后访问未映射的物理外设地址。构建命令：

```sh
KERNEL_SRC=build/kernel-src-5.10 KERNEL_BUILD=build/kernel-5.10 \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal-psci-v1.dts \
rtk scripts/build-kernel.sh
cp build/artifacts/zImage build/artifacts/zImage-5.10-psci-v1-secondary-trace
```

首次候选的最终配置确认该 symbol 为 `y`；`head.o` 反汇编确认 `secondary_startup` 首条实际
路标写出 ASCII `0x41`，其余为 `0x42`、`0x43`、`0x44`，而
`__secondary_switched` 写 `0x45`。该候选随后被 C 路径扩展版取代；两者都不会写入
recovery：现代 U-Boot proper 的 `loady 0x62000000` 将它传入 RAM，ramdisk 仍从
eMMC 只读加载，随后使用既验证的 PSCI v1 DTB `bootz`。

### 对 OP-TEE 次核诊断假设的二进制复核

外部诊断提出了三项可检验假设：传给 PSCI 的次核 entry、ACTLR.SMP 和 DDR/SGRF 的
normal-world 属性。对当前实际执行的 `build/tee/rk322x_tee_os.bin` 的本地复核结果：

- Linux 5.10 的 `arch/arm/kernel/psci_smp.c` 明确调用
  `psci_ops.cpu_on(cpu_logical_map(cpu), virt_to_idmap(&secondary_startup))`；实机
  `Setting up static identity map for 0x60300000 - 0x603000ac` 因而直接证明本轮
  CPU1 的预期入口为 `0x60300000`，而不是仅凭地址形态推测。
- 虽然 blob 没有 ELF 符号表，`arm-none-eabi-objdump -D -b binary -m arm
  --adjust-vma=0x68400000` 在 `0x6840016c` 给出：

  ```text
  mrc p15, 0, r0, c1, c0, 1
  orr r0, r0, #64
  mcr p15, 0, r0, c1, c0, 1
  isb
  ```

  这正是把 Cortex-A7 ACTLR bit 6（SMP）置位的序列，故“当前 blob 缺少
  ACTLR.SMP 代码”被排除。单靠反汇编尚不能证明该小函数在 CPU1 每次启动时均被调用，
  但它不再是首要的“未编入”嫌疑。
- DDR/SGRF 的 region 属性为全局属性；CPU0 已在 normal world 从同一 DDR 执行 U-Boot
  和 Linux，不能彻底否定次核 transition 状态问题，但它不符合“仅 CPU1 首次取
  `0x60300000` 指令就失败”的首要解释。

当前二进制还保留 `core/arch/arm/plat-rockchip/psci_rk322x.c` 和
`psci_cpu_on` 字符串，可确认 blob 包含 RK322x PSCI 后端；但它不是 `tee.elf`，没有可
直接插入 DMSG 的源码/符号。检索 upstream 仓库也未能以 blob version 字符串中的短 SHA
`a34a269b7` 定位可复现源提交，因此禁止把当前 upstream `master` 的具体实现细节当作
本 blob 的已验证事实。相比先改一个未复现的 TEE 源树，Linux 路标能以最小变量直接回答
OP-TEE 是否已跳入 `0x60300000`。

### CPU1 已到达 Linux C 路径；扩展 `F`–`N` 路标（2026-08-08）

实机回传连续 `ABCDE`。按上述汇编路标的已验证语义，这不是“PSCI 起不了第二核”：CPU1 已
从开源 OP-TEE 返回到 Linux `secondary_startup`，完成 SVC mode、Cortex-A7 processor lookup、
读取 `secondary_data`、启用 Linux 页表，穿过 `__secondary_switched` 并 branch 到
`secondary_start_kernel()`。因此以下假设均不再是当前首要嫌疑：OP-TEE 把 CPU1 返回到错误
的 Linux entry、blob 完全缺少 ACTLR.SMP 设置、以及 Linux 次核汇编的早期 MMU 切换失败。

为将停点继续缩小到 C 函数，补丁在 `arch/arm/kernel/smp.c` 加入虚拟 DEBUG_LL UART
(`0xfed30000`) 写入，重新构建同一 PSCI v1 DTB 组合。新标记语义为：

```text
F  已进入 secondary_start_kernel()
G  secondary_biglittle_init() 返回（本配置未启用 CONFIG_BIG_LITTLE，实际为空）
H  cpu_switch_mm、branch predictor/TLB 初始化返回
I  init_mm 引用、current->active_mm 与 mm_cpumask 完成
J  cpu_init() 返回
K  notify_cpu_starting() 返回
L  ipi_setup() 返回
M  set_cpu_online(cpu, true) 返回
N  complete(&cpu_running) 返回
```

构建仍使用：

```sh
KERNEL_SRC=build/kernel-src-5.10 KERNEL_BUILD=build/kernel-5.10 \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal-psci-v1.dts \
rtk scripts/build-kernel.sh
cp build/artifacts/zImage build/artifacts/zImage-5.10-psci-v1-secondary-trace
```

`smp.o` 反汇编验证连续写出 ASCII `0x46`–`0x4e`，且地址为虚拟 UART。当前候选为
`build/artifacts/zImage-5.10-psci-v1-secondary-trace`，10,162,688 B，SHA-256
`db7c8d454b3661632f4de5ac165d0778bfe2179a1e1e53fb283a120eacb06899`。下一次只需经
U-Boot proper `loady 0x62000000` 传入该 zImage，再以 eMMC 只读 ramdisk 与既验证 DTB
执行 `bootz`；不执行任何 eMMC 写入。观察到的最后一个字符将直接对应上表的最后完成步骤。

### CPU1 完成 online/completion；进入 IRQ 与 idle 分支追踪（2026-08-08）

实机使用上述 `F`–`N` 候选，串口连续输出 `ABCDEFGHIJKLMN`。其中 `M` 位于
`set_cpu_online(cpu, true)` 之后，`N` 位于 `complete(&cpu_running)` 之后。因此本轮已经
实证 CPU1 不仅到达 C 函数，而且完成 CPU online 位图、CPU 信息、IPI setup 和对 CPU0
`__cpu_up()` 等待者的 completion。此前关于 OP-TEE PSCI CPU_ON return、Linux entry、
次核页表与 C 初始化早段的假设不再是当前首要问题。

新的最小扩展不改变 PSCI、DT、rootfs 或 eMMC 内容：CPU1 在 `N` 后、`local_irq_enable()`
前后依次写 `O/P`，`local_fiq_enable()` 后写 `Q`，`local_abt_enable()` 后写 `R`，然后才
进入 `cpu_startup_entry(CPUHP_AP_ONLINE_IDLE)`。CPU0 在 `wait_for_completion_timeout()` 返回后
写小写 `o`，在清除 `secondary_data` 前写小写 `p`。这可将下一次的范围区分为“CPU1 IRQ
开启/idle 路径”与“CPU0 completion 后续路径”。`smp.o` 反汇编确认 `N/O/P/Q/R` 对应 ASCII
`0x4e`–`0x52`，`o/p` 对应 `0x6f/0x70`，均写入虚拟 DEBUG_LL UART。

新候选仍为 10,162,688 B，SHA-256 曾更新为
`022b226dba191770b2c6eeafc2b455313dcfd9e84c2f834f6ecf80cdf508697b`；重新生成的
`patches/linux-5.10.262/0001-arm-phicomm-r1-secondary-uart-trace.patch` 已通过 reverse
apply check。下一次仍只经 U-Boot `loady` 传入 RAM 后 `bootz`，没有 eMMC 写入。

### CPU1 到达 idle；CPU0 PSCI CPU_ON return 追踪（2026-08-08）

实机运行含 `O/P/Q/R` 的候选，输出扩展为连续 `ABCDEFGHIJKLMNOPQR`。`R` 是
`local_abt_enable()` 返回后、调用 `cpu_startup_entry(CPUHP_AP_ONLINE_IDLE)` 前的路标，
所以 CPU1 已完成 online、completion、IRQ/FIQ/abort 开启，并已交给通用 idle 路径。现有
CPU0 `o/p` 没有出现；这不足以直接证明其冻结（必须以完整尾部日志复核），但把下一个
最小疑点定位为 CPU0 发起 PSCI CPU_ON 后是否返回到 `__cpu_up()`。

为消除“CPU0 卡在 SMC 内”与“SMC 已返回但 completion wait/后续路径异常”的歧义，新增六个
小写 CPU0 路标：`a` 在调用 platform `smp_boot_secondary` 前，`s` 在
`psci_ops.cpu_on()` 前，`t` 在它返回后，`b` 在 platform callback 返回后，`o` 在
`wait_for_completion_timeout()` 返回后，`p` 在清除 `secondary_data` 前。由于 `s/t` 直接
夹住 Linux 的 PSCI SMC wrapper，若实机只见 `...as` 后 CPU1 的 `A`–`R` 而始终没有 `t`，
即可实证 CPU0 没有从 OP-TEE CPU_ON 调用返回；若见 `t/b/o/p`，则 OP-TEE 返回正常，需转查
CPU0 后续 hotplug/调度路径。

该候选重新构建成功，大小仍 10,162,688 B，SHA-256 为
`babacc4ec7835c00e9baa8e63f38857ae109f9ee5d47898a86c5dcfc49822bb8`。它仍是纯 RAM 的
`loady`/`bootz` 测试，不修改 eMMC；补丁 reverse apply 和工作树 whitespace 检查均通过。

### PSCI CPU_ON 已返回；隔离 CPU0 completion wakeup（2026-08-08）

实机完整次核相关输出为：

```text
asD/TC:0   psci_cpu_on:278 core_id: 1
tbD/TC:1   init_secondary_helper:1133 Secondary CPU Switching to normal world boot
ABCDEFGHIJKLMNOPQR
```

`s/t` 直接夹住 Linux `psci_ops.cpu_on()`，所以 `t` 是 CPU0 从 SMC 返回的实机证据；`b`
说明 PSCI platform callback 也已返回。CPU1 随后跑到 `N`（`complete(&cpu_running)` 后）和
`R`（调用通用 idle 前）。没有 `o/p` 表明 CPU0 在 `wait_for_completion_timeout()` 内未返回，
包括预期的一秒超时；这已排除“OP-TEE 把 CPU0 留在 CPU_ON SMC 中”。

为测试 CPU0 的 sleep/wakeup/IPI/调度路径是否是新的停点，追踪配置下将该等待临时改为：

```c
r1_secondary_trace('w');
while (!try_wait_for_completion(&cpu_running))
	cpu_relax();
r1_secondary_trace('x');
```

它仍等待同一个 CPU1 `complete()`，但 CPU0 不再进入 scheduler，也不依赖 wakeup IPI 或 timeout
timer。`x` 代表 CPU0 观察到 completion，随后应有 `o/p` 和正常 Linux 启动输出；若仍只有 `w`
而没有 `x`，则 completion 的跨核可见性/一致性是直接嫌疑。此改动仅在
`CONFIG_PHICOMM_R1_SECONDARY_UART_TRACE=y` 的诊断内核编入，不能作为最终修复。

新 zImage 仍为 10,162,688 B，SHA-256 为
`47549b71e48e268788d01f175ba0a9a184e7b7c1a373f785d872a768decb59ba`；构建、补丁 reverse
apply 和 whitespace 检查均通过。传输方式不变：U-Boot `loady` 到 RAM 后 `bootz`，不写 eMMC。

### 首个 completion 已越过；追踪通用 CPU hotplug idle completion（2026-08-08）

使用 polling 候选后，实机在 CPU1 `A`–`R` 后给出 `xop`，但一分钟内仍没有 shell。`x` 证明
CPU0 已通过 `try_wait_for_completion(&cpu_running)` 观察到 CPU1 的 completion，`o/p` 证明
`__cpu_up()` 已返回；这排除了首个 completion 的跨核可见性问题，也证明先前的停止是 CPU0
睡眠/wakeup 路径而非 U-Boot 传递 kernel/DTB、OP-TEE CPU_ON return 或共享 DDR 可见性。

`__cpu_up()` 返回后，通用 `bringup_wait_for_ap()` 会再次等待 CPU1。在 CPU1 上，
`cpu_startup_entry()` 调用 `cpuhp_online_idle()`，该函数依次 `stop_machine_unpark()`、设置
`CPUHP_AP_ONLINE_IDLE` 并 complete `done_up`；CPU0 收到后才会继续后续 hotplug callbacks 和
打印 `Brought up ...`。当前候选新增：CPU1 `S`（进入 `cpuhp_online_idle`）、`T`
（`stop_machine_unpark` 后）、`U`（写 state 后）、`V`（`done_up` complete 后）；CPU0 `y`
（第二个 wait 前）、`z`（观察到 completion 后）。追踪配置下 CPU0 同样以
`try_wait_for_completion(&st->done_up)` polling 暂代可睡眠等待。

新的纯 RAM 候选 10,162,688 B，SHA-256 为
`196eb2f8e5fa638c80bf62f4631a169bcd27e45d8d33feeb44e7b09ac2de504f`。若见 `S` 无 `T`，停点
在 `stop_machine_unpark`；若见 `V` 却无 `z`，第二 completion 的可见性有问题；若见 `z` 后仍
停，则继续追踪 CPU hotplug callback/调度层。构建、补丁 reverse apply 和 whitespace 检查均通过；
全程没有 eMMC 写入。

### 两个 completion 均成功；追踪 CPU1 hotplug-thread reschedule SGI（2026-08-08）

最新实机路标为 `ABCDEFGHIJKLMNOPQRxopSyTUVz`（并发输出中 `y` 可在 CPU1 `T` 前出现）。这证明
CPU1 已进入 `cpuhp_online_idle()`，完成 `stop_machine_unpark()`、写 `CPUHP_AP_ONLINE_IDLE` 与
`done_up` completion；CPU0 已观察到该 completion。因此 kernel/DTB/OP-TEE、两个跨核 completion
及其可见性均已排除，但系统一分钟内仍未进入 shell。

`z` 后 CPU0 会调用 `kthread_unpark(st->thread)` 激活绑定 CPU1 的 `cpuhp/1`，随后让它执行
online hotplug callbacks。CPU1 此时在 idle，唤醒它依赖 reschedule SGI；这正是前两次可睡眠
completion 均无法唤醒 CPU0 的同类机制。新候选以 `0/1/2/3` 标记 CPU0 的 online check、
`kthread_unpark` 前后和 callback-kick 前，并在 ARM IPI 代码中以 `i/j/k` 标记“向 CPU1 发
reschedule SGI / CPU1 IRQ handler 进入 / scheduler_ipi 返回”。若见 `i` 没有 `j`，SGI 未送达
CPU1；若有 `j/k` 但没有 CPU0 后续 `2/3`，停在 unpark 同步；若都有，继续查 cpuhp thread 回调。

新候选大小 10,162,688 B，SHA-256
`7cf6ee88e81e465a9a85ca731f64c3fdac2b8f9042b02de73791ba0e8db539a9`。构建、补丁 reverse apply
与 whitespace 检查均通过；仍只通过 U-Boot `loady`/`bootz` 在 RAM 中测试。

### clangd 双内核源码浏览环境（2026-08-09）

新增 `scripts/generate-clangd.sh`，从各 Linux `O=` 输出树已生成的 `.cmd` 文件构造 clangd
数据库，不运行 `make`、不修改内核源码、不会访问设备：

```sh
scripts/generate-clangd.sh all
```

生成 `build/kernel/compile_commands.json`（Linux 6.18.42）和
`build/kernel-5.10/compile_commands.json`（Linux 5.10.262）。两个源码根各有本地 `.clangd`，
分别指向相邻输出目录并设 `/usr/bin/arm-none-eabi-gcc`。实测对两边
`arch/arm/kernel/smp.c` 执行 `clangd --check` 都得到 `Compile command from CDB` 与
`--target=arm-none-eabi`。`scripts/build-kernel.sh` 现在会在每次成功构建后自动刷新对应数据
库，传 `GENERATE_COMPILE_COMMANDS=0` 可跳过。

Linux 6.18.42 仍是项目的主线目标；5.10.262 只用于证明 SMP 停止不是 6.18 特有回归及承载
当前 UART 路标诊断。两套数据库并存，避免阅读或修改错版本。

### clangd 调用层级文本导出（2026-08-09）

为避免编辑器 Call Hierarchy UI 无法复制到调试记录，新增
`scripts/clangd-call-tree.py <source> <symbol> --direction incoming|outgoing --depth N`。脚本直接
作为 JSON-RPC LSP client 查询 clangd，而不是用 grep/cflow 猜宏展开；对 incoming 查询会先解析
包含当前函数名的候选调用文件，并随递归层级继续解析上游候选，补足 clangd 单文件索引无法找出
跨 translation-unit caller 的限制。

实测：

```sh
scripts/clangd-call-tree.py build/kernel-src/kernel/cpu.c \
  bringup_nonboot_cpus --direction incoming --depth 5
```

输出的 6.18 静态调用链为：

```text
bringup_nonboot_cpus
└─ smp_init
   └─ kernel_init_freeable
      └─ kernel_init
         └─ rest_init
            └─ start_kernel
```

该结果可直接重定向为 Markdown 后引用；它只描述 clangd 可解析的静态直接调用关系，不等价于
运行时 stack trace，函数指针、汇编跳转和条件路径须继续从源码或实机路标核对。

### 一键 RAM-only 开源 OP-TEE → U-Boot 启动器（2026-08-08）

为消除人工执行 `db`、关闭/重开串口终端和发送 FIT 之间的竞态，新增
`scripts/boot-r1-optee-uboot.py`，以及 bridge 的 `--start-command` 机制。调用为：

```sh
sudo python3 scripts/boot-r1-optee-uboot.py --port /dev/ttyUSB0
```

执行顺序固定为：bridge 先以 1500000 8N1、无流控独占 USB-TTL；随后才运行
`rkdeveloptool/rkdeveloptool db build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-rxfast-loader.bin`；
bridge 从同一串口日志捕获 SPL 的 YMODEM CRC 请求 `C` 后，才运行 `sb -k -vv`，并保留此前
验证所需的每 byte 50 µs 节流。默认 loader 和 FIT 分别强制核对 SHA-256
`7220f8b7508cca136d87ba33098b8bf2851d9a1343a5442568ce2a8414aeda3f` 与
`5687f549a82d3f2e0b51fe064df05a4b623ba180872c581132e6ecbe6a49cd84`；若本地重建了默认
名字的产物，必须显式传 `--skip-default-hash-check` 才会继续，避免误传未验证镜像。

脚本源代码只构造 `rkdeveloptool db <loader>`，没有 `UL`、`WL`、`EF`、`GPT`、`PRM` 或任何
U-Boot 存储写命令；仍要求设备处于真 MaskROM，而不是普通 Rockusb Loader。离线验证已执行
`python3 -m py_compile scripts/ymodem-serial-bridge.py scripts/boot-r1-optee-uboot.py`、两者
`--help`，并重新计算两个默认产物的 SHA-256，均通过。尚待下一次实机使用该封装流程验证。

### GIC DT A/B 与 SGI 路标候选（2026-08-09）

实机在原厂两窗口 `arm,cortex-a15-gic` DT 与仅改为上游四窗口
`arm,gic-400` 的 DT 下均停在 `ABCDEFGHIJKLMNOPQRxopSyTUVz0123`。因此 GIC compatible
字符串和 DT 的寄存器窗口描述不解释该停点；保持后者作为后续候选 DT。

下一诊断内核在 CPU0 `__cpuhp_kick_ap()` 中以 `4`–`8` 包住 `should_run`、
`wake_up_process()` 和其后的 completion wait；GICv2 `gic_ipi_send_mask()` 向 CPU1 写
`GICD_SGIR` 前后输出 `d/e`；CPU1 任意 IPI handler 入口/出口输出 `h/l`；CPU1
`cpuhp/1` thread function 入口/完成输出 `W/X`。这可以区分“唤醒函数本身停止”、
“GICD 未写 SGI”、“SGI 没到 CPU1”以及“CPU1 已收到中断却没执行 hotplug thread”。

候选 `zImage-5.10-psci-v1-gic400-sgi-trace` 构建成功，SHA-256 为
`ba6c79afc2f3672f48fe3badd4d795d722930fe7b4398b33e889fc869b8a1372`。其补丁
`0002-arm-phicomm-r1-cpuhp-gic-sgi-trace.patch` 已通过 reverse-apply check；所有载荷仍只经
U-Boot `loady` 放入 RAM，不写 eMMC。

### 5.10 双核启动：kthread 轮询诊断回归已撤回（2026-08-10）

在开源 OP-TEE → U-Boot proper 链上，带 GIC/CPU-hotplug 路标的 5.10 内核已实机证明：CPU1
收到 PSCI `CPU_ON`、进入 normal world、收到并处理 reschedule SGI，且 CPU0 已打印
`SMP: Total of 2 processors activated`。`smp_init()`、`sched_init_smp()`、页分配初始化和
`cpuset_init_smp()` 也都返回；路标随后定位至 `driver_init()` 中 `devtmpfs_init()` 的
`kthread_run()`。这是实机观察，尚不是最终根因判定。

为验证该处等待，曾把 `kernel/kthread.c::__kthread_create_on_node()` 的
`wait_for_completion_killable()` 临时改为 `try_wait_for_completion()` 加 `cpu_relax()` 轮询，并以
`M/N/O` 记录唤醒/完成。实机只输出到 CPU0 枚举后的 `MN`，没有进入
`smp: Bringing up secondary CPUs ...`。这是确定的诊断补丁回归：首次创建 `kthreadd` 时仍处在单核
启动阶段，CPU0 忙等不会调用调度器，因此 `kthreadd` 无法被运行，也永远不可能完成创建请求。
该轮询改动已完全撤回；它不构成 PSCI、GIC、设备树或 OP-TEE 的失败证据。

撤回后以如下命令重建冻结的 RAM-only 内核：

```sh
KERNEL_SRC=build/kernel-src-5.10 KERNEL_BUILD=build/kernel-5.10 \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dts \
GENERATE_COMPILE_COMMANDS=0 scripts/build-kernel.sh
cp build/artifacts/zImage \
  build/artifacts/zImage-5.10-psci-v1-gic400-devtmpfs-trace-revert
```

产物大小为 10,162,688 B，SHA-256 为
`d08fd99477fdb0cfd6dfb6414c42e8bbc1d5603f64a7b63316c7975b53ad5a32`，U-Boot `crc32`
预期为 `310a09bb`。构建完成且源码/仓库的 `git diff --check` 通过。下一步只能在恢复正常可睡眠
等待的前提下继续跟踪 `devtmpfs` 的 kthread 创建与调度，不能再对通用 kthread 创建路径做无条件忙等。

随后实机以该撤回版复测，重新得到 CPU1 的 `CPU_ON`/normal-world return、`W...Y` hotplug
路标、`SMP: Total of 2 processors activated` 和 `CPU: All CPU(s) started in SVC mode.`，末尾仍为
`aDEidehjkl`。这重复确认正常路径在 `devtmpfs_init()` 内 `kthread_run(devtmpfsd, ...,
"kdevtmpfs")` 返回之前停止，且不受前一次错误轮询补丁影响。

下一候选只跟踪该命名为 `kdevtmpfs` 的创建请求，保持所有 completion 为标准可睡眠等待：`M/N` 为
请求入队及唤醒 `kthreadd`，`O/P` 为 `kthreadd` 取出请求并从 `kernel_thread()` 返回，`Q/R/S` 为
子 kthread 入口、分配 `struct kthread`、执行创建 completion，`T` 为 CPU0 从创建 completion
返回，随后原有 `F` 才表示 `kthread_run()` 返回。`devtmpfs` 的 `setup_done` 等待也已从遗留的
busy loop 恢复为 `wait_for_completion()`，避免越过创建点后产生第二个单核忙等。

该候选是 `build/artifacts/zImage-5.10-psci-v1-gic400-kdevtmpfs-create-trace-v2`，大小
10,162,688 B，SHA-256
`bc405e07b076aba3745cd4ac534dbb776d8df259b5aed36ca206ad364c3cbd0b`，U-Boot `crc32` 为
`f3c5a258`。构建和两个 `git diff --check` 均通过；尚待实机验证。

### YMODEM 完成后立即释放串口并发送桌面通知（2026-08-10）

`scripts/ymodem-serial-bridge.py` 原默认在 `sb` 退出后继续占用物理串口 45 秒，
`scripts/boot-r1-optee-uboot.py` 又显式传入 30 秒，因此即使 U-Boot 已显示 `=>`，后续串口工具
也要等待观察窗口结束。两者的 `--monitor-seconds` 默认值现均改为 `0`：YMODEM sender 退出后
立即离开 serial context、关闭 fd，再发出完成通知。仍可显式传 `--monitor-seconds 30` 恢复
有界日志观察。

新增 `scripts/desktop_notify.py`，使用 freedesktop `notify-send`。脚本经 `sudo` 运行时从
`SUDO_USER` 解析实际桌面用户，以 `runuser` 切回该用户并连接
`/run/user/<uid>/bus`，适配 Fedora 44 + niri + DMS；通知失败只写 stderr，不改变下载退出码。
独立 bridge 默认自行通知；一键 U-Boot 脚本令内部 bridge 使用 `--no-notify`，再由外层脚本只弹
一次带完整流程语义的通知。两者均提供 `--no-notify`。

`python3 -m py_compile`、两个脚本的 `--help` 和 `git diff --check` 均通过。沙箱内直接连接桌面
D-Bus 被拒绝；获准在沙箱外运行同一 `notify-send` 后返回状态 0，验证当前桌面通知链路可达。

### `kdevtmpfs` 子线程已执行至创建 completion（2026-08-10）

`kdevtmpfs-create-trace-v2` 实机末尾为：

```text
aDEpMNOidePhjklQRS
```

其中 `p/M/N/O/P/Q/R/S` 证明 CPU0 已进入 `kthread_run()`、创建请求已入队、`kthreadd` 已取出
请求且 `kernel_thread()` 返回，新 kthread 也已执行到 `complete(done)` 调用前。没有 `T` 表明创建者
仍未从 `wait_for_completion_killable()` 返回。穿插的 `i/d/e/h/j/k/l` 是既有的 CPU0→CPU1
reschedule SGI 发送、GIC 写入和 CPU1 handler 路标，所以它只验证该方向，不能证明反向
CPU1→CPU0 IPI 正常。

下一候选在 `complete(done)` 返回后增加 `U`，并给 devtmpfs 调用者、`kthreadd`、新 kthread
附加 `0/1` CPU 编号。它还跟踪发往 CPU0 的 call-function IPI：`m/n` 包住 ARM
`arch_send_call_function_single_ipi(0)`，`D/E` 包住 GICD_SGIR 写入，CPU0 handler 用
`H...L` 包住处理过程；reschedule handler 内为 `J/K`，call-function handler 内为 `C/c`。
这些都是路标，不改变 completion 或调度逻辑。

产物为 `build/artifacts/zImage-5.10-psci-v1-gic400-kdevtmpfs-cpu0-ipi-trace-v3`，大小
10,162,688 B，SHA-256
`b115261f40db73b3a36625875d7ab649a523a10a874c7cd7492c21a1fc973840`，U-Boot `crc32` 为
`ffdbaa39`。构建及源码/仓库 `git diff --check` 均通过；待 RAM-only 实机测试。

### 当前 YMODEM 链上的 Rockchip TEE v2.00 严格 A/B（2026-08-10）

鉴于当前停点疑似 CPU1→CPU0 completion/IPI 唤醒方向异常，重新建立 TEE-only A/B。Rockchip
官方 `rockchip-linux/rkbin` 当前 master 的 `bin/rk32/` 目录对 RK322x 仍只提供
`rk322x_tee_v2.00.bin`（Rockchip 发布编号，内部版本 `1.0.1-86-g31e775b`，构建日期
2019-01-31），没有比 v2.00 更新的 RK322x TEE。该固件是 Rockchip 专有 OP-TEE 派生二进制，
不能称为“最新版开源 OP-TEE”。来源：Rockchip 维护的 rkbin master：
<https://github.com/rockchip-linux/rkbin/tree/master/bin/rk32>；精确 blob：
<https://github.com/rockchip-linux/rkbin/blob/master/bin/rk32/rk322x_tee_v2.00.bin>。

从官方 raw master 临时下载的当前文件与本地 `build/tee/rk322x_tee_v2.00.bin` 逐字节相同，均为
333,896 B、SHA-256
`a568cba05d073fc6192bcbf43b8128f2601e34fbc310e94eb85c32294b6c36f6`。新增
`scripts/r1-ymodem-fit-dtb-rk-v2.00.its`，用当前已实机工作的 YMODEM FIT 布局生成 B 线：

```text
A: U-Boot 371360 B + open OP-TEE 423248 B + U-Boot DTB 25752 B
B: U-Boot 371360 B + RK TEE v2.00 333896 B + U-Boot DTB 25752 B
```

`dumpimage` 分别抽取 B 线三项后，U-Boot、TEE、FDT 均与各自指定源文件 `cmp` 相同；load/entry
仍为 `0x61000000`/`0x68400000`，configuration 仍为 `firmware=op-tee`、
`loadables=u-boot`、`fdt=fdt-1`。因此 A/B 除 TEE payload 外不改变 loader、U-Boot、U-Boot
DTB、Linux、Linux DTB、initramfs 或地址布局。

B 线产物 `build/artifacts/r1-ymodem-fit-dtb-rk-v2.00.itb` 为 732,994 B，SHA-256
`4ebc55f53e998d85da7dd6d812935dd87818c6953eb7dea996fa4b882019ab7e`；该哈希已加入
`scripts/boot-r1-optee-uboot.py` 的强制校验表。全程仍只执行 RAM `rkdeveloptool db` 和 UART
YMODEM，不写 eMMC。尚待实机 B 线启动及同一 Linux v3 路标对比。

紧接 B 线测试请求后，用户回传的 v3 末尾为：

```text
aDE0pM0NO0idePhjklQ1RSmDEnU
```

按测试顺序将其作为 Rockchip TEE v2.00 B 线结果（该截取片段自身没有 TEE version 行，若实际
仍为 A 线需更正 provenance）。`0p/M0/O0` 表明 init 创建者与 `kthreadd` 在 CPU0；`Q1` 表明
新 kthread 在 CPU1。`S...U` 证明 CPU1 的 `complete(done)` 已完整返回；中间
`m/D/E/n` 证明内核选择 CPU1→CPU0 call-function IPI，进入 ARM sender、执行并返回
GICD_SGIR 写入。此后没有 CPU0 handler 的 `H`，也没有等待者返回的 `T`。所以当前直接停点是
CPU1 已发出而 CPU0 未处理 IPI，不是 kthread 创建或 completion 本身。

若该 provenance 确认为 B 线，则开源 OP-TEE 3.7 与 Rockchip TEE v2.00 在同一停点复现，排除
“开源 OP-TEE 3.7 独有实现错误”，但仍不能排除两者共享的 RK322x secure-world 初始化假设。

下一 A/B 仅在 `CONFIG_PHICOMM_R1_SECONDARY_UART_TRACE` 诊断内核中，把“CPU1 向 CPU0 发送
IPI”由 GICv2 target-list 模式临时改为 `TargetListFilter=1`（发送给请求者之外所有 PE）。当前
只启用 CPU0/CPU1，因此从 CPU1 发出只应命中 CPU0，绕过 `gic_cpu_map[0]`。路标 `DxxfE`
中的 `xx` 是原 target-list map 十六进制值，`f` 表示实际采用 filter 写入。其他方向和其他 IPI
保持原实现。

候选 `build/artifacts/zImage-5.10-psci-v1-gic400-cpu1-to-cpu0-sgi-filter-ab-v4` 为
10,166,784 B，SHA-256
`e33874dfcf8c39352a813c4e5df8c0f5c15e8ede55b68fa7e1ad9b6755875c58`，U-Boot `crc32` 为
`71b38bb3`。反汇编确认写入值含 GICv2 filter bit `0x01000000`；构建及两个工作树的
`git diff --check` 通过。该版本是诊断 A/B，不是最终修复。

### CPU1→CPU0 SGI target-map 排除与 GIC banked 状态快照候选（2026-08-10）

v4 实机末尾为：

```text
aDE0pM0NO0idePhjklQ1RSmD01fEnU
```

`D01fE` 证明正常路径算出的 CPU0 target-list 为 `0x01`，且本次实际使用了
`TargetListFilter=1` 并完成 GICD_SGIR 写入。此后仍没有 CPU0 IPI handler 入口 `H`，也没有
等待者返回 `T`。因此 CPU1→CPU0 故障不是 `gic_cpu_map[0]` 或 SGIR target-list 编码错误；当前
范围收敛到 CPU0 的 SGI 接收侧，包括 banked SGI enable/pending/active、GICC enable/PMR，及
secure firmware 留下的 Group0/Group1 和 SoC 安全路由状态。

当前 U-Boot 配置的 `# CONFIG_IRQ is not set` 已核实，现代 U-Boot 本身不负责初始化 GIC。
Linux 5.10 的 GICv2 驱动会初始化 distributor、每 CPU interface、SGI/PPI enable 和 priority，
但没有写 `GICD_IGROUPR0`；TrustZone 中断安全分组仍属于固件契约。旧厂商链能运行双向 IPI
约 30 秒，只能证明旧 U-Boot/Trust OS 组合建立了可用状态，尚不能把责任单独归于旧 U-Boot。

下一候选在 primary GIC 的每 CPU 初始化前后输出只读快照：

```text
G[P|A][cpu]D<gicd_ctlr>I<igroupr0>E<isenabler0>P<ispendr0>A<isactiver0>C<gicc_ctlr>M<gicc_pmr>;
```

`P/A` 分别表示 Linux 每 CPU GIC 初始化前/后。CPU0 的 `P` 已发生在 distributor 全局初始化
之后，但该路径不写 `IGROUPR0`，且尚未清理 CPU0 banked SGI/PPI；CPU1 的 `P` 则位于其次核
GIC 初始化入口。用同一 zImage 配合各自 PSCI DTB在旧厂商链和当前链对照，可直接比较固件
遗留的 normal-world 可见状态。

候选 `build/artifacts/zImage-5.10-psci-v1-gic400-register-snapshot-v5` 为 10,166,784 B，
SHA-256 `5ebe13a90b52f104a45e3822506b1e97a71da376be2732a67af238dedf638bcd`，U-Boot/主机
`crc32` 为 `f118e436`。构建、反汇编确认快照调用和两个工作树的 `diff --check` 均通过；待
RAM-only 实机测试，不是最终修复。

v5 完整日志截取到 CPU0 与 CPU1 初始化前快照开头：

```text
GP0D00000001I00000000E000000ffP00000000A00000000C00000001M000...
GP1D00000001I00000000E000000ffP00000000A00000000C00000001M000...
```

可确定 CPU0/CPU1 pre-init 的 normal-world 可见值相同：GICD_CTLR=`1`、IGROUPR0=`0`、
ISENABLER0=`0xff`、pending/active=`0`、GICC_CTLR=`1`。这排除 CPU0 从一开始 GICC 未使能，
也排除两核在这些可见 banked enable/pending/active 值上的初始差异；PMR 和两个 post-init 快照
被其他 CPU 的逐字符 UART 路标穿插或覆盖，不能可靠解码。
IGROUPR0=`0` 本身也不能直接证明所有 SGI 属于 secure group，因为 non-secure 受限读取可能
RAZ，且 CPU1 已实证能接收 CPU0 的 SGI。为避免再次得到半截寄存器值，v6 将快照改为单次
`pr_emerg()` 格式化输出，由 printk 串行化；既有短路标与 v4 filter 行为保持不变。

v6 产物 `build/artifacts/zImage-5.10-psci-v1-gic400-register-printk-v6` 为 10,166,784 B，
SHA-256 `33284f675c48e0e7753163829a443ee5d0ac44d00682b7de627e812032b7ab2c`，U-Boot/主机
`crc32` 为 `b53f1e7a`。反汇编确认 `gic_cpu_init()` 前后均调用 snapshot 且 snapshot 调用
`printk`；构建与两个工作树 `diff --check` 均通过，待 RAM-only 实机测试。

v6 实机得到四组完整快照：

```text
R1GIC P cpu0: D=00000001 I=00000000 E=000000ff P=00000000 A=00000000 C=00000001 M=00000000
R1GIC A cpu0: D=00000001 I=00000000 E=000000ff P=00000000 A=00000000 C=00000001 M=000000f0
R1GIC P cpu1: D=00000001 I=00000000 E=000000ff P=00000000 A=00000000 C=00000001 M=00000000
R1GIC A cpu1: D=00000001 I=00000000 E=000000ff P=00000000 A=00000000 C=00000001 M=000000f0
```

CPU0/CPU1 的 normal-world 可见 pre/post 状态逐字段相同；Linux 每 CPU 初始化在这些字段中唯一
可见变化是把 GICC_PMR 从 `0` 设为 `0xf0`。因此当前故障不是 DT 指向错误 GIC window、某核
GICC_CTLR 未使能、某核 SGI enable 缺失、可见 pending/active 残留，或 Linux 两核执行出不同
的 `gic_cpu_init()` 结果。结合 v4 filter A/B，下一检查点应是 CPU0 调用 PSCI_CPU_ON 前后以及
进入 kdevtmpfs completion 等待前的本地 IRQ/CPSR 状态；若这些正常，则首要嫌疑转为
normal world 读不到的 banked secure Group0/Group1 或 RK322x 安全 IRQ 路由。

### PSCI_CPU_ON 前后与 completion 等待前 GIC/CPSR 快照 v7（2026-08-10）

在 v6 基础上新增三个 CPU0 runtime 快照：`B` 位于 `psci_ops.cpu_on()` 调用前，`R` 位于
SMC 返回后，`W` 位于 kdevtmpfs 创建者进入 `wait_for_completion_killable()` 前。所有快照
附加读取 GICC_HPPIR (`H`)、GICC_RPR (`R`) 和当前 CPSR (`X`)；_HPPIR 是只读的最高优先级
pending 查询，不会像 IAR 一样 acknowledge/消费中断。既有 v4 TargetListFilter A/B 和其他
路标不变。

候选 `build/artifacts/zImage-5.10-psci-v1-gic400-psci-wait-state-v7` 为 10,166,784 B，
SHA-256 `533f4c8b2f2354b6ff3425cae92eaac40ba0a5f0d46389f2c954d9f3371fe3bb`，U-Boot/主机
`crc32` 为 `712226dd`。构建、两个工作树 `diff --check` 均通过；`nm/objdump` 确认最终
`vmlinux` 含 `r1_gic_trace_runtime`，PSCI 路径有两次调用，kthread 路径有一次调用。该候选
仅增加只读诊断，不写 eMMC，待 RAM-only 实机测试。

v7 实机关键值为：

```text
R1GIC B cpu0: E=600000ff P=40000000 C=00000001 M=000000f0 H=0000001e R=00000000 X=60000053
R1GIC R cpu0: E=600000ff P=40000000 C=00000001 M=000000f0 H=0000001e R=00000000 X=60000053
R1GIC W cpu0: E=600000ff P=40000000 C=00000001 M=000000f0 H=0000001e R=00000000 X=60000053
```

`E=0x600000ff` 表明 SGI 0–7 及 PPI 29/30 已使能；`P=0x40000000` 表明 PPI 30 pending；
`H=0x1e` 表明 GICC_HPPIR 已把 INTID 30 选为当前最高优先级 pending interrupt；`C=1`、
`M=0xf0` 表明 GICC CPU interface 和 priority mask 允许投递。CPSR `X=0x60000053` 的 I bit
(`0x80`) 为零，CPU0 本地 IRQ 未 mask。该状态从 `PSCI_CPU_ON` 前的 `B` 到返回后的 `R` 再到
completion 等待前的 `W` 完全不变，因此 `PSCI_CPU_ON` 不是破坏 CPU0 可见 GIC 状态的动作。

这是当前最强的实机结论：GIC CPU interface 已有一个对 normal-world 可见且优先级合格的
physical timer PPI 30，但 CPU0 没有进入 IRQ exception/acknowledge；故障位于 GICC 输出到
CPU0 normal-world 异常入口之间，而不是 SGI target、completion、DT GIC window 或 Linux
GIC 初始化。首要嫌疑是 secure monitor 的每核异常路由状态（例如 CPU0 与 CPU1 的 secure
banked Group/IRQ routing handoff 差异）；这类状态不能由 normal world Linux 直接修复或读取。

此外，v7 暴露出决定性的主/次核非对称：CPU0 从最早的 `P/A` 到 `B/R/W`，GICC_RPR 始终为
`0x00`；CPU1 的 `P/A` 则为正常 idle priority `0xff`。RPR=`0x00` 表示 CPU0 的 GIC CPU
interface 仍认为有一个最高优先级 interrupt 正在运行，普通优先级的 PPI 30 和 SGI 无法抢占。
CPU0 的 normal-world `ISACTIVER0=0` 与此并不矛盾：未完成的 active interrupt 或 active
priority 可能属于 secure bank/Group0，普通世界读取不到。因该异常状态在 Linux GIC 初始化
前已经存在，且 Linux 清理 normal-world APR 后仍为 `0x00`，当前最具体的根因假设是 OP-TEE
主核切回 normal world 前没有 EOI/deactivate 某个 secure interrupt，或没有清理 secure APR；
次核路径没有该残留。下一修复应进入 secure-world GIC 主核初始化/交接路径，而不是再改 Linux DT。

同一 v7 随后在严格 B 线 Rockchip RK322x TEE v2.00 上得到相同的主/次核 RPR 非对称：CPU0
从 `P/A` 到 `B/R/W` 均为 `R=0x00`，CPU1 为 `R=0xff`。B 线的 CPU0 还显示
`E=0x400000ff`、`P=0x40000000`，但 HPPIR=`0x3ff`；即 PPI 30 已 enable/pending，却因当前
running priority 已是最高优先级而没有 eligible interrupt。该结果正式排除“开源 OP-TEE
3.7 独有错误”；开源 3.7 与专有 Rockchip v2.00 都没有消除当前 hybrid 链主核的 active
priority 状态。

尚未区分该状态由 vendor DDR/BootROM/现代 SPL 在进入 TEE 前遗留，还是两套同源 RK322x TEE
主核初始化共同产生。下一最小证据应由仍在 secure state 的 SPL 在跳入 TEE 前只读打印
GICC_RPR/APR；若此时 RPR 已为 `0x00`，修复点在前级 loader/SPL 清理，若为 `0xff` 而 Linux
入口变为 `0x00`，修复点在 TEE 主核初始化/normal-world handoff。

### secure SPL 跳入 TEE 前 GIC active-priority 快照候选（2026-08-10）

在当前 RX-fast YMODEM SPL 的 `jump_to_image_optee()` 中加入两次只读 GIC 快照：`GB` 位于
`cleanup_before_linux()` 前，`GA` 位于其后、最终 `R/T` 路标前。两次都运行在进入 TEE 前的
CPU0 secure SPL 上，读取：GICC_RPR、GICC_CTLR、GICC_PMR、secure-view GICD_IGROUPR0、
GICC_APR0–APR3，并按 GICD_TYPER 给出的 implemented group 数扫描所有非零
GICD_ISACTIVERn。输出格式为：

```text
{G[B|A]R<rpr>C<ctlr>M<pmr>I<igroup>A0<apr0>...A3<apr3>V<index><active>...}
```

若所有 ISACTIVERn 为零则输出 `V-`。该探针不读取 GICC_IAR，因而不会 acknowledge 或消费
中断；也不写任何 GIC/eMMC 状态。构建仍使用当前已验证的 vendor DDR 471、RX-fast SPL 与
YMODEM 外置 FIT，`rkdeveloptool pack` 只生成本地容器。

产物 `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-pretee-trace-loader.bin` 为
854,293 B，SHA-256
`05b804d7bfdbd403c187f379dd23e332e808d75e8c0dc99d85c0cccea9f8d810`，主机 CRC32
`f3c48aa7`，pack 保存 CRC `0x3f9d4eb6`。其 472 源为 836,608-byte
`u-boot-rockchip-usb472.bin`，SHA-256
`f649043761afbfd1541dd817595ec01d7271d79b7394d73ec5756721dd19d5a4`。离线 unpack 后，
471/FlashData 的前 7,196 bytes 与已验证 vendor DDR 逐字节相同，472 的前 836,608 bytes 与
本次 binman 产物逐字节相同；padding 大小符合 pack 对齐。SPL `nm/objdump` 确认 snapshot
函数、两个调用及物理 GICD/GICC 地址 `0x32011000/0x32012000` 均进入最终二进制。
一键脚本已加入该候选的强制 SHA-256 校验。

### secure SPL 实机结果与 INTID 55 精确清理 A/B（2026-08-10）

只读探针实机在 `Q` 后得到：

```text
{GBR00000000C00000001M000000f8I00000000A000000001A100000000A200000000A300000000V0100800000}
{GAR00000000C00000001M000000f8I00000000A000000001A100000000A200000000A300000000V0100800000}
```

`GB` 与 `GA` 完全相同，证明 `cleanup_before_linux()` 没有改变该状态。进入 OP-TEE 前 CPU0
已经是 GICC_RPR=`0`、GICC_APR0=`1`；扫描结果 `V01 00800000` 表示唯一 active 位位于
GICD_ISACTIVER1 bit 23，即 INTID `32 + 23 = 55`。这是 R1 实机验证事实，正式排除“该 active
priority 由开源 OP-TEE 主核初始化新产生”。上游 Linux 源码
`arch/arm/boot/dts/rockchip/rk322x.dtsi` 将 `usb@30040000` 配置为 `GIC_SPI 23`，因此
INTID 55 对应 RK322x USB OTG。由 MaskROM/RockUSB USB 下载路径遗留该 active interrupt 是
当前最强推断；旧厂商完整链是否主动清理仍待对照。

基于该唯一签名构建下一 A/B。`r1_spl_gic_cleanup_stale_usb_otg()` 仅在 RPR=`0`、APR0=`1`、
APR1–3=`0` 且所有 active 位只有 INTID 55 时执行；它短暂关闭 GICC，写
GICD_ICENABLER1/ICPENDR1/ICACTIVER1 bit 23，清 GICC_APR0，恢复原 GICC_CTLR 并执行
barrier。若签名不匹配则完全跳过。清理后新增 `GC` 只读快照，预期成功特征为 RPR=`ff`、
APR0=`0`、`V-`。

构建命令：

```sh
make -C build/u-boot CROSS_COMPILE=arm-none-eabi- DTC=/usr/bin/dtc TEE=tee.bin -j8
(cd rkdeveloptool && ./rkdeveloptool pack)
```

产物 `build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-int55-cleanup-ab-loader.bin` 为
854,293 B，SHA-256
`ff47e369966feac248510aaa7577e54484e3ecfb80a53fef0f99d818d087bd50`，主机 CRC32
`7ff26f3d`，pack 保存 CRC `0x27d5d4d8`。472 源为 836,800 B，SHA-256
`329e151de3ef8baa864481267b8fcee24dec569dad9321c05ade38879511bba0`。离线 unpack 后，
471、472、FlashData 的有效字节均与输入逐字节相同，padding 全零；构建、补丁 reverse-apply、
脚本语法与两个工作树的 `diff --check` 均通过。该产物只供 RAM `db`，不含 eMMC 写操作。

### INTID 55 清理实机成功与 clean Linux v8（2026-08-10）

精确清理 loader 实机输出：

```text
{GCR000000ffC00000001M000000f8I00000000A000000000A100000000A200000000A300000000V-}
```

触发前的 `GB/GA` 仍复现 RPR=`0`、APR0=`1`、INTID 55 唯一 active；`GC` 则准确恢复
RPR=`0xff`、所有 APR 为零且无 active interrupt，证明签名匹配、清理写入生效。OP-TEE 3.7
随后正常初始化并进入 U-Boot proper。

同一 v7 Linux 的 CPU0 首次 GIC 快照已经变为 RPR=`0xff`、HPPIR=`0x3ff`，CPU1 拉起后两核
同样为 RPR=`0xff`；此前持续 pending 的 PPI 30 不再出现。CPU1 online、CPU hotplug、
kdevtmpfs kthread 创建与 completion 全部完成，串口明确出现 `devtmpfs: initialized`，并继续
到 VFP、pinctrl、NET、DMA 与 cpuidle 初始化。由此验证旧的 CPU0 IRQ/SGI 死锁由进入 TEE 前
遗留的 INTID 55/APR0 直接造成，不是 Linux DT、PSCI CPU_ON 或开源 OP-TEE 独有错误。

该次 v7 在约 0.051 秒后未继续输出，但它包含累计 436 行的逐字符 UART 路标、polling
completion 和 GIC SGI TargetListFilter A/B，故不能把新边界直接归因于正常内核。保留当前
诊断源码不动，另从原始 Linux 5.10.262 commit
`065a677fad98698de04279ba2cb152a472ab8b1f` 的 clean clone 完整重建。clean v8 使用同一
`rk3229-phicomm-r1-minimal-psci-v1-gic400.dtb` 和 kernel command line，但不含任何
`PHICOMM_R1_SECONDARY_UART_TRACE` symbol、R1 trace symbol/string、polling completion 或
SGI filter 改写。

可复现构建命令（临时 clone 用于隔离现有诊断源码工作树）：

```sh
git clone --shared --no-checkout build/kernel-src-5.10 /tmp/r1-linux-5.10-clean-src
git -C /tmp/r1-linux-5.10-clean-src checkout --detach 065a677fad98
KERNEL_SRC=/tmp/r1-linux-5.10-clean-src \
KERNEL_BUILD=build/kernel-5.10-clean \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10-clean.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dts \
JOBS=8 scripts/build-kernel.sh
cp build/artifacts/zImage \
  build/artifacts/zImage-5.10-psci-v1-gic400-clean-post-int55-v8
```

产物 `build/artifacts/zImage-5.10-psci-v1-gic400-clean-post-int55-v8` 为 10,166,784 B，
SHA-256 `fc5d1e207ffc143c2d34cb59296f0e9b07b3051e7e085404f37873e2d85cd5e7`，CRC32
`a262021a`。配置副本 SHA-256 为
`32fd6d306d45fd7218d37705be7d5b33477d91e8636eda0bc81eff24b3df3e7e`；重新生成的 DTB 与
既有 GIC400 DTB 逐字节一致，SHA-256
`3a8b8685652f39690edc2141454a7f397ced99f4563975a84a1f2c39fd660a12`。构建成功，`nm` 与
`strings` 均确认无 R1 诊断符号/文本，源码 clone 保持 clean；待 RAM-only 实机验证。

### clean v8 双核进入 shell，用户确认约 700 秒（2026-08-10）

clean v8 实机启动日志确认：

```text
Linux version 5.10.262-phicomm-r1 ... #1 SMP
psci: PSCIv1.0 detected in firmware.
smp: Brought up 1 node, 2 CPUs
SMP: Total of 2 processors activated (96.00 BogoMIPS).
[    0.046235] No ATAGs?
[    2.078554] Run /init as init process
Linux (none) 5.10.262-phicomm-r1 #1 SMP ... armv7l GNU/Linux
/bin/sh: can't access tty; job control turned off
#
```

这直接验证实际运行的是 clean v8 而非 `-dirty #24` 的 v7，并证明它正常越过 v7 最后的
`No ATAGs?` 可见位置、完成双核 SMP、运行 `/init` 并进入交互 shell。保存日志
`build/artifacts/clean-v8-open-optee-first-shell-20260810.log` 为 15,438 B，SHA-256
`035930590c099a04285d6ee2db955d156e63e75fc26e6d150eb6097469d960e1`。

用户最初明确报告本次 uptime 已超过 30 秒，随后继续运行并确认已到约 700 秒，远超旧链稳定
复现的约 30 秒冻结边界，满足本轮目标；但附件末尾实际只保存：

```text
# while true; do cat /proc/uptime; sleep 5; done
8.99 15.92
/bin/sh: sleep: not found
```

因此证据分类为：双核进入 shell 与 uptime 8.99 秒由日志直接验证；约 700 秒为用户实机确认。
原因不是系统在 8.99 秒停止，而是 initramfs 没有生成 `sleep` applet 链接。后续可复现的串口
留档命令应为：

```sh
while true; do cat /proc/uptime; /bin/busybox sleep 5; done
```

本轮结论：当前 RAM-only 链的 SMP 冻结根因已定位并修复。MaskROM/RockUSB 遗留 USB OTG
INTID 55 active 与 APR0=`1`，使 CPU0 GICC_RPR 保持最高优先级 `0`，阻止 timer PPI 与
CPU1→CPU0 SGI；SPL 在严格签名匹配时清理该状态后，开源 OP-TEE 3.7、主线 U-Boot 和
clean Linux 5.10.262 双核链完整进入 shell。未执行 eMMC 写入。

### clean v9 四核单变量候选（2026-08-10）

双核已由用户确认运行约 700 秒后，下一 A/B 只撤销内核强制命令行中的 `maxcpus=2`。新增
`kernel/config/r1-5.10-clean-4core.fragment`，继续使用 clean Linux 5.10.262 commit
`065a677fad98698de04279ba2cb152a472ab8b1f`、同一 initramfs 和同一
`rk3229-phicomm-r1-minimal-psci-v1-gic400.dts`。为防止上板拿错镜像，仅把可见版本后缀改为
`-phicomm-r1-4core`。构建命令为：

```sh
KERNEL_SRC=/tmp/r1-linux-5.10-clean-src \
KERNEL_BUILD=build/kernel-5.10-clean-4core \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10-clean-4core.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dts \
JOBS=8 scripts/build-kernel.sh
```

最终配置确认 `CONFIG_SMP=y`、`CONFIG_NR_CPUS=16`、`CONFIG_CMDLINE_FORCE=y`，命令行中没有
`maxcpus`，源码 clone 保持 clean。与成功的双核 v8 `.config` 逐行比较只有
`CONFIG_LOCALVERSION` 和 `CONFIG_CMDLINE` 两处预期差异。新 DTB 的 SHA-256 仍为
`3a8b8685652f39690edc2141454a7f397ced99f4563975a84a1f2c39fd660a12`，与双核 DTB 逐字节
相同。四核 zImage 保存为
`build/artifacts/zImage-5.10-psci-v1-gic400-clean-4core-v9`，大小 10,158,592 B，SHA-256
`5bc8624169e60ef4558cc78cb80ae887a2a4f798c2a824a9afecece29d3c2565`，CRC32
`14f97549`。待 RAM-only 实机验证；成功标志是串口版本明确含 `-4core`、CPU0–CPU3 online、
`SMP: Total of 4 processors activated`、进入 shell并越过旧约 30 秒边界。

### 首次四核测试使用错误默认 SPL，结果作废（2026-08-10）

首次运行四核 v9 时，Linux 版本和强制命令行均确认正确，但在 OP-TEE 输出
`psci_cpu_on: core_id: 1` 与 CPU1 切回 normal world 后停止，连 `CPU1:` 都没有打印。这与
未清理 GIC 时的旧边界完全一致，不是到 CPU2/CPU3 后才出现的新失败。随后审计
`scripts/boot-r1-optee-uboot.py` 发现其默认 `DEFAULT_LOADER` 仍指向旧 RX-fast loader
`r1-phicomm-r1-uboot-spl-ymodem-rxfast-loader.bin`（SHA-256 `7220f8b7...`），而不是使双核
稳定约 700 秒的 INTID55 cleanup loader
`r1-phicomm-r1-uboot-spl-ymodem-gic-int55-cleanup-ab-loader.bin`（SHA-256
`ff47e369966feac248510aaa7577e54484e3ecfb80a53fef0f99d818d087bd50`）。本轮日志又未包含 SPL
的 `GB/GA/GC`，因此此前无法从 Linux 段直接看出 loader 拿错。

该首次四核结果作废，不能作为四核或 OP-TEE 多核能力的反证。一键脚本默认 loader 已改为
上述 cleanup 产物，旧 RX-fast 仍可由显式 `--loader` 选择；默认和显式已知产物继续执行
SHA-256 强校验。下一步须从 MaskROM 重新运行修正后的一键命令，并在 SPL 日志确认出现
`{GCR000000ff...V-}` 后再加载同一四核 zImage/DTB。

### cleanup SPL 下 clean v9 四核实机成功（2026-08-10）

使用修正后的默认 cleanup loader 重测同一四核 v9 后进入救援 shell。用户提供的串口输出直接
确认 `/sys/devices/system/cpu/online` 和 `present` 均为 `0-3`，`/proc/cmdline` 中没有
`maxcpus`，uptime 从 42.92 秒继续到 72.92 秒。`/proc/interrupts` 中 CPU0–CPU3 的
reschedule IPI 分别为 15/8/24/21，function-call IPI 分别为 60/83/47/40，四颗 CPU 均在实际
处理跨核中断。四核 clean Linux 因而已实机越过旧约 30 秒冻结边界；本轮没有写 eMMC。

执行 `/bin/busybox uptime` 时出现 `ANDROID_DATA not set` 与 `ANDROID_ROOT not set`。本地复核
确认 `scripts/build-initramfs.sh` 默认复制
`backup/unpacked/recovery/ramdisk/sbin/busybox`；它是静态 ARM EABI5 BusyBox 1.22.1，SHA-256
`bf663a6ece662cf387c071950016f9c7a87949510441add4c335e1e08d4703c7`，字符串中明确包含 bionic
libc 源路径以及 Android tzdata 查找逻辑。警告来自该复用工具的时区初始化，不是 Android
内核、init 或文件系统仍在运行。短期可直接读 `/proc/uptime`；正式用户空间应改用独立构建的
静态 musl/uClibc BusyBox，并同时补齐 applet 链接和 controlling TTY。

### 从日志反推并构建白名单救援内核 v10（2026-08-10）

四核 v9 已稳定后审计其完整启动日志和最终 `.config`。日志中的大量通用平台、网络、文件系统
和外设初始化不是 U-Boot 临时加给 Linux 的驱动，而是 clean 5.10 构建仍以
`multi_v7_defconfig` 为基线；最终配置共有 3285 个 `y/m` 项，并包含 CAN、NFS、NTFS、PCI、
MTD、DRM、声音及多个非 RK322x 平台。先在该基线上叠加禁用 fragment，zImage 由
10,158,592 B 降到 5,755,168 B，但仍会被 Kconfig 默认值拉入大量无关平台，证明负向黑名单
不适合作为可维护基线。

第一次从 `allnoconfig` 合并旧 fragment 时，Kconfig 合法地选择了 Cortex-M NOMMU，虽然能生成
zImage，却不能用于 RK3229。该产物被判为无效且未上板。随后在
`kernel/config/r1-5.10-rescue-minimal.fragment` 中显式加入 ARMv7-A MMU、multiplatform、
multi-v7、Rockchip 和四核约束，并保留 UART2、GIC/arch timer、gzip initramfs、eMMC、ext4、
USB、IPv4/IPv6 与 ramoops。`scripts/build-kernel.sh` 新增可选的 `KERNEL_DEFCONFIG`，最终重建命令：

```sh
KERNEL_SRC=/tmp/r1-linux-5.10-clean-src \
KERNEL_BUILD=build/kernel-5.10-rescue-whitelist-v3 \
KERNEL_DEFCONFIG=allnoconfig \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10-rescue-minimal.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dts \
JOBS=8 GENERATE_COMPILE_COMMANDS=0 scripts/build-kernel.sh
```

最终配置有 447 个 `y/m` 项，确认 `CONFIG_SMP=y`、`CONFIG_NR_CPUS=4`、
`CONFIG_ARM_GIC=y`、`CONFIG_ARM_ARCH_TIMER=y`、`CONFIG_MMC_DW_ROCKCHIP=y`、
`CONFIG_PSTORE_RAM=y`；CAN、NFS/NFSD、NTFS、CIFS/9P、PCI、MTD、UBIFS、SquashFS、DRM/fb、
声音、媒体、无线、模块、swap、SCSI 与 gzip 以外的 initrd 解压器均未启用。zImage 为
2,328,112 B，vmlinux 为 5,367,112 B。固定候选与 SHA-256：

```text
e17c84d45130124a2c453be8f5b2bd7f46238bf0dd9744106c27cf1979e95333  build/artifacts/zImage-5.10-r1-rescue-minimal-4core-v10
bcbcd634e7216e03416c805375cdda3947f0ea8cbc503c7b33b565b5799eb238  build/artifacts/kernel-5.10-r1-rescue-minimal-4core-v10.config
3a8b8685652f39690edc2141454a7f397ced99f4563975a84a1f2c39fd660a12  build/artifacts/rk3229-phicomm-r1-minimal-psci-v1-gic400-rescue-v10.dtb
```

这是主机构建与静态配置审计结论，尚未实机验证。下一步只通过现有 RAM-only U-Boot 上传该
zImage/DTB，继续只读使用 recovery initramfs；成功标准为版本后缀 `-minimal`、CPU0–CPU3
online、进入 shell且 `/proc/uptime` 超过 30 秒。当前最小 GIC A/B DT 不含 eMMC/USB 控制器
节点；本轮不能用它验证二者的 probe。ramoops 节点存在，v10 日志已确认它以
`0x7bf00000`、1 MiB 成功注册。

### v10 实机失败审计与基础 ABI v11（2026-08-10）

v10 RAM-only 日志保存为 `build/artifacts/rescue-v10-four-core-20260810.log`。内核成功解压、
初始化 arch timer、ramoops、串口和网络协议栈，但只打印
`smp: Brought up 1 node, 1 CPU`。用户进入 fallback shell 后读取 `/proc/uptime` 和
`/proc/version` 均得到目录不存在。日志中的决定性错误是：

```text
Failed to execute /init (error -8)
Run /bin/sh as init process
```

最终配置复核显示 `CONFIG_PROC_FS=y`、`CONFIG_SYSFS=y`、`CONFIG_DEVTMPFS=y`；所以 `/proc`
不是被裁掉，而是 `CONFIG_BINFMT_SCRIPT` 缺失导致 shebang init 返回 `ENOEXEC`，挂载虚拟文件
系统的脚本从未执行。另有 `CONFIG_SMP=y`/`NR_CPUS=4`，但 `CONFIG_ARM_PSCI` 被关闭，解释了
为何 DT 中四颗 CPU 最终只启动 CPU0。该日志还暴露 `allnoconfig` 默认关闭 futex、POSIX
timers、time32 兼容、epoll 和多个现代 libc/event-loop 基础 ABI；v10 因而判为过度裁剪失败，
不能用来否定此前四核 v9 的结论。

v11 保持 CAN、NFS/NFSD、NTFS、CIFS/9P、PCI、MTD、图形、声音、媒体、无线、模块关闭，补回
脚本执行、ARM PSCI/hotplug、CPU idle/PM、multiuser、SysV IPC、POSIX timers、time32、futex、
epoll/signalfd/timerfd/eventfd、AIO、membarrier/rseq、inotify、RTC 和救援诊断接口。initramfs
同时补齐常用 BusyBox applet 链接，并把 shell 启动改为 `setsid cttyhack`。重新构建仍使用：

```sh
scripts/build-initramfs.sh
KERNEL_SRC=/tmp/r1-linux-5.10-clean-src \
KERNEL_BUILD=build/kernel-5.10-rescue-baseline-v11 \
KERNEL_DEFCONFIG=allnoconfig \
KERNEL_EXTRA_FRAGMENT=kernel/config/r1-5.10-rescue-minimal.fragment \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-minimal-psci-v1-gic400.dts \
JOBS=8 GENERATE_COMPILE_COMMANDS=0 scripts/build-kernel.sh
```

最终启用项 508，zImage 2,721,856 B，新 initramfs 627,135 B（十六进制 `0x991bf`）。固定产物：

```text
997fcf92c97a6f623650daea226df40179684827f1ce3bce89ef3b449037cb7b  build/artifacts/zImage-5.10-r1-rescue-baseline-4core-v11
79d15a1d7daa7aad49f77f3bf2ea60dc08f772cd3d2c1a29384a8b902cd7f06b  build/artifacts/kernel-5.10-r1-rescue-baseline-4core-v11.config
3a8b8685652f39690edc2141454a7f397ced99f4563975a84a1f2c39fd660a12  build/artifacts/rk3229-phicomm-r1-minimal-psci-v1-gic400-rescue-v11.dtb
2a64b30d56816cf43674e02f4509a3efa186b09fe0de815e97e82bd92983a199  build/artifacts/r1-initramfs-rescue-v11.cpio.gz
```

为验证新版 initramfs，下一轮不能再用 `mmc read` 取旧 recovery ramdisk，而要把这 627,135 B
文件 YMODEM 上传到 `0x64000000`，再以 `0x64000000:0x991bf` 启动。该候选尚未上板。

### 内核裁剪方法论与面试材料（2026-08-10）

新增 `docs/acknowledge/kernel-trimming.md`，把 v9 → v10 → v11 从多平台已知基线迁移到
`allnoconfig` 白名单的过程整理为可复用方法。文档区分硬件驱动、固件/SMP、程序格式、基础
用户空间 ABI、虚拟文件系统、时间与诊断能力；记录 `depends on`/`select`、defconfig/
fragment/最终 `.config` 的边界，并提供需求矩阵、KEEP/LATER/DROP 分类、静态门禁、RAM-only
验收、症状到层级映射和 `diffconfig`/`bloat-o-meter` 等命令。

文档没有把 v10 失败隐藏为单纯配置修正，而是作为核心反例：`PROC_FS=y` 不代表 `/init` 已挂载
`/proc`，`SMP=y` 也不代表 PSCI 路径完整。另附一分钟项目叙述、STAR 模板和十个常见面试问题。
通用 Kconfig 与 initramfs 结论链接 Linux kernel maintainers 的官方文档并记录访问日期；R1
数字与结论链接本地最终配置、日志和 journal，未把单板经验提升为未经验证的普遍事实。

### 启动链与 GICv2 调试专题材料（2026-08-10）

新增 `docs/acknowledge/arm-boot-gicv2.md`。文档将通用启动阶段与 R1 的具体 Rockchip 链分开：
BootROM、TPL、SPL 和 U-Boot proper 使用上游 U-Boot 定义，R1 日志中的 471/472 只作为当前
Rockchip USB loader 阶段标签，不把二者提升为通用同义词。另说明 FIT 如何装载
`0x68400000` 的 OP-TEE 与 `0x61000000` 的 U-Boot proper，以及 kernel/DTB/initramfs 之外的
cache、CPU world、GIC 和次核状态同样属于启动交接契约。

GICv2 部分覆盖 Distributor/CPU interface、SGI/PPI/SPI、pending/active 状态机、PMR/RPR/
HPPIR/APR 和 TrustZone 可见性。实战部分按真实证据链记录：次核汇编/C 路标证明 CPU1 online，
polling completion 排除共享内存不可见，TargetListFilter A/B 排除 CPU map，Linux 快照发现
CPU0 RPR=`0`，Rockchip TEE v2.00 A/B 排除开源 OP-TEE 3.7 独有问题，最后把探针前移到
secure SPL，在 OP-TEE 前确认唯一 active 的 ISACTIVER1 bit23，即 INTID55。

文档逐项解释最终 cleanup patch 的完整匹配条件和寄存器写入，强调它不是全量 GIC reset；并以
clean v8 双核约 700 秒、clean v9 四核 online/IPI/uptime 作为因果闭环。INTID55 来自
MaskROM/RockUSB USB OTG 路径仍标为强推断，最初遗漏 deactivate 的具体组件仍为待确认。
通用原理引用 Arm GICv2 规范、U-Boot SPL/init/DT/FIT 官方文档和 OP-TEE core architecture，
项目事实链接本地 patch、DT、日志和 journal。文末加入源码阅读入口、一分钟项目叙述、STAR
模板及 15 个常见面试问答。

### 冻结救援核心并生成首个 eMMC 外设候选（2026-08-10）

用户决定不再继续无目标地浏览 `menuconfig`，先维护一条稳定最小救援线，再按 eMMC、USB、
Wi-Fi/BT、音频逐个完善外设。为支持这个工作流，`scripts/build-kernel.sh` 新增按顺序合并
`KERNEL_EXTRA_FRAGMENTS` 和 `KERNEL_ARTIFACT_TAG`，同时保留原单 fragment 接口。带 tag 的
构建会保存 zImage、DTB、最终 `.config` 与预处理 DTS，避免后续串口试验误加载旧产物。

首个外设 fragment 为 `kernel/config/r1-5.10-peripheral-emmc.fragment`。构建命令：

```sh
KERNEL_SRC=/tmp/r1-linux-5.10-clean-src \
KERNEL_BUILD=build/kernel-5.10-peripheral-emmc-a1 \
KERNEL_DEFCONFIG=allnoconfig \
KERNEL_EXTRA_FRAGMENTS="kernel/config/r1-5.10-rescue-minimal.fragment kernel/config/r1-5.10-peripheral-emmc.fragment" \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-emmc-open-optee.dts \
KERNEL_ARTIFACT_TAG=rescue-v11-emmc-a1 \
JOBS=8 GENERATE_COMPILE_COMMANDS=0 scripts/build-kernel.sh
```

主机编译成功。与保存的 v11 最终配置运行 `scripts/diffconfig`，除
`LOCALVERSION="-phicomm-r1-rescue-v11-emmc-a1"` 外没有配置差异。这是预期结果：v11 过渡
白名单已经包含 eMMC、DesignWare/Rockchip MMC、I2C、RK805 MFD/regulator 与 ext4；新的
fragment 先把这些选项的长期所有权从救援核心中显式分离，稍后才能安全瘦身 rescue 层。

新 DT 复用既有 R1 完整板级时钟、pinctrl、RK805 和 eMMC 描述，覆盖 PSCI 为
`arm,psci-1.0`/`arm,psci-0.2`，并禁用 USB PHY/OTG。`fdtget` 静态核对得到：

```text
/psci compatible: arm,psci-1.0 arm,psci-0.2
/mmc@30020000 status: okay
/mmc@30020000 bus-width: 8
/usb@30040000 status: disabled
/syscon@11000000/usb2-phy@760 status: disabled
```

固定产物：

```text
5013b84d149e431f0c2886be8b4d0f8e0aea2e96acdd0de08b058332cb23861b  build/artifacts/zImage-rescue-v11-emmc-a1
7d343820991e8bc7592114c303e7a663b719e5c1015417bd1e044038d30635bc  build/artifacts/rk3229-phicomm-r1-rescue-v11-emmc-a1.dtb
ad7619355dc60a49abca7bd82a417df99cac758ddf311b2d364483f290b8059e  build/artifacts/kernel-rescue-v11-emmc-a1.config
2a64b30d56816cf43674e02f4509a3efa186b09fe0de815e97e82bd92983a199  build/artifacts/r1-initramfs-rescue-v11.cpio.gz
```

以上仅是主机构建与静态审计，不是 R1 实机事实。实机验收必须保持 zImage/initramfs 不变：
A 线配最小 v11 DT，先验证四核、init、虚拟文件系统、TTY 和 uptime；B 线只替换 eMMC DT，
只读检查 Samsung eMMC、`mmcblk0`、boot0/boot1、IPI 与 uptime。不得自动挂载或写 eMMC；
USB 要等 eMMC A/B 单独闭环后再加入。

### `rescue-v11-emmc-a1` 最小 DT A 线通过（2026-08-10）

用户按约定加载 `zImage-rescue-v11-emmc-a1`、`r1-initramfs-rescue-v11.cpio.gz` 和最小
`rk3229-phicomm-r1-minimal-psci-v1-gic400-rescue-v11.dtb`，执行：

```text
bootz 0x62000000 0x64000000:0x991bf 0x65000000
```

进入 shell 后，用户确认 `uname -a`、CPU online/present、`/proc/mounts`、`/proc/uptime`、
间隔 35 秒后的 uptime 以及 CPU/IPI 中断统计均无问题，系统超过 30 秒仍正常。因此 A 线通过：
带 eMMC 候选版本标识的 zImage 在已验证最小 DT 下没有引入内核、SMP、initramfs 或基础 ABI
退化。

证据边界：以上是用户实机确认，尚无本轮完整串口日志；最小 DT 不含 eMMC 节点，因此不能由
A 线声称 eMMC 已工作。下一 B 线必须重新走 RAM-only 链，逐字节复用同一 zImage 和 initramfs，
只把 `0x65000000` 的 DTB 替换为
`rk3229-phicomm-r1-rescue-v11-emmc-a1.dtb`，且只读检查存储。

### eMMC B1 枚举通过，但完整 DT 使次核未 online（2026-08-10）

B1 保持 A 线 zImage/initramfs 不变，只换 eMMC DT。用户实机日志直接确认：

```text
CPU1: failed to come online
CPU2: failed to come online
CPU3: failed to come online
rk808 0-0018: chip id: 0x8050
mmc0: new HS200 MMC card at address 0001
mmcblk0: mmc0:0001 8GME4R 7.28 GiB
mmcblk0boot0: ... 4.00 MiB
mmcblk0boot1: ... 4.00 MiB
mmcblk0rpmb: ... 512 KiB
```

`/dev` 与 `/proc/partitions` 也出现对应 user、boot0、boot1 与 RPMB 设备，`/proc/uptime` 到
31.74 秒。故 eMMC/RK805/HS200 只读枚举阶段已经验证，但 B1 整体失败：CPU online 只有 `0`。
次核超时均发生在 RK805/eMMC probe 之前，不能据此归因于 MMC I/O。

对 A/B DTB 反编译比较后，首先选择 arch timer 做严格单变量 B2。完整 `rk322x.dtsi` 多出
`arm,cpu-registers-not-fw-configured`，Linux 5.10 的 `arm_arch_timer.c` 因而强制选择 secure
physical PPI；arch timer 又在次核 `notify_cpu_starting()`、`set_cpu_online()` 之前执行 hotplug
回调，与“每核启动后 1 秒超时但 online 位未置上”的边界吻合。这仍是待实机验证的推断。

新增 `rk3229-phicomm-r1-emmc-open-optee-timer-minimal.dts`：完整保留 B1 eMMC、CPU OPP/reset/
supply 等描述，只删除该 timer 属性，并把 interrupts 恢复为已验证最小 DT 的 PPI13/PPI14。
生成的 22,557-byte DTB SHA-256 为：

```text
4800850ad50e8109ccd763a245128b4b3f08477cff583e77c932e5dc90625c6b  build/artifacts/rk3229-phicomm-r1-rescue-v11-emmc-a2-timer-minimal.dtb
```

静态 `fdtget` 确认该属性不存在、timer interrupts 为 `1 13 0xf04 1 14 0xf04`，eMMC 仍为
`okay`。下一轮仍不得写或挂载 eMMC。

B1 复位前又保存了针对性诊断。PSCI 日志明确为 `PSCIv1.0`、标准 v0.2 function IDs、SMC
Calling Convention v1.0，故 PSCI binding/conduit 本身正确。中断表关键值为：

```text
25:      0  GIC-0 29 Level arch_timer
26:  20815  GIC-0 30 Level arch_timer
```

即 secure physical timer PPI29 没有产生 normal-world 中断，实际推进 CPU0 的是 non-secure
physical PPI30；这与 B1 属性强制主选 secure PPI29 的不匹配吻合。仍不把相关性提前写成已验证
根因，等待 B2 单变量结果。

### 普通 SPL+ITB 拼接不可行，生成 direct-UART-RX 加速候选（2026-08-10）

用户指出每次从 MaskROM 到 U-Boot proper 的 FIT 串口下载过慢，降低 `tx-gap-us` 不能保持可靠。
重新核对文件布局确认当前 `u-boot-rockchip-usb472.bin` 本来就是 39,558-byte 左右的 SPL 后紧接
FIT；本轮重建前的对应文件为 836,800 B，其中 FIT magic 紧跟在对齐偏移 39,616。历史实机
15 地址地图已经证明，MaskROM `0x472` 路径在运行时只有约 36–40 KiB SPL 窗口可靠，后续区域
是 471 DDR 阶段遗留数据。故再次 `cat`、binman 或 pack 不能使 822 KiB FIT 随 `db` 生效。

当前 YMODEM 首块无节流时稳定只收到 82/83 bytes。即使把 `schedule()` 从每字符入口移到
`!tstc()` 空闲分支，任一瞬时空 FIFO 仍可能进入较慢的通用 console/scheduler 路径，并在
USB-TTL 连续 burst 到来时溢出。新增
`patches/u-boot-phicomm-r1-ymodem-direct-uart-rx.patch`：仅在 R1 SPL 中直接以 32-bit、
shift-2 访问 `0x11030000` 的 LSR/RBR，等待期间不调度；ACK/日志和其他平台路径不变。

使用当前已验证 INTID55 cleanup 工作树重新构建、由独立临时 `config.ini` pack，避免覆盖默认
loader。离线解包确认 7,196-byte vendor 471、836,800-byte 新 472 和 FlashData 前缀分别与
输入一致，尾部填充为零。固定候选：

```text
ee8466d7217c5b9d52990d6c4393d32c4aeb8c63f92cdddfc6b02eb4e06e3015  build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-int55-cleanup-directrx-loader.bin
1bea04018d97eb24a74ce5856ac53dec0844a6ec36d5e88b6749864ef20eb0b1  build/u-boot/u-boot-rockchip-usb472.bin
ecbc3828d9e180add7b539cfb0759bd2d4232b40b52aa2d86e810f650f5707b2  build/u-boot/spl/u-boot-spl.bin
```

该结果仅是主机构建与封装验证。下一实机 A/B 显式选择新 loader 和 `--tx-gap-us 0`；若完整
传入 822,318 B 并到达 U-Boot proper，再测实际秒数并考虑成为默认。若仍在首块 82/83 bytes
失败，下一方向是 SPL DWC2 USB gadget/DFU RAM，而不是继续拼接或写 eMMC。

### B1 完整日志复核、initramfs 修复与 U-Boot DFU RAM 候选（2026-08-10）

用户补交的完整日志版本串仍为 `5.10.262-phicomm-r1-rescue-v11-emmc-a1`。三个次核分别在
1.039、2.080、3.120 秒超时，RK805 与 MMC 从约 3.31 秒才开始 probe；随后 Samsung
`8GME4R`、user area、boot0/boot1 和 RPMB 都成功只读枚举。因此这份日志再次验证 B1 结论，
并非新 B2 实验。B2 仍必须加载 SHA-256 为 `4800850a...` 的 timer-minimal DTB。

同一日志中 `/init` 已由内核正常执行并进入交互 shell，但多行报 `echo: not found`。检查
`scripts/build-initramfs.sh` 发现原厂静态 BusyBox 二进制包含 `echo` 字符串，而生成列表没有
建立 `/bin/echo`。本轮补齐 `echo/printf/test/[/true/false/clear` 链接并重新生成；归档列表已
确认这些入口存在：

```text
31508a74ad5265cf27b8a077f994cf00887f238e2d297d26f7a9198fd8cf3095  build/artifacts/r1-initramfs-rescue-v11-emmc-a2.cpio.gz
```

为使下一轮不再分别下载 kernel、ramdisk、DTB，新增
`scripts/r1-linux-rescue-v11-emmc-a2.its`，其固定载荷与地址为：

```text
kernel   0x62000000  2721840 B  SHA-256 5013b84d...
ramdisk  0x64000000   627218 B  SHA-256 31508a74...
fdt      0x65000000    22557 B  SHA-256 4800850a...
FIT      0x6a800000  3373032 B  SHA-256 22555b88192f8fc6c6096003b8b4dde71457134004a66e729d66b48216957c94
```

`dumpimage -p 0/1/2` 解出的三个 payload 已分别与源 zImage、initramfs、B2 DTB 执行 `cmp`，
全部一致。`0x6a800000` 位于 Linux 日志中 OP-TEE 保留缺口 `0x68400000..0x6a3fffff` 之后，
且不覆盖 U-Boot proper、kernel、ramdisk 或 FDT 地址。

U-Boot proper 侧新增 `patches/u-boot-phicomm-r1-usb-dfu-ram.patch`，启用 RK322x DWC2 OTG
peripheral、`CMD_DFU` 与 `DFU_RAM`。最终配置的安全审计结果是：

```text
CONFIG_CMD_DFU=y
CONFIG_DFU_RAM=y
# CONFIG_DFU_MMC is not set
# CONFIG_USB_FUNCTION_FASTBOOT is not set
# CONFIG_USB_FUNCTION_MASS_STORAGE is not set
# CONFIG_USB_FUNCTION_ROCKUSB is not set
# CONFIG_DM_USB_GADGET is not set
```

主线 RK322x legacy DWC2 gadget 路径已链接进 U-Boot，`nm` 可见 `do_dfu`、
`dfu_fill_entity_ram` 和 `dwc2_udc_probe`；最终 DT 的 OTG controller、u2phy0 与 otg-port 均为
`okay`，模式为 `peripheral`。没有启用旧 `ROCKCHIP_USB2_PHY`，因为其表中不含
`rockchip,rk3228-usb2phy`，启用反而会在找不到平台数据时 `hang()`。

重建 YMODEM U-Boot FIT 后用独立 config 打包 loader，并离线 unpack 核对 471、472 与
FlashData 前缀均与输入逐字节一致：

```text
466b4e8ae6e6cccf6009d591d42bb12c7451ec01cce1576e03358a44545fa359  build/artifacts/r1-phicomm-r1-uboot-spl-ymodem-gic-int55-cleanup-directrx-usb-dfu-ram-loader.bin
ba1cd6347454ee79dca66ba843f63bab9cfc0cf8a1c7daaa506b4eef317572f5  build/artifacts/r1-ymodem-fit-dtb-usb-dfu-ram.itb
1ad041d216d32a142125f349fbe766712435835a5116ee4b3a11fe78734704bb  build/artifacts/r1-phicomm-r1-uboot-usb-dfu-ram.config
```

新 `scripts/usb-dfu-r1-linux.py` 强制校验 Linux FIT 哈希，只调用 `dfu-util` 的
`linux-fit` alternate 下载与 detach。U-Boot 端预期单行命令为：

```text
setenv dfu_alt_info 'linux-fit ram 6a800000 01000000' && dfu 0 ram 0 && iminfo 6a800000 && bootm 6a800000
```

主机侧 `dfu-util -e` 触发 U-Boot `g_dnl_trigger_detach()` 后，`dfu` 返回并继续校验/启动 FIT。
以上均是主机构建和源码/产物审计，不是 R1 实机事实。下一次要保存完整日志，确认 DFU USB
枚举、FIT hash、B2 CPU0–CPU3 online、eMMC 只读枚举和 uptime；不得把 USB 枚举失败提前归因
为硬件或 PHY，也不得执行任何 storage-backed DFU 命令。

### B2 实机否定 timer 假说，改用已验证 multi_v7 v9 做 config A/B（2026-08-10）

用户通过新的单文件启动路径运行了 B2。保存的 Linux 段为
`build/artifacts/rescue-v11-emmc-b2-usb-20260810.log`（10,921 B，SHA-256
`7377b3613326b20f5544f37ccc9758a9bde5e7e75358843980896b070e5874ef`）。修正后的 initramfs
已生效：标题能够正常输出，未再出现 `/init: echo: not found`。RK805、DesignWare MMC、HS200
Samsung `8GME4R`、user area、boot0/boot1 与 RPMB 仍正常只读枚举。

但 CPU1、CPU2、CPU3 仍分别在约 1.039、2.080、3.120 秒报告 `failed to come online`，最终只有
CPU0。因此 B2 的单变量结果是否定：删除 `arm,cpu-registers-not-fw-configured` 并恢复最小 DT
的两路 arch-timer PPI 不能恢复 SMP；B1 中 PPI29/PPI30 的不匹配不是当前次核失败的充分根因。
该日志只保存了 Linux 段，没有保存 `dfu-util`、U-Boot `iminfo/bootm` 或传输计时；USB 下载速度
快来自用户实机确认，不能由该文件单独量化。

用户随后明确要求回到之前实机四核稳定的 `multi_v7_defconfig` 系配置。为避免重新编译引入差异，
C1 直接复用已验证的
`zImage-5.10-psci-v1-gic400-clean-4core-v9`（10,158,592 B，SHA-256
`5bc8624169e60ef4558cc78cb80ae887a2a4f798c2a824a9afecece29d3c2565`），保持 B2 DTB 与修正
initramfs 逐字节不变。保存的 v9 最终配置确认 `ARM_PSCI`、Rockchip clock/pinctrl/I2C、RK808
MFD/regulator 与 DesignWare Rockchip eMMC 均为 built-in；RK808 RTC 为模块，不影响本轮 SMP/eMMC
判定。

新 ITS 为 `scripts/r1-linux-multiv7-v9-emmc-c1.its`。FIT 解出的三个 payload 已与上述源文件逐字节
比较一致：

```text
kernel  10158592 B  5bc8624169e60ef4558cc78cb80ae887a2a4f798c2a824a9afecece29d3c2565
ramdisk   627218 B  31508a74ad5265cf27b8a077f994cf00887f238e2d297d26f7a9198fd8cf3095
fdt        22557 B  4800850ad50e8109ccd763a245128b4b3f08477cff583e77c932e5dc90625c6b
FIT     10809808 B  ed8c5b932c3853242e0bb0476cbfa17d0d2f2e70379bbd23f85d2006531a24b5
```

FIT 约 10.31 MiB，仍小于 U-Boot `linux-fit ram 6a800000 01000000` 的 16 MiB RAM alternate。
`scripts/usb-dfu-r1-linux.py` 默认文件与强制哈希已切换到 C1。该 C1 目前只完成主机侧打包和解包
核对；下一次实机若四核恢复，说明 v11 裁剪配置与完整 DT 的组合存在问题，再做 config 二分；若
仍为单核，则保留 multi_v7 内核，转而二分完整 DT 的早期 clock/CPU/reset 描述。全程不挂载或
写入 eMMC。

### multi_v7 C1 四核与 eMMC 同时通过，准备 Cortex-A7 erratum C2（2026-08-10）

用户实机版本串明确为 `5.10.262-phicomm-r1-4core #1 SMP`，证明运行的是复用的 clean v9
zImage。使用与失败 B2 逐字节相同的 DTB/initramfs 时，四个 CPU 在 35 ms 内全部上线：

```text
[    0.027440] CPU1: thread -1, cpu 1, socket 15, mpidr 80000f01
[    0.030959] CPU2: thread -1, cpu 2, socket 15, mpidr 80000f02
[    0.034194] CPU3: thread -1, cpu 3, socket 15, mpidr 80000f03
[    0.034428] smp: Brought up 1 node, 4 CPUs
[    0.036417] SMP: Total of 4 processors activated (192.00 BogoMIPS).
```

`/sys/devices/system/cpu/online` 与 `present` 均为 `0-3`。RK805 `0x8050`、HS200 Samsung
`8GME4R` 7.28 GiB、boot0/boot1 与 RPMB 也继续正常只读枚举。保存的本轮输出只显示 uptime
`13.56 51.77`，没有返回 35 秒后的第二个数值，也没有 IPI 表；因此“四核与 eMMC 同时工作”
已由实机直接验证，但本组合 uptime >30 秒和四核 IPI 增长尚未在本轮文本中留证。

该结果是严格的 kernel-config 边界：失败 B2 与成功 C1 的 DTB、initramfs、OP-TEE、U-Boot 和
装载地址不变，只有 v11 白名单 zImage 换成已验证 multi_v7 v9 zImage。它排除 OP-TEE/PSCI/GIC
和 timer DT 为当前直接根因，并证明完整 eMMC DT 本身可以支持四核；下一步应二分最终 config。

两份最终 config 的早期 CPU 相关差异中，`CONFIG_ARM_ERRATA_814220` 是唯一明确针对本机
Cortex-A7 的 workaround。Linux 5.10 Kconfig 说明该 erratum 影响 Cortex-A7 r0p2–r0p5，使 L2
set/way cache maintenance 可能越过 L1 操作；multi_v7 为 `y`，v11 为 `n`。其他已见 ARM errata
针对 A8/A9/A15，CPUFreq/CPUIdle/MCPM 和大部分驱动也不处于约 35 ms 前的 PSCI 上线路径。
这只是由源码适用范围和时序作出的优先级推断，尚未在 R1 验证因果。

C2 从失败的 v11 eMMC A1 最终配置仅改变两行：可见版本后缀，以及
`ARM_ERRATA_814220 n -> y`。继续复用 B2 DTB 和修正 initramfs。构建和 FIT 解包核对已通过：

```text
e16652f44590e395d02e0a8f553d4703102ffe8269c82afb4293a8642d47d190  build/artifacts/zImage-rescue-v11-emmc-c2-a7-814220
4800850ad50e8109ccd763a245128b4b3f08477cff583e77c932e5dc90625c6b  build/artifacts/rk3229-phicomm-r1-rescue-v11-emmc-c2-a7-814220.dtb
e69b7839cc2900dd9d8c912189d1e460e706cfbd99654ceea537baad186113f6  build/artifacts/r1-linux-rescue-v11-emmc-c2-a7-814220.itb
```

`scripts/usb-dfu-r1-linux.py` 的默认 FIT/哈希已切到 C2。C2 尚未上板，不能把 814220 提前写成
根因；成功标准是版本串含 `-c2-a7-814220`、CPU online/present 均为 `0-3`、eMMC 只读枚举、
四核 IPI 非零并保存间隔至少 35 秒的两次 uptime。

用户还报告 U-Boot 前置 YMODEM 在 `--tx-gap-us 5` 时总耗时约 1 分 56 秒，与 50 us 基本相同。
源码检查解释了该现象：bridge 对任何非零 gap 都逐字节执行 `board.write()`、`flush()` 和
`time.sleep()`；Linux/Python 无法精确调度 5 us sleep，调用与唤醒开销远大于设定值。因此调小
非零数字不会按比例提速。`tx-gap-us=0` 才切换为块写，但必须配合尚待实机验证的 direct-RX SPL
避免 UART FIFO 溢出；本轮不把传输优化混入 C2 内核验证。

### C2 否定 ARM 814220，B3 转测 SMP 前 CRU 批量时钟配置（2026-08-10）

C2 完整日志保存为
`build/artifacts/rescue-v11-emmc-c2-a7-814220-failed-20260810.log`（17,720 B，SHA-256
`7023d7871746c4f7a24378496ca789bd1f3ec4368cb151d2245f53659384d3e0`）。U-Boot 明确识别 C2
FIT，kernel、ramdisk 与 FDT 的 SHA-256 全部校验通过；Linux 版本也为预期
`5.10.262-phicomm-r1-rescue-v11-emmc-c2-a7-814220`。所以不存在拿错内核或 DTB 的歧义。

结果仍是 CPU1/CPU2/CPU3 在 1.039/2.080/3.120 秒超时，`online=0`、`present=0-3`；RK805、RTC
和 HS200 `8GME4R` 继续正常枚举，记录 uptime 为 10.63 秒。由此实机否定
`CONFIG_ARM_ERRATA_814220` 单变量；它是适用的防护项，但不是本次次核不能 online 的根因。

随后检查 PL310 方向：B2/B3 DTB 都没有外置 `arm,pl310-cache` 或 L2 controller 节点。Linux
binding 又明确说明 Cortex-A7 的集成 L2 应由 early secure boot 预配置，不使用该 PL310 DT
合同。因此 multi_v7 开启的四个 `CONFIG_PL310_ERRATA_*` 不能解释“完整 DT 失败、最小 DT
成功”，本轮不为它们制作无证据候选。

更直接的时序证据是：失败 B2/C2 都在约 11.98 ms 进入 `smp: Bringing up secondary CPUs`，
成功 multi_v7 C1 到约 23.88 ms 才进入；完整 B2 DT 的 CRU 节点在 `time_init()` 的
`of_clk_init()` 阶段、SMP 之前批量设置 GPLL/ARMCLK/CPLL 以及 CPU/PERI 总线时钟，而成功最小
DT 没有 CRU 节点。B3 因而保留失败 B2 的 v11 kernel、initramfs、timer、CPU 与 eMMC 描述，
只从 `clock-controller@110e0000` 删除：

```dts
assigned-clocks = <...>;
assigned-clock-rates = <...>;
```

排序反编译后的 B2/B3 DT diff 仅有上述两项删除。B3 zImage 与失败 B2 的 A1 zImage 逐字节
相同，initramfs 也不变：

```text
5013b84d149e431f0c2886be8b4d0f8e0aea2e96acdd0de08b058332cb23861b  build/artifacts/zImage-rescue-v11-emmc-b3-clock-inherit
18631905bced9bec45718745429cf92a3689208d77b19bad4f998c38b56ccf9c  build/artifacts/rk3229-phicomm-r1-rescue-v11-emmc-b3-clock-inherit.dtb
9e2ff4a214c29fcc2a06ba1445528209ba54d4093e5ad6256a8b4abed5b215d6  build/artifacts/r1-linux-rescue-v11-emmc-b3-clock-inherit.itb
```

FIT payload 已解包核对，`scripts/usb-dfu-r1-linux.py` 默认值与哈希已切换到 B3。该候选尚未
上板；即使成功，也只能先证明“CRU assigned-clock 批量配置参与故障”，还需用显式延时或逐项
恢复 clock assignment 区分时钟值错误与切换后稳定时间不足。

用户随后明确当前优先级是直接使用已经四核成功的约 10 MiB multi_v7 内核完善外设，而不是先
完成 v11 裁剪根因二分。因此工作默认恢复为 C1：精确复用
`zImage-5.10-psci-v1-gic400-clean-4core-v9`，配 B2 完整 eMMC DT 与修正 initramfs；
`scripts/usb-dfu-r1-linux.py` 默认 FIT/哈希重新指向
`r1-linux-multiv7-v9-emmc-c1.itb` / `ed8c5b93...`。B3 保留为后续最小化阶段的诊断候选，不再
覆盖工作基线。

当前四组实机边界应按组合解释：

| Kernel | 最小 DT | 完整 eMMC DT |
|---|---|---|
| 10 MiB multi_v7 clean v9 | 四核通过 | C1 四核与 eMMC 同时通过 |
| 2.6 MiB v11 | A 线四核、uptime >30 秒 | B1/B2/C2 均只有 CPU0 |

所以 `rk3229-phicomm-r1-emmc-open-optee.dts` 确实通过 `rk3229-phicomm-r1.dts` →
`rk3229.dtsi`/`rk322x.dtsi` 引入完整 clocks/reset/OPP/PMIC/eMMC 描述，但 DTS 本身不是“加上必
死”：同一完整 B2 DT 已被 C1 证明可四核启动。更准确的未解问题是 v11 裁剪/布局/时序与完整 DT
早期初始化之间的交互；它不阻塞以 C1 继续 RAM-only 外设 bring-up。

### DDR DVFS 延后，C1 USB Host A1 主机端候选（2026-08-11）

用户指出 DDR 471 输出的 `300MHz` 是否意味着只有一个频点。当前可以确认的是：
`rk322x_ddr_300MHz_v1.06.bin` 在 MaskROM RAM 下载阶段负责 DDR3 初始化和训练，并把启动频率
设为 300 MHz；这不能证明运行期存在 DDR DVFS。当前 clean Linux 5.10 源码只有 RK3399 DMC
devfreq 驱动，没有 RK322x/RK3229 DMC 驱动。即使以后 A/B Rockchip TEE v2.00，也必须同时确认
其 DDR SMC ABI、板级 timing/频点表以及 Linux 调用方。因此该方向已明确写入 `TODO.md`，留到
基本外设稳定后处理，不把“换 TEE”或“DDR 以 300 MHz 启动”写成动态调频已可用。

USB Host A1 严格复用已验证 C1 的 zImage 与修正 initramfs，只新增设备树覆盖。原厂 R1 3.10
实机日志证明 `0x30080000`、`0x300c0000`、`0x30100000` 三组 EHCI 及其 OHCI 配对都能注册
root hub；原厂 DT 没有 USB VBUS GPIO/regulator。A1 因而开启 `u2phy0_host`、
`u2phy1_otg`、`u2phy1_host` 和六个固定 Host 控制器，但继续禁用 `usb_otg`/`u2phy0_otg`，
避免重新引入曾留下 active INTID 55 的 DWC2 OTG 路径。

离线验证结果：C1 内核已内建 USB core、EHCI platform、OHCI platform、USB storage 与 HID；
最终 DT 中 `usb@30040000` 为 `disabled`，六个 Host controller 均为 `okay`；FIT 提取的 DTB
与源 DTB `cmp` 一致。产物为：

```sh
cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
  -I /tmp/r1-linux-5.10-clean-src/arch/arm/boot/dts \
  -I /tmp/r1-linux-5.10-clean-src/arch/arm/boot/dts/rockchip \
  -I kernel/dts \
  -I /tmp/r1-linux-5.10-clean-src/scripts/dtc/include-prefixes \
  -o build/artifacts/rk3229-phicomm-r1-emmc-open-optee-usb-host-a1.dts.preprocessed \
  kernel/dts/rk3229-phicomm-r1-emmc-open-optee-usb-host.dts

build/kernel-5.10-clean-4core/scripts/dtc/dtc \
  -I dts -O dtb -Wno-unit_address_vs_reg \
  -o build/artifacts/rk3229-phicomm-r1-emmc-open-optee-usb-host-a1.dtb \
  build/artifacts/rk3229-phicomm-r1-emmc-open-optee-usb-host-a1.dts.preprocessed

mkimage -f scripts/r1-linux-multiv7-v9-emmc-usb-host-a1.its \
  build/artifacts/r1-linux-multiv7-v9-emmc-usb-host-a1.itb
```

```text
5bc8624169e60ef4558cc78cb80ae887a2a4f798c2a824a9afecece29d3c2565  build/artifacts/zImage-5.10-psci-v1-gic400-clean-4core-v9
31508a74ad5265cf27b8a077f994cf00887f238e2d297d26f7a9198fd8cf3095  build/artifacts/r1-initramfs-rescue-v11-emmc-a2.cpio.gz
daa2cebdfe29627b430f9c826cc97a0c71cb5c1e4765d8fd2c0f739d2228094d  build/artifacts/rk3229-phicomm-r1-emmc-open-optee-usb-host-a1.dtb
3f6533ff47091f878e3c94cceab45df779f5005b84e8b9971eaa52b45fc45e07  build/artifacts/r1-linux-multiv7-v9-emmc-usb-host-a1.itb
```

该候选尚未上板，不能将 root hub 或外接设备枚举提前标为成功。`usb-dfu-r1-linux.py` 默认值
继续保持已验证 C1，A1 必须用显式 `--fit`，以便失败时直接省略参数回退。测试只从 U-Boot
DFU 下载到 RAM，不挂载或写入 eMMC。

### 硬件范围纠正与 Wi-Fi SDIO A1（2026-08-11）

用户确认 R1 成品是没有对外 USB 外设接口、也没有 SD 卡槽的智能音箱。上一节 USB Host A1
因此降级为 SoC/DFU 研究产物，不再作为板载外设候选上板。外设主线按产品目标改为：板载 SDIO
Wi-Fi、UART Bluetooth、I2C/SPI 控制和 I2S/AK7755 音频。

原厂 R1 DT 与实机日志共同确认 Wi-Fi 硬件合同：Broadcom combo 模组由 `0x30010000` SDIO
控制器连接，4-bit、37.5 MHz、支持 SDIO IRQ；WL_REG_ON 是 legacy GPIO 90，即 GPIO2_D2；
HOST_WAKE 是 legacy GPIO 28，即 GPIO0_D4。原厂最终识别 chip `0x4345 rev 6 pkg 2`，运行时选择
`fw_bcm43455c0_ag.bin` 和 `nvram_ap6255.txt`。DT 字符串写 `ap6335`，但不得据此覆盖实际固件
选择和芯片读数；模组营销名、Broadcom die ID 与固件命名需要分开记录。

Wi-Fi A1 只验证供电和 SDIO 枚举。它复用 C1 kernel/initramfs，新增 `mmc-pwrseq-simple`，以
GPIO2_D2 active-low reset 描述实现物理 WL_REG_ON 上电为高，等待 200 ms 后扫描；SDIO 设置为
4-bit、37.5 MHz、non-removable、keep-power-in-suspend 和 cap-sdio-irq。C1 config 已内建
`CONFIG_PWRSEQ_SIMPLE=y`、`CONFIG_MMC_DW_ROCKCHIP=y`，所以此阶段无需重编内核，也不加入
brcmfmac 或固件。

```text
d1cdfb65d9d05f8cdd7821041b1074802f306a6594c5a26c98d7a8fcc07db667  build/artifacts/rk3229-phicomm-r1-emmc-open-optee-wifi-sdio-a1.dtb
0139f8f7153caccf27bcc674be70f47c1070352db4c46c92e40ad1ed5d10a0d7  build/artifacts/r1-linux-multiv7-v9-emmc-wifi-sdio-a1.itb
```

离线检查确认 `/mmc@30010000/status = "okay"`、bus-width `4`、max-frequency `37500000`，
power sequence GPIO specifier 指向 GPIO2 pin 26 且为 active-low；FIT 提取 DTB 与源 DTB 逐字节
一致。A1 尚未上板，成功标准仅是出现 `new high speed SDIO card` 和 SDIO function，不要求
`wlan0`。A2 才启用 cfg80211/brcmfmac 并按主线请求文件名放入固件/NVRAM。

### Wi-Fi SDIO A1 实机通过（2026-08-11）

A1 实机日志确认 `dwmmc_rockchip 30010000.mmc` 获得 pwrseq，设备被标记为 non-removable；
WL_REG_ON 上电后完成 CIS 读取，并以实际 35,714,286 Hz（请求 37.5 MHz）进入高速 SDIO：

```text
[    2.162545] dwmmc_rockchip 30010000.mmc: allocated mmc-pwrseq
[    2.534690] mmc_host mmc1: Bus speed (slot 0) = 35714286Hz
[    2.543034] mmc1: new SDIO card at address 0001
```

sysfs 出现 `mmc1:0001:1`、`:2`、`:3` 三个 SDIO function，三者 vendor 均为 `0x02d0`、
device 均为 `0xa9bf`。`0x02d0` 与板上 Broadcom combo 模组证据一致；此处只记录读出的
`0xa9bf`，不在缺少固定上游 ID 依据时单凭该数字重命名芯片。CPU online 仍为 `0-3`，eMMC
继续以 `mmc0`/HS200 枚举。这直接验证 R1 板载 Wi-Fi 走 `0x30010000` SDIO，并证明 A1 的
GPIO2_D2 power sequence、pinctrl、4-bit 数据线和控制器 IRQ 链可用。CIS 的 unknown tuple
`0x80/0x81` 是厂商扩展 tuple，未阻止枚举，不能记为失败。

A1 不含 cfg80211/brcmfmac，所以没有 `wlan0` 符合预期。下一 A2 保持该 DT、电源时序、C1
initramfs 和启动链不变，只补无线内核配置及主线固件文件命名；验收点是 brcmfmac 绑定三个
function 中对应的 WLAN function、加载固件/NVRAM并创建 `wlan0`，扫描和联网继续后置。

### Wi-Fi brcmfmac A2 主机端构建（2026-08-11）

A2 从实机成功的 multi_v7 v9 最终 `.config` 派生，不重新运行 defconfig；除可辨识
LOCALVERSION 外，只启用 cfg80211、BRCMUTIL、BRCMFMAC、SDIO BCDC backend 与 BRCMDBG，
明确关闭 brcmfmac USB/PCIe backend。A1 DTB 逐字节复用，因此 GPIO2_D2 power sequence、
SDIO pinctrl/频率和 eMMC/SMP 合同不变。

首次构建因 Fedora 缺少 `openssl/engine.h` 停在 host `extract-cert`。该工具由 cfg80211 默认
强制 signed regulatory.db 间接引入，不是 ARM 或 brcmfmac 编译失败。A2 的验收仅为固件加载
和 `wlan0` 创建，不做扫描/发射；最终配置在已有 `CONFIG_EXPERT=y` 的前提下显式启用
`CFG80211_CERTIFICATION_ONUS` 并关闭 `CFG80211_REQUIRE_SIGNED_REGDB`，使配置回到 v9 原有的
无 system trusted keyring 边界。此选择只适用于受控 A2 bring-up；进入扫描/联网阶段前必须加入
可信 regulatory.db/签名策略和明确国家码，不能把它当最终产品配置。

主线 5.10 源码对 chip 4345 的 rev mask 会选择 `brcmfmac43455-sdio`。initramfs 因而把原厂已
验证使用的文件复制为：

```text
/lib/firmware/brcm/brcmfmac43455-sdio.bin
/lib/firmware/brcm/brcmfmac43455-sdio.txt
/lib/firmware/brcm/brcmfmac43455-sdio.phicomm,r1.txt -> brcmfmac43455-sdio.txt
```

源文件及校验和为：

```text
7ba44359248ee7437df1156695bff3eef81c02d1119fabc57345f41729386887  backup/extracted/system/etc/firmware/fw_bcm43455c0_ag.bin
8453d34e328645636108f70b3c6a3b11ac3782def85a52cb7216d6f2d4c10b78  backup/extracted/system/etc/firmware/nvram_ap6255.txt
```

最终 `vmlinux` 符号确认 `brcmf_sdio_register`、`brcmf_fw_request_firmware` 和
`cfg80211_register_wdev` 都是 built-in。FIT 三个 payload 解包后与源文件逐字节一致：

```text
1ea940b4fab7d383287dbb8de9852599953d70b2cb74386703c43f76caca80bb  build/artifacts/zImage-5.10-multiv7-v9-wifi-a2
7194c76d4299eb5b2f8b8a56707a561441caa17fbaec26f97e7c0051f1d459e1  build/artifacts/r1-initramfs-wifi-a2.cpio.gz
d1cdfb65d9d05f8cdd7821041b1074802f306a6594c5a26c98d7a8fcc07db667  build/artifacts/rk3229-phicomm-r1-emmc-open-optee-wifi-sdio-a1.dtb
7fdc5cbbc1816e23ffdefa9fabb6f8addcee5f4fdc2b2cba35dcaa160f86328d  build/artifacts/r1-linux-multiv7-v9-emmc-wifi-brcmfmac-a2.itb
```

FIT 为 11,468,836 B，小于 16 MiB DFU RAM alternate。A2 尚未实机；成功标准是版本含
`-wifi-a2`、A1 三个 SDIO function 仍存在、日志确认加载上述 firmware/NVRAM、出现 `wlan0`，
同时四核/eMMC/uptime >30 秒保持正常。本轮不执行扫描、关联或任何 eMMC 写入。

### Wi-Fi brcmfmac A2 实机创建 wlan0（2026-08-11）

A2 实机保持 A1 的 35,714,286 Hz SDIO 与三个 function，brcmfmac 绑定 function 1，并读出与
原厂一致的 chip signature/identity：

```text
brcmfmac: F1 signature read @0x18000000=0x15264345
brcmfmac: brcmf_fw_alloc_request: using brcm/brcmfmac43455-sdio for chip BCM4345/6
brcmfmac: brcmf_c_preinit_dcmds: Firmware: BCM4345/6 wl0: Jul 19 2017 22:14:06 version 7.45.100.6 (r665936 CY) FWID 01-2425
```

`/sys/class/net/wlan0` 指向
`30010000.mmc/.../mmc1:0001:1/net/wlan0`，证明 cfg80211、brcmfmac SDIO/BCDC、固件下载和
netdev 注册链全部完成。设备返回了非 NVRAM 占位值的 MAC；按项目安全规则不在公开文档记录
设备真实地址。

用户随后确认该版本能够长期运行，并补交四核 `/proc/interrupts` 摘要：CPU0–CPU3 四列均存在，
IPI2（rescheduling）与 IPI3（function call）在四个 CPU 上均有非零计数。这不仅证明四核保持
online，也证明核间重调度与函数调用中断确实发生；结合此前同基线的 eMMC 枚举结果，A2 的
`wlan0`、四核稳定性和基础存储回归检查均闭环。具体计数会随运行时间变化，不作为固定接口值。
A2 阶段因此完成。

日志仍有两项预期但必须在扫描前解决的开放项：initramfs 未携带 `regulatory.db`，cfg80211
报 `-2`；未提供 BCM43455 CLM blob，brcmfmac 明确提示可用信道可能受限。两者不影响 A2
固件启动和 wlan0 创建，但不得在缺少法规数据时把扫描/发射标为最终可用。下一 A3 需要确定
可信 regulatory database、签名策略、目标国家码和与 7.45.100.6 firmware 匹配的 CLM blob，
然后只做扫描，不立即关联网络。

### Wi-Fi regulatory/CLM A3a 主机端构建（2026-08-11）

原厂 system 提取物没有 `regulatory.db` 或 BCM43455 CLM blob；`nvram_ap6255.txt` 也没有明确的
`ccode/regrev`。因此没有修改原厂板级 NVRAM，而是采用主机 Fedora 44 已安装、版本可固定的
数据：`wireless-regdb-2026.05.30-1.fc44` 与 `linux-firmware-20260622-1.fc44`。原厂 recovery
的 `default.prop` 记录 `ro.product.locale.region=CN`，据此将本轮受控测试的初始监管域设为 CN；
这是 R1 原厂软件区域证据，不代表已经独立核验设备销售区域或射频认证。

输入文件校验和：

```text
2fb33ca0074db573e05ef7dd50bb45b63c0ff98b7e852e1105ebad536fae8e6b  /usr/lib/firmware/regulatory.db
c941c08f51c93e46722293b85631604c3740d86c3de0c75f79aef50d2e919179  /usr/lib/firmware/regulatory.db.p7s
8d921c31c6a54831e84d35762cd9d10d3f300909d966838f9be19072690d7b88  /usr/lib/firmware/cypress/cyfmac43455-sdio.clm_blob.xz
15f50a27020b263d1bea215c8f68d0550d912932d1d9ef19ffd59f18d82dd460  brcmfmac43455-sdio.clm_blob（解压后）
```

构建脚本仅在 `R1_WIFI_REGULATORY=1` 时安装上述数据，CLM 会解压到 brcmfmac 5.10 实际请求的
`/lib/firmware/brcm/brcmfmac43455-sdio.clm_blob`。A3 config 只改变 LOCALVERSION 与强制 cmdline，
加入 `cfg80211.ieee80211_regdom=CN`；A2 其余 config 与 A1 DT 均保持一致。

首次从空 A3 build tree 高并发构建时，两个互不相关目标先后出现编译后 `.o` 缺失；磁盘空间与
inode 正常。失败树保留在 `/tmp/r1-kernel-wifi-a3-failed-20260811`，随后从完整 A2 build tree
创建副本并以 `JOBS=4` 增量构建成功。这是主机构建树/并发问题，不是 R1 A3 源码失败。

最终产物：

```text
da80ee6cabed9af6419719a9daf99567f571b297b70a90209a2588535de10fa7  build/artifacts/zImage-5.10-multiv7-v9-wifi-a3
200ea439e47bfc96ceda18fcbfe9f935c763daf659daf18d2e3b87cf833c2ec1  build/artifacts/r1-initramfs-wifi-a3.cpio.gz
d1cdfb65d9d05f8cdd7821041b1074802f306a6594c5a26c98d7a8fcc07db667  build/artifacts/rk3229-phicomm-r1-emmc-open-optee-wifi-sdio-a1.dtb
0b8ec5eebdda65a981d985b10b5408b3edbf1ce7346b07afd0002abe1bc06b1c  build/artifacts/r1-linux-multiv7-v9-emmc-wifi-regulatory-a3.itb
```

FIT 解包后三个 payload 均与源产物逐字节一致；initramfs 中 regdb、签名和解压 CLM 也逐字节
核对通过。A3a 尚未实机。成功标准是 cmdline 包含 CN、`regulatory.db` 的 `-2` 与“no clm_blob”
提示消失、`wlan0` 存在且四核/uptime 不回退。当前 initramfs 没有 `iw`，所以 A3a 不声称完成
扫描；最小扫描工具和只读扫描属于 A3b。

### Wi-Fi A3a 实机通过与 A3b 扫描候选（2026-08-11）

A3a 实机 cmdline 正确包含 `cfg80211.ieee80211_regdom=CN`。过滤后的日志中不再出现 A2 的
`regulatory.db` 加载 `-2` 或 `no clm_blob available`，同时 BCM4345/6 7.45.100.6 firmware
继续启动并创建 `wlan0`。`/proc/interrupts` 仍有 CPU0–CPU3 四列，IPI2/IPI3 在四核均为
非零，故 A3a 的无线数据加载与四核回归检查通过。日志中的 `cpufreq-dt ... -19` 是此前已有的
独立 DVFS 开放项，不能归因于 Wi-Fi A3。

由于现有静态 BusyBox 没有 `iw`，A3b 新增 `tools/r1-nl80211-scan.c`。它是 freestanding ARM
EABI5 ELF，直接调用 generic netlink 与 nl80211，不依赖 libc/libnl；固定操作 `wlan0`，仅使
接口 UP、触发 wildcard active scan、等待八秒并 dump scan cache。输出只含 SSID、MHz 与 mBm，
刻意不解析/输出 BSSID；程序没有关联、认证、密钥或存储写功能。

```text
6a4f7d9fa569060d266d7c5c43e5d61c81762ee3f684edf40cdddbb5f328231b  build/artifacts/r1-nl80211-scan
68c7c89a5e892765d34d84895efa3740d3446ebbe81df847bf2dd14e90b6cf5e  build/artifacts/r1-initramfs-wifi-a3b.cpio.gz
931b616af63bdd24525fd983b65fb1985bfa61a7239e64cce0e817b8f6c64201  build/artifacts/r1-linux-multiv7-v9-emmc-wifi-scan-a3b.itb
```

scanner 无未解析符号；initramfs 提取出的 scanner 与源产物一致；FIT 解包后的 zImage、ramdisk、
DTB 均逐字节核对通过。

A3b 首版随后实机成功完成 active scan，在 2.4 GHz 和 5 GHz 共返回 9 个 BSS，末尾为
`scan_entries=9` 且进程退出码为 0。SSID 属于现场信息，不在项目文档复录；BSSID 按工具设计
完全没有输出。由此 regulatory/CLM、brcmfmac、cfg80211、generic netlink 与 nl80211 scan
链路均已在 R1 验证，且没有进行网络关联。

内核在启动 scanner 时提示 executable stack。功能结果有效，但 ELF 权限不应保留。新增
`scripts/build-r1-nl80211-scan.sh` 固定 freestanding 编译参数并加入 `-Wl,-z,noexecstack`；
重建后 `readelf -W -l` 的 `GNU_STACK` 为 `RW` 而非 `RWE`，无未解析符号。重新封装结果：

```text
273e9125d8de390e04748f6ad82915e7f2379801c9ce250cce7319b1fad6c709  build/artifacts/r1-nl80211-scan
808e0cdad37f82f607fb4179d70514f5c75f4d3a505a32acf508a3abd2f8d3ee  build/artifacts/r1-initramfs-wifi-a3b.cpio.gz
9e0f11ed93ca8ce8ff8c2c96193c9d8f8918d43877c52386f51207b86aae6d3c  build/artifacts/r1-linux-multiv7-v9-emmc-wifi-scan-a3b.itb
```

权限修正版的 initramfs/FIT 再次通过逐字节审计，但尚未重复上板；A3b 扫描功能阶段已由首版
实机结果完成。

### Bluetooth UART A1 主机端构建（2026-08-11）

重新对照原厂 DT 后发现，R1 Bluetooth 不是主线 `rk322x.dtsi` 的 UART1 默认引脚组。原厂
`serial@11020000` 的 pinctrl 指向 `uart1-1`：TX/RX 为 GPIO3_B6/B5，CTS/RTS 为
GPIO3_A7/A6；板级节点还给出 BT_REG_ON=GPIO2_D5、BT_WAKE=GPIO3_D3、HOST_WAKE=GPIO3_D2。
若只写 `&uart1 { status = "okay"; };`，主线会选择 GPIO1_B1/B2，实机不可能连到焊接模组。

A1 从已验证 Wi-Fi A3 DT 继承，只新增 R1 alternate UART pinctrl、`uart-has-rtscts` 和
GPIO2_D5 output-high hog。暂不描述 wake GPIO，不添加 `brcm,bcm4345c5` serdev child，也不
下载 HCD；因此内核保留 `/dev/ttyS1`，可以在 controller ROM 的初始 115200 baud 下直接发送
标准 HCI Reset command packet `01 03 0c 00`，预期 Command Complete event 为
`04 0e 04 01 03 0c 00`。该试验只验证 UART/pinmux/flow-control/power/controller response。

```text
da80ee6cabed9af6419719a9daf99567f571b297b70a90209a2588535de10fa7  build/artifacts/zImage-wifi-bt-uart-a1
6ad4e665a6d8307276cd83c8310c563be9443309bfb393a790ed7390906abeb5  build/artifacts/r1-initramfs-wifi-bt-uart-a1.cpio.gz
8ffdee773192ce568beb9aff64ac8e4e94db158e5040cb366ffcf9442cd7876d  build/artifacts/rk3229-phicomm-r1-wifi-bt-uart-a1.dtb
60b8cc08672d46c6702a268d2a2d4133d74ca5a96b70ead7e39d33bad99fc9b0  build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1.itb
```

最终 DT 反编译确认 UART1 为 `okay`、pinctrl 指向 GPIO3 alternate xfer/CTS/RTS、GPIO2_D5
hog 为 output-high，且不存在 Bluetooth serdev child。zImage 与 A3 逐字节相同；A1 尚未实机。

### Bluetooth UART A1 gpio-hog 失败与 A1r2（2026-08-11）

A1 实机在 init 前停止，但日志证明 initramfs 已完成解包。最早且重复出现的错误为：

```text
gpio gpiochip2: (gpio2): setup of own GPIO bt-reg-on failed
requesting hog GPIO bt-reg-on (chip gpio2, offset 29) failed, -517
gpiochip_add_data_with_key: GPIOs ... (gpio2) failed to register, -517
rockchip-pinctrl pinctrl: failed to register gpio_chip gpio2, error code: -517
```

`-517` 是 `EPROBE_DEFER`。gpio2 在注册自身 gpiochip 的过程中处理其子节点 gpio-hog，GPIO/pinctrl
资源尚未就绪，导致自注册被 defer；随后 eMMC、UART1、UART2 和 I2C 都因依赖 pinctrl/gpio2
在 deferred-probe timeout 被忽略。这解释了为何串口最终停止在 `Disabling unused clocks`，
并排除 initramfs 或 HCI 操作为根因。

A1r2 不再把 BT_REG_ON 放在 gpio2 的 gpio-hog 中，而是在根节点使用 always-on fixed regulator，
由 fixed-regulator 在 gpiochip 注册完成后申请 GPIO2_D5。UART1 alternate pins、硬件流控、kernel
和 initramfs 均不变，仍不添加 serdev/HCD。

```text
eeb4066b250366baadca315ece3ab478bd97a3b05a458ce7712f9930c7c7220e  build/artifacts/rk3229-phicomm-r1-wifi-bt-uart-a1r2.dtb
1688800ce0f91de1475ba2cca0562b6a3e78bb14b2fa2b18723f74faa6acf677  build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1r2.itb
```

FIT 解包 kernel/ramdisk/DTB 均与源产物一致；反编译 DT 确认 fixed regulator 指向 GPIO2_D5、
UART1 仍为 GPIO3 alternate group，且不存在 gpio-hog 或 Bluetooth serdev child。A1r2 尚未实机。

### Bluetooth UART A1r2 实机无响应与 A1r3（2026-08-11）

A1r2 已解决启动回归并进入 shell。内核注册
`11020000.serial: ttyS1 ... is a 16550A`；debugfs 显示 GPIO93 由 `regulator-bt-reg-on` 占用且
为 output-high，regulator summary 显示 `bt_reg_on` enabled/3300 mV。UART 首次 open 的 DMA
请求失败后使用 PIO，不妨碍 tty 注册。

在 115200 baud 下，无硬件流控和 RTS/CTS 两种设置均没有 HCI Reset response。随后解绑并重新
绑定 fixed-regulator，形成 BT_REG_ON 低→高复位，再次测试仍为空。旧 BusyBox `stty` 不接受
1500000 参数，只说明用户空间 baud 常量受限，不能据此判断控制器速度。

主线 `drivers/pinctrl/pinctrl-rockchip.c` 的 `rk3228_mux_route_data` 明确包含：GPIO3_B5 function 1
会写 GRF `0x50` 的 bit 11，选择 `uart1-1_rx`。因此当前 DTS 的 RX pin 会触发正确 route，暂不
进行无证据的 TX/RX 交换。

A1r3 增加原厂 DT 已确认的 BT_WAKE=GPIO3_D3、高有效，用第二个 deferred-probe-safe always-on
fixed regulator 拉高；HOST_WAKE=GPIO3_D2 仍不申请，UART/BT_REG_ON/kernel/initramfs 均不变。

```text
4565beff9522ac2f8add58da0c42325fe63dcc7e678004e877d69d27522e0511  build/artifacts/rk3229-phicomm-r1-wifi-bt-uart-a1r3.dtb
da312e92a29096e0d741dd85c23cfa5ec7654b2e04b546beecfda6de93a8153a  build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1r3.itb
```

FIT payload 与源产物逐字节一致；反编译 DT 确认 GPIO2_D5 BT_REG_ON、GPIO3_D3 BT_WAKE 均为
always-on high，且无 gpio-hog/serdev child。A1r3 尚未实机。

### Bluetooth UART A1r3 实机边界与 A1r4 低速时钟候选（2026-08-11）

A1r3 实机确认 GPIO93 BT_REG_ON 与 GPIO123 BT_WAKE 均为 output-high；UART1 alternate TX/RX/CTS/RTS
四个 pinmux 均由 `11020000.serial` 正确占用。发送 HCI Reset 前后 `/proc/tty/driver/serial` 从
`tx:0 rx:0` 变为 `tx:4 rx:0`。这是 R1 上的已验证事实：Linux UART 已发送完整 4-byte command，
但 controller 没有返回任何字节。由此优先排查 controller 侧低速时钟与上电时序，而非 HCD。

主线 multi_v7 config 原为 `CONFIG_COMMON_CLK_RK808=m`，但当前 initramfs 不加载模块；因此 RK805
可控的 CLK32KOUT2 没有 provider/consumer 保持开启。A1r4 将该驱动改为 built-in，把 PMIC binding
修正为一个 clock argument，并用 always-on `regulator-fixed-clock` 请求 `<&rk805 1>`。UART、
BT_REG_ON、BT_WAKE、initramfs 和其余板级 DT 保持不变。

```text
1de056dcd669d7cddc79fccdb064804fdccdf57d80bbb03117735f7f75b15e34  build/artifacts/zImage-wifi-bt-uart-a1r4
2462b8fda5e1277289e98627e3ef0c60022cab93f52c7060ad47cbb1a3ae9883  build/artifacts/rk3229-phicomm-r1-wifi-bt-uart-a1r4.dtb
3bc999abb1ba313d369afa2d9dde0f5e7d41cb1aee2299145f553435001a89d1  build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1r4.itb
```

最终 config 已确认 `CONFIG_COMMON_CLK_RK808=y`；反编译 DT 已确认 PMIC `#clock-cells=<1>`、
clock-backed always-on consumer 请求 CLK32KOUT2。A1r4 尚待 RAM-only 实机验证，不能据此认定
PCB 上蓝牙 LPO 一定连接到 RK805 CLK32KOUT2。

用户根据模组丝印补充硬件型号 AW-CM256SM。AzureWave 的 `AW_CM256SM_DS_Rev15_CYW.pdf`
（AW-CM256SM datasheet，AzureWave，直接链接
<https://www.azurewave.com/img/wireless-modules/AW_CM256SM_DS_Rev15_CYW.pdf>）确认该模组主芯片为
CYW43455、WLAN host interface 为 SDIO、Bluetooth host interface 为 UART。AzureWave
`AW-CM256SM Reference Design Guide Rev. A`（2020-05-07，镜像链接
<https://manualzz.com/doc/77899805/azurewave-aw-cm256sm-design-guide>）显示模组具有独立 LPO、BT_REG_ON、
BT_WAKE/BT_DEV_WAKE 与 BT_HOST_WAKE；其启动图要求 LPO 存在，并在 BT_REG_ON 上升前由 host
把 BT_DEV_WAKE 驱动为低。资料是模组级证据；R1 PCB 的 LPO 实际布线仍未直接测量。

因此 A1r3/A1r4 将 GPIO3_D3 一直驱动为高不符合该启动时序。A1r5 复用 A1r4 kernel，只修改 DT：
GPIO3_D3 改为 physical-low，使用 supply dependency 表达 `CLK32KOUT2 → BT_DEV_WAKE → BT_REG_ON`，
并删除 BT_REG_ON 的 `regulator-boot-on`，避免 fixed-regulator 在解析依赖前立即输出高。

```text
a9b9032f345a20053d142ac5af6d7ba6575efc6395551e1e9530c76afcc917fd  build/artifacts/rk3229-phicomm-r1-wifi-bt-uart-a1r5.dtb
3ac5b522c6512b4ba008f4838417fa636c61e14291b1c85d33dc690a69212555  build/artifacts/r1-linux-multiv7-v9-wifi-bt-uart-a1r5.itb
```

A1r5 的 DT/FIT 已静态核对，尚待实机；在收到 HCI response 前不能标记 Bluetooth UART 完成。

A1r5 实机确认 GPIO93 BT_REG_ON 为 high、GPIO123 BT_WAKE 为 physical-low，RK805 CLK32KOUT2
enable/prepare count 均为 1 且频率 32768 Hz；发送 HCI Reset 后仍为 `tx:4 rx:0`。因此静态保持
BT_WAKE low 不能建立 controller response。主线 5.10 `hci_bcm.c:bcm_gpio_set_power()` 的本地
源码显示标准流程是先启用 supplies/LPO，再依次 assert shutdown 与 device-wakeup，等待
100–120 ms；下一步应测试 BT_WAKE 在 REG_ON 后由 low 转 high，而不是继续保持 low。

手册补充还说明 WL_REG_ON 与 BT_REG_ON 在 AW-CM256SM 内部 OR；只要 Wi-Fi 的 WL_REG_ON 保持
high，单独切换 BT_REG_ON 就不会触发完整模组 POR。这解释了此前 regulator unbind/rebind 为何
不是有效冷复位。当前 Wi-Fi `mmc-pwrseq-simple` 已有 200 ms post-power-on delay，满足手册要求
VDDC/VDDIO 过 POR threshold 后至少等待 150 ms 再访问 SDIO。

### Bluetooth serdev A1r6 主机端构建（2026-08-11）

A1r6 停止用 fixed-regulator 模拟蓝牙状态机，改用主线 5.10 `hci_bcm` serdev。BT core、BCM、
HCI UART/H4/serdev 均改为 built-in；DT 子节点绑定 GPIO2_D5 shutdown、GPIO3_D3 device-wakeup、
GPIO3_D2 host-wakeup IRQ、RK805 CLK32KOUT2 lpo、RTS/CTS 与 1.5 Mbit/s operational baud。
Wi-Fi WL_REG_ON 及其已验证 200 ms pwrseq 保持不变。

initramfs 新增原厂提取的 `BCM4345.hcd`，安装到主线搜索目录
`/lib/firmware/brcm/BCM4345.hcd`；源文件与打包后文件 SHA-256 均为
`7158831431b50178833c8cc8e72743d0704e1d74c8f18a266ecd49c01d193b70`。首轮仍以驱动日志给出的
精确 firmware filename 为准，不预设该通用名称必然命中。

```text
12a27fef73485d39e4b3636f79e7d2e4fa6b9889e513c080689e67e092f5ea37  build/artifacts/zImage-wifi-bt-serdev-a1r6
775a5c3a2b5d0057943b5a4fe2d0525a302e9fc28ac2e26466ffbfa986369994  build/artifacts/r1-initramfs-wifi-bt-serdev-a1r6.cpio.gz
d0216bbddb2da4735141ce6090a842782449c40f71d6506061cd41f773aae8bc  build/artifacts/rk3229-phicomm-r1-wifi-bt-serdev-a1r6.dtb
ccc6f63461bd903b7d366dc710a3b8d03215fd423d8cefbe09550b02ed13cfe3  build/artifacts/r1-linux-multiv7-v9-wifi-bt-serdev-a1r6.itb
```

最终 config、反编译 DT、initramfs HCD 和 FIT 三个 payload 已静态核对；A1r6 尚待实机，不能据此
声称 `hci0` 已创建。

A1r6 实机成功创建 `/sys/class/bluetooth/hci0`。主线驱动识别 chip id 107、features `0x2f`、
`BCM4345C0 (003.001.025) build 0000`；UART1 统计已有大量双向数据并显示 RTS/CTS，GPIO93 shutdown
与 GPIO123 device-wakeup 均为 high，RK805 CLK32KOUT2 为 32768 Hz 且已 enable。由此 Bluetooth
UART、pinmux、硬件流控、LPO、控制 GPIO、serdev 与 HCI ROM 链全部在 R1 实机验证。

唯一剩余错误是 firmware filename：驱动尝试 `brcm/BCM4345C0.hcd` 与 fallback `brcm/BCM.hcd`，
而 A1r6 只安装了内容正确但名称为 `BCM4345.hcd` 的原厂文件。A1r7 仅新增
`BCM4345C0.hcd -> BCM4345.hcd` symlink，kernel/DT 不变：

```text
a7314befca120610334b4a337d2f4f5fa1248ccdbd96ab403121ea4fb3eaa959  build/artifacts/r1-initramfs-wifi-bt-serdev-a1r7.cpio.gz
80f914a9c2100abe19b43774781d2295ffb4e7da05607f488032a4aee7a26552  build/artifacts/r1-linux-multiv7-v9-wifi-bt-serdev-a1r7.itb
```

A1r7 已静态核对别名指向原厂 HCD，尚待实机确认 Patch 下载成功和 controller build 变化。

A1r7 实机通过。`hci_bcm` 首先识别 `BCM4345C0 (003.001.025) build 0000`，随后明确命中
`brcm/BCM4345C0.hcd` 并打印 `Patch`；下载完成后重新报告
`BCM4345C0 UART 37.4 MHz wlbga_ref_iLNA_iTR_eLG 0124` 与 `build 0124`。`hci0` 持续存在于
`/sys/class/bluetooth`，UART1 统计达到 `tx:54092 rx:3205`，证明 HCD patchram 双向传输完成。
因此 R1 的 AW-CM256SM Bluetooth UART、主线 serdev/hci_bcm、LPO/控制 GPIO、硬件流控、原厂
HCD 与 `hci0` 阶段均已由实机验证。下一阶段转入最小 Bluetooth management 用户空间和扫描；
尚未声称 BlueZ 配对或 A2DP 可用。

### Bluetooth management A1r8 主机端候选（2026-08-11）

在 A1r7 kernel/DT/HCD 不变的前提下，新增 `tools/r1-btmgmt.c`。它是 freestanding ARM EABI
程序，直接打开 `HCI_CHANNEL_CONTROL` 并使用 Linux Bluetooth Management 协议，提供 controller
info、power on/off、BR/EDR inquiry 与 LE public/random discovery；不引入 D-Bus、`bluetoothd`
或完整 BlueZ。扫描输出不记录 controller/peer 地址，只打印类型、RSSI 与可打印名称。

可复现构建与封装命令：

```sh
scripts/build-r1-btmgmt.sh
R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt \
INITRAMFS_ARTIFACT_TAG=wifi-bt-mgmt-a1r8 scripts/build-initramfs.sh
mkimage -f scripts/r1-linux-multiv7-v9-wifi-bt-mgmt-a1r8.its \
  build/artifacts/r1-linux-multiv7-v9-wifi-bt-mgmt-a1r8.itb
```

```text
10fb72fd582d4647ff1613e0586d9615382e046c86ad0ff0bae262a24f52ce44  build/artifacts/r1-btmgmt
2e0abf4f9a188c1b86e201386cc7c6913d581a8ff52311b910f4c67ffbbf4888  build/artifacts/r1-initramfs-wifi-bt-mgmt-a1r8.cpio.gz
beed6cfee4cc99200a3fcf4a1b2853a9f0d79c00f48824260e7d8935477de2fb  build/artifacts/r1-linux-multiv7-v9-wifi-bt-mgmt-a1r8.itb
```

静态审计确认 ELF 无动态依赖/未解析符号，`GNU_STACK` 为 `RW`；initramfs 内 `r1-btmgmt`、
`r1-wifi-scan`、HCD alias 以及 FIT 三个 payload 均核对通过。另加入可 Ctrl-C 中止的
`r1-bt-coexist-test [cycles]`，每轮依次做 Wi-Fi、LE、BR/EDR 扫描并输出 uptime/IPI。以上仍为
主机端候选，A1r8 尚未实机，不能标记 management、扫描或共存测试完成。

A1r8 首次上板后，`info`、`power on` 等所有带参数调用都只打印 usage。该结果发生在建立 management
socket 之前，因此不是 Bluetooth/controller 故障。反汇编确认首版 C `_start()` 在读取初始
`argc/argv` 前允许编译器生成函数序言，导致读取的已经不是进程入口栈。现已将 `_start` 改为
`naked` 汇编入口：先从原始 SP 取 argc/argv，再调用 C `main`；反汇编明确显示 `_start` 的第一条
指令即保存 SP，随后才 `bl main`。修正版重新封装后的三项 SHA-256 如上，等待再次上板验证。

入口修正版上板后 `info` 成功读取 BCM4345C0 build 0124、manufacturer 15 及 BR/EDR/LE/SSP/
Secure Connections/advertising/privacy/PHY 等 supported settings；rfkill 为 state 1、hard 0、soft 0。
但 `power on` 静默返回 1，current settings 仍只有 BR/EDR。源码复核发现 ARM EABI 的直接
`send`/`recv` syscall 都有第四个 `flags` 参数，首版封装只装载 r0-r2，导致 r3 残留；无 payload
的 `READ_INFO` 偶然成功，而带 1-byte payload 的 `SET_POWERED` 在 send 阶段失败。现已改为
四参数 syscall 并显式设置 flags=0，同时让 power send failure 打印 errno；上面校验和已再次
更新，等待第三次上板。该失败与 controller、HCD 或 rfkill 无关。

四参数 syscall 修正版实机成功执行 `SET_POWERED`，返回 `powered=on`、退出码 0，current settings
由 `0x80` 变为 `0x81`。controller 为 manufacturer 15 的 BCM4345C0 build 0124，management
version 7；supported settings 明确包含 BR/EDR、LE、SSP、Secure Connections、advertising、
privacy 与 PHY configuration。power 过程中内核提示无法创建 CMAC crypto context，说明当前
内核未内建所需 CMAC crypto，暂不阻塞 discovery，但进入完整 BlueZ 前必须补齐。

BR/EDR inquiry 已正常开始并结束，现场 `scan_entries=0` 只说明测试时没有经典设备处于可发现状态。
随后 LE scan 返回 management `REJECTED (0x0b)`。5.10 源码的 `mgmt_le_support()` 表明 controller
虽支持 LE，但未置 `HCI_LE_ENABLED` 时必然拒绝；当时 current settings 也确实只有 powered 与
BR/EDR。工具现已在 LE discovery 前自动执行 `MGMT_OP_SET_LE=on`，上述校验和对应这一修正版，
等待再次上板验证 LE scan。

LE 自动 enable 修正版实机通过：`SET_POWERED` 和 `SET_LE` 均成功，20 秒 discovery 收到 29 个
advertising report 并以退出码 0 结束；随后 `READ_INFO` 的 current settings 为 `0x281`，明确
包含 powered、BR/EDR 与 LE。输出中的 address type 1/2 是 public/random，工具继续隐藏实际地址；
29 是重复广播在内的 report 数，不等同于 29 个唯一设备。板载天线当前已损坏，因此本结果只能
验证 management→HCI→BCM4345C0 firmware→LE radio discovery 链路，不能用 RSSI 推断正常覆盖。
CMAC crypto context 警告和 Wi-Fi/四核长时共存仍是下一步。

用户随后确认 `r1-bt-coexist-test` 通过，交替扫描期间 Wi-Fi 仍发现 5 GHz 网络，未报告四核、
uptime、SDIO、HCI 或 firmware 回退。A1r8 的最小 management 与共存阶段因此完成。

CMAC 警告的 config 审计显示 `CONFIG_CRYPTO_CMAC=y` 已正确内建，但它依赖的通用 AES 实现仍为
`CONFIG_CRYPTO_AES=m`；当前 initramfs 不加载 kernel modules，所以运行时无法解析 `cmac(aes)`。
A1r9 新增单变量 fragment 把 AES 改为 built-in。最终 config 与 A1r8 kernel config 的 diff 只有
这一行，DTB 与 A1r8 逐字节相同，initramfs 也保持不变：

```text
e7e932e2b41dd6a504376231634010f7d60d77d2376f501fc5b9d57f94c4a850  build/artifacts/zImage-wifi-bt-mgmt-a1r9
d0216bbddb2da4735141ce6090a842782449c40f71d6506061cd41f773aae8bc  build/artifacts/rk3229-phicomm-r1-wifi-bt-mgmt-a1r9.dtb
567a6775dc1bdbbc82180eda5b229588d4892dceb8d60081564cc14435391882  build/artifacts/r1-linux-multiv7-v9-wifi-bt-mgmt-a1r9.itb
```

A1r9 仍是主机端候选；成功标准是 `power on` 返回 0、current settings 保持 powered/BR-EDR/LE，
且 dmesg 不再出现 `Unable to create CMAC crypto context`。

A1r9 实机通过：`power on` 返回 0，10 秒 LE scan 收到 19 个 report 并以 0 退出，current settings
仍为 `0x281`；过滤 dmesg 只有正常 Bluetooth/HCD patchram 日志，不再出现 CMAC crypto context
错误。由此确认缺口确为未加载的 AES module，built-in 单变量修复有效。Bluetooth management、
两类 discovery、Wi-Fi/四核共存和进入配对前的 CMAC/AES 前置条件均完成；下一阶段固定 A1r9
kernel/DT/firmware，开始打包最小 BlueZ、system D-Bus 与配对 agent。

### eMMC 常驻启动 A1 主机构建与零写入测试准备（2026-08-11）

用户希望停止每次进入 MaskROM 后重复下载启动链。本轮先审计当前 RAM 调试版，确认其最终
`.config` 为 `# CONFIG_SPL_MMC is not set`、`CONFIG_SPL_YMODEM_SUPPORT=y`；它依赖先下载的
DDR 471 并从 UART 接收 FIT，不能原样写入 eMMC 自举。重新执行干净的
`phicomm-r1_defconfig` 后，标准配置则为 `CONFIG_SPL_MMC=y`、关闭 YMODEM，并固定
`CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR=0x4000`。

R1 板级 U-Boot DT 没有 EVB 的 `rockchip,sdram-params`，所以没有把未在 R1 冷启动验证的主线
TPL 当作基线。A1 改用已在本机 RAM 链验证的
`rk322x_ddr_300MHz_v1.06.bin`（7,196 B，SHA-256 `cab11c3a...`）作为 external TPL，后接
主线 MMC SPL。新增的 SPL boot-order patch 显式指定 `mmc0`；SPL 仍保留已验证的 secure
INTID 55/APR0 cleanup，U-Boot proper 仍提供只写 RAM 的 USB DFU，而
`CONFIG_SPL_MMC_WRITE`、`CONFIG_MMC_WRITE` 和 `CONFIG_DFU_MMC` 均关闭。

可复现构建命令：

```sh
scripts/build-r1-emmc-boot-candidate.sh
```

脚本复制当前 U-Boot 工作树到 `/tmp`，不清理或改写原工作树；锁定 DDR/OP-TEE 输入 SHA，应用
`patches/u-boot-phicomm-r1-emmc-boot-order.patch`，构建 external TPL + SPL + FIT，并用
`rkdeveloptool pack/unpack` 验证 RAM 诊断 loader。`rkdeveloptool` 即使只执行离线 `pack` 也会先
调用 `libusb_init()`，受限沙箱会返回 `-99` 且只写入自己的 log；本轮在非沙箱环境完成了同一
纯离线 pack，未执行 `db`、`wl`、`rd` 或任何设备命令。

FIT 解包后的 U-Boot、OP-TEE 和 DTB 与构建输入逐字节一致；Rockchip loader 解包后的 DDR、
SPL 和 FlashData 前缀一致，所有对齐尾部为零。固定产物：

```text
f311b58efacd241ad2e5e7f6915961674e73be563271e5e7177566950b7f1d7e  build/artifacts/r1-emmc-idbloader-open-optee-a1.img
6ee1da1dc69a9f98253924698e81aa1aff2b50b45a57d85576854912ec7a23ce  build/artifacts/r1-emmc-u-boot-open-optee-a1.itb
cdb31bdda0c1560c5eaae8fc48d52c2a35e583332b60d5d018ceb0f36af02625  build/artifacts/r1-emmc-spl-open-optee-a1.bin
b03c72fb67116147fa6976f9c7a00490f0c9e0937a89e9ee68a97f7c9d9f270b  build/artifacts/r1-emmc-mmc-spl-ram-test-a1-loader.bin
```

原厂 parameter 的绝对分区表再次核对为 `uboot@0x2000` 长 `0x2000` sectors、
`trust@0x4000` 长 `0x4000` sectors、`misc@0x8000`。A1 ID block 为 53,248 B，占
`0x40..0xa7`；FIT 为 826,880 B，占 `0x4000..0x464e`。它不会触及 misc/boot/recovery，
但会替换原 ID block 和 trust 前部，因此仍属于破坏性写入。当前结论仅为“主机构建和字节审计
通过”，不是“已可安全刷写”或“冷启动已验证”。下一步只执行 RAM-only MMC SPL 诊断：预期
DDR/SPL 启动后从现有 eMMC `0x4000` 读到原厂 trust，并报告非 FIT/坏 FIT；完成该 A/B、目标
复核、精确读回与真 MaskROM 恢复演练后，才请求对两个明确 LBA 区间的写入授权。

专用零写入诊断命令如下；不要使用会等待 YMODEM `C` 的 `boot-r1-optee-uboot.py`：

```sh
sudo python3 scripts/test-r1-emmc-mmc-spl.py
```

该脚本先独占 `/dev/ttyUSB0`，仅执行 `rkdeveloptool db`，保存原始串口到
`build/artifacts/r1-emmc-mmc-spl-ram-test-a1.log`，默认采集 12 秒后释放串口。验收组合为
`Trying to boot from MMC1` 与随后针对原厂 trust 的 non-FIT/bad-FIT 错误；若没有两项同时出现，
保持 eMMC 不变并先分析日志。

#### A1 零写入实机失败与 A2 修正

A1 RAM-only 实机输出为：

```text
Trying to boot from RAM
Error: -19
Trying to boot from RAM
Error: -22
SPL: Unsupported Boot Device!
```

DDR v1.06 和主线 SPL 已正常运行，但没有出现 MMC1，因此 A1 未访问 eMMC。两个 RAM 行来自
Rockchip RAM loader 与 common SPL RAM loader 同时注册到 `BOOT_DEVICE_RAM`，不是 MMC 被误打印。
直接解析 A1 `u-boot-spl.bin` 末尾 DTB，确认 `/chosen` 虽保留字符串 `mmc0`，但 `/aliases` 与
`/mmc@30020000` 都被 SPL fdtgrep 裁掉；`board_boot_order()` 无法把 `mmc0` 解析为设备节点，
而 USB boot source 已预先加入 RAM，使最终列表只剩 RAM。

A2 将 boot order 改为 DTS path reference `&emmc`，并在 eMMC 节点增加 `bootph-all`。重新构建、
pack/unpack 和 FIT payload 比对均通过；解析 A2 SPL DTB 已确认：

```text
u-boot,spl-boot-order = "/mmc@30020000"
/mmc@30020000 status = "okay"
compatible = "rockchip,rk3228-dw-mshc", "rockchip,rk3288-dw-mshc"
/clock-controller@110e0000 retained
```

```text
f6d59fc3499858fe8a36875062875e12907026c831ac311ca57220c503802622  build/artifacts/r1-emmc-idbloader-open-optee-a2.img
bb2e5ab94c96f0184e01440993309cacb48738cf72f309f2b34dd38b4ae9847b  build/artifacts/r1-emmc-u-boot-open-optee-a2.itb
61c47339f63716088f81b9a14e4eb00cd0578d14127e599175d247ab5e4fdaa7  build/artifacts/r1-emmc-spl-open-optee-a2.bin
c0e4cb1977a34e8d18d91a3736063298dc0375c784800d63952e3df8a4e01a1c  build/artifacts/r1-emmc-mmc-spl-ram-test-a2-loader.bin
```

`scripts/test-r1-emmc-mmc-spl.py` 的默认 loader、日志名和强制 SHA 已切换到 A2。A1 保留为失败
证据；下一次仍执行同一命令，不写 eMMC：

```sh
sudo python3 scripts/test-r1-emmc-mmc-spl.py
```

#### A2 实机进入 MMC1 与 A3 首读探针（2026-08-11）

A2 RAM-only 实机首次出现：

```text
Trying to boot from MMC1
mmc_load_image_raw_sector: mmc block read error
Error: -38
```

已验证事实是 SPL DT、boot-order 映射和 MMC1 loader 路径均已工作；设备仍未被写入。源码审计
同时发现旧提示具有误导性：`mmc_load_image_raw_sector()` 对 `spl_load()` 的任何非零返回都打印
“block read error”，而 A2 又启用了 `CONFIG_SYS_MMCSD_FS_BOOT=y`、没有启用任何 SPL 文件系统。
因此 raw 路径失败后会 fallback 到 FS 路径并返回 `-ENOSYS`（`-38`），覆盖 raw 的真实返回码。
仅凭 A2 日志不能区分“块读失败”和“已读到非 FIT header 后解析拒绝”。

完整 User Area 备份的只读复核确认物理 LBA `0x4000` 前 8 bytes 为
`54 4f 53 20 20 20 20 20`（`TOS     `），与提取的原厂 `trust.img` 一致。A3 因而只在临时构建副本
的 MMC read callback 增加一次性探针，输出 sector、请求/返回块数和前 8 bytes，并单独打印 raw
返回码；不增加写能力。构建脚本继续验证 external DDR/SPL loader 的解包前缀、零填充及 FIT 三个
payload，未调用 `db` 或任何存储命令。固定诊断产物为：

```text
eab978cf23f688730ccddab7a7c52223b86ba2d4115d8f1bc152838411c26d05  build/artifacts/r1-emmc-idbloader-open-optee-a3.img
bb2e5ab94c96f0184e01440993309cacb48738cf72f309f2b34dd38b4ae9847b  build/artifacts/r1-emmc-u-boot-open-optee-a3.itb
5ccf9fcec6807ae583e906cf013e4500841b90e840099139e26c51a2426098c5  build/artifacts/r1-emmc-spl-open-optee-a3.bin
d4c52eb737d14182fb0a303fbbfe7dfec6292bed16a800edfc1f0a683b2a59a7  build/artifacts/r1-emmc-mmc-spl-ram-test-a3-loader.bin
```

下一次仍执行同一零写入命令：

```sh
sudo python3 scripts/test-r1-emmc-mmc-spl.py
```

通过标准是同时出现 `Trying to boot from MMC1`、
`R1MMC sector=4000 count=1 got=1 hdr=544f532020202020` 和 raw rejection。只有这三项均满足，
才可确认 SPL 从绝对 LBA `0x4000` 成功读到了原厂 trust header；仍不代表已经允许写 eMMC。

#### A3 实机确认双 LBA 视图与 A4 修正（2026-08-11）

A3 实机的块读成功，但 header 与原先预期不同：

```text
Trying to boot from MMC1
R1MMC sector=4000 count=1 got=1 hdr=4c4f414445522020
R1MMC raw load rejected: -22
Error: -38
```

`got=1` 直接证明块读链路正常；`4c4f414445522020` 为 `LOADER  `，与原厂 `uboot.img` header
一致，而不是 trust 的 `TOS     `。这与此前主线 U-Boot 读取 recovery 时验证的地址关系完全
一致：mainline MMC 请求 LBA 比 Rockchip Loader/备份中的对应位置大 `0x2000` sectors。这里不再
混用“物理 LBA”措辞，而明确区分两个已观察到的地址视图。

因此 A3 否定了 `CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR=0x4000`。A4 改为：

```text
Rockchip loader/write view: FIT target LBA 0x4000
mainline SPL MMC view:      FIT read LBA   0x6000
```

构建脚本和 manifest 已拆分记录 `fit_write_lba` 与 `spl_fit_read_lba`，避免后续再次混淆。A4
仍只有读能力，host build、loader pack/unpack、零填充及 FIT payload 比对全部通过：

```text
81fea15cbc4b73cae51908b7e290c955eab074cb8b8db5260acb64f17c7811c5  build/artifacts/r1-emmc-idbloader-open-optee-a4.img
bb2e5ab94c96f0184e01440993309cacb48738cf72f309f2b34dd38b4ae9847b  build/artifacts/r1-emmc-u-boot-open-optee-a4.itb
1cf0d15303eb6438bec99cbeb0cd32d5b73978fb577c918a37a409f611241bae  build/artifacts/r1-emmc-spl-open-optee-a4.bin
429bb70e19c39d2d92de75c92895c0b417dacfb31438052a79b43b22cbc3d65d  build/artifacts/r1-emmc-mmc-spl-ram-test-a4-loader.bin
```

下一步继续运行同一 `sudo python3 scripts/test-r1-emmc-mmc-spl.py`。通过标志变为
`R1MMC sector=6000 count=1 got=1 hdr=544f532020202020`；仍不写 eMMC。

#### A4 实机通过与写入前只读预检（2026-08-11）

A4 RAM-only 实机得到：

```text
Trying to boot from MMC1
R1MMC sector=6000 count=1 got=1 hdr=544f532020202020
R1MMC raw load rejected: -22
Error: -38
```

由此验证 mainline MMC LBA `0x6000` 确实读取到原厂 trust 的 `TOS     ` header，且读取块数完整。
`-22` 是预期的非 FIT 拒绝；最终 `-38` 仍是 raw 失败后进入未实现 FS fallback 的结果，不是 MMC
错误。A4 阶段完成，但设备从未被写入。

为满足引导区写入前的恢复门槛，新增 `scripts/preflight-r1-emmc-install.sh`。脚本本地先锁定：

```text
parameter-idb.img  SHA-256 66aedcbf5f9e9070c731afa6e7ba1d1982eacba8ad7ff7c37dddd131b4c662e1
trust.img          SHA-256 2b0e823316de63e255a2194e305100f8db104d3dd98efcd8e30055c64105db45
IDB restore slice  SHA-256 5fd0979fbbecbde4e4c00555c04c739c9769b18923009bc715c012201afeaa13
FIT restore slice  SHA-256 1c4bc724e6a881db0f5d1aa0e862522a2db305e9c83eb752c7f549e8e92ed519
```

随后只允许 `ld/rci/rid/rfi/rl`，检查 A223、Samsung eMMC、15,269,888-sector 容量，并对
Rockchip LBA `0x40..0xa7` 和 `0x4000..0x464e` 各读取两次：两次结果必须相同，且必须逐字节
等于上述原始恢复切片。脚本不含 `wl/ef/rd`，不会改变 eMMC。下一实机命令：

```sh
sudo scripts/preflight-r1-emmc-install.sh
```

首轮运行在 `ld` 发现原始 `Maskrom` 后阻塞于 `rci`：原始 BootROM 枚举尚未运行 eMMC usbplug
协议，脚本也没有给查询设置超时。用户以 `Ctrl+C` 中止；在阻塞点之前没有执行 `rl`，更没有
写入。预检脚本已修正为先用 8 秒限时 `rci` 探测；若不可用，只对 RAM 执行一次已实机验证且
SHA-256 固定为 `13be7694...` 的 `rk322x_loader_v1.06.237.bin` `db`，等待重新枚举后再查询。
所有 `rci/rid/rfi/rl` 也增加明确超时。该 `db` 只下载 DDR/usbplug 到 RAM，脚本仍不含 eMMC
写入、擦除或复位命令。

修正版预检成功完成真 MaskROM → RAM usbplug、A223、Samsung eMMC 与容量查询，并对
`rl 0x40 0x68` 连续读取两次；两次设备读取彼此一致，但与最初从 `parameter-idb.img + 0x40`
生成的参考片从第一个字节就不同，脚本按设计停止，trust 尚未读取，设备未改变。

回查备份内容发现：`parameter-idb.img` 在 offset 0 为 `PARM`，随后每 1 MiB 重复一次 parameter，
而其 sector 64 全零。它是早期厂商 Rockusb 会话暴露的 parameter/逻辑视图，不包含当前 RAM
usbplug 在 raw sector 64 返回的实际 ID block，因此旧比较基准错误。U-Boot 官方 Rockchip
[flashing 文档](https://docs.u-boot.org/en/stable/board/rockchip/rockchip.html)（访问于
2026-08-11）规定 MMC idbloader 位于 sector 64；Rockchip Linux 的 `rkdeveloptool` commit
[`304f0737/main.cpp`](https://github.com/rockchip-linux/rkdeveloptool/blob/304f073752fd25c854e1bcf05d8e7f925b1f4e14/main.cpp)
也在 `upgrade_loader()` 调用 `RKU_WriteLBA(64, ...)`。同 commit 的
[`RKComm.cpp`](https://github.com/rockchip-linux/rkdeveloptool/blob/304f073752fd25c854e1bcf05d8e7f925b1f4e14/RKComm.cpp)
证明主机端把用户 LBA 原样放入 CBW；偏移/虚拟化来自设备端 loader 是结合源码与 R1 证据得到
的推断。

预检现改为：raw IDB 两次读取必须一致；首次通过后将该 53,248-byte 原件保存到 gitignored
`backup/boot/r1-emmc-user-idb-original-0x40-0xa7.img`，后续每次都必须与它逐字节一致；trust/FIT
目标仍与 `trust.img` 的前 1,615 sectors 比较。这里只新增主机恢复备份，不写设备。

第二轮预检确认 raw IDB 两次读取完全一致，并保存为：

```text
52c91233878ba72f8470be2aceaa0c8ff3c5c158120475c94f9c5cf187d82783  backup/boot/r1-emmc-user-idb-original-0x40-0xa7.img
```

但 usbplug `rl 0x4000 0x64f` 两次均得到 `LOADER  `，SHA-256 同为
`b1e363a9da5bda0d03a10ba7e6e5d9c17cc4bf851f7917ad3f4125df8ae8999d`，而不是旧逻辑备份中
`trust@0x4000` 的 `TOS     `。这与 A3 mainline MMC `0x4000 → LOADER` 完全相同，否定此前
“usbplug 写 `0x4000`、SPL 读 `0x6000`”的拆分假说。正确解释是：parameter 的分区地址属于
逻辑视图，raw eMMC、RAM usbplug `rl/wl` 与 mainline MMC 都必须加 `FwPartOffset=0x2000`。

因此 A5 最终部署地址统一为：

```text
raw IDB write/read: 0x0040..0x00a7
raw FIT write/read: 0x6000..0x664e
raw misc begins:    0xa000  (parameter logical 0x8000 + 0x2000)
```

A5 只改变构建 manifest 的写入命名空间；SPL 仍使用已经由 A4 实机验证的 `0x6000`。主机重新
pack/unpack 与 FIT payload 比对通过：

```text
81fea15cbc4b73cae51908b7e290c955eab074cb8b8db5260acb64f17c7811c5  build/artifacts/r1-emmc-idbloader-open-optee-a5.img
bb2e5ab94c96f0184e01440993309cacb48738cf72f309f2b34dd38b4ae9847b  build/artifacts/r1-emmc-u-boot-open-optee-a5.itb
1cf0d15303eb6438bec99cbeb0cd32d5b73978fb577c918a37a409f611241bae  build/artifacts/r1-emmc-spl-open-optee-a5.bin
62c328f5273db88baec1daa3999ab0620ccc3add7ec89ba93195941b519dc29a  build/artifacts/r1-emmc-mmc-spl-ram-test-a5-loader.bin
```

预检已切换为 raw `0x6000` 双读并与 `trust.img` 前 1,615 sectors 比较；下一次通过后，才准备
分别锁定 raw `0x40` 与 `0x6000` 的安装/恢复脚本。仍未获得或执行任何 eMMC 写入授权。

第三轮预检完全通过：真 MaskROM 自动 RAM-only 下载 usbplug 后，A223、Samsung eMMC 和
15,269,888-sector 容量均匹配；raw IDB 与 raw trust/FIT 两个目标分别连续读取两次且逐字节
一致，最终恢复哈希为：

```text
52c91233878ba72f8470be2aceaa0c8ff3c5c158120475c94f9c5cf187d82783  raw 0x40..0xa7
1c4bc724e6a881db0f5d1aa0e862522a2db305e9c83eb752c7f549e8e92ed519  raw 0x6000..0x664e
```

写入前只读阶段至此完成。新增 `scripts/install-r1-emmc-open-optee.sh` 与
`scripts/restore-r1-emmc-original-boot.sh`。默认运行只打印 dry-run；安装确认串必须精确为
`--confirm-write-raw-0x40-and-0x6000`，恢复确认串必须精确为
`--confirm-restore-raw-0x40-and-0x6000`，并强制传入已验证 USB `R1_LOCATION_ID`。安装顺序为
FIT 写入/读回后 IDB 最后写入/读回，异常时尝试用本轮原始切片恢复两个范围；恢复脚本独立执行
同样的原始切片写回与比较。静态复核又补上 usbplug 重枚举后的 LocationID 二次确认、所有身份
查询的超时，以及安装异常回滚后的两个范围读回比较；若回滚不能被确认，脚本会保留证据目录并
明确要求不得断电。二者都不 reset，便于先启动 UART capture 再冷启动。本轮只生成、语法检查、
`git diff --check` 并运行无确认参数的 dry-run，没有执行 `wl`。

### eMMC 常驻 A5 首次精确写入（2026-08-12）

用户明确授权 USB LocationID `502` 的 RK3229/Samsung eMMC raw `0x40` 和
`0x6000` 两个范围，以及失败时在相同范围恢复原厂切片。实机执行：

```sh
sudo R1_LOCATION_ID=502 scripts/install-r1-emmc-open-optee.sh \
  --confirm-write-raw-0x40-and-0x6000
```

脚本先重新运行完整只读预检：MaskROM 下只向 RAM 下载已验证 usbplug，
LocationID 502、A223、Samsung eMMC 与 15,269,888 sectors 均匹配；raw IDB/FIT 两个
原始目标各连续双读一致，恢复哈希分别为 `52c91233...` 和 `1c4bc724...`。

随后按安全顺序写入：先把 SHA-256 `bb2e5ab9...` 的 A5 FIT 写入 raw
`0x6000..0x664e` 并读回逐字节比较，再把 SHA-256 `81fea15c...` 的 A5
IDB 写入 raw `0x40..0xa7` 并读回比较。两个 `wl` 与两个 `rl` 均成功，
最终输出：

```text
A5 INSTALL WRITEBACK VERIFIED
Evidence directory: /tmp/r1-emmc-install.IapKsC
```

未触发现场回滚，脚本没有 reset，其他 LBA 没有写入。这只验证了存储介质上的
A5 字节与主机候选一致；BootROM 能否冷启动 IDB、SPL 能否从 MMC 装载 FIT、以及
OP-TEE/U-Boot proper 交接仍需下一次预先捕获 UART 的冷启动验证，不在此时提前标记成功。

### eMMC 常驻 A5 首次冷启动通过（2026-08-12）

在 picocom 以 1,500,000 baud、8N1、无流控预先打开日志后，对 R1 完全断电三秒并
不按 MaskROM 键重新上电。首次 eMMC 冷启动完整进入 U-Boot proper 提示符。
关键实机证据为：

```text
DDR Version V1.06 20171026
U-Boot SPL 2026.10-rc1-00121-g3ceba8432bef-dirty
Trying to boot from MMC1
R1MMC sector=6000 count=1 got=1 hdr=d00dfeed00000600
I/TC: OP-TEE version: 3.7.0-1-ga34a269b7-dev
I/TC: Initialized
Primary CPU switching to normal world boot
U-Boot 2026.10-rc1-00121-g3ceba8432bef-dirty
DRAM:  512 MiB (total 480 MiB)
MMC:   mmc@30020000: 0
=>
```

`d00dfeed` 是 FDT/FIT big-endian magic，同时 `count=1 got=1` 直接证明常驻 SPL 的 MMC
块读成功；OP-TEE `Initialized` 后 U-Boot proper 继续运行，证明 secure → normal
world 转交成功。本次没有使用 `rkdeveloptool db`、YMODEM 或 USB FIT 下载，因而
已验证 BootROM → vendor DDR → mainline SPL → open OP-TEE → mainline U-Boot proper
的 eMMC 常驻自举链。该阶段完成；尚未验证的是从这个常驻 U-Boot 启动当前
Linux FIT 后的四核、eMMC、Wi-Fi、Bluetooth 与 uptime >30 s 回归。

### 常驻 U-Boot 的 DFU RAM Linux 回归（2026-08-12）

首次执行 DFU 主机脚本时，物理 USB 尚未重新枚举为 DFU gadget，`dfu-util` 报
`No DFU capable USB device available`；该失败发生在主机枚举阶段，没有传输 FIT，也没有
访问 eMMC。重新插拔 USB 数据线后，`scripts/usb-dfu-r1-linux.py` 完成 A1r9 FIT 下载；
U-Boot `iminfo` 识别 FIT，并对 kernel、initramfs、DTB 的三个 SHA-256 全部输出 `+`。

随后无前缀的 `bootm 6a800000` 只打印 usage。源码与实机行为一致：地址含
字母 `a` 但没有 `0x` 前缀时，`do_bootm()` 的参数判别把它当成非法子命令。正确的
单行命令因而修正为：

```text
setenv dfu_alt_info 'linux-fit ram 6a800000 01000000' && dfu 0 ram 0 && iminfo 0x6a800000 && bootm 0x6a800000#config-1
```

用户重新进入后已由该 A1r9 FIT 到达 rescue shell，验证常驻 U-Boot → DFU RAM FIT
→ Linux/initramfs 的路径。本轮尚未提供 `cpu/online`、uptime、Wi-Fi 和 Bluetooth 回归输出，
所以这些检查继续保留为下一步，不由“进 shell”推断为已重测。

### AK7755EN Audio I2C A1 主机候选（2026-08-12）

用户实物确认 DSP 完整型号为 `AK7755EN`。AKM 官方
[`AK7755EN` product page](https://www.akm.com/global/en/products/audio-voice-dsp/lineup-audio-voice-processor/ak7755en/)
（访问于 2026-08-12）说明该器件是 36-pin HVQFN、RAM-based audio DSP，集成
mono ADC、stereo codec、mic/line-out amplifier，支持 I²C/SPI 控制，AD/DA 最高
96 kHz。这是型号级来源；R1 上 I2C1 `0x19`、I2S2 和 GPIO 映射继续由原厂
DT/日志与实物支持。

当前主机没有 ARM Linux sysroot/BlueZ/D-Bus 源码，只有 `arm-none-eabi` 裸机工具链；
因而不在 rescue initramfs 中临时堆叠一套不可维护的完整 BlueZ 根文件系统。进入
音频硬件时仍遵守单变量：新 DTS 继承已验证 A1r9 板级 DT，只将 RK3229
I2C1 (`0x11060000`) 以 100 kHz 启用。它没有声明任何 I2C child，不申请
AK7755EN PDN，不申请 TPA3118D2 shutdown/mute，不发送 I2C payload，也不启用
I2S/ASoC。

主机构建、反编译与 FIT 解包核对通过：I2C1 最终 `status="okay"`、
`clock-frequency=100000`，child 列表为空；kernel 和 initramfs 与 A1r9 逐字节相同。

```text
7cdf4ed322c3c25387151082ff84d00bf67216e6ca8029c8769eb9380ad80e49  build/artifacts/rk3229-phicomm-r1-wifi-bt-audio-i2c-a1.dtb
a56bfd890e18b7f532174b3e3042f1396efc01f947a0a2bfa6f53ec62907d060  build/artifacts/r1-linux-multiv7-v9-wifi-bt-audio-i2c-a1.itb
```

该候选尚未上板。首轮只验证 `11060000.i2c` adapter、GPIO0_A2/A3 pinmux、
PCLK_I2C1 和四核/无线旧功能；不在没有准确寄存器资料时用通用 `i2cdetect`盲扫音频总线。

Audio I2C A1 实机已通过控制器阶段：`/dev/i2c-0` 和 `/dev/i2c-1` 同时存在；
pin 2/3 (GPIO0_A2/A3) 都由 `11060000.i2c` 的 `i2c1-xfer` function 占用；
`pclk_i2c1` prepare count 为 2，空闲时 enable count 为 0，符合按需 clock gating。启动后
brcmfmac 固件与 BCM4345C0 HCD build 0124 均仍正常。`gpio35/111/113` 未出现在
debugfs GPIO consumer 列表，pinmux 也显示 GPIO1_A3、GPIO3_B7、GPIO3_C1 全部 unclaimed，
证明 A1 确实没有隐式控制 DSP/功放。

AKM AK7755EN 数据手册 revision `014006643-E-01` 的 I²C read sequence 要求先以 write
slave address 写入 read command，再 repeated-start 为 read slave address；Device Identification command
为 `0x60`，返回一字节 `01010101b` (`0x55`)。这比 SMBus quick 或全总线扫描更有
身份区分力，且不修改 control register。TI TPA3118D2 官方数据手册同时确认：
SDZ low 使输出 Hi-Z，MUTE high 使输出 Hi-Z。下一步 A2 必须保持这两个安全电平，
再拉高 AK7755EN PDN、等待至少 1 ms 并只执行 `0x60` 读取。在生成 A2 前，先对
GPIO1/GPIO3 的 direction/data/external-port 寄存器做只读采样，保留当前上电基线。

### AK7755EN Audio A2 安全身份读取候选（2026-08-12）

首轮只读 `devmem` 输出不能用于判断 GPIO3 基线：串口粘贴将第三条命令的 `32` 与下一条
`echo` 拼成 `32echo`，导致一条读取失败，剩余数值也不能无歧义对应到 DR、DDR 与
EXT_PORT。GPIO1 的 DR/DDR 均读到零，但第三个 EXT_PORT 结果同样缺失。因此不从该组输出
推断引脚实际电平，A2 直接建立确定性的安全状态。

A2 DTS 继承已经实机通过的 Audio I2C A1，并以三个 always-on fixed regulator 的 supply
dependency 建立顺序：先把 TPA3118D2 SDZ (GPIO3_B7/global 111) 置 low，再把 MUTE
(GPIO3_C1/global 113) 置 high，最后把 AK7755EN PDN (GPIO1_A3/global 35) 置 high，
PDN 节点声明 2 ms startup delay。I2S/ASoC 仍禁用，功放始终同时保持 shutdown 与 mute。
反编译 DT 审计确认 SDZ gpio flag 为 active-low，MUTE/PDN 为 active-high，依赖与延时均存在。

新增 freestanding ARM 工具 `r1-ak7755-id`，通过 `/dev/i2c-1` 的 `I2C_RDWR` 提交两个
message：对 `0x19` 写一字节 `0x60`，随后 repeated-start 读一字节。它不扫描其他地址、
不写 control register、不加载 PRAM/CRAM。返回 `0x55` 才退出 0。该事务来自 AKM
[`AK7755EN datasheet`](https://www.mouser.com/datasheet/3/5939/1/ak7755en_en_datasheet.pdf)，
revision `014006643-E-01` (2018-08)。功放安全电平来自 TI
[`TPA3118D2 datasheet`](https://www.ti.com/lit/ds/symlink/tpa3118d2.pdf)。

主机构建、FIT hash 检查与三个 payload 解包逐字节比较均通过：

```text
10a0a1797ba551be8a286b2120b5324351302fe6470688b2be09159b68914d88  build/artifacts/r1-ak7755-id
6895ca4ec9332344e68e8e34dde31ff8200e721741126bbb071e928d7d66fdc4  build/artifacts/r1-initramfs-wifi-bt-audio-ak7755-a2.cpio.gz
8b2f83c2512c81d55b533f632c81df2264e6f61b05c0d51b18b50724b1ebfcef  build/artifacts/rk3229-phicomm-r1-wifi-bt-audio-ak7755-a2.dtb
72e154e71cf9c0156617bfbe2976f1cccc915e6919933d82704cfffe5beba424  build/artifacts/r1-linux-multiv7-v9-wifi-bt-audio-ak7755-a2.itb
```

这是主机候选，尚未上板。实机必须先从 debugfs/EXT_PORT 验证 GPIO35=high、GPIO111=low、
GPIO113=high，确认功放保持安全后，才执行 `/bin/r1-ak7755-id`；不得把 FIT 构建成功写成
AK7755EN 身份已经验证。

### AK7755EN Audio A2 实机身份验证通过（2026-08-12）

用户经常驻 U-Boot 的 DFU RAM alternate 启动 A2。首先只读 debugfs GPIO 状态：

```text
gpio-35  (|regulator-ak7755-pdn) out hi
gpio-111 (|regulator-amp-shutdo) out lo ACTIVE LOW
gpio-113 (|regulator-amp-mute-s) out hi
```

随后分别读取 GPIO1/GPIO3 EXT_PORT，避免此前多行粘贴造成的歧义：

```sh
/bin/busybox devmem 0x11120050 32
/bin/busybox devmem 0x11140050 32
```

实机返回 `0xFFFE9F3E` 和 `0x128261FE`。前者 bit3=1；后者 bit15=0、bit17=1，
与 debugfs 独立一致地证明 AK7755EN PDN high、TPA3118D2 SDZ low、MUTE high。
确认功放仍同时处于 shutdown 与 mute 后，执行唯一身份事务：

```text
# /bin/r1-ak7755-id
AK7755EN device_id=0x55 expected=0x55
# echo "ak7755_rc=$?"
ak7755_rc=0
```

这不是通用地址 ACK：返回值与 AKM Device Identification command 的固定值匹配，故
R1 I2C1 地址 `0x19` 的器件身份已确认为 AK7755EN。A2 阶段没有启用 I2S/ASoC、没有
加载 PRAM/CRAM，也没有解除功放 shutdown/mute。该身份阶段完成；本次尚未重新提交
CPU online、uptime、Wi-Fi 与 Bluetooth 输出，下一步先做旧功能回归，再提取原厂最小
初始化序列。

### AK7755 公开驱动源码溯源（2026-08-12）

目标是优先找到 R1 原厂 AK7755 codec/machine driver 的可审计源码，避免直接从二进制
猜寄存器。先通过 GitHub API 获取 Rockchip 官方历史分支和递归 tree，例如：

```sh
curl --http1.1 -L --fail \
  'https://api.github.com/repos/rockchip-linux/kernel/git/trees/release-3.10?recursive=1' \
  -o /tmp/r1-rockchip-release-3.10-tree.json
rg -i 'ak7755' /tmp/r1-rockchip-release-3.10-tree.json
```

同样检查 `release-3.14`、`release-4.4`、`develop-4.4`、`others/kylin/brillo`、
`others/miniarm` 和 `others/multi-os`，七个 tree 均为零命中。`release-3.10` 固定为
`6c04d006ae881944d32c19e68b992d0bd5ab4fde`；其 `sound/soc/rockchip` 有 RK322x/I2S
等公版驱动，但没有 AK7755 codec 或 `rockchip-ak7755` machine driver。公开代码索引对
R1 精确字符串 `rockchip,ak7755-audio`、`ak7755,pdn-gpio`、
`ak7755_pram_data2.bin`、`PRAM CRC success` 和 `CRAM CRC success` 同样没有源码命中。
因此“R1 同源驱动未公开”是本轮检索结论，不等于证明网络任何角落都不存在副本。

唯一找到的 Linux AK7755 实现位于 themactep/ingenic-sdk commit
`8addc4a9acc93a4547dbd2a937f30a8c9745520a`：

```text
8b671c626b48c9a0d73bb6bbef3ffd3d30333bf93f1cf16699b80add50451d32  4.4.94/audio/a1/oss3/ex_codecs/ak7755_codec.c
7a9f43d398c4de8ed5d87f7dee53554a815cdb12356d773994208e2c98742d15  4.4.94/audio/a1/oss3/ex_codecs/ak7755_codec.h
```

固定链接：[codec C](https://github.com/themactep/ingenic-sdk/blob/8addc4a9acc93a4547dbd2a937f30a8c9745520a/4.4.94/audio/a1/oss3/ex_codecs/ak7755_codec.c)、
[register header](https://github.com/themactep/ingenic-sdk/blob/8addc4a9acc93a4547dbd2a937f30a8c9745520a/4.4.94/audio/a1/oss3/ex_codecs/ak7755_codec.h)。
源码包含 combined I2C read、`0xC0..0xEA` 寄存器、mute、sample-rate、PDN 与
POWERDOWN/STANDBY/RUN 状态；但是依赖 Ingenic 私有 OSS3 codec API，不是 ASoC。
头文件虽列出 PRAM/CRAM/OFREG/ACRAM enum，C 文件没有 `request_firmware()`、RAM
download 或 CRC 实现，所以不能重放 R1 已验证的 data2 启动路径。3.10 与 4.4 版本主要
差异是板级 GPIO 和少量 mode 配置，也没有补上 firmware download。

许可审计发现文件本身只有 `MODULE_LICENSE("GPL v2")`，没有 SPDX/版权头；仓库根
[MIT LICENSE](https://github.com/themactep/ingenic-sdk/blob/8addc4a9acc93a4547dbd2a937f30a8c9745520a/LICENSE)
标的是 2024 thingino，不能无条件推定覆盖所有被汇集的旧 kernel 文件。因此不把该源码
复制进仓库，只记录固定来源并用于行为交叉检查。

另一个一级来源是 Microchip 官方 Harmony v1.11
[`Driver Libraries`](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/UserGuides/DriverLibraries_v111.pdf)
第 188–199 页：其明确列出 production 级 `drv_ak7755.c`，依赖 PIC32 I2C/I2S driver。
它可以帮助核对 AK7755 通用状态机，但不是 Linux/Rockchip 代码，也没有证明与 R1 data2
算法相同。

本轮结论：没有找到可直接移植的 R1 ASoC driver。下一步以 AKM 数据手册为规范、R1 原厂
DT/日志/firmware 为实机证据、上述实现为次级交叉参考，先从原厂 zImage 恢复函数/命令边界，
再编写最小 clean-room Linux 5.10 codec driver。功放继续保持 SDZ low + MUTE high，直到
firmware 下载和 CRC 状态均可读回验证。

### Ambarella AK7755 bootloader 样本审计（2026-08-12）

用户提供了 GitHub 仓库
[`nrnjnkr/dvt_factory_ak7755_HwCodec`](https://github.com/nrnjnkr/dvt_factory_ak7755_HwCodec/tree/cacdbbc9b9620ecc31e8b461758223e29fd55411)。
GitHub API 显示该仓库是约 440 MB 的 Ooma Butterfleye Gen2/Ambarella S2L factory firmware
SDK，而不是独立 Linux codec driver；仓库只有一次提交，作者 Niranjan K，提交时间
2018-08-14，固定 commit 为 `cacdbbc9b9620ecc31e8b461758223e29fd55411`，commit tree 为
`ec5e9b474657166989aa1c103d1b5b84f0d4825e`，GitHub 标记该提交未签名。

递归 tree 中唯一以 `ak7755` 命名的源码是：

```text
Ooma-Butterfleye-Gen2FW/source/s2l_linux_sdk/ambarella/boards/btfl/bsp/iav/codec_ak7755.c
SHA-256 e802cdaa19980789a2e1547d3f76c4d47ae37474850ba9b05ad49e57c99a2848
191 lines, 5778 bytes
```

固定源码链接：[Ambarella `codec_ak7755.c`](https://github.com/nrnjnkr/dvt_factory_ak7755_HwCodec/blob/cacdbbc9b9620ecc31e8b461758223e29fd55411/Ooma-Butterfleye-Gen2FW/source/s2l_linux_sdk/ambarella/boards/btfl/bsp/iav/codec_ak7755.c)。
本轮通过 GitHub API/固定 raw blob 读取并以 `rg` 审计，命令形式如下：

```sh
curl -L --fail \
  'https://api.github.com/repos/nrnjnkr/dvt_factory_ak7755_HwCodec/git/trees/master?recursive=1' \
  -o /tmp/r1-ak7755-hwcodec-tree.json
curl -L --fail \
  'https://raw.githubusercontent.com/nrnjnkr/dvt_factory_ak7755_HwCodec/cacdbbc9b9620ecc31e8b461758223e29fd55411/Ooma-Butterfleye-Gen2FW/source/s2l_linux_sdk/ambarella/boards/btfl/bsp/iav/codec_ak7755.c' \
  -o /tmp/r1-oob-ak7755-codec.c
sha256sum /tmp/r1-oob-ak7755-codec.c
rg -ni 'pram|cram|ofreg|acram|crc|firmware|download|spi|i2c|bypass' \
  /tmp/r1-oob-ak7755-codec.c
```

该文件是 Ambarella bootloader 的板级初始化函数，不是 Linux 驱动。它以 SPI mode 3、
1 MHz 访问 AK7755，先将 PDN 拉低 2 ms 后拉高；随后配置 `0xC0..0xEA` 寄存器，支持
8/16/48 kHz，并有明确标记为 `Bypass mode` 的三项寄存器更新。这使它可作为 AK7755
通用 reset、串行格式和 bypass 行为的独立交叉线索。它不适合直接移植到 R1：R1 控制链
已实机验证为 I2C1 `0x19`，而该样本走 SPI；文件没有 ASoC codec/machine driver，亦无
PRAM/CRAM/OFREG/ACRAM、firmware download 或 CRC 代码，所以不能解释 R1 原厂 `data2`
加载链。

许可边界是硬限制，而不是普通的“仓库未放 LICENSE”：该源码头明确称其为 Ambarella
confidential/proprietary，并禁止在没有签署 license agreement/NDA 时使用、复制、修改或
制作衍生作品；仓库元数据也没有检测到 license。故本项目不复制、不改写该实现或其序列。
后续只以 AKM 数据手册为寄存器规范，用该样本提示需要独立验证的假说；最有价值的新假说是
在下载 DSP firmware 前先设计一个功放仍保持 shutdown+mute 的 AK7755 bypass 状态测试，
但具体寄存器值必须从 AKM 资料和 R1 原厂行为独立恢复。

### 找到与 R1 data2 链同源的 GPL ASoC driver（2026-08-12）

用户继续提供两份固定源码。第一份是 hello/kasa commit
[`762398dc7ceff508a4ac834ff93b14955d802328`](https://github.com/hello/kasa/blob/762398dc7ceff508a4ac834ff93b14955d802328/ambarella/kernel/linux-3.10/sound/soc/codecs/ak7755.c)
的 Linux 3.10 `sound/soc/codecs/ak7755.c`；该提交作者 Jackson Chen、日期
2017-04-24、tree `cf0b45ad5c7e227bd86fe1b01cb681ccaa474a59`，GitHub 标为 unsigned。
第二份是 iesah/IPC-SDK commit
[`1986333e26bd50a453edca5749433071cf88b390`](https://github.com/iesah/IPC-SDK/blob/1986333e26bd50a453edca5749433071cf88b390/opensource/drivers/audio/oss3/ex_codecs/ak7755_codec.c#L231)，
提交作者 iesah、日期 2023-11-28、tree `62c1806bea29268384941c2ba764e22149635220`，
GitHub 签名验证为 valid。

固定 blob 的本地审计结果：

```text
d8c61b6310cac3345b15610f36fc5cfbbdf65940446e206c0c27230438a366a8  kasa ak7755.c (2952 lines, 81303 bytes)
e88a4b0eacd8cff7988f4d4a631a424a8531dab67abdc27e783d2667ab9769d3  kasa ak7755.h
47e806015fd931083fb117a3965d27f2df7b196a6c1138048a808171992bf064  kasa ak7755_dsp_code.h
9bd1d671a4fcc9fcf114b2530638e0cc71b8a0297c3b73ecafa9d95fe3f38986  kasa ak7755_pdata.h
8c6a22346beb23ffd1c0f1d0c43045f99fb8f79c83c6946b768dfe81c1536ec1  IPC-SDK ak7755_codec.c (684 lines, 17973 bytes)
7a9f43d398c4de8ed5d87f7dee53554a815cdb12356d773994208e2c98742d15  IPC-SDK ak7755_codec.h
```

Kasa 文件头表明这是 Asahi Kasei Microdevices 2014–2016 reference driver revision
2.01，许可为 GPL version 2 or later，且 `MODULE_AUTHOR` 指向 AKM 的 Junichi Wakasugi。
它不是 Ambarella 手写的几百行 boot init，而是完整的旧 ASoC codec driver：含 I²C/SPI、
register cache、DAPM/controls、`hw_params`/`set_fmt`/mute/trigger、`ak7755-AIF1` DAI、
PRAM/CRAM/OFREG/ACRAM firmware mode、RAM download、CRC16-CCITT 和读回命令 `0x72`。

R1 同源关系通过以下独立指纹建立：

1. Kasa driver 的 OF compatible、GPIO property、DAI 分别为 `akm,ak7755`、
   `ak7755,pdn-gpio`、`ak7755-AIF1`，均与 R1 原厂 DT/启动日志一致。
2. driver 以 mode `data2` 生成 `ak7755_pram_data2.bin`、`ak7755_cram_data2.bin`、
   `ak7755_ofreg_data2.bin`，恰好是 R1 原厂 system 中的文件名。
3. Kasa 定义的 PRAM/CRAM/OFREG write command 是 `0xB8/0xB4/0xB2`；R1 三个 data2
   文件首字节也分别为 `b8/b4/b2`。
4. 按源码的 CRC16-CCITT polynomial `0x1021`、初值 0 重算 R1 文件，结果为：

```text
9916  ak7755_pram_data2.bin
4453  ak7755_cram_data2.bin
96c1  ak7755_ofreg_data2.bin
```

其中 PRAM `0x9916` 和 CRAM `0x4453` 与原厂 `bringup_dmesg.md` 的 `Cal=... Read=...`
逐字相同。结合原厂日志也使用内部函数名 `ak7755_firmware_write_ram` 和
`ak7755_ram_download`，可将“Kasa AKM driver 是 R1 codec driver 的上游祖先”记为强推断；
尚未找到的仍是 Rockchip `rockchip-ak7755` machine driver 和 R1 厂商在该参考驱动上的
具体补丁。

第二份 IPC-SDK 并不是新的完整 driver。它与此前 themactep/ingenic-sdk 固定版本的头文件
SHA-256 完全相同；两个 C 文件的 unified diff 只有 5 个 hunk：reset GPIO、speaker GPIO、
双声道 gain fallback、重复的 I²S transfer mode 初始化和 reset GPIO 请求条件。两者都是
Ingenic OSS3 external-codec wrapper，没有 request_firmware、RAM download 或 CRC。因此
“看起来手搓”对这份判断基本正确；它只是板级删减/修改版，价值低于 Kasa AKM ASoC source。

Kasa source 同样不能不经审计直接复制。已确认至少存在这些问题：

- 依赖 Linux 3.10 的 `snd_soc_codec`/`snd_soc_register_codec` API，需要移植到 5.10 component；
- 全局 `ak7755_data` 使其只能安全支持单实例；
- `request_firmware()` 成功后所有路径都没有 `release_firmware()`；若 size/kmalloc 失败也泄漏；
- CRC mismatch 返回正数 `1`，而重试上层以 `ret >= 0` 提前退出并最终返回成功；
- OFREG/ACRAM filename 分支误用 `ak7755_firmware_cram[cmd]`；
- `ak7755_dsp_code.h` 没有独立许可头，且包含示例 DSP program，不纳入公开移植。

下一步不再从原厂 zImage 盲猜完整协议。以 GPL Kasa `ak7755.c/.h` 和 AKM 数据手册为规范，
先实现只支持 R1 I²C 的最小 Linux 5.10 component：regmap/gpiod、ID、reset、直接请求本地
data2 PRAM/CRAM、严格 size/CRC/错误返回和资源释放。第一阶段不带 SPI、misc ioctl、内嵌
DSP program、machine driver 或功放解除；功放继续保持 SDZ low + MUTE high。原厂 data2
二进制没有公开再分发许可，仍只留在 `backup/`/本地 initramfs，不能提交公开仓库。

## Linux 6.18 AK7755 安全 firmware verifier A3 主机构建（2026-08-12）

用户决定将 canonical 移植目标从 Linux 5.10 改为 6.18，减少未来迁移到 Armbian/主线时的
API 重写。对 5.10 与 6.18 的本地源码比较显示，本阶段使用的 component、firmware、gpiod、
regulator 和 I²C transfer API 均仍存在；明显版本差异主要是 I²C driver 的 probe/remove
回调签名。因此 A3 直接基于固定 Linux 6.18.42 实现，不额外维护一份 5.10 driver 副本。

新增源码 overlay、binding、Kconfig/Kbuild patch、config fragment 和 A3 DTS。驱动只实现：

- `0x60` repeated-start 读 ID，必须等于 `0x55`；
- reset/PDN 的受控释放，且先取得 `safe-supply`，保持功放 shutdown+mute；
- 只接受 data2 PRAM `0xB8` 与 CRAM `0xB4` 命令头，并限制文件大小；
- 对完整 firmware 计算 CRC16-CCITT (`0x1021`, init 0)，再以 `0x72` 读取器件 CRC 严格比较；
- 所有 firmware 引用均释放，错误路径重新断言 reset；
- 注册没有 PCM DAI 的 ASoC component，I2S2 继续 disabled。

第一次整核构建暴露了一个纯 Kbuild 错误：Makefile 目标写成
`snd-soc-ak7755.o`，实际 overlay 文件名为 `ak7755.c`，所以单对象测试虽通过，整核构建报
“没有规则可制作目标”。将目标修正为 `ak7755.o` 后重新构建成功；这说明新驱动验证不能
只停在 `make sound/soc/codecs/ak7755.o`，必须至少完成一次整核链接。

可复现构建命令为：

```sh
KERNEL_BUILD=build/kernel-6.18-ak7755-a3 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-fw-a3.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-fw-a3 \
scripts/build-kernel.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 \
R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt \
R1_AK7755_FIRMWARE=1 \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-fw-a3 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-fw-a3.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-fw-a3.itb
```

最终 config 已核对 `SMP/ARM_PSCI/ARM_PSCI_FW`、brcmfmac SDIO、Bluetooth HCI UART BCM、
AES/CMAC、I2C RK3X、ASoC、AK7755 和 firmware loader 均为 built-in。DTB 反编译确认
I2C1 `audio-dsp@19` 具有 `akm,ak7755`、reset GPIO、safe supply 和两份 firmware 名称。
initramfs 清单确认含 PRAM/CRAM、Wi-Fi/BT firmware 与 `r1-btmgmt`；没有加入会与内核 driver
争用 `0x19` 的旧 `r1-ak7755-id` 工具。

产物 SHA-256：

```text
85cc53fab64014c48bb617140ff5111605b0e9aefca3d78bf98a706cc31d7e57  kernel/overlays/linux-6.18.42/sound/soc/codecs/ak7755.c
6f9a31ed181775338bf79b1b4231026b751ac031659c93a19b3a9c5e7f2c2ada  build/artifacts/kernel-mainline-6.18-ak7755-fw-a3.config
63ee4975c90c244fce4b6cf44c1a9522cd1f03b5ed6444d042a01ff8b7e16c69  build/artifacts/zImage-mainline-6.18-ak7755-fw-a3
34ace61e54e991a9fd259909accfd49af0abc1b128fa7bc068f7f62778c731c4  build/artifacts/r1-initramfs-mainline-6.18-ak7755-fw-a3.cpio.gz
f3c76a6cc8a0abece8aa4f170075803d0d43975da8dcb788a08725814bda6b92  build/artifacts/rk3229-phicomm-r1-mainline-6.18-ak7755-fw-a3.dtb
3b5a4d788f7f66ab57c5dfc62d554b89754594c5a8473be2aff82a63a8e4679f  build/artifacts/r1-linux-mainline-6.18-ak7755-fw-a3.itb
```

`dumpimage` 分别抽取三个 FIT component 后均与输入逐字节相同。主机工具在列出 FIT 时会先
输出 `Truncated file`，但不影响 component 完整抽取、`cmp` 和内嵌 SHA-256；仍保留为主机
工具提示，不把它误记为板端验证。当前主机缺 `dtschema` 的 `dt-doc-validate`，因此正式
`dt_binding_check` 尚未执行。A3 仍只是主机候选；下一步必须 RAM-only 上板看到 ID `0x55`、
PRAM CRC `0x9916`、CRAM CRC `0x4453`，并回归四核、uptime >30 s、Wi-Fi 和 Bluetooth。

## Linux 6.18 AK7755 A3 RAM-only 实机验证通过（2026-08-12）

用户从 eMMC 常驻 U-Boot 的 DFU RAM alternate 启动 A3，`uname -a` 返回：

```text
Linux (none) 6.18.42-phicomm-r1-4core-wifi-a3-dirty #1 SMP Wed Aug 12 12:27:25 CST 2026 armv7l GNU/Linux
```

AK7755 driver 的实机日志为：

```text
[    1.704956] ak7755 1-0019: PRAM firmware ak7755_pram_data2.bin: 5308 bytes, CRC 9916 verified
[    1.827286] ak7755 1-0019: CRAM firmware ak7755_cram_data2.bin: 1113 bytes, CRC 4453 verified
[    1.828617] ak7755 1-0019: AK7755EN ID 0x55; firmware verified, DSP intentionally stopped
```

这同时验证了 I2C1 `0x19`、reset/PDN、两次大块 I²C write、CRC enable/readback 和两份
data2 firmware。CRC 与主机独立计算及原厂启动日志完全相同。该结论是 R1 实机事实；尚未
验证的是 PCM DAI、I2S2 时钟/格式、machine card 和声音输出，因为 A3 有意不实现它们。

无线和 SMP 回归结果：

- `/bin/r1-wifi-scan` 返回 `scan_entries=28`，频点同时覆盖 2412–2462 MHz 与
  5180–5825 MHz；BSSID 按工具设计隐藏。
- `/bin/r1-btmgmt power on` 返回 `powered=on`；controller 仍为 BCM4345C0 build 0124。
- LE 10 秒扫描返回 `scan_entries=20`，包含命名与匿名广播。
- `/proc/interrupts` 显示 CPU0–CPU3 四列；四核 IPI2/IPI3 均有非零计数。
- Bluetooth management 初始化时间戳为 31.867 秒，之后完整完成 10 秒 LE scan；因此即使
  本次没有单独粘贴 `/proc/uptime`，串口时间线也直接证明运行时间超过约 41 秒，满足 >30 秒。

日志另有三个 `rockchip-pm-domain ... sync_state() pending`，消费者分别为两个 video-codec
节点和 RGA。它们发生在 12.645–12.647 秒，不阻止 init、无线、SMP 或 AK7755 验证；本阶段
记录为未启用多媒体消费者导致的非阻塞提示，不将其误判为音频故障。

A3 阶段完成。下一步 A4 保持 GPIO111 shutdown、GPIO113 mute，不写 eMMC，先注册 AK7755
DAI、RK3229 I2S2 和最小 machine card。验收只到 ALSA card/PCM 枚举、DAI format 与 clock
contract；在明确 MCLK/BCLK/LRCK 和通道映射前不解除功放、不播放音频。

## Linux 6.18 AK7755 安全 DAI A4 主机候选（2026-08-12）

A4 继续采用 fail-closed 范围。原厂 DT 的 I2S2 pin 是 GPIO0_D2 RX、GPIO0_D3 TX、
GPIO3_B3 BCLK、GPIO3_B4 LRCK；原厂启动日志另有 `i2s2 has no mclk`。据此实现专用
`phicomm,r1-ak7755-sound` machine driver：CPU 是 BCLK/LRCK provider，codec 是 consumer；
AK7755 从 BICK 派生内部时钟。首版 DAI 只接受 48 kHz、stereo、S16，machine driver 强制
32fs，并把 I2S controller 内部 clock 请求为 12.288 MHz。DSP 仍停止，功放 shutdown/mute
regulator 与 A3 完全继承，不提供任何解除功放的路径。

可复现构建命令：

```sh
KERNEL_SRC=$PWD/build/kernel-src \
KERNEL_BUILD=$PWD/build/kernel-6.18-ak7755-dai-a4 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-dai-a4.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-dai-a4 \
scripts/build-kernel.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-dai-a4 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-dai-a4.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-dai-a4.itb
```

整核链接和 DT 编译通过；最终 config 中 `SND_PCM`、`SND_SOC_ROCKCHIP_I2S`、
`SND_SOC_AK7755`、`SND_SOC_PHICOMM_R1_AK7755` 均为 built-in。DTB 反编译确认 sound node、
两个 DAI phandle、四个 I2S2 pin 和安全 regulator 均存在。FIT 为 14,339,592 bytes；三个
component 经 `dumpimage` 抽出后分别与输入 `cmp` 一致。SHA-256：

```text
80ccc4c77348fa3da8ff4c6fe9af4bdee3edb2aed3364f12682728c9bcd75936  build/artifacts/kernel-mainline-6.18-ak7755-dai-a4.config
843fdecc6c383bfbeaba77ff68a11bbcf7426ef7277524e75c2da89b294f5cd7  build/artifacts/zImage-mainline-6.18-ak7755-dai-a4
34ace61e54e991a9fd259909accfd49af0abc1b128fa7bc068f7f62778c731c4  build/artifacts/r1-initramfs-mainline-6.18-ak7755-dai-a4.cpio.gz
657b8f9fff815e5590abd5a63608f237e7790b005ef0019a305bcd57662533e4  build/artifacts/rk3229-phicomm-r1-mainline-6.18-ak7755-dai-a4.dtb
245e705f07ad5d0ed585ad91e2a3b9c3379e199ca54205cba7bc7aa75ef32ba5  build/artifacts/r1-linux-mainline-6.18-ak7755-dai-a4.itb
```

当前主机缺少 `dt-doc-validate`，binding formal check 未执行。A4 尚未上板；不能把 ALSA
card、PCM、12.288 MHz clock 或 pinmux 提前写成实机事实。下一步仅通过 eMMC 常驻 U-Boot
的 DFU RAM alternate 启动该 FIT，保持禁止播放。

## Linux 6.18 AK7755 安全 DAI A4 RAM-only 核心验证（2026-08-12）

用户以默认 hash-pinned DFU 脚本启动 A4，内核明确报告
`6.18.42-phicomm-r1-ak7755-dai-a4-dirty`，因此不是旧 FIT。PRAM 5308-byte CRC `0x9916`、
CRAM 1113-byte CRC `0x4453`、ID `0x55` 再次通过；machine link 报告 CPU clock provider、
48 kHz/stereo/S16/32fs，codec 报告 slave/BICK-derived、32fs、DSP stopped。

实机 ALSA 和硬件映射证据：

- `/proc/asound/cards`：card 0 `RK_AK7755`；
- `/proc/asound/pcm`：`AK7755 PCM ak7755-AIF1-0`，playback 1、capture 1；
- pinctrl：GPIO0_D2/D3、GPIO3_B3/B4 均由 `100e0000.i2s2` 占用为 RX/TX/CLK/SYNC；
- clock：`i2s2_frac`、`i2s2_pre`、`sclk_i2s2` rate 为 12.288 MHz；没有打开 stream 时
  `sclk_i2s2` gate 为 off，`hclk_i2s2_2ch` enable/prepare 为 1，符合 idle runtime 状态；
- debugfs GPIO：AK7755 shutdown high、功放 shutdown physical low/ACTIVE_LOW、功放 mute high；
- regulator summary：`amp_shutdown_safe -> amp_mute_safe -> 1-0019-safe`；
- `/sys/devices/system/cpu/online`：`0-3`。

Linux 6.18 的 debugfs 行显示动态 gpiochip 局部编号 `gpio-29/15/17`，与旧内核全局编号
`35/111/113` 不同；consumer 名、电平和 DT pin 仍一致。一次 `devmem` 命令因串口粘贴把
`32` 与下一条路径连成 `32/bin/busybox` 而失败，该失败不作为寄存器证据，也不影响此前
debugfs GPIO 与 regulator 两条独立安全证据。

A4 的 card/PCM、DAI contract、pinmux、clock rate、功放安全状态和四核 online 核心验收
完成，全程未打开 PCM、未播放、未写 eMMC。本轮没有贴出 A4 自身的 `/proc/uptime`、Wi-Fi
scan、Bluetooth LE scan 或 IPI 增长，故只把它们记录为待补回归；不能用 A3 的 >30 秒与
无线成功替代本版证据。

## Audio A5 全零 PCM clock/DMA 工具主机候选（2026-08-12）

用户明确要求先不补 A4 回归，优先添加工具。新增 freestanding ARM 工具
`r1-pcm-clock-test`，不用 alsa-lib，直接调用 ALSA PCM UAPI。为避免 ABI 猜错，源码在编译期
断言 ARM `struct snd_pcm_hw_params` 为 604 bytes，对应 ioctl `0xc25c4110/11`。参数被硬限制为
RW interleaved、48 kHz、双声道、S16_LE、1024-frame period、4 periods/4096-frame buffer；
payload 是静态零缓冲。默认持续 20 秒，输入限制 1–120 秒；EPIPE 会计数并 prepare，任何
其他错误立即 drop/close，xrun 非零时最终返回失败。

构建与打包命令：

```sh
scripts/build-r1-pcm-clock-test.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-pcm-clock-a5 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-pcm-clock-a5.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-pcm-clock-a5.itb
```

工具为 7,468-byte ELF32 little-endian ARM EABI5 static executable，符号表只有 ELF 0 号 UND，
没有运行时未解析符号。initramfs 清单确认含 `bin/r1-pcm-clock-test`、无线工具和 AK7755
firmware。FIT 14,340,996 bytes，低于 16 MiB DFU RAM alternate；三个 payload 经 dumpimage
抽出并分别 `cmp` 一致。SHA-256：

```text
f36d959d82dab252a7ad9d1e415b015e77b6b3eb37256e6cfc9bc50028b4cd91  build/artifacts/r1-pcm-clock-test
d624e87edbd1a124283d7ba31169b2847f62cf924a719ac6a4129419560c82c3  build/artifacts/r1-initramfs-mainline-6.18-ak7755-pcm-clock-a5.cpio.gz
bb59d10590d9c61add007a34c55c275766d8ea199df80759df4aea79305771f1  build/artifacts/r1-linux-mainline-6.18-ak7755-pcm-clock-a5.itb
```

A5 未修改已验证的 A4 kernel/DT，DSP 仍 stopped，功放仍由 shutdown+mute 安全链托管。当前
只完成主机构建和封装，不把 PCM 参数协商、DMA、运行态 clock gate 或 xrun 提前标为成功。
实机顺序必须是安全 GPIO 前检、后台零流、运行中 clock/IRQ/error 观测、安全 GPIO后检；
禁止非零 PCM，仍不写 eMMC。

## Audio A5 RAM-only 全零 PCM 验证通过（2026-08-12）

用户从 A5 FIT 进入 shell 后执行：

```sh
/bin/r1-pcm-clock-test 30 &
/bin/busybox sleep 2
/bin/busybox grep -Ei 'i2s2|sclk_i2s2|hclk_i2s2' /sys/kernel/debug/clk/clk_summary
/bin/busybox grep -Ei 'dma|i2s|100e0000' /proc/interrupts
```

工具协商到固定的 48 kHz、双声道、S16_LE、RW_INTERLEAVED、1024-frame period 和
4096-frame buffer，并报告 `zero_stream_seconds=30 state=running`。运行中 `i2s2_src`、
`i2s2_frac`、`i2s2_pre`、`sclk_i2s2` 的 enable/prepare 均为 1，后三者为 12.288 MHz；
PL330 `110f0000.dma-controller` IRQ 32 观察到 901 次。这里记录的是运行时活动快照，未在同一
输出中保存启动前 IRQ 基线，因此不把 901 写成精确增量，但结合 stream 正常完成足以证明
PCM DMA 链实际工作。

30 秒后工具输出：

```text
zero_stream_complete xruns=0
```

结束后的 clock summary 显示 `i2s2_src`、`i2s2_frac`、`i2s2_pre`、`sclk_i2s2` 的
enable/prepare 均回到 0，`hclk_i2s2_2ch` 仍为 1；这符合 stream clock runtime gate 关闭、
controller bus clock 保持的预期。过滤 dmesg 没有发现 PCM/I2S/DMA xrun、underrun、timeout
或新错误。日志中的 cpufreq `-19`、UART DMA request 和 brcmfmac board-specific firmware
首次查找 `-2` 均是此前已知且不阻塞相应功能的提示，不归因于 A5。

用户随后重复执行安全检查：

```sh
/bin/busybox grep -Ei 'amp|shutdown|mute' /sys/kernel/debug/gpio
/bin/busybox grep -A3 -B3 -Ei 'amp_shutdown_safe|amp_mute_safe' \
  /sys/kernel/debug/regulator/regulator_summary
```

结果仍为 AK7755 `shutdown` high、功放 shutdown physical low/ACTIVE_LOW、功放 mute high，
并保留 `amp_shutdown_safe -> amp_mute_safe -> 1-0019-safe` dependency。由此 A5 的正式结论是：
R1 上 ALSA PCM 参数协商、I2S2 stream clocks、PL330 DMA 和无 xrun 的 30 秒全零传输均通过，
且退出后的时钟与功放 fail-closed 状态正确恢复。本次没有解除功放、没有发送非零样本，DSP
仍 stopped；不能据此声称扬声器可播放、AK7755 DSP routing 正确或外部 BCLK/LRCK 波形已测量。

## Audio A6 fail-closed DSP RUN/STANDBY 主机候选（2026-08-12）

A5 已证明 ALSA→I2S2→PL330 可无 xrun 连续传输全零数据，但当时 AK7755 DSP 一直处于
stopped。A6 只改变这个状态边界，继续让 TPA3118D2 保持 shutdown+mute，也不加入任何非零
样本。规范来源是 Asahi Kasei Microdevices 的 GPL-2.0-or-later Linux 3.10 ASoC driver，
固定提交及链接为 hello/kasa commit
[`762398dc`](https://github.com/hello/kasa/blob/762398dc7ceff508a4ac834ff93b14955d802328/ambarella/kernel/linux-3.10/sound/soc/codecs/ak7755.c)，
本地固定文件 SHA-256 为：

```text
d8c61bb3a66ae7c61ad2d3f5e6c0c460465093f8279d97c742f5bd64a1ad35b8  /tmp/r1-kasa-ak7755.c
e88a4b4b6791c975fda9163c95bd7517fe5a4d41974706f8d064ccbf500417ba  /tmp/r1-kasa-ak7755.h
```

该来源的 RUN 状态先设置 C1 bit 0 `CKRESETN`，等待 10 ms，再设置 CF bits 3/2
`CRESETN|DSPRESETN` 并等待 10 ms；STANDBY 保持前者、清除后两位。A6 Linux 6.18 driver
复用这个顺序，但不照搬旧 ASoC 生命周期：PCM `.prepare` 才进入 RUN，最后一个打开 stream
的 `.shutdown` 立即回到 STANDBY。RUN/STANDBY 都以 repeated-start 读回 C1/CF；RUN 不匹配
会阻止 prepare；任一状态读回失败都会重新断言 AK7755 reset。driver 不持有也不修改功放 GPIO。

R1 原厂日志只明确记录 data2 PRAM/CRAM 加载及 CRC，没有 OFREG/ACRAM 下载行。因此 A6
不把 data2 OFREG/ACRAM 推断成 RUN 前置条件；这是待实机验证的保守假设，不是已经确认的
算法 routing。A4 DT 与 A5 全零工具保持不变。

可复现构建命令：

```sh
KERNEL_BUILD=build/kernel-6.18-ak7755-dsp-run-a6 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-dai-a4.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-dsp-run-a6 \
scripts/build-kernel.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-dsp-run-a6 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-dsp-run-a6.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-dsp-run-a6.itb
```

完整 multi_v7 内核链接通过；最终 config 中 `SND_SOC_ROCKCHIP_I2S`、
`SND_SOC_PHICOMM_R1_AK7755`、`SND_SOC_AK7755` 均为 built-in，localversion 为
`-phicomm-r1-ak7755-dsp-run-a6`。`checkpatch.pl --strict` 为 0 errors、0 warnings、6 个
非阻塞 CHECK。FIT 大小 14,345,092 bytes，低于 DFU RAM alternate 的 16 MiB；三个 component
分别用 `dumpimage` 抽出并与输入 `cmp` 一致。产物 SHA-256：

```text
297f3705058c04f1222b5ae494ed4b91eccd03ef32d74512363bdb23bf53d50f  kernel/overlays/linux-6.18.42/sound/soc/codecs/ak7755.c
972f9de6dd15ca5d6a6ed199cec66cb09559ee1ad8f94f1fe319d7a2ace2ae4a  build/artifacts/kernel-mainline-6.18-ak7755-dsp-run-a6.config
7d645658443dc64ee39cbb20266dc57494b79dd63d32983d1684b02c2e9bf606  build/artifacts/zImage-mainline-6.18-ak7755-dsp-run-a6
d624e87edbd1a124283d7ba31169b2847f62cf924a719ac6a4129419560c82c3  build/artifacts/r1-initramfs-mainline-6.18-ak7755-dsp-run-a6.cpio.gz
657b8f9fff815e5590abd5a63608f237e7790b005ef0019a305bcd57662533e4  build/artifacts/rk3229-phicomm-r1-mainline-6.18-ak7755-dsp-run-a6.dtb
bf7ff93e05c5c36407b04ebdf4dfcb16a32c86ca00cbfd348f4d5638721733de  build/artifacts/r1-linux-mainline-6.18-ak7755-dsp-run-a6.itb
```

当前只完成主机构建与静态审计，不能声称 R1 已进入 DSP RUN。下一步经 eMMC 常驻 U-Boot
把 A6 FIT 下载到 RAM，先检查功放安全 GPIO，再在前台运行 `/bin/r1-pcm-clock-test 10`。
验收必须同时得到 DSP RUN/STANDBY 寄存器读回、`zero_stream_complete xruns=0` 和测试后功放
仍 shutdown+mute；失败时停止，不运行非零 PCM。

## Audio A6 RAM-only 实机验证通过（2026-08-12）

用户经默认 hash-pinned DFU 脚本启动 A6，内核版本明确为
`6.18.42-phicomm-r1-ak7755-dsp-run-a6-dirty`。启动时 PRAM 5308-byte CRC `0x9916`、
CRAM 1113-byte CRC `0x4453`、AK7755EN ID `0x55` 和 `RK_AK7755` card 均正常。

测试前安全状态：

```text
gpio-29  shutdown                 out hi
gpio-15  regulator-amp-shutdo    out lo ACTIVE LOW
gpio-17  regulator-amp-mute-s    out hi
```

随后在前台执行 `/bin/r1-pcm-clock-test 10`。关键原始输出为：

```text
[   20.844335] ak7755 1-0019: DSP RUN armed: C1=0x21 CF=0xc; amplifier controls unchanged
pcm=48kHz stereo S16_LE access=RW_INTERLEAVED period=1024 buffer=4096
zero_stream_seconds=10 state=running
[   30.778254] ak7755 1-0019: DSP STANDBY verified: C1=0x21 CF=0x0; amplifier controls unchanged
zero_stream_complete xruns=0
pcm_rc=0
```

这里 `C1=0x21` 验证 32fs bit 5 与 CKRESETN bit 0；RUN 的 `CF=0x0c` 验证
CRESETN/DSPRESETN bits 3/2 均释放，最后关闭 PCM 后 `CF=0x00` 验证两位重新进入 reset。
测试后的三个 GPIO 值与测试前完全一致，独立证明 TPA3118D2 始终保持 shutdown+mute。

因此 A6 阶段完成：R1 已实机验证 AK7755 data2 firmware、受限 DAI、DSP RUN/STANDBY 读回、
10 秒全零 PCM 和无 xrun 的可回退链。日志中 cpufreq `-19`、UART DMA request 和 brcmfmac
board-specific firmware 首次查找 `-2` 均为既有非阻塞提示，本轮没有新增音频错误。尚未验证
的是 data2 算法 routing、DAC/ADC 模拟路径、外部波形、声道映射和扬声器输出；下一阶段必须
先设计受控 mute/unmute 与低幅测试，不能从本结果直接跳到普通音频播放。

## 2026-08-12：Audio A7 单命令并发 soak 主机候选

### 目的与安全边界

用户希望把此前分散的手工观测合并成较长的一条验证链。A7 保持 A6 kernel driver 与 A4 DT
不变，功放继续没有 unmute/enable 接口，playback 继续只能发送全零。新增 capture 方向只在
内存中计算统计，既不保存原始 PCM，也不输出样本。该阶段目标是一次覆盖 PCM 双向活动、DSP
状态、DMA、四核和无线共存；不是扬声器播放或音质测试。

### 实现

- `tools/r1-pcm-capture-test.c`：freestanding ARM ALSA capture 工具，固定 48 kHz、stereo、
  S16_LE、1024/4096 frames；输出左右声道 nonzero、peak 与近似 RMS；xrun 或双声道全零失败。
- `initramfs/r1-audio-soak`：默认 60 秒并发启动全零 playback/capture；在 PCM 活动期间执行
  Wi-Fi、BR/EDR、LE scan；前后核对功放安全 GPIO，并检查四核、PL330 IRQ、DSP RUN/STANDBY
  和错误日志。所有长操作都有 timeout，信号 trap 会杀掉 PCM 子进程。
- `scripts/build-initramfs.sh` 新增两个显式输入，只有工具存在且 ELF 静态 ARM 审计通过才打包。
- A7 只用 localversion fragment 区分内核，DTB 与 A4/A6 相同。

可复现主机构建命令：

```sh
scripts/build-r1-pcm-capture-test.sh

KERNEL_BUILD=build/kernel-6.18-ak7755-audio-soak-a7 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment kernel/config/r1-6.18-ak7755-audio-soak-a7.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-dai-a4.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-audio-soak-a7 \
scripts/build-kernel.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-audio-soak-a7 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-audio-soak-a7.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-audio-soak-a7.itb
```

### 主机验证结果

完整 multi_v7 内核和 DT 编译成功；A7 config 仍将 SMP/PSCI、brcmfmac、BCM HCI UART、
AES/CMAC、Rockchip I2S、AK7755 codec 与 R1 machine driver 全部 built-in。capture ELF 为
ARM EABI5 static，GNU_STACK 为 `RW`，符号表没有运行时 UND。initramfs 清单确认五个测试工具
均存在。14,348,252-byte FIT 低于 16 MiB，三个 component 经 `dumpimage` 抽出后均与输入
`cmp` 一致：

```text
88aea676bc170409e6248c8ce6837a648568a3aececd6cd068c23cb904cfd464  kernel-mainline-6.18-ak7755-audio-soak-a7.config
15e904c9c5686d6e719660440dec9df1c11a39325b13177c0547357c96cdba22  zImage-mainline-6.18-ak7755-audio-soak-a7
657b8f9fff815e5590abd5a63608f237e7790b005ef0019a305bcd57662533e4  rk3229-phicomm-r1-mainline-6.18-ak7755-audio-soak-a7.dtb
c224590b98a3bf1246d513a733561f1944c04fef7830d427642c6b04fbb22e5f  r1-initramfs-mainline-6.18-ak7755-audio-soak-a7.cpio.gz
e9be41a37de7a39f66ac9669bdbcf431b77a41170f5bcf6f36d157929f57c6dd  r1-pcm-capture-test
571c8927c705dd87aab9d935a30d90ddbfc3e4b47e3ad6b7f2b945af5e7c719d  r1-linux-mainline-6.18-ak7755-audio-soak-a7.itb
```

当前证据类型是主机构建产物，不是实机结果。默认 DFU 脚本已经锁定上述 A7 FIT/hash。下一步
在 R1 上运行 `/bin/r1-audio-soak 60`；只有最终出现 `AUDIO_SOAK_PASS` 且 shell 退出码为 0，
才能完成 A7。失败应保留并回传命令输出和其打印的 `/tmp/r1-audio-soak.*` 目录，不进入
功放解除或非零播放阶段。

### 首轮实机失败与 A7r2 修正

首轮实机正确启动 `6.18.42-phicomm-r1-ak7755-audio-soak-a7-dirty`，前后两次 GPIO 检查均为
功放 shutdown physical low/ACTIVE_LOW、mute high，CPU online 为 `0-3`。但所有 timeout
调用分别报告 `can't execute '30'/'15'/'75'`，playback/capture 返回 127；因此 DMA delta、
DSP RUN/STANDBY 都为零。这不是音频或无线失败，而是设备中的旧 BusyBox applet 要求
`timeout -t SEC PROG`，首版脚本采用了新式 `timeout SEC PROG`。

A7r2 已把五处调用全部改为旧版语法，并把 FIT description 和脚本 banner 标为 A7r2，便于
确认没有重复启动首版。kernel/DT/capture ELF 均未改变；重新打包后的 initramfs 与 FIT hash
已更新为上表值。该修正仍是主机候选，需再次运行同一条 `/bin/r1-audio-soak 60` 才能评价
PCM、capture、DMA、DSP 或无线共存。

### A7r2 实机完整通过

用户启动后确认脚本 banner 为 A7r2。测试前 GPIO 为 AK7755 shutdown high、功放 shutdown
physical low/ACTIVE_LOW、功放 mute high；CPU online 为 `0-3`。随后一次前台命令完成全部链路：

```text
DSP RUN armed: C1=0x21 CF=0xc
wifi_scan_entries=31
bt_bredr_scan_entries=1
bt_le_scan_entries=18
zero_stream_complete xruns=0
capture_frames=2880000 capture_xruns=0
dma_irq_before=0 dma_irq_after=5622 dma_irq_delta=5622
dsp_run_delta=1 dsp_standby_delta=1
AUDIO_SOAK_PASS seconds=60 full_duplex=1 wireless_coexist=1 amp_safe=1
```

最后一个 stream 关闭时硬件读回 `DSP STANDBY verified: C1=0x21 CF=0x0`。测试后功放两个
安全电平与测试前一致；uptime 为 `65.32` 秒，四核 IPI2/IPI3 均有活动。由此 A7 的 PCM
full-duplex 生命周期、PL330 DMA、DSP RUN/STANDBY、四核、无线共存和功放 fail-closed 条件
均已在同一 60 秒窗口验证。

必须保留一个边界：capture 左右声道都是 2,880,000 个 nonzero，但 peak 只有 1 LSB，近似
RMS 为 0。该统计证明数据不是全零且 capture DMA 在前进，却没有证明真实麦克风信号进入；
它很可能只是固定量化偏置/数字底噪。下一步先扩展只统计、不保存样本的 capture 工具，增加
DC 均值、min/max、变化次数，并做静音与近场声音 A/B；继续保持功放 shutdown+mute。

## 2026-08-12：Audio A8 保守实际外放主机候选

### 授权、目标与安全设计

用户在询问直接外放风险后，明确要求做一次保守的实际验证。A8 的目标只回答“R1 的既有
data2 program 是否把 Linux I2S playback 送到 DAC/TPA3118D2/扬声器”，不开放音量、频率、
时长或任意 PCM。真实声音只能由固定 `/bin/r1-audible-test` 触发。

DT 将原 A4/A7 中始终占用 GPIO3_B7/GPIO3_C1 的 always-on 安全 regulator 替换为两个默认
disabled gate。disabled 的电气状态仍为 SDZ physical low、MUTE physical high；最终 DTB
反编译确认 enable gate 是 active-high，unmute gate 是 active-low。AK7755 codec 的旧
`safe-supply` dependency 被移除，两个 gate 只交给 machine driver。

machine driver 新增 mode `0600` 的 `/dev/r1-audio-safety`，并要求 `CAP_SYS_RAWIO` 与 exclusive
open。ARM_MUTED 先保证 mute+shutdown，再只释放 SDZ并等待 20 ms；unmute 后看门狗缩短到
500 ms，工具每个约 21 ms PCM period 续期。正常 SAFE、file release、进程被杀、timeout、
driver remove 和 shutdown 都按 MUTE → 10 ms → SDZ low 回退。mute 操作失败也不会跳过
shutdown 尝试。

freestanding ARM 工具固定 48 kHz/stereo/S16_LE、1 kHz、peak 32/32767（约 -60.2 dBFS）、
100 ms 淡入/淡出、总长 1 秒。它先在 mute 状态送 8 个全零 period，再 unmute、按 period
续期并发送短音，最后送 4 个全零 period 后立即 SAFE。所有错误路径也请求 SAFE，内核
close/timeout 是独立的第二道保护。无声仍可能表示 data2 routing 没有把 I2S input 接到 DAC，
不能直接判定功放或扬声器损坏。

### 构建过程与遇到的问题

首先构建静态工具：

```sh
scripts/build-r1-audible-test.sh
```

首轮链接暴露两个工具实现错误：局部字符串初始化让 freestanding ELF 引入未提供的 `memcpy`，
`frame % 48` 又引入 `__aeabi_uidivmod`。补入最小 `memcpy` 并用 0..47 phase 递增替代运行时除法
后，ELF 成功静态链接，GNU_STACK 为 `RW`，符号表没有运行时 UND。

旧 `build/kernel-src` 已有上一轮 patch，重复执行 source prepare 会在 `arch/arm/Kconfig.debug`
冲突。因此保留旧树不清理，另建固定 v6.18.42 source：

```sh
KERNEL_SRC=build/kernel-src-a8 scripts/prepare-kernel-source.sh
```

首轮内核编译发现 Linux 6.18 已无 `no_llseek`，改为 `noop_llseek`。随后一次增量重试在前一
并行 make 尚未完全退出时误复用了同一 output directory，出现互相删除 `.o/.d` 的假故障；
它不是源码问题。保留现场并换全新 output directory 后完整构建通过：

```sh
KERNEL_SRC=build/kernel-src-a8 \
KERNEL_BUILD=build/kernel-6.18-ak7755-audible-a8-clean \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment kernel/config/r1-6.18-ak7755-audio-soak-a7.fragment kernel/config/r1-6.18-ak7755-audible-a8.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-audible-a8 \
scripts/build-kernel.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
R1_AUDIBLE_TEST_TOOL=build/artifacts/r1-audible-test \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-audible-a8 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-audible-a8.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-audible-a8.itb
```

### 主机验证与结论

完整 multi_v7 zImage 链接和 A8 DT 编译通过。最终 config localversion 为
`-phicomm-r1-ak7755-audible-a8`，AK7755 codec 与 R1 machine driver 均 built-in。initramfs
清单同时含 audible、A7 soak、playback/capture、Wi-Fi 和 Bluetooth 工具。FIT 为 13.7 MiB，
低于 DFU 16 MiB window；三个 component 用 `dumpimage` 抽出后都与输入 `cmp` 一致。
`checkpatch.pl --strict` 对两份 audio driver 得到 0 errors、0 warnings、6 个既有 alignment
CHECK。产物 SHA-256：

```text
9e828a366d46e316a095525798ff0a226c2cb27a03fb9e026b972a64aca1891e  kernel-mainline-6.18-ak7755-audible-a8.config
bf0b72a0ac4b1c1dfa39580b328a87d8676f36edebb39b383508630425825dfc  zImage-mainline-6.18-ak7755-audible-a8
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-audible-a8.dtb
191e3ecf9f2d0f990ccf7ddfeb0c5d23c9468cb3fec185dedecdeabf1fec4e28  r1-initramfs-mainline-6.18-ak7755-audible-a8.cpio.gz
2140038092475f22eb134d11a9c28e26114f12a494ebd6252d97f25edb0abc1b  r1-audible-test
8fd60b34bbb2de433ff58bd3553ad7bad7a1f85b25b2be4dd47cee56eb98ac1b  r1-linux-mainline-6.18-ak7755-audible-a8.itb
```

当前结论仅为主机候选就绪。默认 DFU 脚本已更新并锁定上述 FIT/hash；尚未运行 R1 实机，
所以不能声称扬声器已出声或无爆音。下一步只允许 RAM-only 启动 A8，先观察默认 SDZ low/
MUTE high 与 `/dev/r1-audio-safety`，再在前台运行一次 `/bin/r1-audible-test`，记录听感、退出码、
DSP RUN/STANDBY 和最终 GPIO。未通过前不执行普通音频文件、扫频或更高幅度测试。

### Audio A8 RAM-only 实机通过

用户从上述已锁定 hash 的 A8 FIT 启动后，在前台运行固定工具：

```sh
/bin/r1-audible-test
echo "audible_rc=$?"
/bin/busybox grep -Ei 'amp|shutdown|mute|unmute|output' /sys/kernel/debug/gpio
/bin/busybox dmesg | /bin/busybox grep -Ei \
  'audible test|keepalive|DSP RUN|DSP STANDBY|xrun|underrun|error|fail'
```

用户报告听到“一段很小声”的短音，`audible_rc=0`。保留的 dmesg 实际包含两次完整测试，
两次均按同一顺序执行：safe → armed（功放 enable、仍 mute）→ DSP RUN → UNMUTED → safe →
DSP STANDBY。第一次 UNMUTED 在 57.770859 秒，58.878812 秒即回到 safe；第二次分别为
67.190468 秒与 68.298810 秒。没有看到 xrun、underrun 或 keepalive timeout。

最终 GPIO 证据为：

```text
gpio-29  (                    |shutdown            ) out hi
gpio-15  (                    |regulator-amp-output) out lo
gpio-17  (                    |regulator-amp-output) out hi ACTIVE LOW
```

结合 A8 DT 极性，GPIO3_B7 low 表示 TPA3118D2 SDZ/shutdown 已断言，GPIO3_C1 high 表示 MUTE
已断言。这里已经实机验证从 Linux PCM/I2S2/PL330 DMA，经 AK7755 data2 DSP 和功放到扬声器的
受控真实出声闭环，也验证了正常结束后的 fail-closed 收口。用户未单独描述有无轻微 pop，故
该项仍是开放问题；本次结果也不证明左右声道、频响、失真、声压或高音量安全。下一阶段先把
A8 作为已知良好基线提交，再单独选择低风险的渐进音量/声道验证或 capture 静音/近场声音 A/B，
不直接开放任意音频文件和长时间 unmute。

## 2026-08-12：Audio A9/A9r2 固定低音量左右声道候选

A8 实机通过后先冻结为 Git commit `1354638`（`audio: verify guarded AK7755 speaker output`）。
A9 不修改内核功放门，只给 freestanding 工具增加编译期声道模式：左输入 750 ms、全零
512 ms、右输入 750 ms；两段均为 1 kHz、peak 32/32767（约 -60.2 dBFS）和 100 ms 淡入淡出。
全部零间隔和 tone period 都续期 500 ms 内核看门狗，退出路径仍由 SAFE/close 双重收口。

构建命令：

```sh
scripts/build-r1-channel-test.sh

KERNEL_SRC=build/kernel-src-a8 \
KERNEL_BUILD=build/kernel-6.18-ak7755-channel-a9 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment kernel/config/r1-6.18-ak7755-audio-soak-a7.fragment kernel/config/r1-6.18-ak7755-channel-a9.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-channel-a9 \
scripts/build-kernel.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
R1_AUDIBLE_TEST_TOOL=build/artifacts/r1-audible-test \
R1_CHANNEL_TEST_TOOL=build/artifacts/r1-channel-test \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-channel-a9 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-channel-a9.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-channel-a9.itb
```

完整内核链接成功；唯一 DT compiler 信息仍是上游 rk322x VOP graph 的既有 warning。最终 uname
localversion 为 `-phicomm-r1-ak7755-channel-a9`。静态 ELF 为 ARM EABI5、GNU_STACK `RW`、没有
运行时 UND。initramfs 同时包含 A8/A9、soak、capture/playback 与无线工具。FIT 的 kernel、
initramfs、DTB 三个 component 均抽取后与输入 `cmp` 一致；DTB 的两个功放 gate 仍分别为
GPIO3_B7 active-high 与 GPIO3_C1 active-low，并与 A8 DTB hash 相同。产物 hashes：

```text
563c4f252389e2a7b819086f9e88f1d0f0a5df19d66d2f1f3ebe115a58bf88c4  kernel-mainline-6.18-ak7755-channel-a9.config
7cb35a93a6956876c36866e6033d6ded77d41852569791add2e8fd1346270f62  zImage-mainline-6.18-ak7755-channel-a9
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-channel-a9.dtb
f574f78d6768844e2de47c13b71a71311ea94bee2be1d220cddb850adcaae435  r1-initramfs-mainline-6.18-ak7755-channel-a9.cpio.gz
a4d38aa741e1be7879b013f1b855d156d10820bf1fff727373123bd1023d3ca9  r1-channel-test
ea5948825cd359b44df9193e6984182d914bfc62281f8e322f0006fc4868ae36  r1-linux-mainline-6.18-ak7755-channel-a9.itb
```

默认 DFU RAM 脚本已钉到最后一个 hash。当前仍只有主机构建证据；下一步 RAM-only 启动 A9，
确认 uname 后只前台运行一次 `/bin/r1-channel-test`，记录两段听感、静音间隔、退出码、DSP
RUN/STANDBY 与最终 GPIO。A9 不授权提高音量或播放任意文件。

### A9 首轮听感与 A9r2 修正

用户上板后报告 512 ms 全零间隔太短，无法明确听出两段之间的停顿，并感觉该音响可能把左右
声道合并。这里能确认的是首轮听感无法分辨间隔；“左右被合并”仍是待验证解释，不能仅凭近距离
扬声器听感确定 DSP routing。A9r2 只把 `CHANNEL_GAP_PERIODS` 从 24 增加为 144，即 3.072 秒；
两段仍各 750 ms、1 kHz、约 -60.2 dBFS，内核、DTB、功放安全门和 500 ms keepalive 均不变。
重新构建工具、initramfs 和 FIT 后，最终 hashes 已更新为上表值。下一步重复相同 RAM-only
命令，重点确认长静音间隔以及两段是否来自相同物理位置；仍不增加音量。

### A9r2 数字全零仍可闻与硬件结构修正

用户运行 A9r2 后确认 3.072 秒标记为 silence 的阶段仍可听到声音，并补充实物结构是一只低音
单元加一只高音单元，不是左右两只扬声器。后者来自用户拆机观察；原厂 DT 只给 AK7755 和
TPA3118D2 一组 shutdown/mute GPIO，也没有描述两套左右功放，但没有原理图或逐线测量证明
具体分频与功放输出拓扑。因此当前只将其记为双单元单声道候选结构，暂停 L/R 定位。

源码复核确认 A9r2 第一段后执行完整 `memset`，并连续向 PCM 写入 144 个清零的 1024-frame
period；不存在直接复用 tone buffer 的实现错误。不过这段代码仍每 period 发送 KEEPALIVE，
TPA3118D2 全程保持 UNMUTE。“数字零仍有声”因而可能来自 data2 DSP 的内部延迟/状态、麦克风
旁路/反馈、DAC/模拟链或功放底噪，不能直接证明电路图或 GPIO 极性错误。下一步 Audio A10
应保持 DSP RUN 和全零 PCM，只切换同一已验证安全门的 UNMUTE/MUTE 做 A/B：硬件 MUTE 无声
则控制电路正确，应继续查 DSP/routing/模拟噪声；硬件 MUTE 后仍出声才提升电路拓扑差异的优先级。

### Audio A10：数字零下的硬件 MUTE A/B 候选

对 hello/kasa 固定 commit `762398dc` 的自动路径再次复核：`resume()` 只调用 data2 PRAM/CRAM
下载；OFREG/ACRAM 仅由独立 mixer control 或 ioctl 触发。R1 原厂启动日志也只明确记录
PRAM/CRAM。因此 OFREG 文件存在不等于当前 RUN 路径漏加载，本轮不增加 OFREG 变量。

A10 不重编 kernel/DTB，复用 A9 已验证的 fail-closed 功放门。新工具先给 1 秒、约 -60 dBFS
的 1 kHz 参考音，再在同一 PCM stream 内连续写数字零并依次产生三个约 2 秒窗口：功放
UNMUTE、硬件 MUTE、再次 UNMUTE。硬 MUTE 由 `ARM_MUTED` 完成，内部顺序为 mute→shutdown→
muted enable；ARM 状态 3 秒看门狗大于 2.005 秒窗口，unmute 状态继续每 period 续期 500 ms
看门狗。任意失败或进程退出仍执行 SAFE。

构建命令：

```sh
scripts/build-r1-audio-mute-ab.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
R1_AUDIBLE_TEST_TOOL=build/artifacts/r1-audible-test \
R1_CHANNEL_TEST_TOOL=build/artifacts/r1-channel-test \
R1_AUDIO_MUTE_AB_TOOL=build/artifacts/r1-audio-mute-ab \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-mute-ab-a10 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-mute-ab-a10.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-mute-ab-a10.itb
```

主机验证：工具为静态 ARM EABI5 ELF，GNU_STACK 为 RW、无运行时 UND；initramfs 包含精确路径
`bin/r1-audio-mute-ab`；FIT 的 kernel/initramfs/DTB 三个 payload 经 `dumpimage` 抽取后分别
与输入 `cmp` 一致。hash：

```text
7cb35a93a6956876c36866e6033d6ded77d41852569791add2e8fd1346270f62  zImage-mainline-6.18-ak7755-channel-a9
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-channel-a9.dtb
3322275b0739e5deed8142d1e852d7224521b0c0978332401e93801a9f4597a6  r1-audio-mute-ab
f31d8e7b82a2b2b70153747b7e28240d7d33fb2220a81bef154276a67a4da210  r1-initramfs-mainline-6.18-ak7755-mute-ab-a10.cpio.gz
837b57c7338aa5469a228b0b3d4c6efb47594aca890b744556ec1e21f88d9585  r1-linux-mainline-6.18-ak7755-mute-ab-a10.itb
```

这仍是主机构建证据。下一步上板只运行一次 `/bin/r1-audio-mute-ab`，按四个标签分别记录听感；
耳听到的固定音高先记为“窄带音/振荡”，不能在没有频谱测量时认定为 1 kHz。

### Audio A10 实机结果

用户在 A10 上前台运行 `/bin/r1-audio-mute-ab`。1 kHz 参考段和第一段 UNMUTE 数字零均有
声音；日志于 `37.918078` 秒执行第二次 ARM_MUTED，用户确认随后有一段无声；于
`39.883097` 秒重新 UNMUTE 后声音恢复，并持续于第二段数字零窗口。最终工具输出
`mute_ab_test=complete result=PASS`，随后 SAFE 和 DSP STANDBY 均完成。

这是实机听感与内核时序配对证据：约 1.965 秒硬 MUTE 窗口无声，解除 MUTE 后数字零声音
恢复。可以确认 GPIO3_C1 极性、regulator 路径和 TPA3118D2 MUTE 控制有效；不能再把 A9r2
现象优先归因于设备树 MUTE 极性错误。该结果仍不能严格把声源定位到功放输入之前，因为
TPA3118D2 的 MUTE 也可能压制其自身工作噪声；后续候选包括 AK7755 DSP/DAC/模拟输入、反馈和
功放未静音自噪声。若声音具有稳定音高而不是沙沙声，DSP/routing/反馈更值得优先检查，但
没有频谱证据时不把它写成确定的 1 kHz。

### Audio A11：固定低电平多音符候选

A10 已证明功放硬 MUTE 有效后，下一步没有直接开放 MP3/WAV 或任意 PCM，而是增加一个固定、
可审计的合成短句。`r1-melody-test` 在 48 kHz stereo S16_LE 下生成
`C C G G A A G / F F E E D D C`，总长约 4.8 秒，两路数据相同，peak 仍为 32/32767
（约 -60.2 dBFS）。每音符使用短淡入淡出和尾部数字零，安全 ioctl、500 ms keepalive、
close/错误/超时 SAFE 均与 A8-A10 相同。

构建命令：

```sh
scripts/build-r1-melody-test.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
R1_AUDIBLE_TEST_TOOL=build/artifacts/r1-audible-test \
R1_CHANNEL_TEST_TOOL=build/artifacts/r1-channel-test \
R1_AUDIO_MUTE_AB_TOOL=build/artifacts/r1-audio-mute-ab \
R1_MELODY_TEST_TOOL=build/artifacts/r1-melody-test \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-melody-a11 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-melody-a11.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-melody-a11.itb
```

构建生成的工具为静态 ARM EABI5、GNU_STACK RW、无运行时 UND。initramfs 同时包含
`bin/r1-melody-test` 与 A10 工具；FIT 的三个 payload 由 `dumpimage` 抽出后与输入 `cmp`
一致。hash：

```text
db94fc31f31fe2989e506e4093aa2c3896a64cc1c3109c0af738bc90bc6cb317  r1-melody-test
d82a80c04be1e4870866353559d2685e3c07a863e3f548c4499e7c190c2d1ba4  r1-initramfs-mainline-6.18-ak7755-melody-a11.cpio.gz
ac66904c2d46fff47d51890b505c98afb3bcc67a188bb77a3ad894c492e50fc1  r1-linux-mainline-6.18-ak7755-melody-a11.itb
```

当前只有主机构建证据。实机只需前台运行一次 `/bin/r1-melody-test`，记录是否能分辨音符变化、
是否始终叠加固定音或噪声、退出码与最终 SAFE/STANDBY；不提高音量。

### Audio A11 初步听感与 A12 分级扫频

用户运行 A11 后描述声音很小，更像“收音机搜不到频”的噪声，没有确认 C 大调音高变化。本轮
未提供工具退出码、SAFE/STANDBY 日志或录音，故只记录为主观低电平听辨失败。A11 peak 只有
32 个 S16 counts，除已知零输入底噪外，低有效量化级本身也可能带来明显失真；当前证据不足以
直接归因于 AK7755 DSP。

A12 改为三次已知线性扫频：每次从 300 Hz 升至 2 kHz、65536 frames（约 1.365 秒），peak
依次为 32/64/128，即约 -60.2/-54.2/-48.2 dBFS。每段前后 2048 frames（约 43 ms）淡入淡出，
段间 8192 frames（约 171 ms）数字零。最高档电压幅度只是 A11 的 4 倍，仍远低于满幅；安全
门、500 ms keepalive 和退出 SAFE 不变。

构建命令：

```sh
scripts/build-r1-sweep-test.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
R1_AUDIBLE_TEST_TOOL=build/artifacts/r1-audible-test \
R1_CHANNEL_TEST_TOOL=build/artifacts/r1-channel-test \
R1_AUDIO_MUTE_AB_TOOL=build/artifacts/r1-audio-mute-ab \
R1_MELODY_TEST_TOOL=build/artifacts/r1-melody-test \
R1_SWEEP_TEST_TOOL=build/artifacts/r1-sweep-test \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-sweep-a12 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-sweep-a12.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-sweep-a12.itb
```

五种编译模式（原 A8、channel、MUTE A/B、melody、sweep）均以 `-Werror` 回归通过。A12 工具
为静态 ARM EABI5、GNU_STACK RW、无运行时 UND；initramfs 包含 sweep/melody/MUTE A/B 三个
工具；FIT 三个 payload 抽取后与输入逐字节一致。hash：

```text
fa28e00fffb019aa5a02e098c1da91094e68b5b81fd0ccf261e9b230adb8130c  r1-sweep-test
82bd51135fd0eac11401141a8771bd92e671900f9b0615fff961b8d19d1d4eca  r1-initramfs-mainline-6.18-ak7755-sweep-a12.cpio.gz
b8f58499805d364ae90ee61d72e941a89e102e56ebe7508969d6dcb8a32696ec  r1-linux-mainline-6.18-ak7755-sweep-a12.itb
```

当前仍是主机构建候选。上板时前台运行 `/bin/r1-sweep-test`，按 -60/-54/-48 三个标签记录是否
听见连续升调、哪档开始压过失台样噪声以及最终退出/SAFE/STANDBY；若意外过响可立即 Ctrl-C，
close 和 500 ms 内核看门狗都会强制静音并 shutdown。

### A12 幅度无响应与 AK7755 模拟输出初始化缺口

用户实机听感显示 -60/-54/-48 dBFS 三段几乎没有变化，仍主要是小声“收音机失台”噪声。
本轮没有退出码或 SAFE/STANDBY 日志，所以只把“幅度无明显响应”作为听感证据，不补写完整
安全回归。

随后用以下只读检索重新对照原厂 DT、原厂日志、当前 6.18 component 与 AKM GPL driver：

```sh
rg -n -A35 -B15 'ak7755@|rockchip-ak7755|bitclock-master|frame-master' \
  backup/unpacked/boot/rk-kernel.dts
rg -n 'ak7755|PRAM|CRAM' backup/bringup_dmesg.md
rg -n 'ak7755_init_reg|C8_DAC|CE_POWER|D4_LO1|module_param.*aec' \
  /tmp/r1-kasa-ak7755.c /tmp/r1-kasa-ak7755.h
rg -n 'AK7755_REG_C8|AK7755_REG_CE|AK7755_REG_D4' \
  kernel/overlays/linux-6.18.42/sound/soc/codecs/ak7755.c
```

原厂 DT 只描述 I2S2、CPU bit/frame clock master、PDN/SDZ/MUTE，不包含 codec 内部电源或 mux。
当前驱动写 C0/C1/C2/C3/C6/C7、验证 PRAM/CRAM 并释放 DSP reset，但没有写 C8、CE、D4 和
原厂 init 的 CC/CD/DA/E6/EA。AKM 数据手册 `014006643-E-01`（2018-08）CONT08 说明 C8
默认 `00` 选择 DSP DOUT4；CONT0E 说明 CE 默认 `00`，PMDAL/PMDAR/PMLO1 为 0 时 DAC 与
Lineout1 power-down/Hi-Z。最接近 R1 的 GPL driver 默认 `aec=1`，明确写 C8=00、CE=C7、
D4=FF 后才建立 DSP DOUT4→DAC→Lineout1。

来源：Asahi Kasei Microdevices，*AK7755 DSP with Mono ADC Stereo CODEC + Mic/Lineout Amp*，
文档号 `014006643-E-01`，2018-08，
<https://www.akm.com/jp/ja/products/audio-voice-dsp/lineup-audio-voice-processor/ak7755en/>，
数据手册镜像 <https://www.mouser.com/datasheet/2/1431/ak7755en_en_datasheet-3515126.pdf>；
参考驱动为 hello/kasa commit `762398dc7ceff508a4ac834ff93b14955d802328`。

当前高可信推断是：I2S/DMA/DSP 与外部功放控制均已工作，但 AK7755 模拟 DAC/Lineout 没有按
原厂上电，TPA3118D2 放大的主要是 Hi-Z/底噪。这不是仅靠 DT 能修复的问题。下一步 A13 应先
读回关键寄存器，再在硬件 MUTE 下做两条单变量路径：A 为 SDIN1→DAC→Lineout1 直通、关闭
ADC 和 DSP 影响；B 为原厂 DSP DOUT4 路由。两条都从 -60 dBFS 开始，不再盲目加幅度。

## 2026-08-12：Audio A13a SDIN1 直通主机候选

### 实现边界

A13a 只实现上节 A 线，不加入 DSP DOUT4 或原厂其余模拟输入寄存器。codec driver
在 DAI 配置前读回 C0/C1/C2/C3/C6/C7/C8/CE/D4/CF 并打印 `route baseline`。已验证
I²S/32fs 格式保持 C6 mask `0x37`/value `0x33`；只把 C8 bits 7:6 设为 `11`，
选择 SDIN1 作为 DAC 数字输入。D4 Lineout1 低四位设为 `0xF`（0 dB）。

PCM prepare 中先释放 C1.CKRESETN，然后只设置 CE bits 2:0，为 DAC L/R 和 Lineout1 上电。
CE 高位保持 0，因此 ADC/数字麦克风不上电。CF 只设 CRESETN bit 3，DSPRESETN bit 2
保持 0。RUN 必须读回 C6/C8/CE/D4/C1/CF 六项合同；失败时清 CF/CE，ALSA prepare 返回
错误且重新断言 PDN。最后 stream close 先清 CF 再清 CE bits 2:0，并读回两者。
外部功放安全门、DTB 与 A12 initramfs 均未改变。

没有把 C6 清零，因为 hello/kasa 固定 commit `762398dc7ceff508a4ac834ff93b14955d802328`
的 AKM GPL driver 在 I²S/32fs 分支明确执行 C6 mask `0x37`/value `0x33`。C8 才是 DSP DOUT4/
SDIN1 的 DAC mux。这使 A13a 不会同时改变 route 和 serial format 两个变量。

### 可复现构建

```sh
KERNEL_SRC=build/kernel-src-a8 \
KERNEL_BUILD=build/kernel-6.18-ak7755-direct-a13a \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment kernel/config/r1-6.18-ak7755-audio-soak-a7.fragment kernel/config/r1-6.18-ak7755-audible-a8.fragment kernel/config/r1-6.18-ak7755-direct-a13a.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-direct-a13a \
scripts/build-kernel.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-direct-a13a.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-direct-a13a.itb
```

全新 output directory 的 multi_v7 zImage 完整链接通过，localversion 为
`-phicomm-r1-ak7755-direct-a13a`；AK7755、R1 machine card 和 Rockchip I2S2 均为 built-in。
FIT 为 14,358,876 bytes，低于 16 MiB DFU RAM alternate。用 `dumpimage -p 0/1/2` 抽取后，
kernel/initramfs/DTB 分别与 ITS 输入 `cmp` 一致。其中 initramfs 逐字节复用 A12，DTB hash
与 A9/A12 已验证功放门 DTB 相同。

```text
7b1afda6f7915beb513f481013efd14cf22d0c014d8981b9cb1c15080c1acd0f  kernel/overlays/linux-6.18.42/sound/soc/codecs/ak7755.c
87f1c4039e5c1f38fa7cdd27180e9e55d4af7a6f23f60ce24fc167547efa8f84  build/artifacts/kernel-mainline-6.18-ak7755-direct-a13a.config
e60ff74c0193191ddd23732a2837fd28e520b24a4a25e5e709172d237c61cb61  build/artifacts/zImage-mainline-6.18-ak7755-direct-a13a
82bd51135fd0eac11401141a8771bd92e671900f9b0615fff961b8d19d1d4eca  build/artifacts/r1-initramfs-mainline-6.18-ak7755-sweep-a12.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  build/artifacts/rk3229-phicomm-r1-mainline-6.18-ak7755-direct-a13a.dtb
75630c2cb15f26447827ca2be4f32b798d8dc7242f31db9acdd37ed0bdcb52b4  build/artifacts/r1-linux-mainline-6.18-ak7755-direct-a13a.itb
```

当前证据只是主机构建和静态审计，不能声称直通声音或寄存器合同已在 R1 通过。
默认 DFU 脚本已锁定上述 FIT/hash。首轮上板只运行一次 `/bin/r1-audible-test`；
必须同时保留 `route baseline`、`DIRECT RUN verified`、实际听感、退出码、最终
`audible test safe` 和 `DIRECT STANDBY verified`。未通过前不运行 A12 的 -54/-48 dBFS 档位。

### A13a RAM-only 实机结果

用户启动 hash-pinned A13a FIT 后，配置前读到的 C0/C1/C2/C3/C6/C7/C8/CE/D4/CF 全为
`0x00`，直接验证了 A12 时 codec 内部 DAC mux、power 和 Lineout volume 仍在复位基线。
执行固定 `/bin/r1-audible-test` 后的关键时序为：

```text
audible test safe: mute+shutdown asserted
audible test armed: amplifier enabled but muted
DIRECT RUN verified: C6=0x33 C8=0xc0 CE=0x7 D4=0xf C1=0x21 CF=0x8; DSP held reset
audible test UNMUTED; 500 ms fail-safe active
audible_window=complete result=PASS
audible test safe: mute+shutdown asserted
DIRECT STANDBY verified: CE=0x0 CF=0x0; amplifier controls unchanged
```

六项 RUN 合同逐项匹配，其中 CF bits 3:2 为 `10`，确认 codec core 已运行而 DSP 仍在
reset。STANDBY 后 CE/CF 回到 0。测试前后 GPIO 都显示功放 SDZ physical low、MUTE physical
high，所以 fail-closed 收口完整。用户没有单独输出 `audible_rc`，因此不从日志外推定数字退出码；
工具自身已输出 `result=PASS`。

实际听感是“好像有点不一样，噪声中混有一点点嘟声”。这与 A12 只有小声失台噪声
不同，支持“原问题是 DAC/Lineout 未上电，A13a 已让 SDIN1 PCM 成分到达扬声器”的结论。
但可辨信号仍被噪声淹没，不能声称已得到干净 1 kHz或完成模拟链配置。

下一步不改 kernel/FIT，直接运行同一 initramfs 中已审计的 `/bin/r1-sweep-test`，判断
-60/-54/-48 dBFS 是否随幅度逐级增强并压过固定底噪。这样只改测试信号，不同时改寄存器。
固定 GPL driver 的 register cache 给出 D8/D9 reset/default `0x18`，其 init 也写 `0x18`，所以尚无证据
认为 DAC digital volume 是当前第一缺口。只有直通三档幅度仍无响应时，才继续读回并单独试验
D8/D9 与 CC/CD/DA，不整组照抄原厂 init。

### A13b 直通幅度响应与 A13c 主机候选

用户随后在同一 A13a 启动中运行 `/bin/r1-sweep-test`。工具依次报告 -60/-54/-48 dBFS，
每段都能听到 300 Hz→2 kHz 扫频且音量逐档增加，主观增幅类似 10%→30%→50%；每档仍有
底噪。状态机完整输出 `sweep_test=complete result=PASS`，同一次测试的 DIRECT RUN 仍为
`C6=33/C8=c0/CE=07/D4=0f/C1=21/CF=08`，关闭后 `CE=0/CF=0`，最终 GPIO 为功放
SDZ physical low、MUTE physical high。日志没有单独打印 `sweep_rc`，因此只记录工具 PASS，
不外推数字退出码。

该结果验证了数字幅度响应，而不是仅验证“偶然有声”。相邻档位相差 6 dB，即电压约 2 倍；
听感不是线性刻度，所以不能用“没有翻倍响”认定增益错误。固定底噪仍是未解决问题，但在
极低的 peak=32/64/128 条件下，先提高测试信号比同时修改 D8/D9/CC/CD/DA 更符合单变量原则。

A13c 因此完全复用 A13a zImage 与 DTB，只把 initramfs 的同一扫频 peaks 改为
128/256/512，即约 -48/-42/-36 dBFS。最高值约为 S16 full scale 电压的 1.6%，仍经过
root-only 独占安全门、500 ms keepalive、close-to-SAFE 和硬件 mute/shutdown。构建命令为：

```sh
scripts/build-r1-sweep-high-test.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
R1_AUDIBLE_TEST_TOOL=build/artifacts/r1-audible-test \
R1_CHANNEL_TEST_TOOL=build/artifacts/r1-channel-test \
R1_AUDIO_MUTE_AB_TOOL=build/artifacts/r1-audio-mute-ab \
R1_MELODY_TEST_TOOL=build/artifacts/r1-melody-test \
R1_SWEEP_TEST_TOOL=build/artifacts/r1-sweep-high-test \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-direct-a13c \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-direct-a13c.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-direct-a13c.itb
```

主机验证已确认新工具为静态 ARM EABI5、GNU_STACK RW；FIT kernel/initramfs/DTB 抽取后与
各自输入逐字节一致。hash：

```text
2cf1cc8e233c648a97d502ec39aaaa7c9d501358c386f3e1d9b9d0783bc45281  r1-sweep-high-test
8e7ca71ca97e92721a10809c01d6e32928bd536619dcea3cd9101d08c5b4b415  r1-initramfs-mainline-6.18-ak7755-direct-a13c.cpio.gz
f9ff60889e83eb3d355d0155fd06c23ead6c80a9a444328c04ba4b0b633cf071  r1-linux-mainline-6.18-ak7755-direct-a13c.itb
```

默认 DFU 脚本已 hash-pinned 到 A13c。当前只有主机构建证据；实机重点不是比较绝对响度，
而是判断 -36 dBFS 时扫频是否明显压过固定底噪。若能压过，下一步进入受控实际音频链；若仍
不能，则设计 direct-powered/Hi-Z/hardware-MUTE 三态噪声 A/B，再决定是否碰模拟寄存器。

## 2026-08-12：Audio A14 受控公版音乐主机候选

用户反馈提高测试幅度后仍有一点底噪，并决定先做受控音乐播放。由于这条反馈没有附带 A13c
工具 PASS、DIRECT STANDBY 和最终 GPIO，当前只记录主观听感，不将 A13c 标为完整实机回归。

A14 新增静态 `/bin/r1-music-test`。它不解析或接受任意外部媒体，而是在程序中合成公版
《欢乐颂》开头 16 个音符，约 10.9 秒。音符为 C4/D4/E4/F4/G4 范围，避开极低频；基音与
二次谐波按 3:1 混合，总 peak 限制为 512（约 -36 dBFS）。单声道样本复制到两个 I2S slot，
不再试图解释低音/高音两个物理单元的左右声道映射。每音符含淡入、淡出和数字零尾部。

安全合同未改变：root-only/exclusive gate、SAFE→ARM_MUTED→PCM preroll→UNMUTE、每 period
500 ms keepalive，以及错误/关闭/进程死亡后的硬件 MUTE+shutdown。A14 完全复用 A13a 的
zImage 和 DTB，所以 codec 仍为 SDIN1→DAC L/R→Lineout1、DSP reset；本阶段只改变
initramfs 工具。

构建命令：

```sh
scripts/build-r1-music-test.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
R1_AUDIBLE_TEST_TOOL=build/artifacts/r1-audible-test \
R1_CHANNEL_TEST_TOOL=build/artifacts/r1-channel-test \
R1_AUDIO_MUTE_AB_TOOL=build/artifacts/r1-audio-mute-ab \
R1_MELODY_TEST_TOOL=build/artifacts/r1-melody-test \
R1_SWEEP_TEST_TOOL=build/artifacts/r1-sweep-high-test \
R1_MUSIC_TEST_TOOL=build/artifacts/r1-music-test \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-music-a14 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-music-a14.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-music-a14.itb
```

新工具及旧 melody/普通 sweep/高一级 sweep 均以 `-Werror` 回归编译；A14 工具为静态 ARM
EABI5、GNU_STACK RW、无 GLOBAL UND。initramfs 中 `/bin/r1-music-test` 与输入 ELF 逐字节
一致；FIT 的 kernel/initramfs/DTB 也全部抽取并与各自输入逐字节一致。hash：

```text
d4be4718b3560d8bfbc6c030db62992f473bae9909c3644c82cf0e2f25297b0f  r1-music-test
b72f74d5560cb75f0e1065bb898331907f61ffb887da032a17abd23ec843dbf6  r1-initramfs-mainline-6.18-ak7755-music-a14.cpio.gz
fca83d17ebec0e46ca471a94e7c1c5d7a6b8cb9201f06abcb458fbdfa0903e74  r1-linux-mainline-6.18-ak7755-music-a14.itb
```

默认 DFU 脚本已 hash-pinned 到 A14。当前只有主机侧构建与解包证据，尚不能声称旋律、音质或
最终安全状态已在 R1 实机通过。

## 2026-08-12：Audio A15 DAC soft-mute 噪声边界候选

用户报告 A14 受控音乐播放期间底噪始终以近似固定响度叠加，并提供了一份按 DSP bypass、I2S、
gain、mixer、初始化时序和 DAC mute 分层的诊断建议。该建议中的首个关键 A/B 实际已由 A13a
完成：`C8=0xc0` 选择 SDIN1，`CF=0x08` 保持 DSPRESETN=0；A13b 又验证扫频音高与幅度响应。
因此 PRAM/CRAM/DSP routing 不再是底噪第一嫌疑，ADC/数字麦克风也因 CE 高位保持 0 而没有
在当前直通路径上电。固定底噪与信号幅度分离更支持 DAC 后模拟链或固定 gain/noise floor。

重新核对 [AK7755EN 数据手册 `014006643-E-01`（AKM，2018-08）](https://www.mouser.com/datasheet/3/5939/1/ak7755en_en_datasheet.pdf)
发现一个已验证的代码缺口：CONT1A write address `0xDA` 的 D4 在 system reset 期间必须为 1，
但 A13a 的实机 baseline 是 `DA=0x00`；固定参考驱动则写 `DA=0x10`。同一寄存器 D5 是 DAC
digital soft mute，置 1 会以 soft transition 衰减至负无穷。A15 因此不照抄整组 vendor init，
只修正 D4 合同并以 D5 设计下一条单变量边界测试。

A15 reset/staging 写 `DA=0x30`；direct RUN 在功放仍硬件 MUTE 时写 `DA=0x10`、等待 25 ms，
再把 DA 加入强制读回合同。正常 standby 和启动失败路径均先写回 `DA=0x30`、等待 25 ms，
再清 CF/CE。machine driver 新增两个仅在独占功放门已 UNMUTE、codec direct route 已运行时
可用的 DAC mute/unmute ioctl；每次调用刷新原 500 ms fail-safe。

静态 `/bin/r1-dac-mute-ab` 先给短参考信号，然后保持同一个 zero-PCM stream 和功放
enable+unmute，依次执行 2 秒 `zero_unmuted_1`、2 秒 `zero_dac_muted`、2 秒
`zero_unmuted_2`。测试中间只改变 DA.D5；关闭、错误或 keepalive 超时仍回到硬件
MUTE+shutdown。构建时发现 `build-kernel.sh` 不会自动把修改后的 repository overlay 同步到
已存在的 `build/kernel-src-a8`；首次“成功”产物仍是旧驱动，已明确同步两份源码、由编译日志
确认重新编译 `ak7755.o` 与 `phicomm_r1_ak7755.o` 后才接受最终产物。

```sh
scripts/build-r1-dac-mute-ab.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-dac-mute-a15.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-dac-mute-a15.itb
```

最终 FIT 为 14,356,976 bytes，低于 16 MiB DFU RAM alternate。`dumpimage -p 0/1/2` 抽取的
kernel/initramfs/DTB 与 ITS 输入逐字节一致，从 initramfs 解出的 `/bin/r1-dac-mute-ab` 也与
输入 ELF 一致。当前只有主机构建证据，不能声称噪声边界已定位。

```text
95e898e6b63ae0e8a0cb9eee5ef9d8e3236bd4093525e0008f3a81589b5068d6  ak7755.c
6c79e5b1f3d4bb752f8bf88ed6d59c4a9b3348667dabd46d900a66c698b4216e  phicomm_r1_ak7755.c
69750bff17a372f7f2964fa0bd3135e27874e268f4bcdbc687e375ea414b2708  r1-dac-mute-ab
fcfefa934cf90e512e3356b596025196f9d1db94f63924dae110905ae05ab32d  zImage-mainline-6.18-ak7755-dac-mute-a15
b39e3c378c78083fa10efed0bdb8aa1d031280031db7b070706352400792285f  r1-initramfs-mainline-6.18-ak7755-dac-mute-a15.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-dac-mute-a15.dtb
b4eae0b6fd1956f22828c3cb91b6da78546e4bc05a2dbd59efa9d2f6df44dd0a  r1-linux-mainline-6.18-ak7755-dac-mute-a15.itb
```

## 2026-08-12：Audio A15 实机结论与 A16 模拟边界候选

用户连续两次运行 A15，日志均显示 direct RUN 为
`C6=0x33 C8=0xc0 CE=0x07 D4=0x0f DA=0x10 C1=0x21 CF=0x08`，DAC soft mute
严格按 `DA=0x10→0x30→0x10` 切换，最后进入功放 SAFE 和 `DIRECT STANDBY`。用户明确报告
三个窗口直到结束都保持固定底噪。这里没有单独提交工具退出码，但两次完整状态转换和最终
收口日志均存在。

再次按 AKM `014006643-E-01` 的寄存器表逐项复核：C8 选择 SDIN1；CE 高三位的 ADC L/R、
ADC2 L 全为 0；CF 的 line-in、ADC2 R 和 DSPRESETN 均为 0；OUT1/OUT2 是 DAC 的直接
lineout，而 C9 的 analog mixer 只送 OUT3。A16 还在 DAI staging 中显式清 C0.AINE、整个 C9
和 D3，并在 RUN 中整字节写 CE=07。因此“ADC 或模拟输入误混进当前 OUT1”与实机寄存器不符；
A15 不变的听感把噪声边界移到 DAC 数字 mute 之后。

基于同一数据手册的 CE/OUT1 状态表，A16 新增三态单变量测试：`DA=30, CE=07` 的 DAC
digital mute；`CE=04` 的 DAC L/R power-down、Lineout1 保持 AVDD/2 低阻输出；`CE=00` 的
Lineout1 Hi-Z。三个窗口中 TPA3118 保持 enable+unmute，工具持续写 zero PCM、刷新 500 ms
keepalive；边界 ioctl 只有在独占安全门已 UNMUTE 且 direct route 正在运行时才允许。每步读回
C0/C8/C9/CE/CF/D3/D4/DA，任何不符立即失败并由 close/timeout 收口到硬件 MUTE+shutdown。

主机构建命令：

```sh
scripts/build-r1-analog-boundary-test.sh

KERNEL_SRC=build/kernel-src-a8 \
KERNEL_BUILD=build/kernel-6.18-ak7755-analog-boundary-a16 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment kernel/config/r1-6.18-ak7755-audio-soak-a7.fragment kernel/config/r1-6.18-ak7755-audible-a8.fragment kernel/config/r1-6.18-ak7755-analog-boundary-a16.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-analog-boundary-a16 \
scripts/build-kernel.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-analog-boundary-a16.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-analog-boundary-a16.itb
```

最终 FIT 为 14,362,256 bytes。`dumpimage -p 0/1/2` 抽取的三个 payload 与 ITS 输入逐字节
一致，从 initramfs 解出的工具也与输入 ELF 一致。默认 DFU 脚本已 hash-pinned 到 A16。

```text
60b779677845d3b6fe810c9f9ccbdf36faa38a12e32925e3bdaee932319545f4  ak7755.c
e3bfd76b6ded8681dca29988eaf506cd4d4f3c40d6e7ba98f4ca63df11f895cd  phicomm_r1_ak7755.c
864f1a56ef40a957709e85e622e4b3d92f8a901d6bb3952f335062da1b662fe0  r1-analog-boundary-ab
52b4ea166e7ff85dec5f79ecb9e60d1b4b786deaceb21f976a9870f49e54ff8e  zImage-mainline-6.18-ak7755-analog-boundary-a16
0db5011c377ffc2f8339d5b987aa00510af17f32533fa3ec907c3b77fe0d7540  r1-initramfs-mainline-6.18-ak7755-analog-boundary-a16.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-analog-boundary-a16.dtb
9ce2ed99b338223529761f0039420fd7c5b050710e43f13dbcf2f212752efd2b  r1-linux-mainline-6.18-ak7755-analog-boundary-a16.itb
```

### A16 实机结果与原厂 GPIO 复核

用户运行 `/bin/r1-analog-boundary-ab` 得到 `analog_boundary_rc=0`，并连续重复五次相同状态
序列。每次 direct RUN 为 `CE=07/DA=10`，DAC mute 为 `DA=30`，随后模拟边界分别读回
`CE=04` 和 `CE=00`；C0/C8/C9/CF/D3/D4/DA 其余合同全部匹配。每次均由硬件 SAFE 收口并
读回 `DIRECT STANDBY CE=0 CF=0`。用户主观结果为：第一段 DAC digital mute 与第二段
DAC-off/Lineout1-AVDD/2 的底噪相同，第三段 Lineout1 Hi-Z 后变为另一种底噪。可验证结论是
DAC 模拟核不主导该噪声，变化边界在 Lineout1 输出阻抗与 TPA3118 输入交界；由于没有记录
第三段相对更响或更安静，不能进一步区分 lineout buffer 与功放输入悬空拾噪。

用户提出可能遗漏外围控制 GPIO。为此只读复核了 `backup/unpacked/boot/rk-kernel.dts`、
`backup/unpacked/recovery/rk-kernel.dts`、`backup/bringup_dmesg.md` 与 A16 最终反编译 DTB：

```text
原厂 ak7755@19:
  GPIO1_A3  ak7755,pdn-gpio
  GPIO3_B7  TPA3118D2,shutdown-gpio
  GPIO3_C1  TPA3118D2,mute-gpio

原厂日志:
  pdn_gpio=35
  sdz_gpio=111
  mute_gpio=113
  RK_AK7755 card registered
```

A16 最终 DTB 使用完全相同的物理 pin，分别映射为 codec reset、amp enable 与 active-low
amp unmute。原厂 boot/recovery DTS 两份 SHA-256 都是
`12ca8dd93e06d1618c359bb69f8167643213687e7c0f943684c6d8cd00216fb`，不是两个不同板型。
原厂 DT 的确还含 `es8323@11` 的 GPIO1_A0/A1 `pa-en1/pa-en2`，但它与失败的 ES7243 占用
同一 I2C 地址；启动日志没有 ES8323 probe 或声卡，ALSA 列表也没有 ES8323。因此它属于
Rockchip 万能板遗留节点，不足以证明 R1 漏了两根功放 GPIO。当前更合理的下一单变量是
保持 Lineout1 low-impedance、功放开放与 DAC soft-mute，只切换 D4 Lineout1 volume。

## 2026-08-12：Audio A17 Lineout1 模拟音量候选

按 A16 的实机结论继续做单变量。AKM GPL 参考驱动的 `lovol1_tlv` 定义为 -3000 centi-dB
起、每个 raw code 增加 200 centi-dB，且同一驱动注释明确 D4=`0x0f` 为 0 dB。因此 A17
保持 DAC soft-mute、DAC/Lineout1 上电、Lineout1 low-Z 和 TPA3118 enable+unmute，只把 D4
低四位按 `0x0f→0x08→0x00` 切为 0/-14/-30 dB。每段继续写 zero PCM 约 2 秒；开始前保留
-60 dBFS 短参考音，以确认链路确实打开。

codec 接口只允许上述严格顺序，并要求 direct route 正在运行、A16 analog-boundary 状态未启用、
DA 已读回 `0x30`。每档重新读回 C0/C8/C9/CE/CF/D3/D4/DA，确认 analog input/mixer/ADC/LIN/
DSP 仍关闭、`CE=0x07`、DAC 仍 soft-muted。machine ioctl 只有在独占安全门和功放 UNMUTE
状态下可调用；成功后刷新 500 ms fail-safe，异常和 close 仍先 hardware MUTE 再 shutdown。

主机构建使用：

```sh
scripts/build-r1-lineout-volume-test.sh

KERNEL_SRC=build/kernel-src-a8 \
KERNEL_BUILD=build/kernel-6.18-ak7755-lineout-volume-a17 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment kernel/config/r1-6.18-ak7755-audio-soak-a7.fragment kernel/config/r1-6.18-ak7755-audible-a8.fragment kernel/config/r1-6.18-ak7755-lineout-volume-a17.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-lineout-volume-a17 \
scripts/build-kernel.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-lineout-volume-a17.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-lineout-volume-a17.itb
```

整核编译确认重新构建 `ak7755.o` 与 `phicomm_r1_ak7755.o`，System.map 包含
`ak7755_component_set_lineout_volume`。最终 FIT 为 14,363,456 bytes；`dumpimage -T flat_dt
-p 0/1/2` 抽取的 kernel/initramfs/DTB 与 ITS 输入逐字节一致，initramfs 中
`/bin/r1-lineout-volume-ab` 与输入静态 ARM ELF 逐字节一致、GNU_STACK 为 RW、无 GLOBAL UND。
默认 DFU 脚本已 hash-pinned 到 A17。当前只有主机验证，实机听感仍待用户执行。

```text
5cc747f499b80f550bb431c20f13a90990f9a22cfca8a65aff94f9eaf14d055a  ak7755.c
9342f8316545c0c4e7fd452f4c1e42ccfce31b3bed57b20906fd63374abf97d4  phicomm_r1_ak7755.c
c93d1354afdeaf13569c6221bef52b28bfa41740f83d30b8eb25a87d50a7340c  r1-lineout-volume-ab
51c35ddd8e6c2c0e784637defbf818fb6308cfa2f574c24b172c1fc94f5ed07a  zImage-mainline-6.18-ak7755-lineout-volume-a17
e3f486ccdc699fb99c85fb5542c661e0483211fe83df9642ac086cb55c962f4c  r1-initramfs-mainline-6.18-ak7755-lineout-volume-a17.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-volume-a17.dtb
f37cb463682ea5b4acf1baff8b71a9fbdf5acc9a5d38a1336f83a6866d10aa2b  r1-linux-mainline-6.18-ak7755-lineout-volume-a17.itb
```

实机听感由用户报告为三段固定底噪基本不变。随后重新核对 GPL 参考驱动的原始相邻注释，发现
其明确写的是 “from -30 to 0 dB in 2 dB steps (mute instead of -30 dB)”；因此 A17 把 raw 0
称作普通 -30 dB 不准确，应视为最低端 mute 编码。更重要的是，A17 只在切换前播放一次参考音，
没有逐档证明板上可听信号确实随 D4 衰减。这个结果目前只支持“固定噪声不响应 A17 写值”，
不能单独排除软件选错实际输出通道/寄存器。下一实验应避开 raw 0，使用 `F/8/1` 并在每档
重复同一参考音和 zero window，形成 D4 自校验闭环。

## 2026-08-12：Audio A18 D4 可听增益自校验候选

为补齐 A17 没有逐档参考音的证据缺口，新增 `/bin/r1-lineout-selfcheck`。它在 D4=`F/8/1`
（0/-14/-28 dB）三档分别播放完全相同的一秒 1 kHz stereo、约 -36 dBFS 信号，紧随约两秒
zero PCM。切档前先 DAC soft-mute，写入并强制读回 D4 后解除 mute；zero 窗口中 DAC 保持
unmuted。功放独占门、500 ms keepalive、close/error 自动 SAFE 均未放宽。

构建命令：

```sh
scripts/build-r1-lineout-selfcheck.sh

KERNEL_SRC=build/kernel-src-a8 \
KERNEL_BUILD=build/kernel-6.18-ak7755-lineout-selfcheck-a18 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment kernel/config/r1-6.18-ak7755-audio-soak-a7.fragment kernel/config/r1-6.18-ak7755-audible-a8.fragment kernel/config/r1-6.18-ak7755-lineout-selfcheck-a18.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-lineout-selfcheck-a18 \
scripts/build-kernel.sh

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL=build/artifacts/r1-nl80211-scan \
R1_BLUETOOTH_MGMT_TOOL=build/artifacts/r1-btmgmt R1_AK7755_FIRMWARE=1 \
R1_PCM_CLOCK_TEST_TOOL=build/artifacts/r1-pcm-clock-test \
R1_PCM_CAPTURE_TEST_TOOL=build/artifacts/r1-pcm-capture-test \
R1_AUDIO_SOAK_TOOL=initramfs/r1-audio-soak \
R1_AUDIBLE_TEST_TOOL=build/artifacts/r1-audible-test \
R1_CHANNEL_TEST_TOOL=build/artifacts/r1-channel-test \
R1_AUDIO_MUTE_AB_TOOL=build/artifacts/r1-audio-mute-ab \
R1_MELODY_TEST_TOOL=build/artifacts/r1-melody-test \
R1_SWEEP_TEST_TOOL=build/artifacts/r1-sweep-high-test \
R1_MUSIC_TEST_TOOL=build/artifacts/r1-music-test \
R1_DAC_MUTE_AB_TOOL=build/artifacts/r1-dac-mute-ab \
R1_ANALOG_BOUNDARY_TOOL=build/artifacts/r1-analog-boundary-ab \
R1_LINEOUT_SELFCHECK_TOOL=build/artifacts/r1-lineout-selfcheck \
INITRAMFS_ARTIFACT_TAG=mainline-6.18-ak7755-lineout-selfcheck-a18 \
scripts/build-initramfs.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18.itb
```

整核明确重编译 `ak7755.o` 和 `phicomm_r1_ak7755.o`，System.map 含导出接口；FIT 三个 payload
解包后与输入逐字节一致，initramfs 内工具与输入 ELF 一致，GNU_STACK 为 RW、无 GLOBAL UND。
A17 旧工具编译模式也以 `-Werror` 回归通过。最终 FIT 14,359,724 bytes，低于 16 MiB DFU
alternate，默认 DFU 已更新并锁定下列 hash。当前仅完成主机验证，实机听感仍待用户执行。

```text
749e32ab12d67d6e2f8ad486e7e78006efa692af484ff32ace38a66c319f546d  ak7755.c
51efdf7c923efd9c2826a694b517cda3fa425cd94781ebfee93b0f2ead1b54e1  phicomm_r1_ak7755.c
dc72960a9fc0a30840f4801911a629be2ca6dbf241a468c69524fc3fff58c455  r1-lineout-selfcheck
7cfeffca1d3c2cb2825486736ea2a012ad541cc2bdd04865cd332f4469bf64ac  zImage-mainline-6.18-ak7755-lineout-selfcheck-a18
fde9534b2145cbea56a6e9ab116c98e1b015906ac4f956a8f85fc444bcd7c7d2  r1-initramfs-mainline-6.18-ak7755-lineout-selfcheck-a18.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-selfcheck-a18.dtb
c8402952e3a9f7ced94d5fd5793c4885f5d4c4d3ae4f8bd8f51a96f0295d823f  r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18.itb
```

## 2026-08-12：Audio A18 首次失败与 A18r2 可重试诊断

用户首次运行 `/bin/r1-lineout-selfcheck` 时，machine gate 已进入 `ARM_MUTED`，但 PCM
`SNDRV_PCM_IOCTL_PREPARE` 返回 `EIO`：

```text
ak7755 1-0019: failed to verify direct output RUN, asserting reset: -5
ASoC error (-5): at snd_soc_dai_prepare() on ak7755-AIF1
SNDRV_PCM_IOCTL_PREPARE errno=0x05
phicomm-r1-ak7755 sound: audible test safe: mute+shutdown asserted
```

可验证事实是测试尚未开始播放，功放已自动回到 SAFE。源码复核又发现 codec 的内部错误路径
已先 DAC soft-mute、清 CF RUN 和 CE power，DAI 外层却再次断言硬件 PDN/reset；后者使同一次
启动无法重试，属于错误收口过度，不是硬件死锁。A18 日志没有打印七个合同寄存器，因此目前
不能判断具体是哪一项不符。

A18r2 保留内部 fail-closed 与 machine gate SAFE，删除额外 PDN 断言，并在合同不符时打印
C6/C8/CE/D4/DA/C1/CF 的 masked actual/expected。构建时先发现仓库 overlay 尚未同步到现有
`build/kernel-src-a8`，首轮只重链版本号；该无效中间产物未交付。同步 overlay 后的日志明确
出现 `CC sound/soc/codecs/ak7755.o`，再重新封装 FIT。

可复现构建命令仍使用 A18 的 fragment 链，只把最后一项和 artifact tag 改为 A18r2：

```sh
KERNEL_SRC=build/kernel-src-a8 \
KERNEL_BUILD=build/kernel-6.18-ak7755-lineout-selfcheck-a18 \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-dsp-run-a6.fragment kernel/config/r1-6.18-ak7755-audio-soak-a7.fragment kernel/config/r1-6.18-ak7755-audible-a8.fragment kernel/config/r1-6.18-ak7755-lineout-selfcheck-a18r2.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts \
KERNEL_ARTIFACT_TAG=mainline-6.18-ak7755-lineout-selfcheck-a18r2 \
scripts/build-kernel.sh

mkimage -f scripts/r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18r2.its \
  build/artifacts/r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18r2.itb
```

最终 FIT 14,363,832 bytes，低于 16 MiB RAM DFU alternate；三个 payload 解包后逐字节一致，
内核字符串同时包含 mismatch actual/expected 和 `retry remains available`。实机尚待验证。

```text
9020a39d4ddbe21613712560595b860e9c2d3f1f5e9be831d75d8c959ed74c55  ak7755.c
a5f0d660102ddd2146477e560ec009f493c65c41a3c15d9dbe03666749ca5421  zImage-mainline-6.18-ak7755-lineout-selfcheck-a18r2
fde9534b2145cbea56a6e9ab116c98e1b015906ac4f956a8f85fc444bcd7c7d2  r1-initramfs-mainline-6.18-ak7755-lineout-selfcheck-a18r2.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-selfcheck-a18r2.dtb
f2dc1713fdaec1e73632c33787582c56deb311dc63a8f22ce50ca744463a0240  r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18r2.itb
```

## 2026-08-12：Audio A18r2 实机定位 xrun/D4 重入与 A18r3

A18r2 首次实机 RUN 合同完整通过：`C6=33 C8=c0 CE=07 D4=0f DA=10 C1=21 CF=08`。
0 dB tone 和随后两秒 zero 均完成，用户报告仍有噪声。切档日志又确认 DAC soft-mute、
D4=`08` 与 DAC unmute 均读回成功，但紧接着首个 -14 dB tone write 返回 xrun：

```text
DAC soft mute=1 verified: DA=0x30
LINEOUT VOLUME stage=minus14db ... D4=0x8 DA=0x30
DAC soft mute=0 verified: DA=0x10
stage_minus14db_tone=started peak=-36dBFS
FAIL: audible PCM xrun
```

三次控制操作与等待累计约 70 ms，而 A18r2 buffer 只有 4096 frames/约 85 ms，实机队列余量
不足。close 后 standby 正确清 CE/CF，但保留 D4=`08`；后续两次 prepare 的七项诊断均显示
唯一 mismatch 为 `D4=0x8/0xf`。这排除了 PDN 锁存、I2C 失败和其余 route 合同，证明测试
不可重入是 driver 没在新 stream 恢复初始 D4。

A18r3 在 direct RUN 的 DAC-unmute 前显式写回并等待 D4=`0f`，同时只为该自校验工具把
period count 从 4 增为 16，即 buffer 16384 frames/约 341 ms。整核日志明确重编
`sound/soc/codecs/ak7755.o`；FIT 三个 payload 已逐字节核对。实机尚待验证，尤其不能用此次
只完成的 0 dB zero 窗口代替 -14/-28 dB 噪声结论。

```text
2bea30dfc109de403c2f87f362266e8f6033d734dae07def7defaba23a2a7f89  ak7755.c
d9f0eb88b42dfb3114859cf401524a1881629251dc8da15fd2d83727c78ad4c0  r1-lineout-selfcheck
f36747959c06b39d72dae30b27de3ccec4ea75300edbb202a4a5654d726ab718  zImage-mainline-6.18-ak7755-lineout-selfcheck-a18r3
377b8b290f551330e965e95a346faf315de5f91159d384a2acbd35199e64abb5  r1-initramfs-mainline-6.18-ak7755-lineout-selfcheck-a18r3.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-selfcheck-a18r3.dtb
7d2672ffc49c5e0becfcd466e249a4272d6307fb7795086fc4f5a487b7b9eee5  r1-linux-mainline-6.18-ak7755-lineout-selfcheck-a18r3.itb
```

### A18r3 首条实机听感

用户随后补充三段 1 kHz tone 明显逐档变小，而三段 zero 底噪音量不变。由此 D4 已作用于
板上实际可听信号，固定噪声可靠位于 D4 之后；PCM/SDIN1/D4 路径的正向自校验闭环完成。

## 2026-08-12：Audio A19 I2S clock A/B 主机候选

A19 不重编内核，逐字节复用 A18r3 zImage/DTB，只新增 8 KiB 级静态工具
`/bin/r1-i2s-clock-ab`。它使用数字 zero，先运行 PCM 两秒，再在 PCM fd 保持打开、功放和
codec route 均不变时调用 `SNDRV_PCM_IOCTL_DROP`，每 100 ms 刷新 fail-safe 并等待两秒，
随后 `PREPARE` 和写 zero 恢复两秒。设计推断是 DROP 会停止 CPU DAI clocks，但不会触发
codec DAI shutdown；需由实机听感与最终日志验证，不能把主机设计当作 clocks 已停止的证据。

```text
95ef1ee5b8b92d566ed2764a58f95cc452fe64a5bbad435291db42acb7f3803a  r1-i2s-clock-ab
855f92832e9de5ae0c182c19a07ad66e7f0892efc6a9f5933f3c32c8336d5859  r1-initramfs-mainline-6.18-ak7755-i2s-clock-a19.cpio.gz
f36747959c06b39d72dae30b27de3ccec4ea75300edbb202a4a5654d726ab718  zImage-mainline-6.18-ak7755-lineout-selfcheck-a18r3
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-lineout-selfcheck-a18r3.dtb
1bba7b088f3d6ba40c1bce737ead66266786facd4489e513a19c6565f72b7c54  r1-linux-mainline-6.18-ak7755-i2s-clock-a19.itb
```

### A19 实机听感与尚缺的 clock gate 证据

用户连续执行三轮 A19。每轮日志均在约 6 秒后从已验证 DIRECT RUN 收口到
`DIRECT STANDBY CE=0 CF=0`，功放 SAFE 日志成对，过滤输出没有 xrun/underrun。用户主观确认
running→DROP→running 三个窗口底噪全程相同。该结果证明 PCM DROP 没有带来可听变化；但现有
内核日志不输出 CPU DAI clock gate，故“DROP 期间 BICK/LRCK 已停止”仍是设计推断。下一步无需
改固件，只需让工具后台运行，并在约第 1/3/5 秒分别读取 debugfs `clk_summary` 的 i2s2 clocks。

该采样随后得到三段完全相同的 `i2s2_src/i2s2_frac/i2s2_pre/sclk_i2s2` enable/prepare
`1/1`。复核 Linux 6.18 `sound/soc/rockchip/rockchip_i2s.c` 后确认：trigger STOP 会关闭
`I2S_DMACR.TDE`，把 `I2S_XFER.TXS/RXS` 置 STOP 并清 FIFO；mclk 仅由 runtime suspend
执行 `clk_disable_unprepare()`。A19 故意保持 PCM fd 打开，因此 clock-framework 引用不归零
是预期结果，既不能证明 serial clock 仍在输出，也不能证明其已停止。下一次同样无需改固件，
改采样 `0x100e0010` DMACR 与 `0x100e001c` XFER。

### A19 I2S serial engine MMIO 闭环

用户在同一次 A19 的 running→DROP/stopped→running 三个听感窗口分别读取 I2S2 MMIO：

```text
              running 1    DROP/stopped   running 2
DMACR 0x10    000f0110     000f0010       000f0110
XFER  0x1c    00000003     00000000       00000003
```

DROP 期间 `DMACR` 的 `0x100` TX DMA enable 位被清除，`XFER` 的 TX/RX START 状态也从
`0x3` 变为 `0`；PREPARE/恢复写入后两项均回到原值。工具最终 PASS，DIRECT STANDBY 与功放
SAFE 完整收口，用户确认三段底噪完全不变。因此这不是 clock-framework 计数造成的假阴性：
PCM DMA 和 I2S serial engine 活动确实停止过，但固定噪声没有响应。结合 A18r3 的 D4 正向
自校验，PCM/SDIN1/I2S switching 已从首要嫌疑中排除，噪声边界可靠位于 D4 之后。下一步
转向原厂 Android 同硬件 idle-noise A/B，以及原厂 kernel/module/用户态音频配置中的 AK7755
Lineout1 输出级与 TPA3118 模拟后端初始化；在获得证据前不把该现象直接归咎于 PCB 硬件。

## 2026-08-12：原厂 AK7755 kallsyms 恢复与 Audio A20 reset contract

原厂 boot/recovery 的 kernel SHA-256 均为
`9ae541809bf9f05ae00145876814fbc4d049e19801bf15a23c6a579b0d5d40a8`。zImage 内第二个 LZO
stream 位于文件偏移 14021；解压后得到 14.6 MiB ARM raw kernel。使用 Marin Moulinier 的
GPL-3.0 `vmlinux-to-elf` commit `19683fb95b29cd31362d49e6f48ab8368f96cbdf`，恢复出 90,294 个
kallsyms、base `0xc0008000`，并生成可反汇编 ELF。工具与中间文件只放 `/tmp`，没有纳入仓库。

```sh
dd if=backup/unpacked/boot/kernel bs=1 skip=14021 status=none |
  busybox lzop -dc > /tmp/r1-factory-vmlinux
PYTHONPATH=/tmp/r1-vte-deps:/tmp/r1-vmlinux-to-elf \
  python3 -m vmlinux_to_elf.scripts.vmlinux_to_elf \
  /tmp/r1-factory-vmlinux /tmp/r1-factory-vmlinux.elf
arm-none-eabi-objdump -d --disassemble=ak7755_init_reg \
  /tmp/r1-factory-vmlinux.elf
```

原厂 `ak7755_init_reg()` 位于 `0xc0722f50`，明确执行：D4=`0xff`、D3 low nibble=`0x0f`、
D0.D6=`1`、C2.D6=`0`、D0.D4=`0`、C0.D3=`1`、CD.D6=`1`、DA.D4=`1`、E6=`0x01`、
EA=`0x80`。其中 AKM 数据手册 014006643-E-01 明确规定 `CD.D6`、`DA.D4`、`E6.D0`、
`EA.D7` 必须在 CRESETN/DSPRESETN 均为 0 时置 1，并保持到下一次 PDN。当前驱动此前只满足
DA.D4，因此“软件初始化不完整”仍是已证实缺口，而非猜测。

A20 为干净单变量 A/B，只在 ID 检查后、firmware 下载前补齐 CD/E6/EA 三项并强制读回；
不照抄原厂 AINE、Lineout3、CRC 或 DSP 路由设置，也不改变 SDIN1、D4、PCM、DT 和功放门。
第一次构建经 overlay/source `cmp` 发现未同步、产物仍是旧 codec，已废弃；同步后重编日志
明确出现 `CC sound/soc/codecs/ak7755.o`，vmlinux 包含 reset-contract success/mismatch 字符串。
最终 FIT 14,364,668 bytes，三个 payload 解包逐字节一致，默认 DFU 已 hash-pinned 到 A20。

```text
9c76a5d3f0a84f2496ab582b44d6f9d65bb3c57d1df376b6e9ded8e8d9d86db9  zImage-mainline-6.18-ak7755-reset-contract-a20
855f92832e9de5ae0c182c19a07ad66e7f0892efc6a9f5933f3c32c8336d5859  r1-initramfs-mainline-6.18-ak7755-i2s-clock-a19.cpio.gz
0c7ae9c997fa228d1d5e4ef4a644f5f18f44eee8d380a2c1dad676f8b5c25d4e  rk3229-phicomm-r1-mainline-6.18-ak7755-reset-contract-a20.dtb
ca9ed449b8bc4422bfd4192e07e5349466babc49182b163b74e5ac4b2bc32aa7  r1-linux-mainline-6.18-ak7755-reset-contract-a20.itb
```

### A20 实机结果：reset contract 不是固定底噪根因

用户提供的 RAM-only 日志确认启动阶段成功读回：

```text
system reset contract verified: CD=0xc0 DA=0x10 E6=0x1 EA=0x80
```

随后多次 `/bin/r1-audible-test` 都进入同一个已验证的 DIRECT RUN 状态，结束时 DIRECT STANDBY
清除 CE/CF，machine driver 也重新断言功放 mute+shutdown；测试本身没有报 xrun、underrun 或
合同错误。用户主观结论是底噪与 A19/A18r3 完全相同。故 A20 的三个补写应作为 datasheet 与
原厂实现共同要求的驱动修正保留，但它们不是当前固定底噪的原因。

### Audio A21 原厂 3.10 驱动 RAM-only A/B 主机候选

为避免继续盲猜原厂寄存器，将本地保存且哈希固定的原厂 3.10 zImage/DTB 与当前 A19 rescue
initramfs 封装为独立 FIT。没有把 Android ramdisk、system 或 userdata 放入 FIT；救援 `/init`
不挂载存储。原厂 kallsyms 中存在 `early_init_fdt_scan_reserved_mem`，因此对原厂 DTB 应用一个
项目自有 overlay，保留 open OP-TEE 的 `0x68400000+0x00300000`，并设置
`rdinit=/init maxcpus=1`。单核是为了排除旧 SMP/PSCI，不是新工作基线。

可复现构建命令：

```sh
scripts/build-r1-factory-audio-ab.sh
```

脚本拒绝哈希不符的原厂 kernel、DTB 或 A19 initramfs；生成 overlay 后还强制核对 bootargs 和
secure-memory reg，再从 FIT 抽取三个 payload 与输入逐字节比较。最终 FIT 8.4 MiB，低于现有
16 MiB DFU RAM alternate。专用下载器独立锁定 A21 hash，不改变默认 A20 下载器。

```text
9ae541809bf9f05ae00145876814fbc4d049e19801bf15a23c6a579b0d5d40a8  factory kernel
ac5f7f3b6a4612486ab348a3bdb6aabb41439b9999115dc540720f76e0f44993  factory DTB
855f92832e9de5ae0c182c19a07ad66e7f0892efc6a9f5933f3c32c8336d5859  A19 rescue initramfs
38a7679fb4629456cc7a77d5261cd814688173c07f14d997dd20128239b0e14a  A21 overlaid DTB
3cbaa1abdf016ca5e4a780576ea4c132df34461cbd1bb11d015949d27224b0b5  A21 FIT
```

待实机步骤：先在 U-Boot 进入现有 `linux-fit` RAM DFU，运行专用下载器；进入 rescue shell 后
先核对 `uname -a`、`/proc/asound/cards` 和 AK7755 probe 日志，再运行
`/bin/r1-pcm-clock-test 5`。只比较数字零窗口的固定底噪，记录退出码、PA GPIO 和相关 dmesg。
如果原厂整套 codec/machine driver 仍产生相同噪声，下一阶段转向 TPA3118 模拟后端测量；如果
明显安静，再从原厂 DAPM/route/status 路径做有证据的差异最小化。

### A21 实机失败与 A21r2 no-MMC 修正

A21 启动到约 25 秒枚举 eMMC `8GME4R` 后，在 `kmmcd/mmc_rescan` 中发生首个 Oops：

```text
PC is at strchr+0x4/0x40
LR is at rkpart_setup_real+0x30/0xe0
r0 : 00000009
```

恢复符号的原厂 ELF 反汇编显示 `rkpart_setup_real+0x24` 正是把当前 partition-definition 指针
传给 `strchr(ptr, ':')`；本次 `ptr=0x9`。保存的原厂启动日志则证明正常 cmdline 带完整
`mtdparts=rk29xxnand:...`，A21 精简救援 cmdline 没有该厂商合同。随后的 `kthread_data` Oops
只是同一个已损坏 workqueue 的次生错误。日志没有出现原厂声卡检查或 zero PCM，因此本次不能
回答底噪问题。

不为音频实验恢复 Android 分区表。A21r2 新增 no-MMC overlay，把原厂 DT 的三组
`rksdmmc@30000000/30010000/30020000` 均设为 disabled；kernel、audio nodes、OP-TEE
reservation、单核 cmdline 和 A19 rescue initramfs 全部保持。构建脚本强制读回三个 status，
并再次抽取比较 FIT 的 kernel/ramdisk/DTB。使用固定 `SOURCE_DATE_EPOCH` 连续构建可得到稳定
产物；专用下载器已更新到 A21r2，不会再次发送失败的 A21。

```text
2543d382529729e32750ab6b22636bc487372ffd4204c677e15a817774a8e63c  A21r2 no-MMC DTB
4acefadf00012638de85ac2d237eb3028b42112a4026d942630f0c19a3e34545  A21r2 FIT
```

### A21r2 实机进入 shell，但 zero PCM 命中错误声卡

A21r2 成功避开 MMC parser Oops，`/proc/partitions` 为空。原厂音频设备按以下顺序注册：

```text
card 0  RK-HDMI-I2S   /dev/snd/pcmC0D0p
card 1  RK_MA4        capture
card 2  RK_AK7755     /dev/snd/pcmC2D0p
card 3  RK-SPDIF-CARD playback
```

AK7755 mapping 和 machine probe 均成功，但 A19 的 `/bin/r1-pcm-clock-test` 是为主线单声卡
环境构建，固定打开 `/dev/snd/pcmC0D0p`。因此五秒测试的 `xruns=0`、`pcm_rc=0` 只证明原厂
HDMI PCM 可接受数据；用户听到完全无声是预期结果，不能推断原厂 AK7755 链没有底噪。
日志中 wireless platform glue 又因 SDIO 已禁用而重试到约 90 秒，虽未崩溃但与实验无关。

A21r3 不修改原厂 `rockchip-ak7755` 节点，只在 overlay 禁用 storage、wireless platform glue、
HDMI/MA4/SPDIF 及其余无关 machine cards，使 `RK_AK7755` 成为唯一声卡/card 0，继续复用已经
验证的 zero-PCM 工具。构建脚本强制断言所有被排除节点为 disabled、AK7755 为 okay，并完成
三个 FIT payload 的逐字节比较。专用下载器已切换并锁定 A21r3 hash。

```text
3651c63aae60a20dddd702c0ed38dfd589ec24c1e09c5d31407ef15460b4d519  A21r3 audio-only DTB
daeb0fe41c5e701b2b0747b789e307778081fd4a30fb31cd66b5d327c1a1cf20  A21r3 FIT
```

### A21r3 实机结论：原厂软件在 PA 解除静音时无固定底噪

A21r3 的 `/proc/asound/cards` 只有 card 0 `RK_AK7755`，`/proc/asound/pcm` 只有 AK7755 的
playback/capture device。十秒 zero PCM 完整执行：

```text
[AK7755] ak7755_set_dai_mute: unmute
zero_stream_seconds=10 state=running
gpio-35  (ak7755 pdn)          out hi
gpio-111 (TPA3118D2,shutdown)  out hi
gpio-113 (TPA3118D2,mute)      out lo
[AK7755] ak7755_set_dai_mute: mute
zero_stream_complete xruns=0
pcm_rc=0
```

GPIO113 low 与此前 A10 的硬件 MUTE A/B 方向一致，即运行窗口不是“仍被静音”的假阴性；GPIO111
high 也表明 PA 已解除 shutdown。用户确认十秒内完全无声。该结果是同硬件、同 zero PCM 参数、
原厂完整 codec/machine driver 对当前 6.18 实现的有效正向 A/B：TPA3118/扬声器在正确软件状态
下不会产生当前固定底噪，根因仍在软件可控的 AK7755/machine 状态中。

> 更正（A21r4）：后续非零 tone 同样完全无声，证明本段当时把 PA enable+unmute 误当成有效
> 信号路径；该解释已被下文 A21r4/A21r5 记录取代，A21r3 只能作为未完成 Android route 的状态证据。

原厂 3.10 kallsyms 同时包含 `soc_codec_reg_show`、`codec_reg_read_file` 和
`snd_soc_dapm_debugfs_init`，所以当前 boot 可先通过 `/sys/kernel/debug/asoc` 的 `codec_reg`
抓硬件状态，无需加入写寄存器工具。下一步记录 idle、zero-PCM running、close-after 三份快照，
再给 A20 提供等价只读 regdump，逐项差分 DAC/Lineout/mixer/status；在此之前不根据单个公开
驱动默认值继续盲写。

### A21r3 原厂 AK7755 三态寄存器快照

原厂 ASoC `codec_reg` 成功读取 idle、zero-PCM running 和 close-after 三态。running 的完整
C0..DE 快照为：

```text
c0=0d c1=01 c2=10 c3=00 c4=00 c5=00 c6=00 c7=00
c8=00 c9=00 ca=60 cb=00 cc=00 cd=40 ce=0f cf=0c
d0=40 d1=00 d2=00 d3=0f d4=ff d5=30 d6=30 d7=30
d8=18 d9=18 da=10 db=00 dc=00 dd=30 de=00
```

idle 中 C1/CE/CF/DA 为 `00/00/00/30`；stream 启动后变为 `01/0f/0c/10`。close-after 仅 DA
回到 `30`，C1/CE/CF 保持 `01/0f/0c`。这与原厂 log 的 unmute→mute 边界一致。对照 A20
DIRECT RUN 已知值，C1/C2/C6/C8/CE/CF/D4 分别是 `21/00/33/c0/07/08/0f`，差异跨越 clock、
serial format、DAC route、DSP reset/power 和两组 Lineout，不支持“只漏一个 mute 位”的假说。

### Audio A22 只读硬件 regdump 主机候选

新增 freestanding `r1-ak7755-regdump`，只打开 `/dev/i2c-1`，对 `0x19` 发送 C0..EA 各寄存器
低 7 位 command byte 并 repeated-start 读一字节；没有任何 write transaction。构建使用
`-ffreestanding -nostdlib -static -Werror -z noexecstack`，无 GLOBAL UND。A22 逐字节复用
A20 zImage/DTB，仅把该工具加入 A19 initramfs；FIT 三个 payload 和 initramfs 内工具都已抽取
后 `cmp` 一致，默认 `usb-dfu-r1-linux.py` 已切换并锁定 A22。

```text
cbe75c041e6242eaab82b6605c197cf1b8d92815cab6d8702ee31de0542cab83  r1-ak7755-regdump
416af28683590255c2cf7a53ea3c5934769bf7e97a971f719f6b545e46483691  A22 initramfs
0d76869bb4aa67b776ad2e9fafb53cfc43cd4ef8a93dfd6610cd3264b950e400  A22 FIT
```

### A22 noisy-running 实机快照与 A21r4 正向控制

A22 的 idle 与 running regdump 均成功。running C0..EA 为：

```text
c0=35 c1=21 c2=10 c3=f0 c6=33 c7=f3 c8=c0 ca=00
cd=40 ce=07 cf=08 d0=40 d3=00 d4=0f da=10
e0=55 e6=01 e9=42 ea=80
```

其余已输出寄存器均按 `docs/audio.md` 记录。该快照与 DIRECT RUN verify 行逐项一致，证明日志
不是软件 cache。close 后 driver 断言 PDN，after regdump 无输出，故不将空文件解释为全零状态。
与 factory quiet-running 的差异覆盖 clock/serial/DSP route/power/Lineout 多组寄存器；其中 factory
C8=`00` 依赖 data2 DSP path，而当前 C8=`c0` 直接选择 SDIN1。仅凭 factory zero 安静仍缺少
“非零 PCM 能穿过 DSP path”的正向控制。

为此从 zero-stream 工具以编译期开关派生 `r1-factory-tone-test`：ALSA 参数、card 0 device、
buffer、xrun 处理完全相同，只把 period 填成固定 1 kHz stereo、峰值 32（约 -60 dBFS）。不开
宏重建 zero 工具后与既有 artifact `cmp` 一致。A21r4 使用 A21r3 factory kernel/audio-only DT，
新 rescue initramfs 只携带 zero、tone 和 data2 firmware；FIT 与两个工具均抽取后逐字节核验，
factory 专用 DFU downloader 已锁定 A21r4。

```text
95ded47c48d8325bae9083a3bc5b9f905e1b00d3ed56937f6b2f08e135afa373  r1-factory-tone-test
b88e684001a88f64eb2c3c8f4b18c39ac0558abacd12a3b02545999c0863fa88  A21r4 initramfs
4b1804055568fa080737cab08b9155744f4e7a08bc86ee2865d0bad0d457e24d  A21r4 FIT
```

### A21r4 实机失败：原厂 zero 与 tone 均无声

用户依次运行 A21r4 的 zero 与固定约 -60 dBFS、1 kHz tone，主观结论为“都没有声音”。因此
A21r3 的无底噪不是有效安静播放基线，而是原厂 kernel/codec/machine driver 在精简 rescue
用户态下没有建立完整播放 route 的假阴性。此前根据 GPIO111=high/GPIO113=low 得出的结论只能
证明外部 PA enable+unmute，不能证明 PCM 已经通过 AK7755 DSP/DAC/Lineout 到达 PA。

本次结论推翻并替代“原厂软件状态已证明同一模拟后端可以无噪”的旧解释。A21r3 寄存器快照
继续保留为未完成 Android route 的硬件状态证据，但不得直接作为 6.18 A23 的寄存器目标。

### 原厂 Android AK7755 media-speaker 路由恢复

对本地只读 `backup/partitions/system.img` 提取 `/system/bin/echo_test` 与
`/system/lib/hw/audio.primary.rk30board.so`；没有挂载或修改镜像：

```sh
tmpdir=$(mktemp -d /tmp/r1-system-audio.XXXXXX)
debugfs -R "rdump /bin $tmpdir" backup/partitions/system.img
debugfs -R "rdump /lib/hw $tmpdir" backup/partitions/system.img
readelf -Ws "$tmpdir/hw/audio.primary.rk30board.so" |
  grep 'ak7755_\(config_table\|speaker_normal_controls\|playback_off_controls\)'
sed -n '290,350p' "$tmpdir/bin/echo_test"
```

原厂 HAL 文件日期为 system image 中记录的 2018-05-22，动态符号给出：

```text
ak7755_config_table             address 0x0001d6cc size 1536
ak7755_speaker_normal_controls address 0x00017d10 size 176
ak7755_playback_off_controls   address 0x00016810 size 32
```

`speaker_normal_controls` 恰为 11 个 16-byte entry。结合 ELF RELATIVE relocations 和 rodata
字符串恢复出精确 `media-speaker` 序列：

```text
DRAM Size(Bank1:Bank0)   = 1
DLRAM Mode(Bank1:Bank0)  = 2
POMODE DLRAM Pointer 0   = 1
DSP Firmware PRAM        = data2
DSP Firmware CRAM        = data2
DSP Firmware OFREG       = data2
DAC MUX                   = DSP
Line Out Volume 1        = 15
Line Out Volume 2        = 15
LineOut Amp1             = On
LineOut Amp2             = On
```

`echo_test` 的 `speaker mid-low` 与 `speaker high` 分支提供第二份明文交叉证据：两者先将 Amp1/2
设为 Off，再用 `tinymix -D 2` 设置 DRAM=`1`、POMODE=`1`、DLRAM=`2`，下载 data2 PRAM、CRAM
和 **OFREG**，选择 DAC MUX=`DSP`，最后按目标扬声器设置 Lineout volume 并只打开对应 Amp。
公开 AKM 驱动祖先 `/tmp/r1-kasa-ak7755.c` 又确认前三项是 `SOC_ENUM`，三项 firmware 是
`SOC_ENUM_EXT`。这解释了 A21r4：initramfs 只带 PRAM/CRAM，且没有任何 Android mixer route
操作；非零 PCM 无法成为正向控制。

本地原厂 OFREG data2 为 39 bytes，SHA-256：

```text
60651af8c30aa3b38feeee60588a8664ee80b4071953a4dd3d96158e5fc875ca  ak7755_ofreg_data2.bin
```

### Audio A21r5：恢复 Android route 的正向控制候选

新增 freestanding `r1-factory-ak7755-route`。它仅打开 `/dev/snd/controlC0`，按名字查询 ALSA
control，并严格按恢复的 11 项白名单顺序设置：数字 enum index 与原厂 tinymix 一致，字符串
enum 会先枚举名称再写对应 item；任何 control、类型、enum name 或 ioctl 失败都会立即退出。
它不直接写 AK7755 I2C 寄存器，不修改 GPIO，也不绕过原厂 machine driver 的 PA mute/shutdown。

A21r5 复用 A21r3 的原厂 kernel、audio-only DT、`maxcpus=1`、no-MMC/no-wireless 边界；initramfs
加入 route、zero、tone 与 data2 PRAM/CRAM/OFREG。可复现构建：

```sh
scripts/build-r1-factory-ak7755-route-a21r5.sh
```

构建脚本检查原厂 kernel、A21r3 DTB、zero/tone/route 工具的固定 SHA；FIT 解包后逐字节比较
kernel、ramdisk、DTB，并从 ramdisk 再比较三个工具与 OFREG。固定 `SOURCE_DATE_EPOCH` 后连续
构建哈希一致：

```text
388dc260c86230b16cf20b82f5c1d51823df1917066a8f9d78dc0ce06b73c65b  r1-factory-ak7755-route
82e8e1d9e8381ad3e3b47aa95374b1fff489af393ef30962b0c2beddfd1a244b  r1-initramfs-factory-3.10-ak7755-route-a21r5.cpio.gz
334f75358c9f109ba3115c268d11cd6b7d638f86cb90af8f61b8694f4438003c  r1-linux-factory-3.10-ak7755-route-a21r5.itb
```

下一次实机顺序固定为：先应用 11 项 route；若出现任一 FAIL 则停止。route 全部成功后，先跑
zero 5 秒，再跑相同低电平 tone 2 秒：

```sh
/bin/r1-factory-ak7755-route
echo "route_rc=$?"
/bin/r1-pcm-clock-test 5
echo "zero_rc=$?"
/bin/r1-factory-tone-test 2
echo "tone_rc=$?"
```

只有 `route_rc=0`、zero 安静且 tone 可闻，才能把这条原厂路径视作有效正向基线。当前 A21r5
只有主机静态验证，尚无实机结果。

### A21r5 实机正向听感：tone 可闻且基本无固定底噪

用户在应用 A21r5 恢复路由后执行：

```sh
/bin/r1-factory-tone-test 2
```

用户报告“这个没啥底噪，但是 1 kHz 有种震动感”。该用户听感属于实机证据：非零 1 kHz 已
通过原厂 data2+OFREG DSP 路由到达扬声器，且没有复现 6.18 direct path 中的恒定明显底噪。
因此 A21r5 达到了 A21r4 缺失的正向控制，A21r3/A21r4 的完全无声可确定为未执行 Android
media-speaker route，而不是同一硬件的有效安静播放状态。

随后补交的完整输出关闭了这两个证据缺口。11 项 route 全部返回 `applied`，工具输出
`FACTORY_AK7755_ROUTE_APPLIED controls=11 profile=media-speaker data=data2` 且 `route_rc=0`；
三份下载日志的 CRC 为 PRAM `9916`、CRAM `4453`、OFREG `96c1`。10 秒 tone 完成
`xruns=0` 且 `tone_rc=0`。三态 `codec_reg` 的关键差分为：

```text
                 C0 C1 C2 C3 C4 CA CE CF D3 D4 DA
routed idle      0d 00 10 02 48 60 00 00 0f ff 30
tone running     0d 01 10 02 48 60 0f 0c 0f ff 10
close after      0d 01 10 02 48 60 0f 0c 0f ff 30
```

其余 C5..C9 均为 `00`，CD/D0 为 `40`，D5/D6/D7/DD 为 `30`，D8/D9 为 `18`。
这证明有效安静路径确实是 AK7755 provider clock + data2 DSP + 两路 Lineout，而非 A22 的
CPU-provider/SDIN1-direct 路径。所谓“震动感”仍只记录为主观听感；没有波形或 THD 测量，
不据此诊断 DSP、扬声器或功放故障。

### Linux 6.18 Audio A23：迁移已验证的原厂 DSP 路由

A23 不再盲调单个增益位，而把 A21r5 的完整合同迁入 6.18：AK7755 改为 12.288 MHz XTI
BCLK/LRCK provider，RK3229 I2S2 改为 consumer，BICK 改为 64fs；在 PRAM/CRAM 前写入
C3=`02`、C4=`48`，新增命令头 `0xb2`、最大 99 bytes 的 OFREG 下载和相同 CRC16 严格校验。
PCM prepare 时按实机 running 状态验证 C0/C1/C2/C3/C4/C6/C7/C8/CA/CE/D3/D4/DA/CF；
任一不符即 soft-mute、reset、power-down。外部功放仍在 boot/idle 保持 shutdown+mute，只有
专用 misc gate 可暂时打开，500 ms keepalive 失联保护不变。

可复现构建：

```sh
scripts/build-r1-ak7755-factory-dsp-a23.sh
```

构建完成整核链接、DT 编译、initramfs firmware/tool 回读和 FIT 三 payload 解包比较。OFREG
仍只从本地 proprietary evidence 注入 initramfs，不纳入源码。产物：

```text
d0365f82aa7f828bfd4550b1c1c82ae6891111a8547e5a239953c821821091d2  zImage-mainline-6.18-ak7755-factory-dsp-a23
5df38a078e3206b0ac0fbac75cd284d365b8b1a1ca709404e385591ba0475eb1  r1-initramfs-mainline-6.18-ak7755-factory-dsp-a23.cpio.gz
304de397758ecf1725c90a58766c6f26e2dd8818fc2e6a27623428d2d341e412  rk3229-phicomm-r1-mainline-6.18-ak7755-factory-dsp-a23.dtb
1af69a405154ab7cb24a40a8221aabccaa97d7b14192e8b9696acc6c1820c217  r1-linux-mainline-6.18-ak7755-factory-dsp-a23.itb
```

这仍是主机候选，不能把 A21r5 的听感借给 A23。下一步 RAM-only 上板先确认 OFREG
CRC `96c1`、provider/64fs、card/PCM；再执行一次固定低电平 audible test，比较是否消除 A22
固定底噪。eMMC 常驻 boot chain 不需要也不允许重写。

### A23 首次打包回归与修正：安全门设备缺失

首次 A23 实机执行 `/bin/r1-audible-test` 立即得到：

```text
open /dev/r1-audio-safety errno=0x02
```

`0x02` 是 `ENOENT`。源码复核发现 DSP/DAI 改动没有触发该错误，真正原因是 A23 构建脚本误选
`rk3229-phicomm-r1-open-optee-ak7755-dai-a4.dts`。A4 只建立声卡，并沿用 always-on 的安全
regulator；只有 A8 DT 才删除旧 `safe-supply`、增加 machine driver 所需的
`amp-enable-supply`/`amp-unmute-supply`，从而注册 root-only `/dev/r1-audio-safety`。因此这次运行
没有打开功放，不包含 tone 或底噪证据。

修正并重建的精确命令：

```sh
JOBS=4 scripts/build-r1-ak7755-factory-dsp-a23.sh
```

构建脚本现使用 `rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts`。最终 DTB 反编译确认
`/sound` 同时含两个 supply，GPIO3_B7 为 active-high enable，GPIO3_C1 为 active-low unmute；
codec 节点不再含 `safe-supply`。FIT 解包和三 payload 逐字节比较通过，eMMC 未修改：

```text
dad8a1cb5bba5f5f04c63f24fa24084a01f74beb0d34e52409dd36f03eb9a45b  zImage-mainline-6.18-ak7755-factory-dsp-a23
5df38a078e3206b0ac0fbac75cd284d365b8b1a1ca709404e385591ba0475eb1  r1-initramfs-mainline-6.18-ak7755-factory-dsp-a23.cpio.gz
4078b6aa84190948f9ffc289c6762645effc68c776e233664055c04be3cae2e7  rk3229-phicomm-r1-mainline-6.18-ak7755-factory-dsp-a23.dtb
7c400089e72ab7fd66d2abefeefbf7a33e5d6090963b6c0bb2f6f8042aa17d54  r1-linux-mainline-6.18-ak7755-factory-dsp-a23.itb
```

默认 DFU 下载器已改锁该 FIT 哈希。下一步仍是 RAM-only 重跑；必须先看到安全设备存在，才允许
执行 audible test。

### 修正版 A23 首次听感：旧固定底噪未复现

用户在修正版 A23 上重新运行 `/bin/r1-audible-test` 后报告“good，很干净”。因为工具只有成功
打开 `/dev/r1-audio-safety` 并进入受控输出窗口才可能实际出声，这也间接证明前述 A4 DT 打包
回归已经越过。与 A22 direct path 的固定明显底噪相比，本次同一硬件主观 A/B 支持：原厂
data2+OFREG DSP 路由解决了当前可闻底噪问题。

本轮没有粘贴 `audible_rc`、FACTORY DSP RUN/STANDBY readback 或最终 GPIO SAFE 行，因此只把
“测试音可闻且很干净”记录为用户听感证据，不把退出码、fail-safe 收口或客观 SNR 写成已验证。
下一步在不重启、不改寄存器的同一 A23 启动中运行 60 秒 zero PCM 和无线/四核回归；通过后再把
受控音乐工具移到 factory DSP 路由，不回到已经判定有噪的 SDIN1 direct 路径。

### A23 回归通过，下一阶段改为普通 ALSA/BlueZ

用户确认前述 60 秒 zero PCM、四核、Wi-Fi、Bluetooth LE 与功放最终安全状态回归均“没问题”。
本轮没有粘贴逐项命令输出，故证据类型记为用户确认，不记录具体 xrun、扫描数量、IPI 增量或
GPIO 行。用户同时决定不再重复已经证明正常的内置低音量旋律，直接进入普通 ALSA/BlueZ。

源码和宿主盘点确认两项边界：当前机器只有 `arm-none-eabi` 裸机工具链，没有 ARM Linux
sysroot；rescue initramfs 也只有静态 BusyBox 和 freestanding 工具。完整 D-Bus/BlueZ/ALSA
库不应以临时二进制堆叠方式加入。采用 Buildroot 2026.05 生成 ARMv7 工具链/rootfs；官方手册
由 Buildroot developers 于 2026-06-08 从 revision `313414b92c` 生成：
<https://buildroot.org/downloads/manual/manual.html>。

BlueZ 5 之后的本地音频需要第三方 audio application。BlueALSA 上游 README（Arkadiusz Bokowy
等维护，访问于 2026-08-12，moving `master`，待 Buildroot 固定实际版本）明确提供
`bluealsa-aplay`，可把 A2DP stream 转发到本地 ALSA PCM：
<https://github.com/arkq/bluez-alsa>。BlueZ 上游 README（Linux Bluetooth maintainers，访问于
2026-08-12，moving `master`）同时确认 A2DP/AVRCP 为 `bluetoothd` 内建 profile，构建时可被
关闭：<https://github.com/bluez/bluez>。项目解释是：第一版选 D-Bus + BlueZ + BlueALSA +
alsa-utils，显式开启 A2DP/AVRCP；先验证 SBC Sink，不同时引入 PipeWire、LDAC 和 DSP 插件。

内核前置工作命名为 A24：普通 PCM 不再要求应用打开 `/dev/r1-audio-safety`，machine driver
根据 playback 生命周期自动执行 mute→enable→settle→unmute，并在 STOP、close、错误与关机时
mute→shutdown。诊断 misc gate 可保留但必须与普通 stream 互斥。A24 与 Buildroot rootfs 都先走
DFU/RAM-only；配对、播放、暂停、断连、崩溃收口和重连全部通过前，不写 eMMC rootfs。
