#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool="$root/rkdeveloptool/rkdeveloptool"
loader="$root/firmware/rk322x_loader_v1.06.237.bin"
original_idb="$root/backup/boot/r1-emmc-user-idb-original-0x40-0xa7.img"
original_trust="$root/backup/partitions/trust.img"
confirmation=${1:-}
location=${R1_LOCATION_ID:-}
idb_sha=52c91233878ba72f8470be2aceaa0c8ff3c5c158120475c94f9c5cf187d82783
fit_slice_sha=1c4bc724e6a881db0f5d1aa0e862522a2db305e9c83eb752c7f549e8e92ed519
loader_sha=13be76942ec70235d2a1460dcdb35aa7a70771747f5237d7c9c9d7118c64d136

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
sha() { sha256sum "$1" | awk '{print $1}'; }

if [ "$confirmation" != '--confirm-restore-raw-0x40-and-0x6000' ]; then
	printf '%s\n' 'DRY RUN: no device command was executed.'
	printf '%s\n' 'This restores only raw 0x40..0xa7 and raw 0x6000..0x664e.'
	printf 'Required command: sudo R1_LOCATION_ID=<verified-id> %s --confirm-restore-raw-0x40-and-0x6000\n' "$0"
	exit 2
fi

[ "$(id -u)" -eq 0 ] || die 'run with sudo'
[ -n "$location" ] || die 'set R1_LOCATION_ID to the verified USB LocationID'
[ "$(sha "$original_idb")" = "$idb_sha" ] || die 'original raw IDB hash mismatch'
[ "$(sha "$loader")" = "$loader_sha" ] || die 'usbplug loader hash mismatch'

run_dir=$(mktemp -d /tmp/r1-emmc-restore.XXXXXX)
fit_slice="$run_dir/original-fit-slice.img"
fit_readback="$run_dir/fit-readback.img"
idb_readback="$run_dir/idb-readback.img"
dd if="$original_trust" of="$fit_slice" bs=512 count=1615 status=none
[ "$(sha "$fit_slice")" = "$fit_slice_sha" ] || die 'original FIT slice hash mismatch'

devices=$(timeout 8 "$tool" ld 2>/dev/null || true)
printf '%s\n' "$devices" | grep -q "Vid=0x2207,Pid=0x320b,LocationID=$location" || die 'verified RK3229 USB device not found'
if ! timeout 8 "$tool" rci >/dev/null 2>&1; then
	timeout 20 "$tool" db "$loader"
	sleep 1
fi
devices=$(timeout 8 "$tool" ld)
printf '%s\n' "$devices" | grep -q "Vid=0x2207,Pid=0x320b,LocationID=$location" || die 'USB device changed after loader download'
chip=$(timeout 8 "$tool" rci)
printf '%s\n' "$chip" | grep -q '41 32 32 33' || die 'unexpected chip'
info=$(timeout 8 "$tool" rfi)
printf '%s\n' "$info" | grep -q 'Flash Size: 15269888 Sectors' || die 'unexpected eMMC capacity'

timeout 60 "$tool" wl 0x6000 "$fit_slice"
timeout 30 "$tool" rl 0x6000 0x64f "$fit_readback"
cmp "$fit_slice" "$fit_readback" || die 'restored FIT slice readback mismatch'
timeout 30 "$tool" wl 0x40 "$original_idb"
timeout 30 "$tool" rl 0x40 0x68 "$idb_readback"
cmp "$original_idb" "$idb_readback" || die 'restored IDB readback mismatch'

printf '%s\n' 'ORIGINAL BOOT RANGES RESTORED AND VERIFIED'
printf 'Evidence directory: %s\n' "$run_dir"
printf '%s\n' 'The board was not reset; cold-power-cycle when UART capture is ready.'
