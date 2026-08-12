#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool="$project_root/rkdeveloptool/rkdeveloptool"
usbplug_loader="$project_root/firmware/rk322x_loader_v1.06.237.bin"
parameter_backup="$project_root/backup/partitions/parameter-idb.img"
trust_backup="$project_root/backup/partitions/trust.img"
raw_idb_backup="$project_root/backup/boot/r1-emmc-user-idb-original-0x40-0xa7.img"
idb_candidate="$project_root/build/artifacts/r1-emmc-idbloader-open-optee-a5.img"
fit_candidate="$project_root/build/artifacts/r1-emmc-u-boot-open-optee-a5.itb"
expected_location=${R1_LOCATION_ID:-}

flash_sectors=15269888
idb_lba=0x40
idb_sectors=0x68
fit_lba=0x6000
fit_sectors=0x64f

parameter_size=4194304
parameter_sha256=66aedcbf5f9e9070c731afa6e7ba1d1982eacba8ad7ff7c37dddd131b4c662e1
trust_size=8388608
trust_sha256=2b0e823316de63e255a2194e305100f8db104d3dd98efcd8e30055c64105db45
idb_candidate_size=53248
idb_candidate_sha256=81fea15cbc4b73cae51908b7e290c955eab074cb8b8db5260acb64f17c7811c5
fit_candidate_size=826880
fit_candidate_sha256=bb2e5ab94c96f0184e01440993309cacb48738cf72f309f2b34dd38b4ae9847b
fit_restore_sha256=1c4bc724e6a881db0f5d1aa0e862522a2db305e9c83eb752c7f549e8e92ed519
raw_idb_backup_sha256=52c91233878ba72f8470be2aceaa0c8ff3c5c158120475c94f9c5cf187d82783
usbplug_loader_size=139598
usbplug_loader_sha256=13be76942ec70235d2a1460dcdb35aa7a70771747f5237d7c9c9d7118c64d136

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

check_file()
{
	path=$1
	expected_size=$2
	expected_sha=$3
	[ -f "$path" ] || die "missing file: $path"
	[ "$(file_size "$path")" = "$expected_size" ] || die "unexpected size: $path"
	[ "$(file_sha256 "$path")" = "$expected_sha" ] || die "unexpected SHA-256: $path"
}

wait_for_device()
{
	deadline=$(( $(date +%s) + 60 ))
	printf 'Waiting up to 60 seconds for one RK3229 USB device...\n'
	printf 'Use the original power supply and the previously verified USB port.\n'
	if [ -n "$expected_location" ]; then
		printf 'Required LocationID: %s\n' "$expected_location"
	fi
	while [ "$(date +%s)" -lt "$deadline" ]; do
		devices=$($tool ld 2>/dev/null || true)
		matches=$(printf '%s\n' "$devices" | awk \
			'/Vid=0x2207/ && /Pid=0x320b/ { print }')
		count=$(printf '%s\n' "$matches" | awk \
			'NF { count++ } END { print count + 0 }')
		[ "$count" -le 1 ] || die 'multiple matching Rockchip devices are connected'
		if [ "$count" -eq 1 ]; then
			if [ -n "$expected_location" ]; then
				case "$matches" in
					*"LocationID=$expected_location"*) ;;
					*) sleep 0.1; continue ;;
				esac
			fi
			printf '%s\n' "$matches"
			return 0
		fi
		sleep 0.1
	done
	die 'no unique RK3229 USB device appeared'
}

[ "$(id -u)" -eq 0 ] || die 'run this script with sudo'
[ -x "$tool" ] || die "rkdeveloptool is missing: $tool"

check_file "$parameter_backup" "$parameter_size" "$parameter_sha256"
check_file "$trust_backup" "$trust_size" "$trust_sha256"
check_file "$idb_candidate" "$idb_candidate_size" "$idb_candidate_sha256"
check_file "$fit_candidate" "$fit_candidate_size" "$fit_candidate_sha256"
check_file "$usbplug_loader" "$usbplug_loader_size" "$usbplug_loader_sha256"

run_dir=$(mktemp -d /tmp/r1-emmc-preflight.XXXXXX)
chmod 0755 "$run_dir"
fit_reference="$run_dir/fit-original-reference.img"
idb_read1="$run_dir/idb-read1.img"
idb_read2="$run_dir/idb-read2.img"
fit_read1="$run_dir/fit-read1.img"
fit_read2="$run_dir/fit-read2.img"

dd if="$trust_backup" of="$fit_reference" bs=512 count=1615 status=none
chmod 0644 "$fit_reference"
[ "$(file_sha256 "$fit_reference")" = "$fit_restore_sha256" ] || die 'trust restore slice mismatch'

