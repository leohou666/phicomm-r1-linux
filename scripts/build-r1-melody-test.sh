#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-arm-none-eabi-gcc}
output=${1:-"$project_root/build/artifacts/r1-melody-test"}

mkdir -p "$(dirname -- "$output")"
"$cc" -DR1_MELODY_TEST -Os -Wall -Wextra -Werror -ffreestanding \
	-fno-builtin -nostdlib -static -Wl,--build-id=none \
	-Wl,-z,noexecstack -Wl,-e,_start -o "$output" \
	"$project_root/tools/r1-audible-test.c"

file "$output"
readelf -W -l "$output" | grep GNU_STACK
printf 'R1 conservative synthesized melody written to %s\n' "$output"
