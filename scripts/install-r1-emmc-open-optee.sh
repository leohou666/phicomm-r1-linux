#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool="$root/rkdeveloptool/rkdeveloptool"
preflight="$root/scripts/preflight-r1-emmc-install.sh"
idb="$root/build/artifacts/r1-emmc-idbloader-open-optee-a5.img"
fit="$root/build/artifacts/r1-emmc-u-boot-open-optee-a5.itb"
original_idb="$root/backup/boot/r1-emmc-user-idb-original-0x40-0xa7.img"
original_trust="$root/backup/partitions/trust.img"
confirmation=${1:-}
location=${R1_LOCATION_ID:-}

idb_lba=0x40
idb_sectors=0x68
fit_lba=0x6000
fit_sectors=0x64f
flash_sectors=15269888
idb_sha=81fea15cbc4b73cae51908b7e290c955eab074cb8b8db5260acb64f17c7811c5
fit_sha=bb2e5ab94c96f0184e01440993309cacb48738cf72f309f2b34dd38b4ae9847b
original_idb_sha=52c91233878ba72f8470be2aceaa0c8ff3c5c158120475c94f9c5cf187d82783
original_fit_slice_sha=1c4bc724e6a881db0f5d1aa0e862522a2db305e9c83eb752c7f549e8e92ed519

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
sha() { sha256sum "$1" | awk '{print $1}'; }

if [ "$confirmation" != '--confirm-write-raw-0x40-and-0x6000' ]; then
	printf '%s\n' 'DRY RUN: no device command was executed.'
	printf '%s\n' 'The authorized write ranges would be:'
	printf '  raw IDB: LBA %s, %s sectors (ends 0xa7)\n' "$idb_lba" "$idb_sectors"
	printf '  raw FIT: LBA %s, %s sectors (ends 0x664e)\n' "$fit_lba" "$fit_sectors"
	printf '%s\n' 'No other LBA is written and the script does not reset the board.'
	printf 'Required command: sudo R1_LOCATION_ID=<verified-id> %s --confirm-write-raw-0x40-and-0x6000\n' "$0"
	exit 2
fi

[ "$(id -u)" -eq 0 ] || die 'run with sudo'
[ -n "$location" ] || die 'set R1_LOCATION_ID to the verified USB LocationID'
[ -x "$tool" ] || die 'rkdeveloptool missing'
[ -x "$preflight" ] || die 'preflight script missing'
[ "$(sha "$idb")" = "$idb_sha" ] || die 'A5 IDB hash mismatch'
[ "$(sha "$fit")" = "$fit_sha" ] || die 'A5 FIT hash mismatch'
[ "$(sha "$original_idb")" = "$original_idb_sha" ] || die 'original raw IDB hash mismatch'

R1_LOCATION_ID="$location" "$preflight"

run_dir=$(mktemp -d /tmp/r1-emmc-install.XXXXXX)
old_idb="$run_dir/original-idb.img"
old_fit="$run_dir/original-fit-slice.img"
fit_readback="$run_dir/a5-fit-readback.img"
idb_readback="$run_dir/a5-idb-readback.img"
rollback_fit_readback="$run_dir/rollback-fit-readback.img"
rollback_idb_readback="$run_dir/rollback-idb-readback.img"
dd if="$original_trust" of="$old_fit" bs=512 count=1615 status=none
[ "$(sha "$old_fit")" = "$original_fit_slice_sha" ] || die 'original FIT restore slice hash mismatch'
cp "$original_idb" "$old_idb"

devices=$(timeout 8 "$tool" ld)
printf '%s\n' "$devices" | grep -q "Vid=0x2207,Pid=0x320b,LocationID=$location" || die 'verified USB device disappeared or changed'
chip=$(timeout 8 "$tool" rci)
printf '%s\n' "$chip" | grep -q '41 32 32 33' || die 'unexpected chip'
info=$(timeout 8 "$tool" rfi)
printf '%s\n' "$info" | grep -q "Flash Size: $flash_sectors Sectors" || die 'unexpected eMMC capacity'

rollback()
{
	printf '%s\n' 'Attempting immediate rollback of both authorized ranges...' >&2
	rollback_ok=1
	timeout 60 "$tool" wl "$fit_lba" "$old_fit" || rollback_ok=0
	timeout 30 "$tool" rl "$fit_lba" "$fit_sectors" "$rollback_fit_readback" || rollback_ok=0
	cmp "$old_fit" "$rollback_fit_readback" || rollback_ok=0
	timeout 30 "$tool" wl "$idb_lba" "$old_idb" || rollback_ok=0
	timeout 30 "$tool" rl "$idb_lba" "$idb_sectors" "$rollback_idb_readback" || rollback_ok=0
	cmp "$old_idb" "$rollback_idb_readback" || rollback_ok=0
	if [ "$rollback_ok" -eq 1 ]; then
		printf '%s\n' 'Rollback readback verified.' >&2
	else
		printf 'CRITICAL: rollback could not be verified; retain %s and do not power-cycle.\n' "$run_dir" >&2
	fi
}

printf 'Writing A5 FIT to raw LBA %s...\n' "$fit_lba"
if ! timeout 60 "$tool" wl "$fit_lba" "$fit"; then rollback; die 'FIT write failed; rollback attempted'; fi
timeout 30 "$tool" rl "$fit_lba" "$fit_sectors" "$fit_readback" || { rollback; die 'FIT readback failed; rollback attempted'; }
cmp "$fit" "$fit_readback" || { rollback; die 'FIT readback mismatch; rollback attempted'; }

printf 'Writing A5 IDB last to raw LBA %s...\n' "$idb_lba"
if ! timeout 30 "$tool" wl "$idb_lba" "$idb"; then rollback; die 'IDB write failed; rollback attempted'; fi
timeout 30 "$tool" rl "$idb_lba" "$idb_sectors" "$idb_readback" || { rollback; die 'IDB readback failed; rollback attempted'; }
cmp "$idb" "$idb_readback" || { rollback; die 'IDB readback mismatch; rollback attempted'; }

printf '%s\n' 'A5 INSTALL WRITEBACK VERIFIED'
printf 'Evidence directory: %s\n' "$run_dir"
printf '%s\n' 'The board was not reset. Start UART capture, disconnect USB data if desired, then cold-power-cycle.'
