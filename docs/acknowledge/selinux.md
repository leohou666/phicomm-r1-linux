# SELinux 前置知识

本文档从内核安全架构和应用调试双重视角，系统介绍 SELinux 的原理、策略语言、审计机制以及在 Android 中的工程实践。目标是作为面试准备及 R1 项目 recovery 调试中的问题速查。

> 以 R1 recovery (Android 7.1 / kernel 3.10) 的 enforced SELinux 为实机背景，但原理回溯到 LSM 框架并上探至 AOSP 13+ 的策略演进。

---

## 1. 为什么需要 SELinux：DAC 的局限

### 1.1 DAC（自主访问控制）

传统的 Unix 权限模型是 DAC（Discretionary Access Control）：

```
进程 (UID) ──→ 文件 (owner/group/other + rwx)
              ↑
         "自主"：所有者可以改变权限，进程可以 chmod
```

DAC 的根本问题：
- **root 即上帝**：UID=0 自动获得一切权限
- **进程自愿遵守**：恶意或被攻破的进程可以自主改变自身权限
- **无法限制 root daemon**：一个以 root 运行的网络服务被攻破 = 整个系统沦陷

### 1.2 MAC（强制访问控制）

SELinux 实现的是 MAC（Mandatory Access Control）：

```
进程 (domain) ──→ 资源 (type)
              ↑
         "强制"：由系统级安全策略决定，进程不能绕过
                root ≠ 上帝
```

| | DAC | MAC (SELinux) |
|---|---|---|
| 权限主体 | UID/GID | Security Context（domain） |
| 权限客体 | 文件 owner 位 | Security Context（type） |
| 谁能改 | 所有者 / root | 安全策略（编译时决定）|
| 进程能否绕过 | root 可以 | **不能** |
| 粒度 | 文件级 rwx | 操作级（open, write, ioctl, setattr...） |

### 1.3 最小权限原则

SELinux 的设计哲学来源于 "Principle of Least Privilege"：

> 每个程序只能访问恰好完成其工作所需的资源。

不是 "root 什么都能做"，而是 "init domain 能做什么"、"adbd domain 能做什么"、"recovery domain 能做什么" ——每个 domain 独立定义，互不干扰。

---

## 2. 架构：LSM、Security Server 和 AVC

### 2.1 LSM（Linux Security Modules）

SELinux 不是独立的内核模块。它是通过 LSM 框架插入内核权限检查点的：

```
用户空间请求 (open / write / ioctl / ...)
    ↓
VFS / 网络栈 / IPC 等子系统
    ↓ 在访问实际资源之前
LSM Hook (security_file_open, security_inode_permission, ...)
    ↓
SELinux security server
    ├── policy DB 查询
    ├── AVC cache 查询
    └── 返回: allow / deny
    ↓
允许 → 继续执行
拒绝 → -EACCES (-13), 同时写入 audit log (AVC)
```

LSM 是一组 hook 点，SELinux 是**其中一个** LSM 实现。当代内核还并行支持 AppArmor、Smack、TOMOYO 等，但 Android 固定选 SELinux。

### 2.2 Security Server

Security Server 是内核中的 SELinux 核心决策引擎：

```
security server 在启动时:
  1. 加载 /sepolicy (二进制策略文件)
  2. 解析 allow rules → 构建访问矩阵
  3. 初始化 AVC (Access Vector Cache)

运行时的每次权限检查:
  1. 查 AVC：scontext + tcontext + tclass → cached decision?
     yes → 直接返回
     no  → 遍历 allow rules → 查找匹配
         → 写入 AVC
         → 返回
         → 若结果为 deny → 同时写入 audit
```

### 2.3 AVC (Access Vector Cache)

AVC 是安全服务器的性能加速器。每毫秒可能有数百次权限检查，每次都遍历全部 allow rules 不可行：

