#!/usr/bin/env python3
"""Add or verify the legacy Rockchip boot-image SHA fields used by R1."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


BOOT_MAGIC = b"ANDROID!"
HEADER_FIXED_SIZE = 0x400
ID_OFFSET = 0x240
ID_SIZE = 32
SHA_FLAG_OFFSET = 0x26C
SHA_OFFSET = 0x270
SHA_STORAGE_SIZE = 64


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def calculate_hashes(image: bytes) -> tuple[bytes, bytes]:
    if len(image) < HEADER_FIXED_SIZE or image[:8] != BOOT_MAGIC:
        raise ValueError("not an Android boot image")

    (
        kernel_size,
        _kernel_addr,
        ramdisk_size,
        _ramdisk_addr,
        second_size,
        _second_addr,
        tags_addr,
        page_size,
        unused0,
        unused1,
    ) = struct.unpack_from("<10I", image, 8)

    if page_size < HEADER_FIXED_SIZE or page_size & (page_size - 1):
        raise ValueError(f"invalid page size: {page_size}")

    kernel_offset = page_size
    ramdisk_offset = align(kernel_offset + kernel_size, page_size)
    second_offset = align(ramdisk_offset + ramdisk_size, page_size)
    logical_end = second_offset + second_size
    if logical_end > len(image):
        raise ValueError("boot image components extend beyond the input file")

    kernel = image[kernel_offset : kernel_offset + kernel_size]
    ramdisk = image[ramdisk_offset : ramdisk_offset + ramdisk_size]
    second = image[second_offset : second_offset + second_size]
    name = image[48:64]
    cmdline = image[64:576]

    chunks = (
        kernel,
        struct.pack("<I", kernel_size),
        ramdisk,
        struct.pack("<I", ramdisk_size),
        second,
        struct.pack("<I", second_size),
        struct.pack("<I", tags_addr),
        struct.pack("<I", page_size),
        struct.pack("<II", unused0, unused1),
        name,
        cmdline,
    )

    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    for chunk in chunks:
        sha1.update(chunk)
        sha256.update(chunk)
    return sha1.digest(), sha256.digest()


def verify(image: bytes, path: Path) -> None:
    expected_sha1, expected_sha256 = calculate_hashes(image)
    stored_sha1 = image[ID_OFFSET : ID_OFFSET + len(expected_sha1)]
    sha_flag = struct.unpack_from("<I", image, SHA_FLAG_OFFSET)[0]
    stored_sha256 = image[SHA_OFFSET : SHA_OFFSET + len(expected_sha256)]

    if stored_sha1 != expected_sha1:
        raise ValueError(
            f"{path}: Rockchip SHA-1 mismatch: stored {stored_sha1.hex()}, "
            f"calculated {expected_sha1.hex()}"
        )
    if sha_flag != 256:
        raise ValueError(f"{path}: expected SHA flag 256, found {sha_flag}")
    if stored_sha256 != expected_sha256:
        raise ValueError(
            f"{path}: Rockchip SHA-256 mismatch: stored {stored_sha256.hex()}, "
            f"calculated {expected_sha256.hex()}"
        )

    print(f"{path}: Rockchip SHA-1   {expected_sha1.hex()}")
    print(f"{path}: Rockchip SHA-256 {expected_sha256.hex()}")


def patch(image: bytes) -> bytes:
    sha1, sha256 = calculate_hashes(image)
    output = bytearray(image)
    output[ID_OFFSET : ID_OFFSET + ID_SIZE] = b"\0" * ID_SIZE
    output[ID_OFFSET : ID_OFFSET + len(sha1)] = sha1
    output[0x260:SHA_FLAG_OFFSET] = b"\0" * (SHA_FLAG_OFFSET - 0x260)
    struct.pack_into("<I", output, SHA_FLAG_OFFSET, 256)
    output[SHA_OFFSET : SHA_OFFSET + SHA_STORAGE_SIZE] = b"\0" * SHA_STORAGE_SIZE
    output[SHA_OFFSET : SHA_OFFSET + len(sha256)] = sha256
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument(
        "--verify",
        action="store_true",
        help="verify existing Rockchip hashes instead of updating the image",
    )
    args = parser.parse_args()

    image = args.image.read_bytes()
    if args.verify:
        verify(image, args.image)
        return

    args.image.write_bytes(patch(image))
    verify(args.image.read_bytes(), args.image)


if __name__ == "__main__":
    main()
