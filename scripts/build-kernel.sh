#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kernel_src=${KERNEL_SRC:-"$project_root/build/kernel-src"}
kernel_build=${KERNEL_BUILD:-"$project_root/build/kernel"}
cross_compile=${CROSS_COMPILE:-arm-none-eabi-}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
fragment="$project_root/kernel/config/r1.fragment"
extra_fragment=${KERNEL_EXTRA_FRAGMENT:-}
board_dts=${BOARD_DTS:-"$project_root/kernel/dts/rk3229-phicomm-r1.dts"}
case "$kernel_src" in
	/*) ;;
	*) kernel_src="$project_root/$kernel_src" ;;
esac
case "$kernel_build" in
	/*) ;;
	*) kernel_build="$project_root/$kernel_build" ;;
esac
case "$board_dts" in
	/*) ;;
	*) board_dts="$project_root/$board_dts" ;;
esac
if [ -n "$extra_fragment" ]; then
	case "$extra_fragment" in
		/*) ;;
		*) extra_fragment="$project_root/$extra_fragment" ;;
	esac
fi
artifacts="$project_root/build/artifacts"

if [ ! -f "$kernel_src/Makefile" ]; then
	printf 'Kernel source is missing: %s\n' "$kernel_src" >&2
	printf 'Run scripts/prepare-kernel-source.sh first.\n' >&2
	exit 1
fi

[ -f "$board_dts" ] || {
	printf 'Board DTS is missing: %s\n' "$board_dts" >&2
	exit 1
}

if [ -n "$extra_fragment" ] && [ ! -f "$extra_fragment" ]; then
	printf 'Extra kernel config fragment is missing: %s\n' \
		"$extra_fragment" >&2
	exit 1
fi

printf 'Board DTS: %s\n' "$board_dts"
if [ -n "$extra_fragment" ]; then
	printf 'Extra config fragment: %s\n' "$extra_fragment"
fi

mkdir -p "$kernel_build" "$artifacts"

make -C "$kernel_src" O="$kernel_build" \
	ARCH=arm CROSS_COMPILE="$cross_compile" multi_v7_defconfig

if [ -n "$extra_fragment" ]; then
	"$kernel_src/scripts/kconfig/merge_config.sh" -m -O "$kernel_build" \
		"$kernel_build/.config" "$fragment" "$extra_fragment"
else
	"$kernel_src/scripts/kconfig/merge_config.sh" -m -O "$kernel_build" \
		"$kernel_build/.config" "$fragment"
fi

make -C "$kernel_src" O="$kernel_build" \
	ARCH=arm CROSS_COMPILE="$cross_compile" olddefconfig

make -C "$kernel_src" O="$kernel_build" \
	ARCH=arm CROSS_COMPILE="$cross_compile" -j"$jobs" zImage

"${CPP:-cpp}" -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
	-I "$kernel_src/arch/arm/boot/dts/rockchip" \
	-I "$kernel_src/scripts/dtc/include-prefixes" \
	"$board_dts" > "$kernel_build/rk3229-phicomm-r1.dts.preprocessed"

"$kernel_build/scripts/dtc/dtc" -I dts -O dtb -Wno-unit_address_vs_reg \
	-o "$artifacts/rk3229-phicomm-r1.dtb" \
	"$kernel_build/rk3229-phicomm-r1.dts.preprocessed"

cp "$kernel_build/arch/arm/boot/zImage" "$artifacts/zImage"
cp "$kernel_build/.config" "$artifacts/kernel.config"

file "$artifacts/zImage" "$artifacts/rk3229-phicomm-r1.dtb"
printf 'Artifacts written to %s\n' "$artifacts"
