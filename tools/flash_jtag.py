#!/usr/bin/env python3
"""Flash and verify Core Pilot through the CoreS3 built-in USB-JTAG adapter."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"
CORE_S3_ADAPTER_SERIAL = "80:45:6B:4D:54:94"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", nargs="?", default="build")
    parser.add_argument(
        "--adapter-serial",
        default=CORE_S3_ADAPTER_SERIAL,
        help="CoreS3 USB-JTAG serial; never use the Nesso adapter",
    )
    args = parser.parse_args()

    openocd = shutil.which("openocd")
    if openocd is None:
        print("openocd is required; activate ESP-IDF with EIM", file=sys.stderr)
        return 1
    build = (FIRMWARE / args.build_directory).resolve()
    images = (
        (build / "bootloader" / "bootloader.bin", "0x0"),
        (build / "partition_table" / "partition-table.bin", "0x8000"),
        (build / "ota_data_initial.bin", "0xf000"),
        (build / "core_pilot.bin", "0x20000"),
    )
    missing = [str(path) for path, _ in images if not path.is_file()]
    if missing:
        for path in missing:
            print(f"missing {path}; build the firmware first", file=sys.stderr)
        return 1
    operations = "; ".join(
        f"program_esp {path} {offset} verify" for path, offset in images
    )
    command = [
        openocd,
        "-c",
        f"adapter serial {args.adapter_serial}",
        "-f",
        "board/esp32s3-builtin.cfg",
        "-c",
        f"init; reset halt; {operations}; reset run; shutdown",
    ]
    return subprocess.run(command, cwd=FIRMWARE, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
