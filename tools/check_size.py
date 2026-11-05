#!/usr/bin/env python3
"""Fail the build if an image is too big for the flash layout.

Usage: check_size.py <build-dir> [--partitions firmware/partitions_8mb.csv]

Checks two limits:
- the app image fills at most MAX_SLOT_FILL of its OTA slot, which leaves room
  for later releases without a partition change that OTA cannot deliver
- the bootloader ends before the partition table, including the signature
  block that Secure Boot v2 adds when signing happens after the build
"""
import argparse
import json
import sys
from pathlib import Path

MAX_SLOT_FILL = 0.85
SIGNATURE_SECTOR = 0x1000


class SizeError(Exception):
    pass


def parse_partitions(text):
    """Returns {name: (type, subtype, offset, size)}. N/A rows are placed by IDF and are left out."""
    table = {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        name, kind, subtype, offset, size = fields[:5]
        if "N/A" in (offset, size):
            continue
        table[name] = (kind, subtype, int(offset, 0), int(size, 0))
    return table


def signed_size(size):
    """Size after padding to a sector and appending the signature sector."""
    padded = (size + SIGNATURE_SECTOR - 1) // SIGNATURE_SECTOR * SIGNATURE_SECTOR
    return padded + SIGNATURE_SECTOR


def check_app(app_size, slot_size):
    limit = int(slot_size * MAX_SLOT_FILL)
    if app_size > limit:
        raise SizeError(f"app is {app_size:#x} bytes, over the {limit:#x} limit ({MAX_SLOT_FILL:.0%} of slot {slot_size:#x})")
    return app_size / slot_size


def check_bootloader(bootloader_size, bootloader_offset, table_offset, needs_signature):
    size = signed_size(bootloader_size) if needs_signature else bootloader_size
    room = table_offset - bootloader_offset
    if size > room:
        raise SizeError(f"bootloader is {size:#x} bytes but only {room:#x} fit between {bootloader_offset:#x} and {table_offset:#x}")
    return size, room


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--partitions", type=Path, default=Path(__file__).resolve().parent.parent / "firmware" / "partitions_8mb.csv")
    args = parser.parse_args()

    description = json.loads((args.build_dir / "project_description.json").read_text())
    config = json.loads((args.build_dir / "config" / "sdkconfig.json").read_text())
    table = parse_partitions(args.partitions.read_text())

    app = args.build_dir / description["app_bin"]
    bootloader = args.build_dir / "bootloader" / "bootloader.bin"
    needs_signature = config.get("SECURE_BOOT", False) and not config.get("SECURE_BOOT_BUILD_SIGNED_BINARIES", False)
    app_size = app.stat().st_size
    if needs_signature:
        app_size = signed_size(app_size)

    try:
        fill = check_app(app_size, min(table["ota_0"][3], table["ota_1"][3]))
        boot_size, boot_room = check_bootloader(
            bootloader.stat().st_size,
            config["BOOTLOADER_OFFSET_IN_FLASH"],
            int(config["PARTITION_TABLE_OFFSET"]),
            needs_signature,
        )
    except SizeError as error:
        sys.exit(f"size check failed: {error}")
    print(f"app: {app_size} bytes, {fill:.1%} of its slot (limit {MAX_SLOT_FILL:.0%})")
    print(f"bootloader: {boot_size:#x} of {boot_room:#x} bytes")


if __name__ == "__main__":
    main()
