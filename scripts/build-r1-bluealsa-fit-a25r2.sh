#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
artifacts="$project_root/build/artifacts"
kernel="$artifacts/zImage-mainline-6.18-ak7755-auto-amp-a24r2"
dtb="$artifacts/rk3229-phicomm-r1-mainline-6.18-ak7755-auto-amp-a24r2.dtb"
rootfs="$project_root/build/buildroot-r1-bluealsa-6.18/images/rootfs.cpio.gz"
its="$project_root/scripts/r1-linux-mainline-6.18-ak7755-bluealsa-a25r2.its"
fit="$artifacts/r1-linux-mainline-6.18-ak7755-bluealsa-a25r2.itb"
kernel_sha=d52b3d7ebc0fa37e414b4bcc3e34d3a4b64a325f9685c28211d071491ddef115
dtb_sha=4078b6aa84190948f9ffc289c6762645effc68c776e233664055c04be3cae2e7

for tool in mkimage dumpimage cpio gzip file readelf sha256sum cmp stat; do
	command -v "$tool" >/dev/null 2>&1 || {
		printf 'Missing required tool: %s\n' "$tool" >&2
		exit 1
	}
done

[ -f "$kernel" ] && [ -f "$dtb" ] && [ -f "$rootfs" ] || {
	echo "Missing A24r2 kernel/DTB or 6.18-header Buildroot rootfs." >&2
	echo "Run scripts/build-r1-ak7755-auto-amp-a24r2.sh and scripts/build-r1-bluealsa-rootfs.sh first." >&2
	exit 1
}

printf '%s  %s\n' "$kernel_sha" "$kernel" | sha256sum -c -
printf '%s  %s\n' "$dtb_sha" "$dtb" | sha256sum -c -

rootfs_dir=$(mktemp -d "${TMPDIR:-/tmp}/r1-bluealsa-rootfs.XXXXXX")
fit_dir=$(mktemp -d "${TMPDIR:-/tmp}/r1-bluealsa-fit.XXXXXX")
trap 'rm -rf "$rootfs_dir" "$fit_dir"' EXIT HUP INT TERM
required_paths='init
sbin/init
bin/busybox
etc/init.d/S30dbus-daemon
etc/init.d/S40bluetoothd
etc/init.d/S45bluealsa
etc/init.d/S50bluealsa-aplay
usr/bin/bluetoothctl
usr/bin/bluealsa
usr/bin/bluealsa-aplay
lib/firmware/brcm/brcmfmac43455-sdio.bin
lib/firmware/brcm/brcmfmac43455-sdio.phicomm,r1.txt
lib/firmware/brcm/brcmfmac43455-sdio.clm_blob
lib/firmware/brcm/BCM4345C0.hcd
lib/firmware/regulatory.db
lib/firmware/regulatory.db.p7s
lib/firmware/ak7755_pram_data2.bin
lib/firmware/ak7755_cram_data2.bin
lib/firmware/ak7755_ofreg_data2.bin'
gzip -dc "$rootfs" | (
	cd "$rootfs_dir"
	# Extract only audited payloads so this host-only check never tries to
	# recreate initramfs device nodes such as /dev/console.
	# shellcheck disable=SC2086
	cpio -id --quiet --no-absolute-filenames $required_paths
)

for path in $required_paths; do
	[ -e "$rootfs_dir/$path" ] || { echo "rootfs missing $path" >&2; exit 1; }
done

for elf in bin/busybox usr/bin/bluetoothctl usr/bin/bluealsa usr/bin/bluealsa-aplay; do
	file "$rootfs_dir/$elf" | grep -q 'ELF 32-bit.*ARM' || {
		echo "not an ARM32 ELF: $elf" >&2
		exit 1
	}
	readelf -h "$rootfs_dir/$elf" | grep -q 'hard-float ABI' || {
		echo "not ARM hard-float EABI: $elf" >&2
		exit 1
	}
done

(cd "$project_root/scripts" && mkimage -f "$(basename "$its")" "$fit")
dumpimage -T flat_dt -p 0 -o "$fit_dir/kernel" "$fit" >/dev/null
dumpimage -T flat_dt -p 1 -o "$fit_dir/ramdisk" "$fit" >/dev/null
dumpimage -T flat_dt -p 2 -o "$fit_dir/fdt" "$fit" >/dev/null
cmp "$kernel" "$fit_dir/kernel"
cmp "$rootfs" "$fit_dir/ramdisk"
cmp "$dtb" "$fit_dir/fdt"

fit_size=$(stat -c %s "$fit")
[ "$fit_size" -le 67108864 ] || {
	echo "FIT exceeds the 64 MiB DFU RAM alternate: $fit_size bytes" >&2
	exit 1
}

echo "A25r2 Buildroot/BlueALSA FIT verified; eMMC was not modified."
sha256sum "$rootfs" "$fit"
