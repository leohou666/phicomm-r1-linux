# Recovery 前置知识

本文档系统介绍 Android Recovery 模式的原理、架构、OTA 机制和工程应用，作为面试准备及 R1 项目 bring-up 策略的参考资料。内容以 AOSP 源码、公开设计文档和 R1 实机验证为基础。

> R1 平台：Rockchip RK3229 / Android based on 7.1 / kernel 3.10 / ARM Cortex-A7
> 但文中的 recovery 通用体系适用于 Android 5.x ~ 14。

---

## 1. Recovery 的定位

### 1.1 它是什么

Recovery 是 Android 系统中**独立于正常系统的迷你启动环境**。它不是"Android 的一个模式"——它是一个完整的、自包含的操作系统：

```
正常 Android:
  boot.img → kernel + ramdisk(init) → system/vendor → zygote → system_server → apps

Recovery:
  recovery.img → kernel + ramdisk(recovery init) → /sbin/recovery 二进制
```

Recovery **不 mount `/system`**（旧版）或只用 `/system` 的极小部分（新版）。所有功能由 ramdisk 内的 BusyBox/toolbox + recovery 二进制实现。

### 1.2 为什么需要独立

如果正常系统损坏（OTA 失败、文件系统崩溃、恶意 root），recovery 作为"救援环境"必须不受影响。因此它拥有：

- 独立的 kernel（与 boot 相同，但可替换）
- 独立的 ramdisk（与 boot 不同）
- 独立的 `/partition`（通常 16~64 MiB）
- 独立于 `/data` 和 `/system` 的信任根

### 1.3 工程上的三个角色

| 角色 | 谁使用 | 功能 |
|---|---|---|
| **用户 recovery** | 最终用户 | 恢复出厂设置、清除缓存 |
| **OEM recovery** | 厂商 | 校准、测试、工厂刷写 |
| **AOSP recovery** | 系统 | OTA 更新、ADB sideload |

R1 的 recovery 属于"OEM recovery + AOSP recovery"混合：同时提供 ADB sideload OTA 能力（`/sbin/recovery`）和厂商调试入口（`ro.debuggable=1`、串口 root shell）。

---

## 2. Android Boot Image 格式

### 2.1 Header 结构

boot 和 recovery 分区内容本质相同，均为 Android boot image，其 header 定义在 `system/tools/mkbootimg/bootimg.h`：

```c
struct boot_img_hdr {
    uint8_t magic[8];              // "ANDROID!"
    uint32_t kernel_size;          // kernel 大小（bytes）
    uint32_t kernel_addr;          // kernel 加载地址
    uint32_t ramdisk_size;         // ramdisk 大小
    uint32_t ramdisk_addr;         // ramdisk 加载地址
    uint32_t second_size;          // second stage 大小
    uint32_t second_addr;          // second stage 地址
    uint32_t tags_addr;            // ATAGs / DTB 地址
    uint32_t page_size;            // flash page size（通常 2048）
    uint32_t dt_size;              // (version 1+) DTB 大小
    uint32_t unused[2];            // (v0) 保留
    uint8_t name[16];              // 产品名
    uint8_t cmdline[512];          // 内核命令行
    uint32_t id[8];                // SHA-1 (kernel+ramdisk+second)
    uint8_t extra_cmdline[1024];   // (v2+) 追加 cmdline
    // ... 后续字段随 header version 扩展
};
```

### 2.2 Header Version 演进

| Version | Android 版本 | 变化 |
|---|---|---|
| 0 | Android 6 及以前 | 经典 header，无 DTB 支持 |
| 1 | Android 7+ | 增加 `dt_size` 字段，支持 recovery DTBO |
| 2 | Android 9+ | 增加 `header_size`、`recovery_dtbo` 字段 |
| 3 | Android 11+ | vendor boot image 分离 |
| 4 | Android 12+ | ramdisk 片段化，boot signature |

R1 使用 v0 header。

### 2.3 内存布局

