# Phicomm R1 bring-up 交接记录

更新时间：2026-08-07（Asia/Shanghai）

本文用于把当前工作交给新的 Codex/开发者继续。它是工作现场摘要，不替代按时间保存全部证据的
[`docs/reverse-engineering-journal.md`](docs/reverse-engineering-journal.md)。开始操作前必须先读
[`AGENTS.md`](AGENTS.md)、[`docs/index.md`](docs/index.md) 和本文。

## 一句话状态

R1 已经能通过真 MaskROM，把“原厂 DDR 471 + 主线 R1 SPL/U-Boot 472”完全从 RAM 启动；
无 OP-TEE 时可进入现代 U-Boot，并以单核启动主线 Linux 到 BusyBox shell。当前正在把 Armbian
提供的 RK322x 开源 OP-TEE 加入 RAM-only FIT，以恢复 PSCI/SMP。最后一个候选已经越过此前的
FIT/FDT 错误，停在 SPL 完成架构 fixup、准备跳转 OP-TEE 的边界附近。

## 当前目标

在**不写 eMMC**的前提下完成：

```text
BootROM MaskROM
  -> 原厂 rk322x DDR 471（DDR3 300 MHz，已验证）
  -> 主线 U-Boot SPL 472（TARGET_PHICOMM_R1）
  -> FIT 将开源 RK322x OP-TEE 装到 0x68400000
  -> OP-TEE 提供 RK322x PSCI，并返回 0x61000000 的 U-Boot proper
  -> U-Boot proper
  -> 双核/四核主线 Linux
```

近期验收标准不是“能打印更多字符”，而是：

1. 开源 OP-TEE 实际获得控制权并返回 U-Boot proper；
2. 从该 RAM-only U-Boot 启动至少双核 Linux；
3. uptime 超过原厂 Trust OS 路线稳定复现的约 30 秒冻结边界；
4. 全程不执行任何存储写操作。

## 已验证事实

### 设备与恢复路径

- SoC 为 RK3229/RK322x，BootROM chip info 为 `41 32 32 33`（ASCII `A223`）。
- DRAM 为 512 MiB DDR3，原厂训练频率 300 MHz。
- eMMC 为 Samsung，User Area 为 15,269,888 个 512-byte sector，共 7,818,182,656 bytes。
- 完整 eMMC User Area 已只读备份为 `backup/r1-emmc-user.img`：

  ```text
  SHA-256 eb4dbb57a7a78ea2604121fd699ba11bf951a970c1c0c5c165b78a2bd7c19cf7
  ```

- PCB 按键可以进入 Rockchip Loader/MaskROM。原 U-Boot 尚存活时，恢复全零 misc BCB 后
  Android 已实机正常启动。
- 真 MaskROM 已成功 `db` 下载匹配的 DDR/usbplug loader，并能只读查询芯片和 eMMC。
- `rkdeveloptool ld` 有时把 RAM usbplug 显示为 `Maskrom`，原因是该 usbplug 的
  `bcdUSB=0x0200` 触发工具的错误启发式分类；应以串口 `UsbHook` 和 `rci/rid/rfi` 是否成功判断。
- USB 对物理主机端口非常敏感：此前某个端口始终失败，换到未使用过的另一个直连端口后稳定。
  使用原装电源，USB-TTL 只接 GND/TX/RX，不能用 TTL 的 5 V 给整机供电。

### Linux 与 30 秒冻结

- Linux 6.18.42 和 5.10.262 都能启动；6.18 已进入 BusyBox shell。
- 原厂 Trust OS 路线下，双核和四核都会在约 30 秒全局停止；单核曾稳定超过 135 秒。
- 已排除 Linux 6.18 特有回归、DesignWare watchdog 到期、次核普通 idle/WFI、NO_HZ、
  timer migration、默认 RCU stall 窗口、PSCI 0.1/0.2 DT binding 和完整上游外设 DT 冲突。
