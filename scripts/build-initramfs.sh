#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
busybox=${BUSYBOX:-"$project_root/backup/unpacked/recovery/ramdisk/sbin/busybox"}
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

if ! file "$busybox" | grep -q 'ELF 32-bit.*ARM.*statically linked'; then
	printf 'BusyBox must be a statically linked 32-bit ARM ELF: %s\n' "$busybox" >&2
	file "$busybox" >&2
	exit 1
fi

mkdir -p "$rootfs/bin" "$rootfs/dev" "$rootfs/proc" "$rootfs/sys"
chmod 0755 "$rootfs"
install -m 0755 "$busybox" "$rootfs/bin/busybox"
install -m 0755 "$project_root/initramfs/init" "$rootfs/init"

for applet in cat dmesg ls mkdir mount sh uname; do
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
) | gzip -9n > "$artifacts/r1-initramfs.cpio.gz"

file "$artifacts/r1-initramfs.cpio.gz"
printf 'Initramfs written to %s\n' "$artifacts/r1-initramfs.cpio.gz"
