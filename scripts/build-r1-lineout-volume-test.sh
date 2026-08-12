#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-arm-none-eabi-gcc}
output=${1:-"$project_root/build/artifacts/r1-lineout-volume-ab"}

mkdir -p "$(dirname -- "$output")"
"$cc" -DR1_LINEOUT_VOLUME_TEST -Os -Wall -Wextra -Werror -ffreestanding \
	-fno-builtin -nostdlib -static -Wl,--build-id=none \
	-Wl,-z,noexecstack -Wl,-e,_start -o "$output" \
	"$project_root/tools/r1-audible-test.c"

file "$output"
readelf -W -l "$output" | grep GNU_STACK
if readelf -W -s "$output" | grep -q 'GLOBAL.* UND '; then
	printf 'Lineout volume A/B has unexpected undefined runtime symbols: %s\n' \
		"$output" >&2
	exit 1
fi
printf 'R1 AK7755 Lineout1 volume A/B written to %s\n' "$output"