Bootloader 根据 header 中的地址字段将各组件放入内存，然后跳转 kernel：

```
DRAM 地址空间:

0x60088000 ──┬── tags_addr  ──→ ATAGs 或 DTB
0x60a00000 ──┤── second_addr ──→ Rockchip resource image (内含 DTB + logo)
0x62000000 ──┤── kernel_addr ──→ zImage → 自解压 → 内核运行
0x65bf0000 ──┤── ramdisk_addr ─→ gzip'd cpio → 解压到 rootfs
```

**陷阱**：更换内核（如 R1 从 3.10 换到 6.18）时，新内核的 `.text`+`.data`+`.bss` 可能显著增大，如果 `kernel_addr` 过高而 `ramdisk_addr` 紧跟其后，解压后内核的 BSS 段会覆盖 ramdisk。这就是 R1 主线 recovery 候选发现并修正的地址重叠问题。

### 2.4 Rockchip 扩展：resource image

Rockchip 在标准 Android boot image 的 ramdisk 之后附加了一个 `second`（second stage）区域，内为 Rockchip resource image：

```
"RSCE" 4字节魔数
  ├── resource_entry[] 数组
  │     ├── RESOURCE_ENTRY_KERNEL_DTB
  │     │     └── rk-kernel.dtb  ← 板级设备树
  │     ├── RESOURCE_ENTRY_LOGO_KERNEL
  │     │     └── 开机 logo 位图
  │     └── RESOURCE_ENTRY_LOGO_CHARGING
  │           └── 充电 logo 位图
  └── ...
```

R1 项目在拆解 boot/recovery 时提取此区域并用 `/scripts/rkresource` 工具分离出 `rk-kernel.dtb`。

### 2.5 Rockchip 扩展：Boot SHA

原厂 U-Boot 实现了比 AOSP 更宽的摘要校验。除 kernel、ramdisk、second 及对应 size（各 4 字节 LE）外：

```
SHA-1 输入 = kernel_size(LE32) + kernel_data +
             ramdisk_size(LE32) + ramdisk_data +
             second_size(LE32) + second_data +
             tags_addr(LE32) + page_size(LE32) +
             unused[0](LE32) + unused[1](LE32) +
             name[16] + cmdline[512]
SHA-256 输入 = 同上
```

SHA-1 写入 `id[8]`（offset `0x248`），SHA-256 + flag `256` 写入 `offset 0x26c`（扩展区）。R1 的 `scripts/add-rockchip-boot-hashes.py` 精确复现该算法，启动前自动注入。

---

## 3. misc BCB：启动控制块

### 3.1 数据结构

misc 分区核心是一个 2048 字节的 `struct bootloader_message`（定义在 `bootloader_message.h`）：

```c
struct bootloader_message {
    char command[32];        // "boot-recovery", "update-radia", 等
    char status[32];         // 操作状态（recovery 更新后清空）
    char recovery[768];      // recovery 与 bootloader 通信区
    char stage[32];          // 更新阶段标识
    char reserved[1184];     // 保留
};
```

### 3.2 "boot-recovery" 命令的生命周期

```
1. Android 应用 / adb 发起:
     setprop ctl.start pre-recovery
     或
     RecoverySystem.installPackage()

2. RecoverySystem 写入 misc:
     echo -n "boot-recovery" | dd of=/dev/block/../misc

3. 重启 → Bootloader (U-Boot) 读取 misc:
     if command == "boot-recovery":
         load recovery.img → jump to kernel

4. Recovery 内核启动 → /sbin/recovery 运行:
     从 /misc 读取 status/recovery 字段
     完成当前操作后:
         memset(command, 0, 32)  # 清除命令
         写入 misc（防止下次启动再进 recovery）

5. Recovery 重启:
     Bootloader 看到空 command → 进入正常 boot
```

