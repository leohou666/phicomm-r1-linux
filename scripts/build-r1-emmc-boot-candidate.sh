#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
UBOOT_SRC=${UBOOT_SRC:-"$ROOT/build/u-boot"}
DDR_BIN=${DDR_BIN:-"$ROOT/rkdeveloptool/rkbin/bin/rk32/rk322x_ddr_300MHz_v1.06.bin"}
TEE_BIN=${TEE_BIN:-"$ROOT/build/tee/rk322x_tee_os.bin"}
CROSS_COMPILE=${CROSS_COMPILE:-arm-none-eabi-}
TAG=${TAG:-a5}
ARTIFACT_DIR=${ARTIFACT_DIR:-"$ROOT/build/artifacts"}
FIT_WRITE_LBA=0x6000
SPL_FIT_READ_LBA=0x6000
EXPECTED_DDR_SHA256=cab11c3a081d2a67a2f07a3387a8bf25889c0356a5bed6b5b5dd373026186cd2
EXPECTED_TEE_SHA256=ff56bb3b22b4763459b9bea407e1cc33bc1fae19b920542b2f48ace735642f3c

IDB_OUT="$ARTIFACT_DIR/r1-emmc-idbloader-open-optee-$TAG.img"
FIT_OUT="$ARTIFACT_DIR/r1-emmc-u-boot-open-optee-$TAG.itb"
SPL_OUT="$ARTIFACT_DIR/r1-emmc-spl-open-optee-$TAG.bin"
CONFIG_OUT="$ARTIFACT_DIR/r1-emmc-u-boot-open-optee-$TAG.config"
RAM_LOADER_OUT="$ARTIFACT_DIR/r1-emmc-mmc-spl-ram-test-$TAG-loader.bin"
MANIFEST_OUT="$ARTIFACT_DIR/r1-emmc-open-optee-$TAG.manifest"

for required in "$UBOOT_SRC/Makefile" "$DDR_BIN" "$TEE_BIN" \
    "$ROOT/patches/u-boot-phicomm-r1-emmc-boot-order.patch" \
    "$ROOT/patches/u-boot-phicomm-r1-emmc-read-probe.patch" \
    "$ROOT/rkdeveloptool/rkdeveloptool"; do
	if [[ ! -e "$required" ]]; then
		echo "missing required input: $required" >&2
		exit 1
	fi
done

check_sha256() {
	local path=$1 expected=$2 actual
	actual=$(sha256sum "$path" | awk '{print $1}')
	if [[ "$actual" != "$expected" ]]; then
		echo "refusing unexpected input $path" >&2
		echo "SHA-256: $actual (expected $expected)" >&2
		exit 1
	fi
}

check_sha256 "$DDR_BIN" "$EXPECTED_DDR_SHA256"
check_sha256 "$TEE_BIN" "$EXPECTED_TEE_SHA256"

WORK=$(mktemp -d /tmp/r1-emmc-build.XXXXXX)
trap 'rm -rf -- "$WORK"' EXIT
SRC="$WORK/u-boot"
PACK="$WORK/pack"

echo "Copying the current U-Boot source, including the verified local GIC cleanup..."
cp -a "$UBOOT_SRC" "$SRC"
make -C "$SRC" mrproper >/dev/null
git -C "$SRC" apply "$ROOT/patches/u-boot-phicomm-r1-emmc-boot-order.patch"
git -C "$SRC" apply "$ROOT/patches/u-boot-phicomm-r1-emmc-read-probe.patch"

make -C "$SRC" phicomm-r1_defconfig
"$SRC/scripts/config" --file "$SRC/.config" --enable ROCKCHIP_EXTERNAL_TPL
"$SRC/scripts/config" --file "$SRC/.config" \
	--set-val SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR "$SPL_FIT_READ_LBA"
make -C "$SRC" olddefconfig

SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$UBOOT_SRC" show -s --format=%ct HEAD)}
export SOURCE_DATE_EPOCH
if ! make -C "$SRC" -j"$(nproc)" \
		CROSS_COMPILE="$CROSS_COMPILE" \
		DTC=/usr/bin/dtc \
		TEE="$TEE_BIN" \
		ROCKCHIP_TPL="$DDR_BIN" >"$WORK/u-boot-build.log" 2>&1; then
	tail -n 80 "$WORK/u-boot-build.log" >&2
	exit 1
fi
tail -n 12 "$WORK/u-boot-build.log"

require_config() {
	local expected=$1
	if ! grep -Fqx "$expected" "$SRC/.config"; then
		echo "required U-Boot config is missing: $expected" >&2
		exit 1
	fi
}

require_config 'CONFIG_ROCKCHIP_EXTERNAL_TPL=y'
require_config 'CONFIG_SPL_MMC=y'
require_config '# CONFIG_SPL_YMODEM_SUPPORT is not set'
require_config '# CONFIG_SPL_MMC_WRITE is not set'
require_config '# CONFIG_MMC_WRITE is not set'
require_config "CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR=$SPL_FIT_READ_LBA"

