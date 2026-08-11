#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cross_compile=${CROSS_COMPILE:-arm-none-eabi-}
output="$project_root/build/artifacts/r1-nl80211-scan"

mkdir -p "$project_root/build/artifacts"
"${cross_compile}gcc" \
	-Os -marm -march=armv7-a -mfloat-abi=soft \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-nostdlib -static \
	-Wl,--build-id=none -Wl,-e,_start -Wl,--gc-sections \
	-Wl,-z,noexecstack \
	-o "$output" "$project_root/tools/r1-nl80211-scan.c"

file "$output"
"${cross_compile}readelf" -W -l "$output" | grep GNU_STACK
"${cross_compile}nm" -u "$output"
sha256sum "$output"
