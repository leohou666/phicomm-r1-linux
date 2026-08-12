#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
busybox=${BUSYBOX:-"$project_root/backup/unpacked/recovery/ramdisk/sbin/busybox"}
include_wifi_firmware=${R1_WIFI_FIRMWARE:-0}
include_wifi_regulatory=${R1_WIFI_REGULATORY:-0}
include_bluetooth_firmware=${R1_BLUETOOTH_FIRMWARE:-0}
regdb_source=${R1_REGULATORY_DB:-/usr/lib/firmware/regulatory.db}
regdb_signature_source=${R1_REGULATORY_DB_SIGNATURE:-/usr/lib/firmware/regulatory.db.p7s}
brcm43455_clm_source=${R1_BRCM43455_CLM_BLOB:-/usr/lib/firmware/brcm/brcmfmac43455-sdio.clm_blob.xz}
wifi_scan_tool=${R1_WIFI_SCAN_TOOL:-}
bluetooth_mgmt_tool=${R1_BLUETOOTH_MGMT_TOOL:-}
ak7755_id_tool=${R1_AK7755_ID_TOOL:-}
include_ak7755_firmware=${R1_AK7755_FIRMWARE:-0}
artifact_tag=${INITRAMFS_ARTIFACT_TAG:-}
artifacts="$project_root/build/artifacts"
rootfs=$(mktemp -d "$project_root/build/initramfs.XXXXXX")

cleanup()
{
	rm -rf -- "$rootfs"
}
trap cleanup EXIT HUP INT TERM

if [ ! -x "$busybox" ]; then
	printf 'Static ARM BusyBox is missing or not executable: %s\n' "$busybox" >&2
	printf 'Set BUSYBOX=/path/to/32-bit-static-arm-busybox and retry.\n' >&2
	exit 1
fi

if [ "$include_bluetooth_firmware" = 1 ]; then
	bt_hcd_source="$project_root/backup/extracted/system/vendor/firmware/BCM4345.hcd"
	[ -f "$bt_hcd_source" ] || {
		printf 'BCM4345 Bluetooth HCD is missing: %s\n' "$bt_hcd_source" >&2
		exit 1
	}
	mkdir -p "$rootfs/lib/firmware/brcm"
	install -m 0644 "$bt_hcd_source" \
		"$rootfs/lib/firmware/brcm/BCM4345.hcd"
	ln -s BCM4345.hcd "$rootfs/lib/firmware/brcm/BCM4345C0.hcd"
fi

if [ -n "$artifact_tag" ]; then
	case "$artifact_tag" in
		*[!A-Za-z0-9._-]*)
			printf 'Unsafe INITRAMFS_ARTIFACT_TAG: %s\n' "$artifact_tag" >&2
			exit 1
			;;
	esac
	initramfs_output="$artifacts/r1-initramfs-$artifact_tag.cpio.gz"
else
	initramfs_output="$artifacts/r1-initramfs.cpio.gz"
fi

if ! file "$busybox" | grep -q 'ELF 32-bit.*ARM.*statically linked'; then
	printf 'BusyBox must be a statically linked 32-bit ARM ELF: %s\n' "$busybox" >&2
	file "$busybox" >&2
	exit 1
fi

mkdir -p "$rootfs/bin" "$rootfs/dev" "$rootfs/proc" "$rootfs/sys"
chmod 0755 "$rootfs"
install -m 0755 "$busybox" "$rootfs/bin/busybox"
install -m 0755 "$project_root/initramfs/init" "$rootfs/init"

if [ "$include_wifi_firmware" = 1 ]; then
	wifi_source="$project_root/backup/extracted/system/etc/firmware"
	mkdir -p "$rootfs/lib/firmware/brcm"
	install -m 0644 "$wifi_source/fw_bcm43455c0_ag.bin" \
		"$rootfs/lib/firmware/brcm/brcmfmac43455-sdio.bin"
	install -m 0644 "$wifi_source/nvram_ap6255.txt" \
		"$rootfs/lib/firmware/brcm/brcmfmac43455-sdio.txt"
	ln -s brcmfmac43455-sdio.txt \
		"$rootfs/lib/firmware/brcm/brcmfmac43455-sdio.phicomm,r1.txt"
fi

