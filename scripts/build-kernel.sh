#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kernel_src=${KERNEL_SRC:-"$project_root/build/kernel-src"}
kernel_build=${KERNEL_BUILD:-"$project_root/build/kernel"}
cross_compile=${CROSS_COMPILE:-arm-none-eabi-}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
kernel_defconfig=${KERNEL_DEFCONFIG:-multi_v7_defconfig}
fragment="$project_root/kernel/config/r1.fragment"
extra_fragment=${KERNEL_EXTRA_FRAGMENT:-}
extra_fragments=${KERNEL_EXTRA_FRAGMENTS:-}
board_dts=${BOARD_DTS:-"$project_root/kernel/dts/rk3229-phicomm-r1.dts"}
artifact_tag=${KERNEL_ARTIFACT_TAG:-}
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
if [ -n "$extra_fragment" ] && [ -n "$extra_fragments" ]; then
	printf '%s\n' \
		'Set only one of KERNEL_EXTRA_FRAGMENT or KERNEL_EXTRA_FRAGMENTS.' >&2
	exit 1
fi
if [ -n "$extra_fragment" ]; then
	extra_fragments=$extra_fragment
fi
if [ -n "$artifact_tag" ]; then
	case "$artifact_tag" in
		*[!A-Za-z0-9._-]*|'')
			printf 'Unsafe KERNEL_ARTIFACT_TAG: %s\n' "$artifact_tag" >&2
			exit 1
			;;
	esac
fi
resolved_extra_fragments=
for extra_fragment in $extra_fragments; do
	case "$extra_fragment" in
		/*) ;;
		*) extra_fragment="$project_root/$extra_fragment" ;;
	esac
	[ -f "$extra_fragment" ] || {
		printf 'Extra kernel config fragment is missing: %s\n' \
			"$extra_fragment" >&2
		exit 1
	}
	resolved_extra_fragments="$resolved_extra_fragments $extra_fragment"
done
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

printf 'Board DTS: %s\n' "$board_dts"
printf 'Kernel baseline config: %s\n' "$kernel_defconfig"
for extra_fragment in $resolved_extra_fragments; do
	printf 'Extra config fragment: %s\n' "$extra_fragment"
done
if [ -n "$artifact_tag" ]; then
	printf 'Artifact tag: %s\n' "$artifact_tag"
fi

mkdir -p "$kernel_build" "$artifacts"

make -C "$kernel_src" O="$kernel_build" \
	ARCH=arm CROSS_COMPILE="$cross_compile" "$kernel_defconfig"

if [ -n "$resolved_extra_fragments" ]; then
	# Paths in this repository contain no whitespace.  Splitting the list here
	# lets callers layer a frozen rescue baseline and one peripheral fragment.
	# shellcheck disable=SC2086
	"$kernel_src/scripts/kconfig/merge_config.sh" -m -O "$kernel_build" \
		"$kernel_build/.config" "$fragment" $resolved_extra_fragments
else
	"$kernel_src/scripts/kconfig/merge_config.sh" -m -O "$kernel_build" \
		"$kernel_build/.config" "$fragment"
fi

make -C "$kernel_src" O="$kernel_build" \
	ARCH=arm CROSS_COMPILE="$cross_compile" olddefconfig

make -C "$kernel_src" O="$kernel_build" \
	ARCH=arm CROSS_COMPILE="$cross_compile" -j"$jobs" zImage

"${CPP:-cpp}" -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
	-I "$kernel_src/arch/arm/boot/dts" \
	-I "$kernel_src/arch/arm/boot/dts/rockchip" \
	-I "$project_root/kernel/dts" \
	-I "$kernel_src/scripts/dtc/include-prefixes" \
	"$board_dts" > "$kernel_build/rk3229-phicomm-r1.dts.preprocessed"

"$kernel_build/scripts/dtc/dtc" -I dts -O dtb -Wno-unit_address_vs_reg \
	-o "$artifacts/rk3229-phicomm-r1.dtb" \
	"$kernel_build/rk3229-phicomm-r1.dts.preprocessed"

if [ -n "$artifact_tag" ]; then
	zimage_artifact="$artifacts/zImage-$artifact_tag"
	dtb_artifact="$artifacts/rk3229-phicomm-r1-$artifact_tag.dtb"
	config_artifact="$artifacts/kernel-$artifact_tag.config"
	cpp_artifact="$artifacts/rk3229-phicomm-r1-$artifact_tag.dts.preprocessed"
	cp "$kernel_build/arch/arm/boot/zImage" "$zimage_artifact"
	cp "$artifacts/rk3229-phicomm-r1.dtb" "$dtb_artifact"
	cp "$kernel_build/.config" "$config_artifact"
	cp "$kernel_build/rk3229-phicomm-r1.dts.preprocessed" "$cpp_artifact"
else
	zimage_artifact="$artifacts/zImage"
	dtb_artifact="$artifacts/rk3229-phicomm-r1.dtb"
	config_artifact="$artifacts/kernel.config"
	cp "$kernel_build/arch/arm/boot/zImage" "$zimage_artifact"
	cp "$kernel_build/.config" "$config_artifact"
fi

if [ "${GENERATE_COMPILE_COMMANDS:-1}" = 1 ]; then
	python3 "$kernel_src/scripts/clang-tools/gen_compile_commands.py" \
		-d "$kernel_build" \
		-o "$kernel_build/compile_commands.json" \
		-a "${AR:-${cross_compile}ar}" \
		"$kernel_build"
	printf 'clangd database written to %s/compile_commands.json\n' "$kernel_build"
fi

file "$zimage_artifact" "$dtb_artifact"
printf 'zImage: %s\n' "$zimage_artifact"
printf 'DTB: %s\n' "$dtb_artifact"
printf 'Config: %s\n' "$config_artifact"
