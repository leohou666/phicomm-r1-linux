#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kernel_src=${KERNEL_SRC:-"$project_root/build/kernel-src"}
kernel_build=${KERNEL_BUILD:-"$project_root/build/kernel-6.18-ak7755-factory-dsp-a23"}
tag=mainline-6.18-ak7755-factory-dsp-a23
artifacts="$project_root/build/artifacts"
fit="$artifacts/r1-linux-$tag.itb"
its="$project_root/scripts/r1-linux-$tag.its"
audible="$artifacts/r1-audible-test"
pcm="$artifacts/r1-pcm-clock-test"

for tool in mkimage dumpimage cmp gzip cpio sha256sum; do
	command -v "$tool" >/dev/null 2>&1 || {
		printf 'Missing required tool: %s\n' "$tool" >&2
		exit 1
	}
done

[ -f "$kernel_src/Makefile" ] || {
	printf 'Prepared Linux 6.18 source is missing: %s\n' "$kernel_src" >&2
	printf 'Run scripts/prepare-kernel-source.sh first.\n' >&2
	exit 1
}

# Overlay sources are the canonical editable copies; refresh the prepared tree.
find "$project_root/kernel/overlays/linux-6.18.42" -type f -print |
while IFS= read -r source; do
	relative=${source#"$project_root/kernel/overlays/linux-6.18.42"/}
	mkdir -p "$kernel_src/$(dirname -- "$relative")"
	install -m 0644 "$source" "$kernel_src/$relative"
done

KERNEL_SRC="$kernel_src" \
KERNEL_BUILD="$kernel_build" \
KERNEL_EXTRA_FRAGMENTS='kernel/config/r1-5.10-clean-4core.fragment kernel/config/r1-5.10-wifi-brcmfmac-a2.fragment kernel/config/r1-5.10-wifi-regulatory-a3.fragment kernel/config/r1-5.10-bt-rk805-clkout.fragment kernel/config/r1-5.10-bt-serdev-a1r6.fragment kernel/config/r1-5.10-bt-crypto-a1r9.fragment kernel/config/r1-6.18-ak7755-fw-a3.fragment kernel/config/r1-6.18-ak7755-dai-a4.fragment kernel/config/r1-6.18-ak7755-factory-dsp-a23.fragment' \
BOARD_DTS=kernel/dts/rk3229-phicomm-r1-open-optee-ak7755-audible-a8.dts \
KERNEL_ARTIFACT_TAG="$tag" \
	"$project_root/scripts/build-kernel.sh"

"$project_root/scripts/build-r1-audible-test.sh" "$audible"
"$project_root/scripts/build-r1-pcm-clock-test.sh" "$pcm"

R1_WIFI_FIRMWARE=1 R1_WIFI_REGULATORY=1 \
R1_BLUETOOTH_FIRMWARE=1 \
R1_WIFI_SCAN_TOOL="$artifacts/r1-nl80211-scan" \
R1_BLUETOOTH_MGMT_TOOL="$artifacts/r1-btmgmt" \
R1_AK7755_FIRMWARE=1 R1_AK7755_OFREG_FIRMWARE=1 \
R1_AUDIBLE_TEST_TOOL="$audible" R1_PCM_CLOCK_TEST_TOOL="$pcm" \
INITRAMFS_ARTIFACT_TAG="$tag" \
	"$project_root/scripts/build-initramfs.sh"

(cd "$project_root/scripts" && mkimage -f "$(basename "$its")" "$fit")

workdir=$(mktemp -d "${TMPDIR:-/tmp}/r1-ak7755-a23.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
dumpimage -T flat_dt -p 0 -o "$workdir/kernel" "$fit" >/dev/null
dumpimage -T flat_dt -p 1 -o "$workdir/ramdisk" "$fit" >/dev/null
dumpimage -T flat_dt -p 2 -o "$workdir/fdt" "$fit" >/dev/null
cmp "$artifacts/zImage-$tag" "$workdir/kernel"
cmp "$artifacts/r1-initramfs-$tag.cpio.gz" "$workdir/ramdisk"
cmp "$artifacts/rk3229-phicomm-r1-$tag.dtb" "$workdir/fdt"

gzip -dc "$workdir/ramdisk" | (cd "$workdir" && cpio -id --quiet)
cmp "$audible" "$workdir/bin/r1-audible-test"
cmp "$pcm" "$workdir/bin/r1-pcm-clock-test"
for firmware in ak7755_pram_data2.bin ak7755_cram_data2.bin ak7755_ofreg_data2.bin; do
	cmp "$project_root/backup/extracted/system/vendor/firmware/$firmware" \
		"$workdir/lib/firmware/$firmware"
done

printf '\nA23 factory-DSP FIT verified. eMMC was not modified.\n'
sha256sum \
	"$artifacts/zImage-$tag" \
	"$artifacts/r1-initramfs-$tag.cpio.gz" \
	"$artifacts/rk3229-phicomm-r1-$tag.dtb" \
	"$fit"
