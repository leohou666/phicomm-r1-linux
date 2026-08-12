#!/bin/sh
set -eu

target_dir=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ -n "${R1_FIRMWARE_MANIFEST:-}" ]; then
	"$script_dir/inject-firmware.sh" "$target_dir" "$R1_FIRMWARE_MANIFEST"
else
	echo "R1 firmware injection skipped: R1_FIRMWARE_MANIFEST is unset" >&2
fi

# These services manipulate Bluetooth and audio hardware and must not be
# writable by non-root users in the generated image.
chmod 0755 \
	"$target_dir/etc/init.d/S45bluealsa" \
	"$target_dir/etc/init.d/S50bluealsa-aplay" \
	"$target_dir/usr/bin/r1-bluetooth-pair"