printf 'Local artifact and restore-slice checks passed.\n'
printf 'Evidence directory: %s\n' "$run_dir"
printf 'Safety: only ld/db/rci/rid/rfi/rl are permitted; db loads usbplug into RAM only.\n'
printf 'This script contains no eMMC write, erase, or reset command.\n'
wait_for_device

if chip_info=$(timeout 8 "$tool" rci 2>&1); then
	printf 'Storage protocol is already available; no RAM loader download needed.\n'
else
	printf 'Initial rci was unavailable; downloading the verified DDR/usbplug loader to RAM only.\n'
	printf '%s\n' "$chip_info"
	timeout 20 "$tool" db "$usbplug_loader"
	sleep 1
	wait_for_device
	chip_info=$(timeout 8 "$tool" rci 2>&1) || die 'rci still unavailable after RAM-only usbplug download'
fi
printf '%s\n' "$chip_info"
printf '%s\n' "$chip_info" | grep -q '41 32 32 33' || die 'unexpected Rockchip chip info'

flash_id=$(timeout 8 "$tool" rid 2>&1) || die 'rid failed or timed out'
printf '%s\n' "$flash_id"
printf '%s\n' "$flash_id" | grep -q '45 4D 4D 43 20' || die 'unexpected storage flash ID'

flash_info=$(timeout 8 "$tool" rfi 2>&1) || die 'rfi failed or timed out'
printf '%s\n' "$flash_info"
printf '%s\n' "$flash_info" | grep -q "Flash Size: $flash_sectors Sectors" || die 'unexpected eMMC capacity'

printf 'Reading original IDB target twice: LBA %s, %s sectors...\n' "$idb_lba" "$idb_sectors"
timeout 30 "$tool" rl "$idb_lba" "$idb_sectors" "$idb_read1"
timeout 30 "$tool" rl "$idb_lba" "$idb_sectors" "$idb_read2"
chmod 0644 "$idb_read1" "$idb_read2"
preflight_failed=0
if ! cmp "$idb_read1" "$idb_read2"; then
	printf '%s\n' 'IDB ERROR: two device reads differ.' >&2
	preflight_failed=1
fi
if [ "$preflight_failed" -eq 0 ]; then
	if [ -f "$raw_idb_backup" ]; then
		[ "$(file_size "$raw_idb_backup")" = "$idb_candidate_size" ] || die 'preserved raw IDB size mismatch'
		[ "$(file_sha256 "$raw_idb_backup")" = "$raw_idb_backup_sha256" ] || die 'preserved raw IDB SHA-256 mismatch'
		if ! cmp "$raw_idb_backup" "$idb_read1"; then
			printf '%s\n' 'IDB ERROR: device data differs from the preserved raw IDB backup.' >&2
			sha256sum "$raw_idb_backup" "$idb_read1" "$idb_read2"
			preflight_failed=1
		fi
	else
		printf 'Capturing the double-read-verified original raw IDB to host backup: %s\n' "$raw_idb_backup"
		install -m 0644 "$idb_read1" "$raw_idb_backup"
		if [ -n "${SUDO_UID:-}" ] && [ -n "${SUDO_GID:-}" ]; then
			chown "$SUDO_UID:$SUDO_GID" "$raw_idb_backup"
		fi
	fi
fi

printf 'Reading original trust/FIT target twice: LBA %s, %s sectors...\n' "$fit_lba" "$fit_sectors"
timeout 30 "$tool" rl "$fit_lba" "$fit_sectors" "$fit_read1"
timeout 30 "$tool" rl "$fit_lba" "$fit_sectors" "$fit_read2"
chmod 0644 "$fit_read1" "$fit_read2"
if ! cmp "$fit_read1" "$fit_read2"; then
	printf '%s\n' 'FIT ERROR: two device reads differ.' >&2
	preflight_failed=1
fi
if ! cmp "$fit_reference" "$fit_read1"; then
	printf '%s\n' 'FIT ERROR: device data differs from preserved backup.' >&2
	sha256sum "$fit_reference" "$fit_read1" "$fit_read2"
	printf '%s\n' 'FIT reference first 64 bytes:'
	xxd -l 64 "$fit_reference"
	printf '%s\n' 'FIT device first 64 bytes:'
	xxd -l 64 "$fit_read1"
	preflight_failed=1
fi

[ "$preflight_failed" -eq 0 ] || die "read-only comparisons failed; evidence retained in $run_dir"

printf 'Raw IDB restore slice SHA-256: %s\n' "$(file_sha256 "$idb_read1")"
printf 'FIT restore slice SHA-256: %s\n' "$(file_sha256 "$fit_read1")"
printf '%s\n' 'READ-ONLY PREFLIGHT PASSED'
printf '%s\n' 'No eMMC data was changed. Keep the evidence directory until installation is complete.'
