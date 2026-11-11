#!/usr/bin/env python3
"""Validate Core Pilot firmware budgets and emit release metadata."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

MIN_APP_HEADROOM = 64 * 1024


@dataclass(frozen=True)
class Partition:
    name: str
    offset: int
    size: int


def parse_number(value: str) -> int:
    return int(value.strip(), 0)


def partitions(path: Path) -> dict[str, Partition]:
    result: dict[str, Partition] = {}
    with path.open(encoding="utf-8", newline="") as source:
        rows = csv.reader(line for line in source if not line.lstrip().startswith("#"))
        for row in rows:
            if not row or len(row) < 5:
                continue
            name = row[0].strip()
            result[name] = Partition(name, parse_number(row[3]), parse_number(row[4]))
    return result


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(128 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_app(image: Path, table: dict[str, Partition]) -> dict[str, int | str]:
    app = table.get("ota_0")
    staging = table.get("ota_stage")
    if app is None or staging is None:
        raise ValueError("partition table must define ota_0 and ota_stage")
    size = image.stat().st_size
    headroom = app.size - size
    if headroom < MIN_APP_HEADROOM:
        raise ValueError(
            f"application leaves {headroom} bytes; at least {MIN_APP_HEADROOM} required"
        )
    if size > staging.size:
        raise ValueError("application does not fit OTA staging partition")
    return {
        "filename": image.name,
        "size": size,
        "sha256": sha256(image),
        "partition_size": app.size,
        "headroom": headroom,
        "minimum_headroom": MIN_APP_HEADROOM,
    }


def inspect_build(args: argparse.Namespace) -> None:
    build = Path(args.build_dir)
    table = partitions(Path(args.partitions))
    app = validate_app(build / "core_pilot.bin", table)
    flash = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    expected = {
        "0x0": "bootloader/bootloader.bin",
        "0x8000": "partition_table/partition-table.bin",
        "0xf000": "ota_data_initial.bin",
        "0x20000": "core_pilot.bin",
    }
    if flash.get("flash_files") != expected:
        raise ValueError("flasher_args.json does not match the release layout")
    print(json.dumps(app, sort_keys=True))


def package_manifest(args: argparse.Namespace) -> None:
    release = Path(args.release_dir)
    table = partitions(Path(args.partitions))
    version = args.version
    initial = release / f"core-pilot-{version}.bin"
    ota = release / f"core-pilot-{version}-ota.bin"
    app = validate_app(ota, table)
    if initial.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("merged image exceeds CoreS3 flash capacity")
    manifest = {
        "schema_version": 1,
        "version": version,
        "tag": f"v{version}",
        "source_commit": args.source_commit,
        "esp_idf_version": args.idf_version,
        "target": "esp32s3",
        "flash_size": 16 * 1024 * 1024,
        "flash_mode": "dio",
        "flash_frequency_mhz": 80,
        "application": app,
        "artifacts": [
            {
                "filename": initial.name,
                "role": "initial",
                "size": initial.stat().st_size,
                "sha256": sha256(initial),
            },
            {
                "filename": ota.name,
                "role": "ota",
                "size": ota.stat().st_size,
                "sha256": app["sha256"],
            },
        ],
        "partitions": {
            name: {"offset": part.offset, "size": part.size}
            for name, part in sorted(table.items())
        },
        "signature_verified": True,
    }
    output = release / f"core-pilot-{version}-manifest.json"
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(output)


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    subcommands = command.add_subparsers(dest="command", required=True)
    inspect = subcommands.add_parser("inspect-build")
    inspect.add_argument("--build-dir", required=True)
    inspect.add_argument("--partitions", required=True)
    inspect.set_defaults(run=inspect_build)
    package = subcommands.add_parser("package-manifest")
    package.add_argument("--release-dir", required=True)
    package.add_argument("--partitions", required=True)
    package.add_argument("--version", required=True)
    package.add_argument("--source-commit", required=True)
    package.add_argument("--idf-version", required=True)
    package.set_defaults(run=package_manifest)
    return command


def main() -> int:
    args = parser().parse_args()
    try:
        args.run(args)
    except (FileNotFoundError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"release validation failed: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
