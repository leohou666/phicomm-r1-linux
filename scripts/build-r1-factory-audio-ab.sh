#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1786464000}
export SOURCE_DATE_EPOCH
factory_kernel="$project_root/backup/unpacked/boot/kernel"
factory_dtb="$project_root/backup/unpacked/boot/rk-kernel.dtb"
overlay_src="$project_root/kernel/dts/rk3229-phicomm-r1-factory-open-optee-audio-only.dtso"
initramfs="$project_root/build/artifacts/r1-initramfs-mainline-6.18-ak7755-i2s-clock-a19.cpio.gz"
output_dtb="$project_root/build/artifacts/rk3229-phicomm-r1-factory-open-optee-audio-only-a21r3.dtb"
output_fit="$project_root/build/artifacts/r1-linux-factory-3.10-audio-ab-a21r3.itb"
its="$project_root/scripts/r1-linux-factory-3.10-audio-ab-a21r3.its"

check_sha256() {
	path=$1
	expected=$2
	actual=$(sha256sum "$path" | awk '{print $1}')
	if [ "$actual" != "$expected" ]; then
		printf 'Refusing unexpected local evidence: %s\n' "$path" >&2
		printf '  expected %s\n  actual   %s\n' "$expected" "$actual" >&2
		exit 1
	fi
}

for tool in dtc fdtoverlay fdtget mkimage dumpimage sha256sum cmp; do
	command -v "$tool" >/dev/null 2>&1 || {
		printf 'Missing required tool: %s\n' "$tool" >&2
		exit 1
	}
done

check_sha256 "$factory_kernel" \
	9ae541809bf9f05ae00145876814fbc4d049e19801bf15a23c6a579b0d5d40a8
check_sha256 "$factory_dtb" \
	ac5f7f3b6a4612486ab348a3bdb6aabb41439b9999115dc540720f76e0f44993
check_sha256 "$initramfs" \
	855f92832e9de5ae0c182c19a07ad66e7f0892efc6a9f5933f3c32c8336d5859

mkdir -p "$project_root/build/artifacts"
workdir=$(mktemp -d "${TMPDIR:-/tmp}/r1-factory-audio-ab.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

dtc -q -@ -I dts -O dtb -o "$workdir/rescue.dtbo" "$overlay_src"
fdtoverlay -i "$factory_dtb" -o "$output_dtb" "$workdir/rescue.dtbo"

test "$(fdtget -t s "$output_dtb" /chosen bootargs)" = \
	"console=ttyFIQ0,1500000n8 androidboot.console=ttyFIQ0 vmalloc=496M psci=enable rockchip_jtag maxcpus=1 rdinit=/init loglevel=8 ignore_loglevel"
test "$(fdtget -t x "$output_dtb" /reserved-memory/optee@68400000 reg)" = \
	"68400000 300000"
for node in /rksdmmc@30000000 /rksdmmc@30010000 /rksdmmc@30020000; do
	test "$(fdtget -t s "$output_dtb" "$node" status)" = "disabled"
done
for node in /wireless-wlan /wireless-bluetooth /rockchip_audio \
	/rockchip_spdif_card /rockchip_hdmi_i2s /rockchip_nau8540 \
	/rockchip_ma4 /rockchip_es8388; do
	test "$(fdtget -t s "$output_dtb" "$node" status)" = "disabled"
done
test "$(fdtget -t s "$output_dtb" /rockchip-ak7755 status)" = "okay"

(cd "$project_root/scripts" && mkimage -f "$(basename "$its")" "$output_fit")

dumpimage -T flat_dt -p 0 -o "$workdir/kernel" "$output_fit" >/dev/null
dumpimage -T flat_dt -p 1 -o "$workdir/ramdisk" "$output_fit" >/dev/null
dumpimage -T flat_dt -p 2 -o "$workdir/fdt" "$output_fit" >/dev/null
cmp "$factory_kernel" "$workdir/kernel"
cmp "$initramfs" "$workdir/ramdisk"
cmp "$output_dtb" "$workdir/fdt"

printf '\nA21r3 factory-driver AK7755 A/B FIT verified.\n'
printf 'Safety: RAM-only; maxcpus=1; storage/wireless/other cards disabled.\n'
sha256sum "$output_dtb" "$output_fit"