**关键教训**（R1 实机验证）：如果没能在 recovery 环境内完成操作就直接重启（如 `reboot -f` 不经过 recovery 二进制），misc 中的 `boot-recovery` 可能**尚未清除**，下次启动仍然进 recovery。反之如果 misc 根本就没写入（没有 sync），则进入正常 Android → 触发 `flash_recovery` service 自动恢复原厂 recovery。

### 3.3 BCB 写入的安全方式

```sh
# 写入 command 字段
busybox dd if=/dev/zero of=/dev/block/by-name/misc bs=1 count=32
busybox echo -n "boot-recovery" | busybox dd of=/dev/block/by-name/misc bs=1 count=13
busybox sync
# 验证
busybox dd if=/dev/block/by-name/misc bs=32 count=1 2>/dev/null
```

---

## 4. OTA 更新机制

### 4.1 传统（非 A/B）OTA 流程

```
Android 正常运行
  ↓ download OTA package (zip)
  ↓ 写入 /cache/recovery/command:
  ↓   "--update_package=CACHE:update.zip"
  ↓ 写入 misc BCB: "boot-recovery"
  ↓ reboot
Recovery 启动
  ↓ /sbin/recovery 读取 BCB → 获得 package 路径
  ↓ 校验 OTA zip 签名（RSA/ECDSA）
  ↓ 解压 zip → 获取 updater-script
  ↓ 执行 edify 脚本:
  ↓   apply_patch() / block_image_update()
  ↓   format() / mount()
  ↓   set_metadata()
  ↓ reboot
Android 正常启动
  ↓ install-recovery.sh 用 recovery-from-boot.p 重建 recovery（如果需要）
```

### 4.2 全量 OTA vs 增量 OTA

| 类型 | 内容 | 大小 | 校验 |
|---|---|---|---|
| **全量 (full)** | 完整的 system/vendor/boot 镜像 | 数百 MB ~ GB | 需设备先按预期分区布局 |
| **增量 (incremental)** | 二进制 diff（bsdiff/imgdiff） | 几十 ~ 数百 MB | 需设备当前镜像与预期一致 |

增量 OTA 的 `apply_patch()` 机制：
```
apply_patch(src_file, tgt_file, tgt_sha1, tgt_size, patch_data)
→ 读 src_file，计算 SHA-1
→ 若匹配预期源 → 应用 patch → 验证目标 SHA-1 → 写入 tgt_file
→ 若不匹配 → 报错，停止更新
```

### 4.3 Block-based OTA（Android 5+）

传统 file-based OTA 逐文件操作慢且容易损坏文件系统元数据。Block-based OTA 直接操作块设备：

```
block_image_update("/dev/block/by-name/system", "system.transfer.list",
                   "system.new.dat", "system.patch.dat")
```

`transfer.list` 描述传输计划：
```
stash <id> <blocks>      ← 暂存当前块（供之后 patch）
erase <range>            ← 擦除指定块范围
new <range>              ← 从 new.dat 写新数据
patch <src_range> <dst_range>  ← 从 patch.dat 打补丁
```

### 4.4 Edify 脚本语言

OTA zip 中的 `META-INF/com/google/android/updater-script` 使用 Edify 语言，这是一门专为 OTA 设计的受限脚本语言：

```edify
# 获取设备信息
getprop("ro.product.device") == "rk322x_echo" || abort("wrong device");

# 显示进度
ui_print("Installing update...");
show_progress(0.5, 0);

# 挂载分区
mount("ext4", "EMMC", "/dev/block/by-name/system", "/system");

# 打补丁
apply_patch_check("/system/build.prop",
    "abc123...", "def456...") || abort("build.prop mismatch");
apply_patch("/system/build.prop", "-",
    "abc123...", 12345,
    "def456...", package_extract_file("patch/build.prop.p"));

# Block-based 写入
block_image_update("/dev/block/by-name/system",
    package_extract_file("system.transfer.list"),
    "system.new.dat", "system.patch.dat");

# 设置权限
set_metadata("/system/bin/sh", "uid", 0, "gid", 2000, "mode", 0755);
```

`updater-binary` 是一个静态链接的 C 程序，内置 Edify 解释器和所有上述函数。

