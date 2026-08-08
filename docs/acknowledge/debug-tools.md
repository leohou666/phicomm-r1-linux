# 主机侧调试工具实战手册

本文记录 R1 项目在"重建源码状态 → 构建 → 打包 → 实机失败 → 离线定位"循环中实际
用到的主机侧工具：**每个工具为什么用、什么场景用、命令怎么写、结果怎么判读**。
全部命令以 2026-08-07/08 的重建与 A/B 线调试为实例（详细时间线见
[逆向学习记录](../reverse-engineering-journal.md)）。

适用对象：R1 的 U-Boot/OP-TEE RAM 链调试。核心思想是**离线验证先行**——所有能在
主机上验证的东西（构建、打包、解包、字节比对、反汇编）先做完，实机只验证"我猜的
停止点"对不对。

## 0. 调试纪律（比工具更重要）

1. **身份固定**：每个产物的 SHA-256、大小、pack CRC 记入 journal；已打包文件是权威
   身份（U-Boot 构建不可复现，重建的二进制与打包版本只有时间戳/布局差异，见第 6 节）。
2. **一次只改一个变量**：怀疑什么就只改什么，A/B 对照。
3. **解包验证**：任何经 `rkdeveloptool pack` 的 loader，必须 `unpack` 后逐字节比对
   471/472/FlashData 与输入文件一致、padding 全零，才允许上板。
4. **检查完整日志**：输出压缩工具（如 rtk）可能吞掉 make 错误和退出码；关键构建
   用 `make ... > /tmp/make.log 2>&1; echo "exit: $?"` 后单独检查日志。本项目
   "SPL image too big" 曾被吞掉、误判为成功（见第 9 节）。
5. **运行时与文件是两回事**：文件里正确的字节，运行时可能被覆盖或根本没装载
   （内存地图探针就是为区分这两者而做）。

## 1. 专有二进制识别与版本鉴定

**场景**：拿到一个没有源码的二进制（vendor TEE、DDR 471、rkbin blob），想知道它
是什么、什么版本、从哪来。

**为什么**：专有固件唯一可靠的"身份"是内容本身——版本字符串、构建时间戳、校验和。

**怎么用**：

```sh
# 版本/构建字符串（OP-TEE 派生的 Trust OS 带 "X.Y.Z-N-g<hash>" + 构建时间）
strings -a build/artifacts/r1-vendor-tee.bin | grep -E "^[0-9]+\.[0-9]+\.[0-9]+|UTC"
# 结果：1.0.1-54-g0d46013 #4 Thu Sep 29 01:09:49 UTC 2016 arm

# 校验和（固定身份，与交接记录/官方值比对）
sha256sum build/tee/rk322x_tee_os.bin    # ff56bb3b...（与手记一致才算对）
wc -c build/tee/rk322x_tee_os.bin        # 423248
```

**判读**：版本串格式 `1.0.1-54-g0d46013` = git describe（tag-commits-hash），
`#4 ... UTC 2016 arm` = 编译序号与时间。同一 OP-TEE 分支不同发布版的对比
（1.0.1-54 vs 1.0.1-86）能判断固件新旧。注意：Rockchip 的发布编号（v1.90/v2.00）
是打包编号，**不等于**内部 OP-TEE 版本（实测 v2.00 内部仍是 `1.0.1-86`）。

**场景延伸——按固定来源重新下载**：交接记录只信"固定 commit + SHA"：

```sh
curl -fsSL -o build/tee/rk322x_tee_os.bin \
  "https://raw.githubusercontent.com/paolosabatino/armbian-build/d80ff015a83b0cf9a2500a2312a31d42931a6da4/packages/blobs/rockchip/rk322x_tee_os.bin"
sha256sum build/tee/rk322x_tee_os.bin   # 与记录一致才继续，不一致就停
```

## 2. 反汇编与地址字面量（最常用的定位手段）

**场景**：探针打印了奇怪的值、固件卡在某处、需要确认代码"到底读哪个地址/写哪个
寄存器"、验证汇编层行为（如 OP-TEE 交接 `mov lr/r1, #0x61000000; mov pc, r3`）。

**为什么**：原始二进制没有符号；反汇编是唯一能确证"这段代码做什么"的方式。地址
字面量（literal pool 里的 `.word`）能直接读出代码引用的固定地址——本项目的
`__bss_end`、栈地址、471 训练区地址全是这样挖出来的。

**怎么用**：

