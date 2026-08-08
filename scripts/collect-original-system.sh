#!/bin/sh
set -eu

OUT="${1:-/data/local/tmp/r1-dump}"
mkdir -p "$OUT"

run_capture() {
    name="$1"
    shift
    "$@" > "$OUT/$name" 2>&1 || true
}

run_capture uname.txt uname -a
run_capture cmdline.txt cat /proc/cmdline
run_capture cpuinfo.txt cat /proc/cpuinfo
run_capture partitions.txt cat /proc/partitions
run_capture mounts.txt cat /proc/mounts
run_capture interrupts.txt cat /proc/interrupts
run_capture iomem.txt cat /proc/iomem
run_capture dmesg.txt dmesg
run_capture getprop.txt getprop
run_capture lsmod.txt lsmod
run_capture block-by-name.txt ls -l /dev/block/by-name
run_capture tty.txt ls -l /sys/class/tty
run_capture mmc.txt ls -l /sys/class/mmc_host

if [ -r /proc/config.gz ]; then
    cp /proc/config.gz "$OUT/config.gz"
fi

if [ -r /sys/firmware/fdt ]; then
    cat /sys/firmware/fdt > "$OUT/original.dtb"
fi

find /system /vendor /odm /etc /lib -type f \
  \( -iname '*43455*' -o -iname '*4345*' -o -iname '*brcm*' \
     -o -iname '*nvram*' -o -iname '*.clm_blob' -o -iname '*.hcd' \
     -o -iname '*7755*' -o -iname '*akm*' -o -iname '*mixer*' \) \
  2>/dev/null > "$OUT/relevant-files.txt" || true

printf 'Collected into %s\n' "$OUT"