```
AVC Entry:
  source context (domain)
  target context (type)
  target class (file / dir / socket / ...)
  permissions bitmask (read=0x1, write=0x2, open=0x4, ...)
  allowed mask (来自 policy)
  decided mask (本次检查结果)
```

后续相同的 `(scontext, tcontext, tclass, permissions)` 组合命中 AVC 后直接返回，无需查 policy。

**关键细节**：AVC denial 日志只在**第一次**被拒时打印。后续相同的被拒操作不再打日志（`avc: denied` 不刷屏），但每次都被实际拒绝。

---

## 3. Security Context（安全上下文）

### 3.1 格式

SELinux 的每个主体（进程）和客体（文件/套接字/...）都有安全上下文：

```
user:role:type:sensitivity[:category]
```

| 字段 | 含义 | 示例 |
|---|---|---|
| **user** | SELinux 用户 | `u` (一般就是 `u`) |
| **role** | SELinux 角色 | `r` (object_r 用于文件，其他为进程角色) |
| **type** | **核心字段**：类型标签 | `init`, `adbd`, `shell`, `system_file` |
| **sensitivity** | MLS 敏感度级别 | `s0` (绝大多数场景固定) |
| **category** | MLS 类别（可选） | `c0,c1` (多级安全) |

**type 是 SELinux 策略的核心**。99% 的 allow/deny 规则都是 type-based。

### 3.2 type 与 domain 的区别

技术上说：
- "domain" 是指主体的 type（进程的安全上下文中的 type 字段）
- "type" 是指客体的 type（文件/socket 的安全上下文中的 type 字段）

实际使用中这两个词经常混用——"一个 SELinux type / domain"指的都是 `.te` 文件中 `type` 声明。

### 3.3 查看安全上下文

```sh
# 当前进程
cat /proc/self/attr/current
# u:r:init:s0

# 文件
ls -Z /system/bin/sh
# u:object_r:shell_exec:s0 /system/bin/sh

# 进程 (所有)
ps -Z
# LABEL                          PID
# u:r:init:s0                    1
# u:r:adbd:s0                    120
```

---

## 4. SELinux 策略语言

### 4.1 Type Enforcement (TE)

TE 是 SELinux 最核心的访问控制机制。策略由一系列声明和规则组成：

```te
# 声明类型（type/domain）
type init, domain;
type adbd, domain;
type shell_exec, exec_type, file_type;
type system_file, file_type;

# 声明属性（用属性分组）
attribute domain;
attribute file_type;

# allow 规则（核心）
allow domain_A type_B:class_C permission_D;

# 示例：允许 init domain 写 /proc/sysrq-trigger
allow init proc_sysrq:file write;

# 示例：允许 adbd 通过 TCP socket 发送数据
allow adbd self:tcp_socket { create bind listen accept };
```

### 4.2 allow 规则的结构

```
allow <source_domain> <target_type> : <object_class> <permissions>;
       ↑                ↑              ↑              ↑
       谁在做            操作什么对象      什么类型的对象    做什么操作
```

`object_class` 定义了资源类别：

| class | 含义 | 典型 permission |
|---|---|---|
| `file` | 普通文件 | read, write, open, create, unlink, getattr, setattr, execute |
| `dir` | 目录 | read, write, open, create, rmdir, search, add_name, remove_name |
| `sock_file` | UNIX domain socket 文件 | create, unlink, write |
| `tcp_socket` | TCP 套接字 | create, bind, listen, connect, accept |
| `unix_stream_socket` | UNIX stream socket | connectto, acceptfrom, read, write, ioctl |
| `process` | 进程操作 | fork, transition, sigkill, setpgid, getsched |
| `capability` | Linux capability | net_admin, sys_admin, sys_ptrace, sys_nice |
| `security` | 安全子系统 | setenforce, setsecparam, load_policy |
| `binder` | Android Binder IPC | call, transfer, impersonate |
| `fd` | 文件描述符 | use |
| `filesystem` | 文件系统操作 | mount, unmount, remount, getattr |

