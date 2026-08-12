#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-arm-none-eabi-gcc}
output=${1:-"$project_root/build/artifacts/r1-lineout-selfcheck"}

mkdir -p "$(dirname -- "$output")"
"$cc" -DR1_LINEOUT_SELFCHECK_TEST -Os -Wall -Wextra -Werror -ffreestanding \
	-fno-builtin -nostdlib -static -Wl,--build-id=none \
	-Wl,-z,noexecstack -Wl,-e,_start -o "$output" \
	"$project_root/tools/r1-audible-test.c"

file "$output"
readelf -W -l "$output" | grep GNU_STACK
if readelf -W -s "$output" | grep -q 'GLOBAL.* UND '; then
	printf 'Lineout D4 self-check has unexpected undefined runtime symbols: %s\n' \
		"$output" >&2
	exit 1
fi
printf 'R1 AK7755 Lineout1 D4 self-check written to %s\n' "$output"
