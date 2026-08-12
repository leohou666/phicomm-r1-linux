# Patches

用于保存：

- U-Boot board support；
- Linux DTS；
- AK7755 ASoC driver；
- Rockchip audio machine driver；
- Buildroot package/config patches。

建议使用 `git format-patch` 维护上游可重放的补丁，不直接存放修改后的完整源码树。

项目自有的新文件可放在 `kernel/overlays/linux-<version>/`，例如尚未上游的 codec
driver 和 DT binding；`scripts/prepare-kernel-source.sh` 会先复制 overlay，再应用本目录
对应版本的 Kconfig/Kbuild 小补丁。overlay 只放具有明确可提交许可的源码，原厂 firmware、
备份与设备唯一数据不得放入其中。