### 4.3 type_transition

`type_transition` 定义了**何时改变 type**：一个进程在特定目录下创建文件时，文件的 type 如何自动确定。

```te
# 当 init 在 /dev 下创建文件时，自动标记为 device_type
type_transition init device:dir device_file "block";

# 效果：
# init 执行: mknod /dev/mmcblk0 b 179 0
# 结果: /dev/mmcblk0 的 type 自动变为 device_file（不依赖父目录的 type）
```

没有 `type_transition` 时，新建文件继承父目录的 type。有了它，可以精确控制在哪个目录下创建什么 type 的文件。

### 4.4 attribute（属性）

属性是 type 的标签，用于批量授权：

```te
# 声明
attribute domain;
attribute file_type;

# 关联
type init, domain;        # init 属于 domain 组
type adbd, domain;        # adbd 也属于 domain 组
type system_file, file_type;  # system_file 属于 file_type 组

# 批量授权（一条规则覆盖所有 domain）
allow domain proc:file read;  # 所有 domain 都可以读 /proc 文件
```

### 4.5 neverallow：防呆规则

`neverallow` 不是运行时规则，是**编译时检查**。它保证某些权限组合永远不可能通过 allow 规则授予：

```te
# 任何 domain 都不能通过任何路径获得“写 boot 分区”的能力
neverallow { domain -recovery } boot_block_device:blk_file write;

# 没有 domain 可以通过任何方式获得 security:setenforce
neverallow { domain -kernel } kernel:security setenforce;
```

`neverallow` 是 Android 安全模型的"护城河"。即使有人提交了 `allow untrusted_app kernel:security setenforce;` 这样的规则，编译过不去。

---

## 5. Android SELinux 策略布局

### 5.1 策略文件组织

```
system/sepolicy/
├── public/             ← 公开 API（所有设备共用）
│   ├── domain.te       ← domain 基础定义
│   ├── app.te          ← 非特权应用
│   ├── adbd.te         ← ADB daemon
│   ├── file_contexts   ← 文件 type 映射
│   └── ...
├── private/            ← 私有定义（编译期，不导出给 vendor）
│   ├── priv_app.te
│   └── ...
├── vendor/             ← SoC / OEM 扩展 [Android 8+]
│   └── ...
├── prebuilts/api/      ← 固化策略 API（保证向后兼容）
└── reqd_mask/          ← ioctl 命令白名单
```

### 5.2 file_contexts

`file_contexts` 定义了文件系统上每个文件/目录的 SELinux type：

```
# 语法：<regex>  <context>
/system/bin/sh          u:object_r:shell_exec:s0
/system/bin/adbd        u:object_r:adbd_exec:s0
/system/bin/recovery    u:object_r:recovery_exec:s0
/data(/.*)?             u:object_r:system_data_file:s0
/dev/block(/.*)?        u:object_r:block_device:s0
/dev/graphics(/.*)?     u:object_r:graphics_device:s0
/sys/fs/selinux(/.*)?   u:object_r:selinuxfs:s0
```

在编译时，`file_contexts` 被编译进 `file_contexts.bin`（二进制格式）。在 Android init 阶段，`restorecon` 命令遍历文件系统标记文件——但初始 ramdisk 中的文件在镜像制作时就已经通过 `setfiles` 工具预先标记好。

### 5.3 策略编译过程

```
*.te 源码
  ├── m4 宏预处理
  ├── checkpolicy / secilc 编译
  ├── 生成中间 policy.conf
  ├── checkpolicy 二次校验（neverallow 检查）
  └── 输出: sepolicy (二进制, ~500 KiB)

file_contexts
  ├── checkfc 语法校验
  └── 输出: file_contexts.bin

sepolicy + file_contexts.bin → 打包进 boot/recovery ramdisk
```

### 5.4 编译时与运行时的策略差异