```sh
# 1) 对带符号的构建产物（ELF）：-d 反汇编，看函数
arm-none-eabi-objdump -d spl/u-boot-spl | sed -n '/^60000a68 <_main>:/,/^60000b20:/p'
arm-none-eabi-objdump -d spl/common/spl/spl_optee.o | tail -12
#     → 确认 spl_optee_entry: mov lr/r1=0x61000000, 打印 'T', mov pc, r3

# 2) 对裸二进制（pack 解包后的 472 等）：按地址切窗 + 强制 Thumb + 修正基址
dd if=u-boot-rockchip-usb of=/tmp/spl-c.bin bs=1 skip=$((0x1000)) count=$((0xe0))
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
  --adjust-vma=0x60001000 /tmp/spl-c.bin
#     → 探针函数：ldr r3, [pc,#216] @ (0x600010a4) → 该地址的 .word 就是被读地址
```

**判读**：Thumb 的 `ldr rN, [pc, #imm]` 从字面量池取值；池中连续 `.word` 通常排列为
`UART基址 / 目标地址 / 魔数`（本项目探针池：
`00 00 03 11 | 00 e5 00 60 | d0 0d fe ed` = UART 0x11030000、读取地址 0x6000e500、
DTB 魔数）。注意 `--adjust-vma` 必须等于切片在内存中的基址，否则所有分支目标全错。

**注意**：`objdump -d` 从函数边界开始才可读；对未知偏移的裸二进制，先从 16 字节
对齐位置尝试，读到乱序就换起始点。

## 3. 链接布局与符号（nm / readelf）

**场景**：SPL 的 `__bss_end` 在哪、DTB 按设计应落在哪、某个数组有没有进最终镜像。

**为什么**：SPL 用 `__bss_end` 定位追加的 DTB（`CONFIG_OF_SEPARATE` 布局），
binman 也按它放置 DTB；这些是链接期符号，只能从 ELF 读。

**怎么用**：

```sh
arm-none-eabi-nm spl/u-boot-spl | grep -E "__bss_end|__bss_start|_image_binary_end"
#     → 60010500 __bss_end（应与文件中 DTB 起始偏移一致）
arm-none-eabi-objdump -h spl/u-boot-spl   # 各段 VMA/大小：.text/.rodata/.data/.bss
arm-none-eabi-readelf -s spl/u-boot-spl | grep r1_ddr471   # 查符号是否被链接丢弃
```

**判读**：`__bss_end` 与文件里 DTB 魔数偏移**相等** = 布局正确（binman 自动跟随）。
`readelf` 显示符号值为 0 = 段被链接脚本丢弃（`-fdata-sections` 下带长后缀的段名会被
`SORT_BY_ALIGNMENT(.rodata*)` 当孤儿段丢掉——修复：显式
`__attribute__((section(".rodata"), used))`）。

## 4. FIT 容器验证（python + libfdt）

**场景**：binman 生成的 `u-boot.itb` 是否真的把 OP-TEE 装进 0x68400000、数据是否与
源 blob 逐字节一致、configuration 是否 `firmware=op-tee, loadables=u-boot`。

**为什么**：FIT 是 DTB 格式容器；`fdtdump` 只能看结构，验证数据要用 libfdt 按
`data-offset/data-size` 抽取后逐字节比对。曾抓到 binman 静默产出
`data-size=0`、`firmware=u-boot` 的错误 FIT（根因：`TEE` 变量为空）。

**怎么用**（需 `pip3 install --user pylibfdt`）：

```sh
python3 -c "
import libfdt
fit = open('u-boot.itb','rb').read()
fdt = libfdt.FdtRo(fit)
n = fdt.path_offset('/images/op-tee')
ds = fdt.getprop(n,'data-size').as_cell('L')
do = fdt.getprop(n,'data-offset').as_cell('L')
data = fit[0x600+do : 0x600+do+ds]     # 数据偏移相对 FIT 结构末尾(0x600)
print(data == open('../tee/rk322x_tee_os.bin','rb').read())
c = fdt.path_offset('/configurations/config-1')
print(fdt.getprop(c,'firmware').as_str(), fdt.getprop(c,'loadables').as_str())
"
```

**判读**：`data-size` 应等于源 blob 大小；`load` 用 `as_cell` 读（注意 libfdt 的
`as_uint32` 对非 cell 属性会炸，先判 `len(p)>=4`）；configuration 必须是
`firmware=op-tee` + `loadables=u-boot`，否则 binman 打包错了。

## 5. Loader 打包与解包验证（rkdeveloptool pack/unpack）

**场景**：把 471（原厂 DDR）+ 472（SPL/U-Boot/OP-TEE）+ FlashData 合成 BootROM 可
`db` 下载的 loader；以及上板前证明"打包没改坏任何东西"。

**为什么**：`db` 只接受 Rockchip loader 容器；条目是 RC4 加密存储的，`unpack` 解密
后可逐字节比对。本项目还用它发现打包工具自身两个坑（见下）。

**怎么用**：

