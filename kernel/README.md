# Mainline kernel

The first mainline milestone is deliberately small: boot to a serial shell,
report the correct RAM size, and enumerate the on-board eMMC without changing
any device storage.

The project tracks the Linux 6.18 longterm series. Kernel source and build
outputs live under `build/` and are not committed. Source preparation uses an
independent shallow Git repository and does not modify the user's existing
Linux working trees.

Prepare the pinned stable source:

```sh
scripts/prepare-kernel-source.sh
```

Build the ARM zImage and the external R1 DTB:

```sh
scripts/build-kernel.sh
```

Build the minimal rescue initramfs:

```sh
scripts/build-initramfs.sh
```

By default the initramfs builder uses the statically linked 32-bit ARM BusyBox
extracted from the local recovery backup. It is copied only into `build/` and
is not distributed as project source. To use a separately built BusyBox:

```sh
BUSYBOX=/path/to/static-arm-busybox scripts/build-initramfs.sh
```

Package the three components in the Android boot format understood by the
vendor Rockchip `bootrk` command:

```sh
scripts/build-boot-image.sh
```

The bundled Rockchip resource tool is a 32-bit x86 executable. If it cannot
run on the host, set `RESOURCE_TOOL` to a compatible build of the same tool.

Environment overrides:

```text
KERNEL_VERSION   Stable version, default: 6.18.42
KERNEL_SRC       Prepared source tree, default: build/kernel-src
KERNEL_BUILD     Out-of-tree build directory, default: build/kernel
CROSS_COMPILE    Cross-tool prefix, default: arm-none-eabi-
```

Expected outputs:

```text
build/artifacts/zImage
build/artifacts/rk3229-phicomm-r1.dtb
build/artifacts/kernel.config
build/artifacts/r1-initramfs.cpio.gz
build/artifacts/r1-resource.img
build/artifacts/r1-mainline-boot.img
build/artifacts/r1-mainline-recovery.img
```

The recovery image is exactly 32 MiB. Its Android header loads the kernel at
`0x60408000`, initramfs at `0x64000000`, and Rockchip resource image at
`0x66000000`; these ranges avoid the linked kernel end at `0x620dfc10` and the
Trust OS reservation at `0x68400000`. These files are build artifacts only.
Do not write them to eMMC until the recovery and rollback commands have been
reviewed and explicitly authorized.