- **编译时**：源语 `.te` 文件，人可读
- **运行时**：`/sepolicy` 二进制文件，内核直接解析
- **Android 8+**：vendor 可以有自己的 `vendor_sepolicy.cil`（Common Intermediate Language），运行时动态合并
- **调试构建**：eng/userdebug 版本有额外的 permissive/debug 规则

---

## 6. 审计与 AVC 分析

### 6.1 AVC denial 日志的完整解读

```
[ 5835.544280] type=1400 audit(1516470184.220:98):
  avc: denied { setenforce } for pid=155 comm="sh"
  scontext=u:r:init:s0
  tcontext=u:object_r:kernel:s0
  tclass=security
  permissive=0
```

逐字段解析：

| 字段 | 值 | 含义 |
|---|---|---|
| `type=1400` | `AUDIT_AVC` | AVC 拒绝事件 |
| `audit(...)` | 时间戳 + 序列号 | `1516470184.220` = epoch, `98` = 第 98 条审计记录 |
| `denied` | `setenforce` | 被拒绝的操作是 `setenforce` |
| `pid=155` | PID 155 | 发起操作的进程 |
| `comm="sh"` | `sh` | 进程的命令名（`/proc/pid/comm`） |
| `scontext` | `u:r:init:s0` | **源**：shell 运行在 `init` domain |
| `tcontext` | `u:object_r:kernel:s0` | **目标**：kernel security 对象 |
| `tclass` | `security` | 客体类别为安全子系统 |
| `permissive=0` | enforcing | 操作被实际拒绝 |

### 6.2 常见 AVC 模式及修复方向

| AVC 模式 | 问题 | 修复方向 |
|---|---|---|
| `denied { open } for ... file` | 文件被拒绝打开 | `allow domain file_type:file open;` 或换可访问的路径 |
| `denied { write } for ... file` | 文件不可写 | `allow domain file_type:file write;` |
| `denied { execute } for ... file` | 不可执行 | `allow domain exec_type:file execute;` |
| `denied { transition } for ...` | 不可做 domain transition | 需要 `type_transition` + `allow` |
| `denied { ioctl } for ... unix_stream_socket` | socket ioctl 被拒 | `allow domain adbd:unix_stream_socket ioctl;` |
| `denied { setenforce }` | 不可关 SELinux | **不可能修复（neverallow 保护）** |
| `denied { dac_override }` | bypass Unix DAC | 通常是程序应该降低请求而非 override |

### 6.3 audit2allow

`audit2allow` 是 SELinux 工具集中的"自动生成 allow 规则"工具：

```sh
# 从 dmesg 提取 AVC → 生成 allow 规则
dmesg | audit2allow

# 输出示例:
# allow init kernel:security setenforce;
```

**注意**：`audit2allow` 只生成语法正确的规则，不判断规则是否安全。直接抄它的输出添加到策略中可能会破坏 `neverallow` 防线。

---

## 7. Enforcing 与 Permissive

### 7.1 全局模式

```sh
# 查看当前模式
cat /sys/fs/selinux/enforce
# 1 = enforcing, 0 = permissive

# 切换（需要 security:setenforce 权限）
echo 0 > /sys/fs/selinux/enforce

# 内核 cmdline 设置初始模式
androidboot.selinux=permissive   # 从 permissive 启动
enforcing=0                      # 直接禁用 enforcing
```

### 7.2 Permissive Domain

Android 8+ 支持**单个 domain 的 permissive 模式**，不影响全局：

```te
# 让 init domain 处于 permissive 模式（全局仍是 enforcing）
permissive init;
```

编译时检测：`permissive_or_unconfined()` 宏会警告任何 permissive domain 的存在，阻止其进入 release build。

### 7.3 为什么 R1 recovery 不能切 permissive

回到 AVC 日志：

```
scontext=u:r:init:s0     ← shell 在 init domain
permissive=0             ← 全局 enforcing
denied { setenforce }    ← init domain 没有 security:setenforce 权限
```

