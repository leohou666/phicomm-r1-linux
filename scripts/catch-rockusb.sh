#!/bin/sh
set -u

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool="$project_root/rkdeveloptool/rkdeveloptool"
timeout_seconds=${1:-45}
deadline=$(( $(date +%s) + timeout_seconds ))

if [ ! -x "$tool" ]; then
	printf 'rkdeveloptool is missing or not executable: %s\n' "$tool" >&2
	exit 1
fi

printf 'Waiting up to %s seconds for Rockchip USB...\n' "$timeout_seconds"
printf 'Hold the board button while applying power. No storage writes will run.\n'

while [ "$(date +%s)" -lt "$deadline" ]; do
	devices=$($tool ld 2>/dev/null || true)
	case "$devices" in
		*'Vid=0x2207'*)
			printf '%s\n' "$devices"
			printf 'Running read-only probes immediately...\n'
			$tool rci
			rci_status=$?
			$tool rid
			rid_status=$?
			$tool rfi
			rfi_status=$?
			printf 'statuses: rci=%s rid=%s rfi=%s\n' \
				"$rci_status" "$rid_status" "$rfi_status"
			exit $((rci_status || rid_status || rfi_status))
			;;
	esac
	sleep 0.1
done

printf 'No Rockchip USB device appeared before timeout.\n' >&2
exit 1
