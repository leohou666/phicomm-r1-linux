#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-arm-none-eabi-gcc}
output=${1:-"$project_root/build/artifacts/r1-pcm-clock-test"}

mkdir -p "$(dirname -- "$output")"
"$cc" -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-nostdlib -static -Wl,--build-id=none -Wl,-z,noexecstack \
	-Wl,-e,_start -o "$output" "$project_root/tools/r1-pcm-clock-test.c"

file "$output"
readelf -W -l "$output" | grep GNU_STACK
printf 'R1 PCM clock test written to %s\n' "$output"
