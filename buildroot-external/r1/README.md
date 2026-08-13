# Phicomm R1 Buildroot userspace

This external tree pins its configuration to Buildroot 2026.05.1.  It builds
an ARM Cortex-A7 hard-float, musl, BusyBox-init cpio root filesystem containing
D-Bus, BlueZ 5.79, `bluetoothd`, `bluetoothctl`, BlueALSA 4.3.1,
`bluealsa-aplay`, alsa-lib and alsa-utils.  BlueALSA is started only as an A2DP
Sink and only its mandatory SBC codec is present.  PipeWire, LDAC, AAC, aptX,
Opus, HFP and oFono are outside this first stage.

The cpio backend creates `/init`; it mounts devtmpfs and executes
`/sbin/init`.  It is therefore compatible with the current kernel command line
`rdinit=/init`.  The empty root password and permanent discoverability are for
RAM-only bring-up, not a production image.

Build from the repository root:

```sh
scripts/build-r1-bluealsa-rootfs.sh
```

The build uses 16 jobs by default. Override this only when needed, for example
`JOBS=8 scripts/build-r1-bluealsa-rootfs.sh`.

The outputs are:

```text
build/buildroot-r1-bluealsa-6.18/images/rootfs.cpio.gz
build/buildroot-r1-bluealsa-6.18/legal-info/manifest.csv
```

To inspect the resolved dependency configuration without downloading or
building packages:

```sh
scripts/build-r1-bluealsa-rootfs.sh --config-only
```

The normal build also refreshes the package license/source manifest.  To
refresh only that evidence directory manually:

```sh
make -C build/buildroot-2026.05.1-src \
  O="$PWD/build/buildroot-r1-bluealsa-6.18" \
  BR2_EXTERNAL="$PWD/buildroot-external/r1" legal-info
```

## Proprietary firmware boundary

No CYW43455, board NVRAM, Bluetooth HCD or AK7755 data2 firmware is stored in
this tree.  To inject locally preserved files, copy `firmware.manifest.example`
outside the repository, add exact SHA-256/source/destination entries and run:

```sh
R1_FIRMWARE_MANIFEST=/absolute/path/to/r1-firmware.manifest \
  scripts/build-r1-bluealsa-rootfs.sh
```

The post-build hook accepts only nine explicit destinations, verifies every
input hash before copying it, and never modifies its source files.

## RAM-only FIT integration

Pack the generated `rootfs.cpio.gz` with the A24 zImage and DTB using the same
FIT layout as A23: kernel load/entry `0x62000000`, embedded gzip ramdisk and
embedded DTB.  Keep the FIT staging address at `0x6a800000`.

The old 16 MiB DFU alternate is too small once this userspace is included.
Use a 64 MiB RAM alternate for this image:

```text
setenv dfu_alt_info 'linux-fit ram 0x6a800000 0x04000000'
dfu 0 ram 0
iminfo 0x6a800000
bootm 0x6a800000#config-1
```

`0x6a800000..0x6e7fffff` is above the OP-TEE reservation ending at
`0x686fffff`, below the top of the 480 MiB U-Boot-visible RAM, and distinct
from the kernel load address.  U-Boot already has `CONFIG_SYS_BOOTM_LEN` set to
64 MiB.  The alternate size is a transfer ceiling; it does not allocate or
send 64 MiB.  Always reject a generated FIT larger than `0x04000000` bytes.

## First boot

The service order is D-Bus (`S30`), `bluetoothd` (`S40`) and the R1 audio-stack
supervisor (`S45`).  The supervisor waits for both dependencies, owns exactly
one BlueALSA and one `bluealsa-aplay`, and restarts the ALSA consumer if it
exits.  Run the persistent pairing agent on the UART console:

```sh
r1-bluetooth-pair
```

After pairing and trusting a phone, its A2DP stream is converted to 48 kHz,
stereo S16_LE and sent to ALSA PCM `hw:0,0` through `r1-output`.  The A24 kernel
must own amplifier sequencing; these services intentionally do not touch the
diagnostic misc gate or GPIOs.
