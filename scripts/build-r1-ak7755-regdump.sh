#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-arm-none-eabi-gcc}
output=${1:-"$project_root/build/artifacts/r1-ak7755-regdump"}

mkdir -p "$(dirname -- "$output")"
"$cc" -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -nostdlib \
	-static -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-e,_start \
	-o "$output" "$project_root/tools/r1-ak7755-regdump.c"

file "$output"
readelf -W -l "$output" | grep GNU_STACK
if readelf -W -s "$output" | grep -q 'GLOBAL.* UND '; then
	printf 'AK7755 regdump has unexpected undefined runtime symbols: %s\n' \
		"$output" >&2
	exit 1
fi
printf 'Read-only R1 AK7755 register dump written to %s\n' "$output"
