#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1786464000}
export SOURCE_DATE_EPOCH
kernel="$project_root/backup/unpacked/boot/kernel"
dtb="$project_root/build/artifacts/rk3229-phicomm-r1-factory-open-optee-audio-only-a21r3.dtb"
zero="$project_root/build/artifacts/r1-pcm-clock-test"
tone="$project_root/build/artifacts/r1-factory-tone-test"
route="$project_root/build/artifacts/r1-factory-ak7755-route"
ramdisk="$project_root/build/artifacts/r1-initramfs-factory-3.10-ak7755-route-a21r5.cpio.gz"
fit="$project_root/build/artifacts/r1-linux-factory-3.10-ak7755-route-a21r5.itb"
its="$project_root/scripts/r1-linux-factory-3.10-ak7755-route-a21r5.its"

check_sha256() {
	path=$1
	expected=$2
	actual=$(sha256sum "$path" | awk '{print $1}')
	[ "$actual" = "$expected" ] || {
		printf 'Refusing unexpected input: %s\nexpected=%s\nactual=%s\n' \
			"$path" "$expected" "$actual" >&2
		exit 1
	}
}

for tool in mkimage dumpimage sha256sum cmp gzip cpio; do
	command -v "$tool" >/dev/null 2>&1 || {
		printf 'Missing required tool: %s\n' "$tool" >&2
		exit 1
	}
done

"$project_root/scripts/build-r1-factory-ak7755-route.sh" "$route"
"$project_root/scripts/build-r1-factory-tone-test.sh" "$tone"

check_sha256 "$kernel" 9ae541809bf9f05ae00145876814fbc4d049e19801bf15a23c6a579b0d5d40a8
check_sha256 "$dtb" 3651c63aae60a20dddd702c0ed38dfd589ec24c1e09c5d31407ef15460b4d519
check_sha256 "$zero" f36d959d82dab252a7ad9d1e415b015e77b6b3eb37256e6cfc9bc50028b4cd91
check_sha256 "$tone" 95ded47c48d8325bae9083a3bc5b9f905e1b00d3ed56937f6b2f08e135afa373
check_sha256 "$route" 388dc260c86230b16cf20b82f5c1d51823df1917066a8f9d78dc0ce06b73c65b

R1_PCM_CLOCK_TEST_TOOL="$zero" \
R1_FACTORY_TONE_TEST_TOOL="$tone" \
R1_FACTORY_AK7755_ROUTE_TOOL="$route" \
R1_AK7755_FIRMWARE=1 \
R1_AK7755_OFREG_FIRMWARE=1 \
INITRAMFS_ARTIFACT_TAG=factory-3.10-ak7755-route-a21r5 \
	"$project_root/scripts/build-initramfs.sh"

workdir=$(mktemp -d "${TMPDIR:-/tmp}/r1-factory-route-a21r5.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

(cd "$project_root/scripts" && mkimage -f "$(basename "$its")" "$fit")
dumpimage -T flat_dt -p 0 -o "$workdir/kernel" "$fit" >/dev/null
dumpimage -T flat_dt -p 1 -o "$workdir/ramdisk" "$fit" >/dev/null
dumpimage -T flat_dt -p 2 -o "$workdir/fdt" "$fit" >/dev/null
cmp "$kernel" "$workdir/kernel"
cmp "$ramdisk" "$workdir/ramdisk"
cmp "$dtb" "$workdir/fdt"

gzip -dc "$ramdisk" | (cd "$workdir" && cpio -id --quiet)
cmp "$zero" "$workdir/bin/r1-pcm-clock-test"
cmp "$tone" "$workdir/bin/r1-factory-tone-test"
cmp "$route" "$workdir/bin/r1-factory-ak7755-route"
cmp "$project_root/backup/extracted/system/vendor/firmware/ak7755_ofreg_data2.bin" \
	"$workdir/lib/firmware/ak7755_ofreg_data2.bin"

printf '\nA21r5 factory Android-route positive-control FIT verified.\n'
printf 'Safety: RAM-only, maxcpus=1, external PA remains machine-driver gated.\n'
sha256sum "$route" "$ramdisk" "$fit"