虽然 `init.te` 在标准 AOSP recovery 中有 `allow init kernel:security setenforce;`，但 R1 的旧版 Rockchip recovery 策略省略了这个权限。且 `neverallow` 规则阻止其他 domain 获得此权限，因此**当前无法动态切换 permissive**。

唯一可行的方式是在 kernel cmdline 中设置 `androidboot.selinux=permissive`，但这需要修改 boot image header 并重新打包。

---

## 8. Android init 与 SELinux 加载

### 8.1 启动时序

```
kernel 启动
  ↓
  early_security_init()      ← LSM 框架初始化
  ↓
  selinux_init()             ← 加载 /sepolicy 二进制
  ↓
  security server 初始化
  ↓
  init 进程启动 (PID 1)
  ↓
  init: selinux_initialize()
  ├── selinux_init_all_handles()
  ├── selinux_set_callback() ← 设置 audit 回调
  └── selinux_restorecon()   ← 遍历 /dev、/sys、/proc 并重标记
  ↓
  init: 启动其他服务 (adbd, servicemanager, ...)
```

### 8.2 Domain Transition

Android 的 init 进程本身是 kernel domain，但通过 **domain transition** 机制，init 可以启动运行在不同 domain 中的进程：

```te
# 1. 声明: 谁可以从 init 转移
type_transition init adbd_exec:process adbd;

# 2. 给源 domain 放行: init 可以"进入" adbd
allow init adbd:process transition;

# 3. 给目标 domain 放行: adbd 允许"被进入"
allow adbd adbd:process { transition entrypoint };
```

当 `init.rc` 中有：

```ini
service adbd /sbin/adbd
    seclabel u:r:adbd:s0
```

init fork 后，在新进程调用 `execve("/sbin/adbd")` 的路径上，SELinux 自动检测到 `adbd_exec` → `adbd` 的 type_transition，然后安全检查通过后，进程的 domain 变为 `adbd`。

---

## 9. R1 Recovery 的 SELinux 实战

### 9.1 环境特征

```sh
# 当前 domain
# cat /proc/self/attr/current
u:r:init:s0

# enforcing 状态
# cat /sys/fs/selinux/enforce
1

# 最近 AVC
# dmesg | grep avc
avc: denied { setenforce } for ... scontext=u:r:init:s0 ...
avc: denied { ioctl } for ... scontext=u:r:shell:s0 ... tcontext=u:r:adbd:s0 ...
```

### 9.2 已确认可操作 vs 被拦截

| 操作 | domain | 结果 | 原因 |
|---|---|---|---|
| `dd if=... of=/dev/block/.../recovery` | `init` | ✅ | block device write 被允许 |
| `echo > /proc/sysrq-trigger` | `init` | ✅ | proc_sysrq 被允许 |
| `echo > /dev/block/.../misc` | `init` | ✅ | block device write 被允许 |
| `busybox sync` | `init` | ✅ | sync syscall 被允许 |
| `echo 0 > /sys/fs/selinux/enforce` | `init` | ❌ | `denied { setenforce }` |
| `adb push ... /data/` | `adbd` | ❌ | `/data/` 目录 write 被拒绝 |
| `adb shell id` | `adbd` | ❌ | `/system/bin/sh` 不存在（非 SELinux 问题） |

### 9.3 绕过策略总结

```
1. 不做跨 domain 操作:
   串口 shell 在 init domain 下 ──→ dd 块设备 ✓

2. 不做文件传输到受限路径:
   不走 adb push ──→ 走串口 xmodem ✓

3. 不关 SELinux:
   不碰 /sys/fs/selinux/enforce ──→ 用已允许的操作 ✓

4. 启动到无 SELinux 的环境:
   主线 recovery 不含 sepolicy ──→ 一劳永逸 ✓
```