mkdir -p "$ARTIFACT_DIR"
install -m 0644 "$SRC/idbloader.img" "$IDB_OUT"
install -m 0644 "$SRC/u-boot.itb" "$FIT_OUT"
install -m 0644 "$SRC/spl/u-boot-spl.bin" "$SPL_OUT"
install -m 0644 "$SRC/.config" "$CONFIG_OUT"

mkdir -p "$PACK"
cp "$ROOT/rkdeveloptool/rkdeveloptool" "$PACK/rkdeveloptool"
cat >"$PACK/config.ini" <<EOF
[CHIP_NAME]
NAME=RK322A
[VERSION]
MAJOR=2
MINOR=30
[CODE471_OPTION]
NUM=1
Path1=$DDR_BIN
Sleep=0
[CODE472_OPTION]
NUM=1
Path1=$SPL_OUT
Sleep=0
[LOADER_OPTION]
LOADERCOUNT=1
LOADER0=FlashData
FlashData=$DDR_BIN
[OUTPUT]
PATH=$RAM_LOADER_OUT
EOF
(
	cd "$PACK"
	./rkdeveloptool pack
	mkdir unpack
	cd unpack
	../rkdeveloptool unpack "$RAM_LOADER_OUT"
)

check_padded_copy() {
	local source=$1 unpacked=$2
	python3 - "$source" "$unpacked" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_bytes()
unpacked = Path(sys.argv[2]).read_bytes()
if unpacked[:len(source)] != source:
    raise SystemExit(f"packed prefix differs: {sys.argv[2]}")
if any(unpacked[len(source):]):
    raise SystemExit(f"packed padding is not all zero: {sys.argv[2]}")
PY
}

loader_entry_name() {
	python3 - "$1" <<'PY'
from pathlib import Path
import sys

# rkdeveloptool stores at most MAX_NAME_LEN - 1 (19) UTF-16 code units
# and strips the last filename extension.
print(Path(sys.argv[1]).stem[:19])
PY
}

check_padded_copy "$DDR_BIN" "$PACK/unpack/$(loader_entry_name "$DDR_BIN")"
check_padded_copy "$SPL_OUT" "$PACK/unpack/$(loader_entry_name "$SPL_OUT")"
check_padded_copy "$DDR_BIN" "$PACK/unpack/FlashData"

"$SRC/tools/dumpimage" -T flat_dt -p 0 -o "$WORK/fit-uboot.bin" "$FIT_OUT" >/dev/null
"$SRC/tools/dumpimage" -T flat_dt -p 1 -o "$WORK/fit-optee.bin" "$FIT_OUT" >/dev/null
"$SRC/tools/dumpimage" -T flat_dt -p 2 -o "$WORK/fit-dtb.bin" "$FIT_OUT" >/dev/null
cmp "$WORK/fit-uboot.bin" "$SRC/u-boot-nodtb.bin"
cmp "$WORK/fit-optee.bin" "$TEE_BIN"
cmp "$WORK/fit-dtb.bin" "$SRC/u-boot.dtb"

idb_bytes=$(stat -c %s "$IDB_OUT")
fit_bytes=$(stat -c %s "$FIT_OUT")
idb_sectors=$(((idb_bytes + 511) / 512))
fit_sectors=$(((fit_bytes + 511) / 512))
idb_end=$((0x40 + idb_sectors - 1))
fit_end=$((FIT_WRITE_LBA + fit_sectors - 1))

{
	echo "R1 eMMC open-OP-TEE boot candidate $TAG"
	echo "status=host-built-and-byte-verified; not written to device; not cold-boot-verified"
	echo "u_boot_commit=$(git -C "$UBOOT_SRC" rev-parse HEAD)"
	echo "source_date_epoch=$SOURCE_DATE_EPOCH"
	echo "ddr_source=$DDR_BIN"
	echo "tee_source=$TEE_BIN"
	echo "idb_target_lba=0x40"
	printf 'idb_end_lba=0x%x\n' "$idb_end"
	printf 'fit_write_lba=0x%x\n' "$FIT_WRITE_LBA"
	printf 'spl_fit_read_lba=0x%x\n' "$SPL_FIT_READ_LBA"
	printf 'fit_end_lba=0x%x\n' "$fit_end"
	echo "first_untouched_partition_logical=misc@0x8000"
	echo "first_untouched_partition_raw=misc@0xa000"
	sha256sum "$DDR_BIN" "$TEE_BIN" "$IDB_OUT" "$FIT_OUT" "$SPL_OUT" "$RAM_LOADER_OUT" "$CONFIG_OUT"
} >"$MANIFEST_OUT"

echo
echo "Built and byte-verified; no device write was performed:"
echo "  $IDB_OUT  (LBA 0x40..$(printf '0x%x' "$idb_end"))"
echo "  $FIT_OUT  (Rockchip write LBA $(printf '0x%x' "$FIT_WRITE_LBA")..$(printf '0x%x' "$fit_end"))"
echo "  $RAM_LOADER_OUT  (RAM-only MMC SPL diagnostic)"
echo "  $MANIFEST_OUT"
echo
echo "Do not write these images until the RAM-only MMC read test and restore drill pass."
