#!/bin/sh
# Generate clangd compilation databases from existing Linux Kbuild .cmd files.
# This does not compile the kernel or touch device artifacts.
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
selection=${1:-all}
cross_ar=${AR:-arm-none-eabi-ar}

generate_database() {
	source_dir=$1
	build_dir=$2
	label=$3

	if [ ! -x "$source_dir/scripts/clang-tools/gen_compile_commands.py" ]; then
		printf '%s source tree does not support Kbuild compile databases: %s\n' \
			"$label" "$source_dir" >&2
		return 1
	fi
	if [ ! -d "$build_dir" ]; then
		printf '%s build directory does not exist: %s\n' "$label" "$build_dir" >&2
		return 1
	fi

	python3 "$source_dir/scripts/clang-tools/gen_compile_commands.py" \
		-d "$build_dir" \
		-o "$build_dir/compile_commands.json" \
		-a "$cross_ar" \
		"$build_dir"
	printf '%s clangd database: %s\n' "$label" "$build_dir/compile_commands.json"
}

case "$selection" in
	6.18)
		generate_database "$project_root/build/kernel-src" "$project_root/build/kernel" "Linux 6.18"
		;;
	5.10)
		generate_database "$project_root/build/kernel-src-5.10" "$project_root/build/kernel-5.10" "Linux 5.10"
		;;
	all)
		generate_database "$project_root/build/kernel-src" "$project_root/build/kernel" "Linux 6.18"
		generate_database "$project_root/build/kernel-src-5.10" "$project_root/build/kernel-5.10" "Linux 5.10"
		;;
	*)
		printf 'Usage: %s [6.18|5.10|all]\n' "$0" >&2
		exit 2
		;;
esac
