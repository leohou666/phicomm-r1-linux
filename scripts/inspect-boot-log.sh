#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 boot.log" >&2
    exit 2
fi

LOG="$1"

grep -inE \
  'U-Boot|Rockchip|DDR|DRAM|mmc|emmc|sdio|brcm|wifi|wlan|bluetooth|hci|uart|i2s|asoc|ak7755|codec|dsp|error|fail|timeout' \
  "$LOG" || true
