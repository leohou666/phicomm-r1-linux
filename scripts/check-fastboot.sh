#!/bin/sh
set -eu

if ! command -v fastboot >/dev/null 2>&1; then
	printf 'fastboot was not found in PATH.\n' >&2
	exit 1
fi

device_list=$(fastboot devices | awk 'NF >= 2 { print $1 }')
device_count=$(printf '%s\n' "$device_list" | awk 'NF { count++ } END { print count + 0 }')

if [ "$device_count" -eq 0 ]; then
	printf 'No Fastboot device detected.\n' >&2
	exit 1
fi

if [ -n "${FASTBOOT_SERIAL:-}" ]; then
	fastboot_serial=$FASTBOOT_SERIAL
elif [ "$device_count" -eq 1 ]; then
	fastboot_serial=$device_list
else
	printf 'Multiple Fastboot devices detected; set FASTBOOT_SERIAL explicitly.\n' >&2
	printf '%s\n' "$device_list" >&2
	exit 1
fi

printf 'Querying Fastboot properties without downloading or flashing anything.\n'

query()
{
	fastboot -s "$fastboot_serial" getvar "$1" 2>&1 || true
}

query product
query version-bootloader
query secure
query unlocked
query max-download-size