### 4.5 OTA 签名

```
OTA zip 签名结构 (JAR signing):

META-INF/
├── CERT.RSA      ← X.509 证书 + PKCS#7 签名
├── CERT.SF       ← 清单文件 SHA-1 摘要 + 签名
├── MANIFEST.MF   ← 所有 zip entry 的 SHA-1
├── com/
│   ├── google/
│   │   └── android/
│   │       ├── update-binary    ← updater 可执行文件
│   │       └── updater-script   ← Edify 脚本
│   └── android/
│       └── metadata             ← OTA 元数据
```

Recovery 使用 `/system/etc/security/otacerts.zip` 中的公钥验证签名。在编译层面，recovery 二进制可以硬编码多个公钥（`test_key` 用于开发/eng 版本，release 版本只用 OEM 密钥）。

---

## 5. A/B（Seamless）更新

### 5.1 传统更新的致命缺陷

```
传统方案:
  [recovery] ← 下载 OTA → 应用 → 重启 → [Android]
                                           ↑
                                    如果 OTA 写坏了:
                                    → 启动失败
                                    → 只能进 recovery
                                    → 需要用户手动刷机
```

### 5.2 A/B 方案

```
A/B 方案:
  [slot A: boot+system+vendor 当前运行中]
  [slot B: boot+system+vendor 空闲]
  ↓ 下载 OTA
  ↓ update_engine 直接写入 slot B（设备正常运行！）
  ↓ 写入完成，设置 slot B 为 active
  ↓ 重启
  [slot B: 新版本启动]
  ↓ 用户使用正常
  [slot A: 保留旧版本作为回退]
  ↓ 下次 OTA 写入 slot A ...
```

优点：
- **OTA 期间设备保持可用**（流式写入，不停机）
- **自动回退**（新版本启动失败 → Boot Control HAL 自动切回旧 slot）
- **不需要 recovery 分区**（系统自带更新守护进程 `update_engine`）

### 5.3 关键组件

| 组件 | 位置 | 功能 |
|---|---|---|
| `update_engine` | `/system/bin/` | 下载、验证、写流式更新 |
| Boot Control HAL | vendor | 管理 active slot、重试计数、unbootable 标记 |
| GPT 分区表 | eMMC/UFS | 无独立 `recovery` 分区，`boot_a/boot_b` |

R1 使用**非 A/B**（传统 recovery）方案。这就是 R1 有独立 `recovery` 分区的根本原因。

---

## 6. Recovery 的内部架构

### 6.1 Ramdisk 内容

```
recovery.img → ramdisk/
├── init.rc                  ← recovery 的 init 脚本
├── default.prop             ← 默认系统属性
├── ueventd.rc               ← 设备节点管理
├── sepolicy                 ← SELinux 二进制策略
├── fstab.*                  ← 分区挂载表
├── sbin/
│   ├── recovery             ← recovery 主二进制（~500 KiB）
│   ├── adbd                 ← ADB daemon
│   ├── busybox              ← 基础 Unix 工具
│   ├── sh                   ← shell
│   └── ...
├── res/
│   ├── images/              ← recovery UI 图片（背景、进度条等）
│   └── keys                 ← 按键映射（如 Volume Up/Down = 菜单上下）
├── system/
│   └── bin/                 ← (新版) 链接符号，非实文件
└── etc/                     ← 配置文件
```

### 6.2 init.rc 流程

```ini
# recovery init.rc（精简版）
on init
    mount tmpfs tmpfs /tmp

service recovery /sbin/recovery
    seclabel u:r:recovery:s0

# 硬件触发进入 recovery（如按键组合）
on property:sys.force_recovery=1
    start recovery
```

Recovery 的 init.rc 极度精简——不需要 zygote、servicemanager、surfaceflinger 等完整 Android 服务栈。

### 6.3 recovery 二进制

`/sbin/recovery` 是一个单二进制程序，集成了：