核心思路不是"攻破 SELinux"，而是**找出并利用当前 domain 已有的权限完成目标**。R1 的 `init` domain 有 block device 写权限，这已经是刷写 recovery 所需的全部能力。

---

## 10. 面试 / 八股要点

### 10.1 高频问答

**Q: DAC vs MAC 的本质区别？**
A: DAC 以用户身份(UIG/GID)为权限主体，进程可以自主改变权限，root 万能。MAC 以安全上下文(type/domain)为权限主体，由系统级策略(编译时决定)强制执行，进程不能绕过——root 不等于上帝。

**Q: SELinux 的 type 和 domain 的关系？**
A: 技术上 type 是安全上下文的第三个字段，domain 是"属于主体的 type"。策略中进程的 type 称 domain，文件的 type 称 type。实际使用中两个词常混用，都指 `.te` 文件中 `type` 声明的标签。

**Q: neverallow 有什么实际作用？**
A: 编译时检车。它定义了"无论如何不能授予"的权限组合。即使有人提交了 `allow untrusted_app kernel:security setenforce;`，`neverallow` 会在编译阶段拒绝。它是 Android 安全模型的最后防线。

**Q: AVC 缓存机制为什么重要？**
A: 每次文件/网络/IPC 操作都会触发 SELinux 权限检查，如每次都要遍历全部 allow rules 性能不可接受。AVC 缓存了最近的 `(scontext, tcontext, tclass)` 决策，命中率通常 >99%，开销几乎为零。

**Q: 为什么写策略不能用 audit2allow 直接抄？**
A: `audit2allow` 只看语法不讲安全。它可能生成 `allow domain kernel:security setenforce;`，在语法上完全正确，但被 neverallow 拒绝，且业务上不该出现。

**Q: 如何调试 SELinux 拒绝问题？**
A: (1) `dmesg | grep avc` 查看被拒操作，(2) `cat /proc/self/attr/current` 确认当前 domain，(3) `ls -Z` 确认文件 type，(4) 找出是否可以使用已有权限，(5) 如果确实需要新规则，写 .te 文件并确认不被 neverallow 拒绝。

### 10.2 关键概念对照

| 概念 | 位置/命令 | 含义 |
|---|---|---|
| LSM | 内核 `security/` | 安全 hook 框架，SELinux 是其插件之一 |
| Security Server | 内核 SELinux 模块 | 核心决策引擎：加载策略、查询规则 |
| AVC | 内核 SELinux 模块 | 访问向量缓存，加速重复检查 |
| Security Context | `/proc/self/attr/current`, `ls -Z` | `user:role:type:sensitivity` 格式标签 |
| TE (Type Enforcement) | `*.te` 文件 | 基于 type 的访问控制规则 |
| file_contexts | `system/sepolicy/file_contexts` | 文件路径 → type 的映射表 |
| neverallow | `*.te` 中的 `neverallow` | 编译时禁止的权限组合 |
| Permissive Domain | `*.te` 中的 `permissive` | 单 domain 放松模式 |
| `sepolicy` 二进制 | `/sepolicy` | 编译后的策略，内核直接加载 |
| `restorecon` | 命令 | 根据 file_contexts 重新标记文件 type |

---

## 11. 相关阅读

- [NSA: Security-Enhanced Linux](https://www.nsa.gov/what-we-do/research/selinux/)
- [AOSP: SELinux](https://source.android.com/docs/security/features/selinux)
- [AOSP: Writing SELinux Policy](https://source.android.com/docs/security/features/selinux/customize)
- [AOSP: Validating SELinux](https://source.android.com/docs/security/features/selinux/validate)
- [The SELinux Notebook](https://github.com/SELinuxProject/selinux-notebook) — 权威参考
- [Recovery 前置知识](recovery.md)
- [U-Boot 前置知识](uboot.md)
- [逆向学习记录](../reverse-engineering-journal.md) — R1 实机 AVC 日志与绕过尝试
