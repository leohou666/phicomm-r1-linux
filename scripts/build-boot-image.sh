#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
artifacts="$project_root/build/artifacts"
kernel="$artifacts/zImage"
ramdisk="$artifacts/r1-initramfs.cpio.gz"
dtb="$artifacts/rk3229-phicomm-r1.dtb"
resource_img="$artifacts/r1-resource.img"
boot_img="$artifacts/r1-mainline-boot.img"
recovery_img="$artifacts/r1-mainline-recovery.img"
rockchip_hash_tool="$project_root/scripts/add-rockchip-boot-hashes.py"
resource_tool=${RESOURCE_TOOL:-"$project_root/rkdeveloptool/rkbin/tools/resource_tool"}
recovery_partition_size=$((0x10000 * 512))

# Keep the vendor base, kernel and tags addresses, but move the initramfs and
# Rockchip resource image above the mainline kernel's decompressed footprint.
# The original addresses were sized for the smaller vendor kernel: the current
# zImage ended past 0x60f00000 and the linked kernel ended past 0x62000000.
base_addr=$((0x60000000))
kernel_addr=$((0x60408000))
tags_addr=$((0x60088000))
ramdisk_addr=$((0x64000000))
second_addr=$((0x66000000))

for input in "$kernel" "$ramdisk" "$dtb"; do
	if [ ! -f "$input" ]; then
		printf 'Required build artifact is missing: %s\n' "$input" >&2
		exit 1
	fi
done

if [ ! -x "$resource_tool" ]; then
	printf 'Rockchip resource_tool is missing or not executable: %s\n' \
		"$resource_tool" >&2
	printf 'Set RESOURCE_TOOL=/path/to/resource_tool and retry.\n' >&2
	exit 1
fi

if ! command -v mkbootimg >/dev/null 2>&1; then
	printf 'mkbootimg was not found in PATH.\n' >&2
	exit 1
fi

"$resource_tool" --image="$resource_img" --pack "$dtb"

# Old Rockchip resource_tool versions leave the unused 12 bytes after the
# 20-byte SHA-1 field uninitialized. They are outside hash_size but make the
# image nondeterministic, so normalize them for this single-entry image.
dd if=/dev/zero of="$resource_img" bs=1 seek=756 count=12 \
	conv=notrunc status=none

kernel_size=$(wc -c < "$kernel")
ramdisk_size=$(wc -c < "$ramdisk")
second_size=$(wc -c < "$resource_img")
kernel_end=$((kernel_addr + kernel_size))
ramdisk_end=$((ramdisk_addr + ramdisk_size))
second_end=$((second_addr + second_size))

if [ "$kernel_end" -gt "$ramdisk_addr" ] || \
   [ "$ramdisk_end" -gt "$second_addr" ]; then
	printf 'Boot image load ranges overlap. Refusing to build.\n' >&2
	exit 1
fi

printf 'kernel load:  [0x%08x, 0x%08x)\n' "$kernel_addr" "$kernel_end"
printf 'ramdisk load: [0x%08x, 0x%08x)\n' "$ramdisk_addr" "$ramdisk_end"
printf 'second load:  [0x%08x, 0x%08x)\n' "$second_addr" "$second_end"

# The vendor bootrk command parses the kernel, ramdisk, and Rockchip resource
# image addresses from this Android boot header.
mkbootimg \
	--header_version 0 \
	--pagesize 16384 \
	--base "$(printf '0x%x' "$base_addr")" \
	--kernel_offset "$(printf '0x%x' $((kernel_addr - base_addr)))" \
	--ramdisk_offset "$(printf '0x%x' $((ramdisk_addr - base_addr)))" \
	--second_offset "$(printf '0x%x' $((second_addr - base_addr)))" \
	--tags_offset "$(printf '0x%x' $((tags_addr - base_addr)))" \
	--kernel "$kernel" \
	--ramdisk "$ramdisk" \
	--second "$resource_img" \
	--output "$boot_img"

# The vendor U-Boot does not accept the standard AOSP image ID by itself.
# Its SecureNSModeBootImageShaCheck adds selected header fields to the SHA-1
# input and also expects a SHA-256 copy in Rockchip's header extension.
python3 "$rockchip_hash_tool" "$boot_img"

boot_size=$(wc -c < "$boot_img")
if [ "$boot_size" -gt "$recovery_partition_size" ]; then
	printf 'Boot image is larger than the 32 MiB recovery partition.\n' >&2
	exit 1
fi

# Build an exact partition-sized image so both the test write and rollback can
# use the same explicit 0x10000-sector boundary. The unused tail is zeroed.
dd if=/dev/zero of="$recovery_img" bs=1048576 count=32 status=none
dd if="$boot_img" of="$recovery_img" conv=notrunc status=none

file "$resource_img" "$boot_img" "$recovery_img"
printf 'RAM-boot candidate written to %s\n' "$boot_img"
printf 'Recovery candidate written to %s (%d bytes)\n' \
	"$recovery_img" "$recovery_partition_size"
printf 'Do not flash this image; it has not booted on the R1 yet.\n'