```
recovery
├── main()
│   ├── get_args()             ← 读 misc BCB + /cache/recovery/command
│   ├── 根据参数分发:
│   │   ├── install_package()  ← OTA / sideload
│   │   ├── wipe_data()        ← 恢复出厂设置
│   │   ├── wipe_cache()       ← 清除缓存分区
│   │   ├── prompt_and_wait()  ← 交互菜单
│   │   └── start_recovery_service()  ← 启动 ADB sideload
│   └── finish_recovery()      ← 清除 misc，重启
├── ui (minui)
│   ├── gr_init()              ← graphics 初始化（framebuffer）
│   ├── gr_color() / gr_fill()
│   └── res_create_*_surface() ← 加载 PNG 资源
├── verifier
│   └── verify_package()       ← RSA/ECDSA 签名验证
├── install
│   └── really_install_package()
├── adb_install
│   └── apply_from_adb()       ← ADB sideload
└── ...
```

### 6.4 MinUI 图形子系统

Recovery 的图形界面称为 **MinUI**（区别于 Android 主系统的 SurfaceFlinger）。它是一个极简 framebuffer 图形库：

- 直接写 `/dev/graphics/fb0`
- 支持双缓冲（back framebuffer）
- 支持 PNG 解码和缩放
- 不依赖 GPU、OpenGL、或任何 Android 图形栈
- 代码量约 3000 行 C

---

## 7. `recovery-from-boot.p` 详细

### 7.1 它的本质

```
/system/recovery-from-boot.p = bsdiff(boot.img, recovery.img)
```

这是一个**二进制 diff 文件**，由编译系统在生成 `system.img` 时计算：

```bash
# 编译时:
imgdiff boot.img recovery.img > recovery-from-boot.p
```

### 7.2 自动恢复流程

每次正常启动 Android 时：

```bash
# /system/bin/install-recovery.sh（由 init.rc 触发）

if ! applypatch -c EMMC:/dev/block/by-name/recovery:<size>:<sha1>; then
    # 校验失败 → recovery 不匹配 → 重建
    applypatch /dev/block/by-name/boot \
               /dev/block/by-name/recovery \
               <target_sha1> <target_size> \
               <source_sha1> /system/recovery-from-boot.p
    # 写入 boot + patch → 还原 recovery
fi
```

### 7.3 对 R1 项目的影响

```
MaskROM 写入主线 recovery → 立即读回校验 ✓
  → 正常启动 Android → install-recovery.sh 校验 ✗
  → applypatch 重建原厂 recovery → 写入
  → adb reboot recovery → U-Boot 加载的仍是原厂 3.10 kernel
```

**绕过方法**：在原厂 recovery 内完成操作，不经过 Android 正常启动：

```
进入原厂 recovery → 写入主线镜像到 recovery 分区 → sync
  → 写 misc BCB "boot-recovery" → sync
  → adb reboot recovery（或直接 echo b > /proc/sysrq-trigger）
  → U-Boot 读 misc → 加载主线 recovery → 主线 6.18 kernel 启动
```

---

## 8. Recovery 中的 ADB

### 8.1 配置差异

| 模式 | `ro.debuggable` | `ro.adb.secure` | `persist.sys.usb.config` | ADB 权限 |
|---|---|---|---|---|
| 正常 Android (user build) | 0 | 1 | mtp | 需确认授权 |
| 正常 Android (eng build) | 1 | 0 | adb | root |
| Recovery (AOSP) | 依赖于 build | 1 | adb | 需确认 |
| R1 Recovery | **1** | 未知 | adb | **`adb root` 可用** |

R1 recovery 的 `ro.debuggable=1` 是关键优势：不需要用户交互即可获得 root ADB，使自动化的 recovery 写入/读回/校验成为可能。

### 8.2 ADB Sideload

```
主机端:
  adb sideload ota.zip

设备端 (recovery):
  进入 ADB sideload 模式
  接收整个 zip（写到 /tmp/update.zip）
  校验签名
  如果签名不对: install aborted
  如果签名正确: 执行 updater-binary
```