```sh
# config.ini 写在 rkdeveloptool/ 目录（工具从 CWD 读），内容：
#   [CHIP_NAME] NAME=RK322A / [VERSION] MAJOR=2 MINOR=30
#   [CODE471_OPTION] NUM=1 Path1=rkbin/bin/rk32/rk322x_ddr_300MHz_v1.06.bin Sleep=0
#   [CODE472_OPTION] NUM=1 Path1=../build/u-boot/u-boot-rockchip-usb472.bin Sleep=0
#   [LOADER_OPTION] LOADERCOUNT=1 LOADER0=FlashData
#   FlashData=rkbin/bin/rk32/rk322x_ddr_300MHz_v1.06.bin
#   [OUTPUT] PATH=../build/artifacts/<name>.bin
cd rkdeveloptool && ./rkdeveloptool pack
./rkdeveloptool unpack ../build/artifacts/<name>.bin   # 输出到 CWD
python3 -c "..."   # 对比 471/472/FlashData 有效字节与输入一致、padding 全零
```

**坑（已踩）**：
- `parseLoader` 的索引是 **0 基**：必须写 `LOADER0=FlashData`（写 `LOADER1` 会
  "parseLoader failed"）；
- `LOADERCOUNT=0` 会被拒（`if (!gOpts.loaderNum) return false`）；
- pack 的 472 条目按 2 KiB 对齐补齐，解包后先按输入文件长度比前缀，再检查尾部
  padding 全零；
- 换目标文件前**先改 config.ini 的 OUTPUT**，否则会覆盖上一个名字的文件
  （本项目 B 线 loader 曾被误覆盖一次，SHA 作废重打）。

## 6. 字节级对比（cmp / python 模式搜索）

**场景**：两个二进制"几乎一样"到底差多少？文件里某字节序列（DTB 魔数、探针值、
tee 签名）在不在？运行时探针值是不是文件内容？

**为什么**：`cmp -l` 给出所有差异位置；配合 python 统计能区分"仅时间戳差异"
（8 字节，版本字符串内）与"结构性差异"（成千上万处、地址整体偏移）。本项目靠它
确认：重建的 472 与已打包 472 只有 8 字节时间戳差异；探针值 `782e54f1` 不在任何
文件里（判定为运行时数据）。

**怎么用**：

```sh
cmp -l a.bin b.bin | wc -l                 # 差异字节总数
cmp -l a.bin b.bin | head                  # 差异位置（十进制）+ 八进制字节值
python3 -c "
import re
d = open('u-boot-rockchip-usb472.bin','rb').read()
print([hex(m.start()) for m in re.finditer(b'\xd0\x0d\xfe\xed', d)])  # 所有 DTB 魔数
print(d.find(bytes.fromhex('f1542e78')))   # 某字节序列在不在、在哪
"
```

**判读**：差异全在版本字符串区间 = 只是重新构建的时间戳；差异遍布全文且地址字面量
统一偏移 = 布局/符号移位（不是同一构建）；`find` 返回 -1 = 该值运行时才产生。

## 7. 内存地图探针（运行时 RAM vs 文件）

**场景**：探针打印的 `__bss_end` 值既不在文件里也不是预期的 DTB 魔数——需要区分
"文件里没有（运行时写入/没装载）"还是"读错地址"。

**为什么**：文件内容可以在主机上完全验证；唯一验证不了的是运行时 RAM 到底是什么。
固定地址探针把"运行时内容"变成 8 位 hex 输出，与文件逐一比对即可画出行之有效的
内存地图。

**怎么用**（源码级，见 `common/spl/spl.c` 中 `R1_SPL_COMMON_MARK/HEX32`）：

```c
R1_SPL_COMMON_MARK('1');
R1_SPL_COMMON_HEX32(*(volatile u32 *)0x60008a90);   // 历史有效位置
R1_SPL_COMMON_HEX32(*(volatile u32 *)0x6000e500);   // 疑似被覆盖位置
... /* 依次打印每个待测地址的 32 位值 */
```

**判读**：每个值与文件同偏移字节比对（注意小端：打印的 hex 是 u32 值，文件字节要
按小端组回 u32）。全对 = 装载完整；某地址起全错 = 该地址起运行时不可用（本项目：
MaskROM 对 472 的装载有效区只有约 36 KB，之上的全是 471 DDR 二阶段初始化留下的
数据——16 个采样点全部与文件不符且不在任何二进制里，据此定位）。

## 8. 源码状态重放验证（git apply / worktree）

**场景**：多补丁有顺序依赖，交接后要确认"从干净树按序打补丁 = 当时的源码状态"。

**为什么**：补丁链可重放性是"重建"可信的前提；不能只信"打上了"。

**怎么用**：

