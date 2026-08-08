#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool="$project_root/rkdeveloptool/rkdeveloptool"
diagnostic_image="$project_root/build/artifacts/r1-mainline-recovery.img"
original_bcb="$project_root/backup/r1-misc-bcb-before-mainline.img"
confirmation=${1:-}
expected_location=${R1_LOCATION_ID:-}

diagnostic_size=33554432
diagnostic_sha256=1bbd729a09cf1e81f8d2ebd5d8226e2ac33e78cb0252eb3df5c6820ed7e555f2
original_bcb_size=1536
original_bcb_sha256=80422bc3d307b4a25bdafcc84ac7fb01cb55a09810e8b0f37bb12e0edb5c48ca
forced_bcb_sha256=648519893c9687c33869fed944175b5e8fc6b26fc86162b040e06cdbc8ab67bf

recovery_lba=0x01e000
recovery_sectors=0x010000
bcb_lba=0x008020
bcb_sectors=0x3
flash_sectors=15269888

die()
{
	printf 'ERROR: %s\n' "$*" >&2
	exit 1
}

file_size()
{
	wc -c < "$1" | tr -d '[:space:]'
}

file_sha256()
{
	sha256sum "$1" | awk '{print $1}'
}

wait_for_loader()
{
	deadline=$(( $(date +%s) + 60 ))
	printf 'Waiting up to 60 seconds for one Rockchip Loader device...\n'
	if [ -n "$expected_location" ]; then
		printf 'Required LocationID: %s\n' "$expected_location"
	fi
	printf 'Hold the PCB button while applying the original power supply.\n'
	while [ "$(date +%s)" -lt "$deadline" ]; do
		devices=$($tool ld 2>/dev/null || true)
		loader_devices=$(printf '%s\n' "$devices" | awk \
			'/Vid=0x2207/ && /Pid=0x320b/ && /Loader/ { print }')
		loader_count=$(printf '%s\n' "$loader_devices" | awk \
			'NF { count++ } END { print count + 0 }')
		if [ "$loader_count" -gt 1 ]; then
			printf '%s\n' "$loader_devices" >&2
			die 'multiple matching Rockchip Loader devices are connected'
		fi
		if [ "$loader_count" -eq 1 ]; then
			if [ -n "$expected_location" ]; then
				case "$loader_devices" in
					*"LocationID=$expected_location"*) ;;
					*) sleep 0.1; continue ;;
				esac
			fi
			printf '%s\n' "$loader_devices"
			return 0
		fi
		sleep 0.1
	done
	if [ -n "$expected_location" ]; then
		die "no unique Loader appeared at LocationID=$expected_location"
	fi
	die 'no unique Rockchip Loader device appeared'
}

[ "$(id -u)" -eq 0 ] || die 'run this script with sudo'
[ "$confirmation" = '--confirm-recovery-write' ] || {
	printf '%s\n' 'This operation writes exactly:'
	printf '  recovery: LBA %s, %s sectors\n' "$recovery_lba" "$recovery_sectors"
	printf '  misc BCB: LBA %s, %s sectors\n' "$bcb_lba" "$bcb_sectors"
	printf '%s\n' 'It does not write boot, parameter/idb, U-Boot, trust, system, or userdata.'
	printf 'Re-run with: sudo %s --confirm-recovery-write\n' "$0"
	exit 2
}

[ -x "$tool" ] || die "rkdeveloptool is missing: $tool"
[ -f "$diagnostic_image" ] || die "diagnostic image is missing: $diagnostic_image"
[ -f "$original_bcb" ] || die "original BCB is missing: $original_bcb"

[ "$(file_size "$diagnostic_image")" = "$diagnostic_size" ] || die 'diagnostic image size mismatch'
[ "$(file_sha256 "$diagnostic_image")" = "$diagnostic_sha256" ] || die 'diagnostic image SHA-256 mismatch'
[ "$(file_size "$original_bcb")" = "$original_bcb_size" ] || die 'original BCB size mismatch'
[ "$(file_sha256 "$original_bcb")" = "$original_bcb_sha256" ] || die 'original BCB SHA-256 mismatch'
python3 "$project_root/scripts/add-rockchip-boot-hashes.py" --verify "$diagnostic_image"

run_dir=$(mktemp -d /tmp/r1-diagnostic-recovery.XXXXXX)
forced_bcb="$run_dir/boot-recovery-bcb.img"
before_image="$run_dir/recovery-before.img"
recovery_readback="$run_dir/recovery-readback.img"
bcb_readback="$run_dir/bcb-readback.img"

cp "$original_bcb" "$forced_bcb"
printf 'boot-recovery' | dd of="$forced_bcb" conv=notrunc status=none
[ "$(file_size "$forced_bcb")" = "$original_bcb_size" ] || die 'generated BCB size mismatch'
[ "$(file_sha256 "$forced_bcb")" = "$forced_bcb_sha256" ] || die 'generated BCB content mismatch'

printf 'Local preflight passed. Run evidence will be kept in %s\n' "$run_dir"
wait_for_loader

chip_info=$($tool rci)
printf '%s\n' "$chip_info"
printf '%s\n' "$chip_info" | grep -q '41 32 32 33' || die 'unexpected Rockchip chip info'

flash_id=$($tool rid)
printf '%s\n' "$flash_id"
printf '%s\n' "$flash_id" | grep -q '45 4D 4D 43 20' || die 'unexpected storage flash ID'

flash_info=$($tool rfi)
printf '%s\n' "$flash_info"
printf '%s\n' "$flash_info" | grep -q "Flash Size: $flash_sectors Sectors" || die 'unexpected eMMC capacity'

printf 'Backing up current recovery...\n'
$tool rl "$recovery_lba" "$recovery_sectors" "$before_image"
[ "$(file_size "$before_image")" = "$diagnostic_size" ] || die 'pre-write recovery backup size mismatch'
printf 'Pre-write recovery SHA-256: %s\n' "$(file_sha256 "$before_image")"

printf 'Writing diagnostic recovery to LBA %s...\n' "$recovery_lba"
$tool wl "$recovery_lba" "$diagnostic_image"

printf 'Reading diagnostic recovery back...\n'
$tool rl "$recovery_lba" "$recovery_sectors" "$recovery_readback"
cmp "$diagnostic_image" "$recovery_readback" || die 'recovery readback differs from source; BCB was not changed'
printf 'Recovery readback verified: %s\n' "$(file_sha256 "$recovery_readback")"

printf 'Writing forced-recovery BCB to LBA %s...\n' "$bcb_lba"
$tool wl "$bcb_lba" "$forced_bcb"
$tool rl "$bcb_lba" "$bcb_sectors" "$bcb_readback"
cmp "$forced_bcb" "$bcb_readback" || die 'BCB readback differs from source; device will not be reset'
printf 'BCB readback verified: %s\n' "$(file_sha256 "$bcb_readback")"

printf '%s\n' 'All writes verified. Resetting directly into diagnostic recovery...'
$tool rd
printf 'Done. Keep the UART logger running and inspect output after Starting kernel ...\n'