ADB sideload 本质上等于"通过 USB 传 OTA 文件，然后按标准流程安装"。它要求文件是有效的 OTA zip（含 `META-INF/com/google/android/update-binary`），不是裸的 boot/recovery image。

R1 实测 sideload 连接失败，判断是 recovery 没有预启动 sideload 服务（`apply_from_adb()` 未在主循环激活）。

---

## 9. 面试 / 八股要点

### 9.1 高频问答

**Q: Recovery 和正常启动有什么区别？**
A: Recovery 使用相同的 kernel 和 DTB，但不同的 ramdisk。ramdisk 包含的是 `/sbin/recovery` 二进制而非 Android init。Recovery 不 mount `/system`、不启动 zygote/service_manager/surfaceflinger，是一个独立的最小系统。

**Q: Android 如何决定进入 recovery？**
A: Bootloader（U-Boot）在启动时读取 misc 分区的前 32 字节。如果内容为 `"boot-recovery"`，加载 recovery.img 而非 boot.img。Recovery 完成工作后清空 misc 字段。

**Q: OTA zip 签名如何验证？**
A: Recovery 使用 `otacerts.zip`（编译时打包到 recovery 资源中的 X.509 公钥集合）验证 `META-INF/CERT.RSA` 的 PKCS#7 签名。`CERT.SF` 验证 `MANIFEST.MF`，`MANIFEST.MF` 验证 zip 内每个文件。形成证书→签名→清单→文件 的信任链。

**Q: A/B 更新相比传统 OTA 的优势？**
A: (1) OTA 下载和写入期间设备正常运行，(2) 新版本启动失败自动回退到旧 slot，(3) 不需要独立 recovery 分区，(4) 支持流式写入（streaming update），下载的同时写入。

**Q: block-based OTA 解决了什么问题？**
A: 传统 file-based OTA 在文件系统上逐文件操作效率低、易损坏文件系统元数据、且对大量小文件极慢。Block-based OTA 直接操作块设备，一个 block range 命令可以覆盖一大片连续区域，大幅提升更新速度和可靠性。

**Q: recovery-from-boot.p 是什么？**
A: boot.img 到 recovery.img 的二进制 diff。Android 正常启动时，install-recovery.sh 用它验证并重建 recovery。如果 recovery 不匹配（被自定义镜像替换），此机制会自动恢复原厂版本。

### 9.2 关键概念对照

| 概念 | 位置 / 文件 | 含义 |
|---|---|---|
| BCB | misc 分区前 2048 字节 | Bootloader 和 recovery 的 IPC 通道 |
| Edify | `updater-script` | OTA 安装脚本语言 |
| `apply_patch()` | `updater-binary` 内置函数 | 二进制 diff 应用与校验 |
| `block_image_update()` | `updater-binary` 内置函数 | 块级镜像写入 |
| MinUI | `/sbin/recovery` 内置 | Recovery 的极简图形库 |
| `otacerts.zip` | recovery 资源文件 | OTA 签名验证公钥集 |
| `install-recovery.sh` | `/system/bin/` | 自动恢复/重建 recovery 的脚本 |

---

## 10. 相关阅读

- [AOSP: Recovery System](https://source.android.com/docs/core/ota/modular-system/recovery)
- [AOSP: OTA 更新](https://source.android.com/docs/core/ota)
- [AOSP: A/B (Seamless) System Updates](https://source.android.com/docs/core/ota/ab)
- [Android Boot Image Header 结构](https://source.android.com/docs/core/architecture/bootloader/boot-image-header)
- [SELinux 前置知识](selinux.md)
- [U-Boot 前置知识](uboot.md)
- [主线 Linux Bring-up](../mainline-bringup.md) — R1 recovery 候选的构建、SHA 注入与实机启动
- [逆向学习记录](../reverse-engineering-journal.md) — R1 recovery 拆解、ADB 测试和自动恢复现象的完整记录