if [ "$include_wifi_regulatory" = 1 ]; then
	[ -f "$regdb_source" ] || {
		printf 'regulatory.db is missing: %s\n' "$regdb_source" >&2
		exit 1
	}
	[ -f "$regdb_signature_source" ] || {
		printf 'regulatory.db signature is missing: %s\n' \
			"$regdb_signature_source" >&2
		exit 1
	}
	[ -f "$brcm43455_clm_source" ] || {
		printf 'BCM43455 CLM blob is missing: %s\n' \
			"$brcm43455_clm_source" >&2
		exit 1
	}
	mkdir -p "$rootfs/lib/firmware/brcm"
	install -m 0644 "$regdb_source" "$rootfs/lib/firmware/regulatory.db"
	install -m 0644 "$regdb_signature_source" \
		"$rootfs/lib/firmware/regulatory.db.p7s"
	case "$brcm43455_clm_source" in
		*.xz)
			xz -dc -- "$brcm43455_clm_source" > \
				"$rootfs/lib/firmware/brcm/brcmfmac43455-sdio.clm_blob"
			;;
		*)
			install -m 0644 "$brcm43455_clm_source" \
				"$rootfs/lib/firmware/brcm/brcmfmac43455-sdio.clm_blob"
			;;
	esac
	chmod 0644 "$rootfs/lib/firmware/brcm/brcmfmac43455-sdio.clm_blob"
fi

if [ -n "$wifi_scan_tool" ]; then
	[ -x "$wifi_scan_tool" ] || {
		printf 'Wi-Fi scan tool is missing or not executable: %s\n' \
			"$wifi_scan_tool" >&2
		exit 1
	}
	if ! file "$wifi_scan_tool" | grep -q \
		'ELF 32-bit.*ARM.*statically linked'; then
		printf 'Wi-Fi scan tool must be a static 32-bit ARM ELF: %s\n' \
			"$wifi_scan_tool" >&2
		exit 1
	fi
	install -m 0755 "$wifi_scan_tool" "$rootfs/bin/r1-wifi-scan"
fi

if [ -n "$bluetooth_mgmt_tool" ]; then
	[ -x "$bluetooth_mgmt_tool" ] || {
		printf 'Bluetooth management tool is missing or not executable: %s\n' \
			"$bluetooth_mgmt_tool" >&2
		exit 1
	}
	if ! file "$bluetooth_mgmt_tool" | grep -q \
		'ELF 32-bit.*ARM.*statically linked'; then
		printf 'Bluetooth management tool must be a static 32-bit ARM ELF: %s\n' \
			"$bluetooth_mgmt_tool" >&2
		exit 1
	fi
	install -m 0755 "$bluetooth_mgmt_tool" "$rootfs/bin/r1-btmgmt"
	install -m 0755 "$project_root/initramfs/r1-bt-coexist-test" \
		"$rootfs/bin/r1-bt-coexist-test"
fi

if [ -n "$ak7755_id_tool" ]; then
	[ -x "$ak7755_id_tool" ] || {
		printf 'AK7755EN identification tool is missing or not executable: %s\n' \
			"$ak7755_id_tool" >&2
		exit 1
	}
	if ! file "$ak7755_id_tool" | grep -q \
		'ELF 32-bit.*ARM.*statically linked'; then
		printf 'AK7755EN identification tool must be a static 32-bit ARM ELF: %s\n' \
			"$ak7755_id_tool" >&2
		exit 1
	fi
	install -m 0755 "$ak7755_id_tool" "$rootfs/bin/r1-ak7755-id"
fi

if [ "$include_ak7755_firmware" = 1 ]; then
	ak7755_firmware_dir="$project_root/backup/extracted/system/vendor/firmware"
	for firmware in ak7755_pram_data2.bin ak7755_cram_data2.bin; do
		[ -f "$ak7755_firmware_dir/$firmware" ] || {
			printf 'AK7755 firmware is missing: %s\n' \
				"$ak7755_firmware_dir/$firmware" >&2
			exit 1
		}
		mkdir -p "$rootfs/lib/firmware"
		install -m 0644 "$ak7755_firmware_dir/$firmware" \
			"$rootfs/lib/firmware/$firmware"
	done
fi

for applet in \
	'[' awk cat clear cttyhack cut date dd devmem df dmesg du echo false free grep head hexdump \
	hostname id kill ln ls mkdir mknod mount poweroff ps readlink reboot \
	printf realpath sed setsid sh sleep sort stty sync sysctl tail test timeout top true tty \
	uname uptime wc zcat
do
	ln -s busybox "$rootfs/bin/$applet"
done

# Normalize timestamps and ownership so identical inputs produce identical
# archives. The archive is assembled without touching the source files.
find "$rootfs" -exec touch -h -d '@0' {} +
mkdir -p "$artifacts"
(
	cd "$rootfs"
	find . -print0 |
		sort -z |
		cpio --null --create --format=newc --owner=0:0 \
			--reproducible 2>/dev/null
) | gzip -9n > "$initramfs_output"

file "$initramfs_output"
printf 'Initramfs written to %s\n' "$initramfs_output"