- 无 Trust 的现代 U-Boot 路线若保留次核节点，会在第一个 `PSCI_CPU_ON` SMC panic；删除
  `cpu@f01`、`cpu@f02`、`cpu@f03` 后，单核 Linux 能进入 shell。这证明当前缺的是有效的
  secure monitor/PSCI，而不是 kernel、ramdisk 或控制台交接。
- Armbian 维护者 jock 记录，专有 RK322x Trust OS 的 watchdog 会因板子不同造成约
  30 秒、60 秒或 30 分钟冻结；开源 OP-TEE 不受该问题影响，但失去 DDR 动态缩放和
  “virtual power off”等原厂特性。直接来源：
  [2025-10-26 论坛回复](https://forum.armbian.com/topic/34923-csc-armbian-for-rk322x-tv-box-boards/?comment=227602&do=findComment)。
  这只是与 R1 证据高度吻合的跨设备推断，尚未在 R1 上验证为修复。

### 现代 U-Boot RAM 链

- 独立 `TARGET_PHICOMM_R1` 已实机进入 U-Boot 2026.10-rc1 提示符。
- 已验证 512 MiB DRAM、driver model、UART2 `1500000 8N1` 和 eMMC 只读枚举。
- 现代 U-Boot 的原始 eMMC LBA 视图相对 Rockchip Loader 逻辑分区地址多
  `FwPartOffset=0x2000` sectors。例如 recovery 的 Rockchip LBA `0x1e000` 在现代
  U-Boot 中应从 `0x20000` 读取。
- 已从 RAM-only U-Boot 只读加载 recovery 中的 zImage、ramdisk 和 DTB，并以单核进入 shell。

## OP-TEE 来源与固定身份

参考链不是通用 RK322x box 的板级 DTS，而只是其开源 TEE/PSCI 实现：

- Armbian 板入口：
  <https://github.com/armbian/build/blob/main/config/boards/rk322x-box.tvb>
- 维护者分支：
  <https://github.com/paolosabatino/armbian-build/tree/rk322x-opensource-tee>
- 固定提交：`d80ff015a83b0cf9a2500a2312a31d42931a6da4`
  （`provide opensource TEE for rk322x`）
- 文件：`packages/blobs/rockchip/rk322x_tee_os.bin`
- 文件大小：423,248 bytes
- SHA-256：`ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`
- 字符串证据包含 `core/arch/arm/plat-rockchip/psci_rk322x.c`、`psci_cpu_on`、
  `psci_cpu_off`，版本为 OP-TEE 3.7.0 派生版本。

不要把该开源 OP-TEE 与 `build/artifacts/r1-vendor-tee.bin` 或原厂 trust 分区混为一谈。

## 最后一次实机结果

最后上板的是：

```text
build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-loader.bin
size:    856341 bytes
mtime:   2026-08-06 14:58:53 +0800
sha256:  169ead28ed3f8a7e658557a834ea102d62d6857d390808d8c8c417a398bcba12
```

对应串口时间戳和文件 mtime 一致，输出为：

```text
OUT
SRM012345ABabsmcdefCDEFGHIJ
U-Boot SPL 2026.10-rc1-gbaa64b2f8928-dirty (Aug 06 2026 - 14:58:43 +0800)
K6Trying to boot from RAM
nopPQrstuvwxyRzFITF os=17 ret=0 addr=? magic=68500000
spl_perform_arch_fixups: could not map boot_device to ofpath: -19
spl_perform_arch_fixups: could not map BootROM boot device to ofpath
```

重要解释：

- 之前的 `FDT_ERR_BADMAGIC`、`fdt_shrink_to_minimum()` 停止和外置 FIT `data-offset`
  错误已经依次绕过或修正。
- 最后一版启用了 `CONFIG_SPL_LOAD_FIT_FULL=y`，使用内嵌 FIT 数据；`FITF ... ret=0`
  表明本次 FDT append 已成功返回。
- 两条 `could not map ... to ofpath` 是 `spl_perform_arch_fixups()` 的警告，现有证据不能把它们
  当成致命错误；输出停止点已接近 `spl_board_prepare_for_optee()`、cache cleanup 和
  `spl_optee_entry()`。
- `addr=? magic=68500000` 的格式输出有可疑之处，不应把 `68500000` 未经复核地解释为
  FDT magic。下一版优先用固定字符路标，不依赖 SPL 的 `%p` 格式化。

## 重启后的工作区状态

```text
U-Boot source: /home/pansy/phicomm-r1-linux/build/u-boot
commit:        b0531571496 (本地 bring-up 提交，基于 baa64b2f892890f00a377eac4a3e685472bb56b5)
branch:        游离 HEAD
status:        clean
.config:       存在（跳转路标候选，同 r1-phicomm-r1-uboot-optee-jump-trace.config）
```

2026-08-07 已按本文"建议的下一步"完成重建（详见 journal"重启后重建最小 R1 U-Boot
源码状态"一节）：

- 8 个基础补丁按依赖顺序全部 `git apply` 成功并提交为 `b0531571496`；
- `patches/u-boot-spl-optee-return-address.patch` 已用真实 blob 哈希重写为合法
  format-patch（原文件尾部签名非法）；
- 新增 `patches/u-boot-phicomm-r1-optee-jump-breadcrumbs.patch`（`L/M/N/O/P/Q/R/T`）；
- OP-TEE blob 按固定 commit 重新下载并验证，位于 `build/tee/rk322x_tee_os.bin`
  （423,248 B，SHA-256 `ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`），
  不再只在 `/tmp`；构建时复制为 `build/u-boot/tee.bin`；
- 修复 binman 数据缺失：构建必须传 `TEE=tee.bin`（Makefile 的 `-a tee-os-path=${TEE}`
  为空时 op-tee 被 binman 判为 missing，FIT 退化为 `firmware=u-boot`、`data-size=0`）；
- 配置 = `r1-phicomm-r1-uboot-optee-os.config` + `CONFIG_SPL_LOAD_FIT_FULL=y` +
  `CONFIG_SPL_FIT_IMAGE_TINY=y`（IMAGE_TINY 是否仍需要未验证）；
- 干净树构建 pylibfdt 需要 `swig`：改用 `pip3 install --user pylibfdt` +
  `DTC=/usr/bin/dtc` 跳过 in-tree 构建，不动系统工具链。

```text
原始（重启前）U-Boot 工作树位于 /tmp，含未固化的 FIT_FULL 最终状态，已丢失
```

重启前的 `/tmp` 工作树和其中尚未固化的改动（最终 `n...z/R` 路标、`FITF` 打印
补丁）仍然丢失，不必再追；当前源码状态是可重放、可构建的最小集合。

### 当前产物（2026-08-07 重建后）

```text
build/artifacts/r1-phicomm-r1-uboot-optee-fit-repro-loader.bin     复现版（无新逻辑）
  size:    880917   sha256: 3b45373aa824c7cd5a42c1c5ae2610b690980639e8e8833605582a19a100c707
  pack CRC: 0x68da28d4   472: 862528 B, sha256 015eb05c67309485d7a9c2dfb35ed146f4e118735be8c351f4b0d10247b17393
build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-loader.bin   已废弃：实机复现历史 SRM012345ABab 停止（缺 DTB 探针补丁）
build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-dtbprobe-loader.bin   下次上板
  size:    880917   sha256: ec7f650d300de1b093406734003ef77a8a2af8d720d831e23d18fb1758693f56
  pack CRC: 0x03d4cad5   472: 862720 B, sha256 433d3dfb4b40f4ada179e8d7364603308443d9257913462adf0232729883bb93
build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-169ead28.bin  旧最后候选的冻结副本
build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace.config       跳转路标候选配置
build/tee/rk322x_tee_os.bin                                       已验证 OP-TEE blob
```

两者解包后 471/472/FlashData 有效字节与输入逐字节一致、padding 全零；`u-boot.itb`
中 OP-TEE 数据与已验证 blob 逐字节一致（load/entry `0x68400000`，firmware=op-tee、
loadables=u-boot）。`rkdeveloptool/config.ini` 已写好当前 pack 配置（注意
`parseLoader` 索引是 0 基：`LOADER0=FlashData` 才能通过）。

### 跳转路标含义（下一次串口判读）

```text
L   spl_perform_arch_fixups() 返回后
M/N spl_board_prepare_for_optee() 前后
O   spl_board_prepare_for_boot() 返回后
P   jumper(&spl_image) 调用前
Q/R jump_to_image_optee() 中 cleanup_before_linux() 前后
T   spl_optee_entry() 中 mov pc, r3 前（进入 OP-TEE 前的最后输出）
```

若最后字符是 `T`：控制权已进入 OP-TEE，停止排查 FIT parser，改查 OP-TEE 入口
约定、`r1` 返回地址、FDT 参数和该 blob 是否期望额外平台参数。

## 关键文件

| 路径 | 含义 |
|---|---|
| `docs/reverse-engineering-journal.md` | 完整时间线、命令、失败与证据 |
| `docs/index.md` | 当前项目入口和阶段摘要 |
| `docs/acknowledge/uboot.md` | R1 U-Boot、内存和 OP-TEE 链说明 |
| `TODO.md` | 阶段清单 |
| `build/u-boot/` | 重启后重新拉取的干净 U-Boot 源码 |
| `patches/u-boot-*.patch` | 之前逐阶段固化的 U-Boot 补丁；`u-boot-spl-optee-return-address.patch` 已重写为合法格式，另新增 `u-boot-phicomm-r1-optee-jump-breadcrumbs.patch`；`u-boot-spl-fit-fdt-debug.patch` 仍是损坏的历史手工会并件，不再使用 |
| `rkdeveloptool/rkbin/bin/rk32/rk322x_ddr_300MHz_v1.06.bin` | 已验证的原厂 DDR 471，7196 bytes，SHA-256 `cab11c3a...` |
| `build/artifacts/r1-phicomm-r1-uboot-spl-dtb-generic-loader.bin` | 不含 OP-TEE、已实机进入现代 U-Boot 提示符的可靠对照 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-169ead28.bin` | 旧最后实机候选的冻结副本（原 `r1-phicomm-r1-uboot-optee-fit-trace-loader.bin` 已不再更新） |
| `build/artifacts/r1-phicomm-r1-uboot-optee-fit-repro-loader.bin` | 重建复现版（无新逻辑），SHA-256 `3b45373a...`，pack CRC `0x68da28d4` |
| `build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-loader.bin` | 新跳转路标候选（`L/M/N/O/P/Q/R/T`），SHA-256 `b41a2955fa41035cb164733d68d0cdaeb0480b5046adb94b5a43ba8cdb2297f7`，pack CRC `0x1fd332e2`；下次上板文件 |
| `build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace.config` | 跳转路标候选的完整 U-Boot 配置 |
| `build/tee/rk322x_tee_os.bin` | 重新下载并验证的开源 OP-TEE blob，423,248 B，SHA-256 `ff56bb3b...`；构建时复制为 `build/u-boot/tee.bin` |
| `backup/r1-emmc-user.img` | 完整 eMMC User Area 备份，禁止公开提交 |

DDR 471 的完整 SHA-256：

```text
cab11c3a081d2a67a2f07a3387a8bf25889c0356a5bed6b5b5dd373026186cd2
```

可靠无 OP-TEE 对照 loader：

```text
build/artifacts/r1-phicomm-r1-uboot-spl-dtb-generic-loader.bin
sha256 3b8959a491c1fead6409e4eaf4cb1598dac5ab9b9e3e171f824ccb7f3746c70d
```

## 建议的下一步

### 1. ~~先冻结现场，不覆盖最后候选~~（已完成）

旧最后候选已冻结为 `build/artifacts/r1-phicomm-r1-uboot-optee-fit-trace-169ead28.bin`。

### 2. ~~在 `build/u-boot` 重建最小 R1 源码状态~~（已完成）

8 个基础补丁按序重放成功、`spl-optee-return-address` 补丁已修复格式、新增
`u-boot-phicomm-r1-optee-jump-breadcrumbs.patch`，全部提交为本地 commit
`b0531571496`，补丁链在干净 worktree 上完整重放验证一致。诊断路标只保留了
`L/M/N/O/P/Q/R/T` 新集合；不要再套用 `u-boot-spl-fit-fdt-debug.patch` 等历史重叠
版本（该文件仍是手工合并的损坏补丁，仅作历史证据）。

### 3. ~~重新取得并验证 OP-TEE blob~~（已完成）

位于 `build/tee/rk322x_tee_os.bin`，大小 423,248、SHA-256
`ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c`，已验证匹配。
不要提交到公开仓库。

### 4. ~~先重现最后候选，再增加跳转路标~~（已完成，含离线验证）

复现版 `r1-phicomm-r1-uboot-optee-fit-repro-loader.bin` 与跳转路标版均已生成并解包逐字节验证。
**注意**：重建必须包含 `patches/u-boot-phicomm-r1-spl-dtb-memory-probe.patch` 的探针代码
（`s/m/n+hex` 路标，位于 `common/spl/spl.c` OF_REAL 块）。交接时 10 补丁清单遗漏了它；
没有它的构建在实机复现 `SRM012345ABab` 停止（历史 spl-common-trace 阶段同款），带它的构建
全部通过。该补丁与 common-init 路标补丁重叠，需手工合并（只加 `R1_SPL_COMMON_HEX32` 宏 +
探针块，当前树已合并并提交为 `78472b20c9e`）。

### 5. 上板命令只允许 RAM 下载

设备进入真 MaskROM 且串口已打开后，只运行：

```sh
sudo ./rkdeveloptool/rkdeveloptool db \
  build/artifacts/r1-phicomm-r1-uboot-optee-jump-trace-dtbprobe-loader.bin
```

串口判读：`ab` 后应出现 `s`；`sm` = 运行时 `__bss_end` DTB magic 合法，继续
`cdefCDEFGHIJ` 与 `LMNOPQRT`；`sn<8位hex>` = 运行时 DTB 未正确到达（记录该值）；
若仍停在 `ab`，探针假说不成立，需细分 `bootstage_init()`/`fdtdec_setup()`。

不要把 `scripts/catch-rockusb.sh` 当下载脚本；它只等待设备并运行 `rci/rid/rfi` 只读探针。
不要执行 `UL`、`WL`、`EF`、`GPT`、`PRM`，也不要写 parameter/idb/U-Boot/trust/recovery。

## 内存与地址速查

```text
0x60000000  SPL 起始地址
0x61000000  U-Boot proper / CONFIG_TEXT_BASE
0x68400000  原厂 Trust OS 保留位置；当前开源 OP-TEE load/entry
0x68500000  Trust 保留区之后；最后日志中的 FDT 地址/格式仍需复核
0x60000000-0x7fffffff  512 MiB DRAM
```

不要让 kernel、ramdisk、FDT、U-Boot proper 或 SPL 工作区覆盖 `0x68400000` 的 OP-TEE。

## 安全边界

- 当前阶段只授权只读检查、主机端构建和 RAM-only `db` 实验。
- recovery/BCB 回滚已经验证，但“引导区全损坏后从真 MaskROM 完整恢复”仍未实测。
- eMMC hardware boot0/boot1 尚未确认并备份。
- 任何设备写入都必须重新核对 USB 设备、目标介质、offset、length、源镜像、备份和恢复流程，
  并取得针对具体命令和目标的明确授权。
- 不公开提交 `backup/`、整盘/分区镜像、设备唯一数据、原厂 loader 或 OP-TEE binary。

## 文档纪律

每完成一个有实机证据或确定产物的阶段，同一轮更新：

1. `docs/reverse-engineering-journal.md`：原始命令、输出、失败、结论和教训；
2. `docs/index.md`：当前状态和下一步；
3. `TODO.md`：对应复选框；
4. 对应专题文档。

外部结论必须链接具体原帖、固定 commit/blob、原始补丁或数据手册；明确区分 R1 已验证事实、
基于 R1 的推断和来自其他 RK322x 板子的旁证。
