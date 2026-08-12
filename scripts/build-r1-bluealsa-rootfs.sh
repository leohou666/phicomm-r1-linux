#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=2026.05.1
archive="$repo_root/build/buildroot-$version.tar.xz"
source_dir="$repo_root/build/buildroot-$version-src"
output_dir="$repo_root/build/buildroot-r1-bluealsa-6.18"
external_dir="$repo_root/buildroot-external/r1"
expected_sha=ae7f706f087b9ae9083a10a587368dfbf53103c28bf81c2d690198dc4090cb58
config_only=0
jobs=${JOBS:-16}

case "${1:-}" in
	'') ;;
	--config-only) config_only=1 ;;
	*) echo "usage: $0 [--config-only]" >&2; exit 2 ;;
esac

case "$jobs" in
	''|*[!0-9]*|0) echo "JOBS must be a positive integer" >&2; exit 2 ;;
esac

mkdir -p "$repo_root/build"

if [ ! -f "$archive" ]; then
	echo "Downloading Buildroot $version from the official release archive..."
	curl --fail --location --output "$archive.tmp" \
		"https://buildroot.org/downloads/buildroot-$version.tar.xz"
	mv "$archive.tmp" "$archive"
fi

actual_sha=$(sha256sum "$archive" | awk '{print $1}')
if [ "$actual_sha" != "$expected_sha" ]; then
	echo "Buildroot archive SHA-256 mismatch:" >&2
	echo "  expected $expected_sha" >&2
	echo "  actual   $actual_sha" >&2
	exit 1
fi

if [ ! -f "$source_dir/Makefile" ]; then
	mkdir -p "$source_dir"
	tar -xf "$archive" -C "$source_dir" --strip-components=1
fi

make -C "$source_dir" O="$output_dir" BR2_EXTERNAL="$external_dir" \
	phicomm_r1_bluealsa_defconfig
make -C "$source_dir" O="$output_dir" BR2_EXTERNAL="$external_dir" \
	olddefconfig

if [ "$config_only" -eq 1 ]; then
	echo "Resolved Buildroot configuration: $output_dir/.config"
	exit 0
fi

make -j "$jobs" -C "$source_dir" O="$output_dir" BR2_EXTERNAL="$external_dir"
make -j "$jobs" -C "$source_dir" O="$output_dir" BR2_EXTERNAL="$external_dir" \
	legal-info

rootfs="$output_dir/images/rootfs.cpio.gz"
[ -f "$rootfs" ] || { echo "missing Buildroot cpio: $rootfs" >&2; exit 1; }
sha256sum "$rootfs"
echo "Buildroot R1 rootfs ready: $rootfs"
