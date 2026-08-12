#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-arm-none-eabi-gcc}
output=${1:-"$project_root/build/artifacts/r1-music-test"}

mkdir -p "$(dirname -- "$output")"
"$cc" -DR1_MUSIC_TEST -Os -Wall -Wextra -Werror -ffreestanding \
	-fno-builtin -nostdlib -static -Wl,--build-id=none \
	-Wl,-z,noexecstack -Wl,-e,_start -o "$output" \
	"$project_root/tools/r1-audible-test.c"

file "$output"
readelf -W -l "$output" | grep GNU_STACK
if readelf -W -s "$output" | grep -q 'GLOBAL.* UND '; then
	printf 'Music test has unexpected undefined runtime symbols: %s\n' "$output" >&2
	exit 1
fi
printf 'R1 controlled public-domain music test written to %s\n' "$output"
