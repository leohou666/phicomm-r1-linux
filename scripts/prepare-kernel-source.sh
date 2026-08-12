#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kernel_version=${KERNEL_VERSION:-6.18.42}
kernel_src=${KERNEL_SRC:-"$project_root/build/kernel-src"}
stable_url=${KERNEL_STABLE_URL:-https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git}
patch_dir="$project_root/patches/linux-$kernel_version"
overlay_dir="$project_root/kernel/overlays/linux-$kernel_version"

mkdir -p "$(dirname -- "$kernel_src")"
git init "$kernel_src" >/dev/null

# Do not borrow objects from another repository here. A partial or damaged
# object cache can make a shallow fetch incorrectly omit required objects.
if [ -f "$kernel_src/.git/objects/info/alternates" ]; then
	mv "$kernel_src/.git/objects/info/alternates" \
		"$kernel_src/.git/objects/info/alternates.disabled"
fi

git -C "$kernel_src" remote remove stable 2>/dev/null || true
git -C "$kernel_src" remote add stable "$stable_url"
git -C "$kernel_src" fetch --depth=1 --no-tags stable \
	"refs/tags/v$kernel_version:refs/tags/v$kernel_version"
git -C "$kernel_src" checkout --detach "v$kernel_version"

if [ -d "$overlay_dir" ]; then
	find "$overlay_dir" -type f -print | while IFS= read -r source; do
		relative=${source#"$overlay_dir"/}
		mkdir -p "$kernel_src/$(dirname -- "$relative")"
		install -m 0644 "$source" "$kernel_src/$relative"
	done
fi

for patch in "$patch_dir"/*.patch; do
	[ -f "$patch" ] || continue
	git -C "$kernel_src" apply "$patch"
done

printf 'Prepared Linux %s in %s\n' "$kernel_version" "$kernel_src"