```sh
git worktree add -f /tmp/opencode/wt <base-commit>
cd /tmp/opencode/wt
for p in <按依赖顺序的补丁列表>; do git apply "$p.patch" || echo "FAIL $p"; done
# 然后与目标树逐文件比对（跳过子模块目录与构建产物）：
diff -rq --exclude=.git /tmp/opencode/wt /path/to/build/u-boot | grep -v "只在"
```

**判读**：`git apply --check` 先试；失败通常意味着补丁间有重叠（历史诊断补丁互相
叠加是常态，需要手工合并——本项目的 memory-probe 补丁与 common-init 路标补丁就
重叠了宏定义）。`diff -rq` 只剩构建产物差异 = 重放成功。另外注意 `git format-patch`
尾部 `-- ` 之后的签名行必须合法（`Armbian/R1 bring-up` 这种非法签名会让
`git apply` 报"补丁损坏"）。

## 9. 构建系统排错（make / binman）

**场景**：产物没有按预期更新（472 还是旧内容）、binman 静默不重跑、构建"成功"但
FIT 是空的。

**为什么**：U-Boot 构建有几个隐蔽行为，只信产物不信任输出：
- `.binman_stamp` 用 `if_changed`，**只有命令行变化才重跑**——改输入文件不会触发
  binman 重生成，需 `rm -f .binman_stamp` 或加 `BINMAN_VERBOSE=1` 改变命令行；
- `TEE` make 变量必须显式传：`make ... TEE=tee.bin`，为空时 binman 的
  `-a tee-os-path=` 让 OP-TEE 数据静默消失（FIT `data-size=0`）；
- 干净树构建 pylibfdt 需要 `swig`：用系统 `DTC=/usr/bin/dtc` 跳过 in-tree 构建 +
  `pip3 install --user pylibfdt`，不动系统工具链；
- 全量日志检查：`make ... > /tmp/make.log 2>&1; echo exit:$?`，`grep -E "error|too big"`。

**怎么用**：

```sh
make CROSS_COMPILE=arm-none-eabi- DTC=/usr/bin/dtc TEE=tee.bin > /tmp/make.log 2>&1
echo "exit: $?" && tail -5 /tmp/make.log
rm -f .binman_stamp   # 强制 binman 重新生成
```

**判读**：`SPL image too big` = 某阶段（本项目是 TPL）超尺寸上限——同一份源码会编进
SPL 和 TPL 两份，加东西要加阶段守卫
（`#if defined(CONFIG_SPL_BUILD) && !defined(CONFIG_TPL_BUILD)`）。exit 0 但 472
时间戳没变 = binman 没重跑。

## 10. 工具速查表

| 工具 | 场景 | 关键用法 |
|---|---|---|
| `strings -a` | 专有二进制版本/来源 | `grep -E "版本串\|UTC"` |
| `sha256sum`/`wc -c` | 身份固定、下载校验 | 与 journal 记录比对 |
| `arm-none-eabi-objdump -d` | ELF 反汇编 | 函数级验证 |
| `arm-none-eabi-objdump -D -b binary -M force-thumb --adjust-vma=` | 裸二进制反汇编 | 先 `dd` 切窗 |
| `arm-none-eabi-nm` | 链接符号（`__bss_end` 等） | 布局核对 |
| `arm-none-eabi-objdump -h`/`readelf -s` | 段布局/符号是否被丢 | 查 `.rodata/.bss` |
| python `libfdt` | FIT 结构+数据验证 | 抽数据逐字节比对 |
| `rkdeveloptool pack/unpack` | loader 打包/解密验证 | 解包后逐字节比对 |
| `cmp -l` | 二进制差异定位 | 区分时间戳 vs 结构差异 |
| python `re.finditer`/`find` | 字节模式定位（魔数/签名） | 探针值归属判定 |
| 源码探针 `R1_SPL_MARK/HEX32` | 运行时内存内容 | 与文件逐地址比对 |
| `git apply --check`/`git worktree` | 补丁链重放 | `diff -rq` 全树比对 |
| `make ... > log` | 构建排错 | 信产物不信输出；检查完整日志 |
| `curl -fsSL`（固定 commit） | 外部 blob 来源固定 | 取回后必须验 SHA |

## 11. 工具链与环境备注

- 交叉工具链：Fedora 自带 `arm-none-eabi-gcc 15.2.0`（`CROSS_COMPILE=arm-none-eabi-`）；
- 主机 Python：`pyelftools==0.31`（U-Boot binman 需要）、`pylibfdt`（FIT 验证）；
- 输出压缩：本项目经 `rtk`（Rust Token Killer）跑命令以省 token——**它会吞 make
  错误与退出码**，涉及成败判断的命令务必写全量日志文件再检查（见第 0 节第 4 条）。
