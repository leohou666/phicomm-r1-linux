#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 TARGET_DIR MANIFEST" >&2
	exit 2
fi

target_dir=$1
manifest=$2

[ -d "$target_dir" ] || { echo "not a target directory: $target_dir" >&2; exit 2; }
[ -f "$manifest" ] || { echo "firmware manifest not found: $manifest" >&2; exit 2; }

allowed_destination() {
	case "$1" in
		brcm/brcmfmac43455-sdio.bin|\
		brcm/brcmfmac43455-sdio.phicomm,r1.txt|\
		brcm/brcmfmac43455-sdio.clm_blob|\
		brcm/BCM4345C0.hcd|\
		regulatory.db|\
		regulatory.db.p7s|\
		ak7755_pram_data2.bin|\
		ak7755_cram_data2.bin|\
		ak7755_ofreg_data2.bin) return 0 ;;
		*) return 1 ;;
	esac
}

while read -r expected destination source extra; do
	case "$expected" in ''|'#'*) continue ;; esac
	[ -z "${extra:-}" ] || { echo "too many fields in firmware manifest" >&2; exit 2; }
	echo "$expected" | grep -Eq '^[0-9a-f]{64}$' || {
		echo "invalid SHA-256 for $destination" >&2
		exit 2
	}
	allowed_destination "$destination" || {
		echo "refusing unlisted firmware destination: $destination" >&2
		exit 2
	}
	[ -f "$source" ] && [ ! -L "$source" ] || {
		echo "firmware source is not a regular non-symlink file: $source" >&2
		exit 2
	}
	actual=$(sha256sum "$source" | awk '{print $1}')
	[ "$actual" = "$expected" ] || {
		echo "firmware SHA-256 mismatch: $source" >&2
		exit 2
	}
	install -D -m 0644 "$source" "$target_dir/lib/firmware/$destination"
	printf 'Injected verified firmware: %s\n' "$destination"
done < "$manifest"
