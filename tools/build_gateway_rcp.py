#!/usr/bin/env python3
"""Build the pinned ESP32-H2 RCP and generate CoreS3 embedding sources."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "firmware" / "h2"
EXPECTED_IDF = "6.0.2"
SEGMENTS = (
    ("bootloader", 0x0000, "bootloader/bootloader.bin"),
    ("partition_table", 0x8000, "partition_table/partition-table.bin"),
    ("application", 0x10000, "esp_ot_rcp.bin"),
)


def recipe_digest() -> bytes:
    inputs = [
        PROJECT / "CMakeLists.txt",
        PROJECT / "dependencies.lock",
        PROJECT / "partitions.csv",
        PROJECT / "sdkconfig.defaults",
        Path(__file__),
        *(path for path in (PROJECT / "main").rglob("*") if path.is_file()),
    ]
    digest = hashlib.sha256()
    digest.update(f"core-pilot-gateway-rcp\0idf-{EXPECTED_IDF}\0".encode())
    for path in sorted(inputs):
        digest.update(path.relative_to(ROOT).as_posix().encode() + b"\0")
        digest.update(path.read_bytes())
    return digest.digest()


def c_bytes(data: bytes) -> str:
    return ", ".join(f"0x{value:02x}" for value in data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    if os.environ.get("IDF_VERSION") != EXPECTED_IDF:
        print(
            f"gateway RCP requires IDF {EXPECTED_IDF}; IDF_VERSION={os.environ.get('IDF_VERSION')!r}",
            file=sys.stderr,
        )
        return 2

    build_dir = args.build_dir.resolve()
    output_dir = args.output_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)
    marker = recipe_digest()
    command = [
        "idf.py",
        "-C",
        str(PROJECT),
        "-B",
        str(build_dir),
        "-D",
        "IDF_TARGET=esp32h2",
        "-D",
        f"GATEWAY_RCP_MANIFEST_HEX={marker.hex()}",
        "build",
    ]
    result = subprocess.run(command, cwd=ROOT, check=False)
    if result.returncode:
        return result.returncode

    records: list[dict[str, object]] = []
    source_lines = [
        '#include "gateway_rcp_payload.hpp"',
        "",
        "namespace core_pilot::gateway::payload {",
        "",
    ]
    declarations: list[str] = []
    for name, address, relative in SEGMENTS:
        path = build_dir / relative
        if not path.is_file() or path.stat().st_size == 0:
            print(f"missing gateway RCP artifact: {path}", file=sys.stderr)
            return 3
        data = path.read_bytes()
        symbol = f"k{name.title().replace('_', '')}"
        md5_hex = hashlib.md5(data, usedforsecurity=False).hexdigest().encode() + b"\0"
        source_lines.extend(
            [
                f"alignas(4) const std::uint8_t {symbol}[] = {{{c_bytes(data)}}};",
                f"const std::size_t {symbol}Size = sizeof({symbol});",
                f"const std::uint8_t {symbol}Md5Hex[33] = {{{c_bytes(md5_hex)}}};",
                "",
            ]
        )
        declarations.extend(
            [
                f"extern const std::uint8_t {symbol}[];",
                f"extern const std::size_t {symbol}Size;",
                f"extern const std::uint8_t {symbol}Md5Hex[33];",
            ]
        )
        records.append(
            {
                "name": name,
                "address": address,
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    source_lines.extend(
        [
            f"const std::uint8_t kManifestDigest[32] = {{{c_bytes(marker)}}};",
            "",
            "} // namespace core_pilot::gateway::payload",
            "",
        ]
    )
    header = (
        """#pragma once
#include <cstddef>
#include <cstdint>
namespace core_pilot::gateway::payload {
"""
        + "\n".join(declarations)
        + "\nextern const std::uint8_t kManifestDigest[32];\n} // namespace core_pilot::gateway::payload\n"
    )
    (output_dir / "gateway_rcp_payload.hpp").write_text(header)
    (output_dir / "gateway_rcp_payload.cpp").write_text("\n".join(source_lines))
    (output_dir / "gateway_rcp_manifest.json").write_text(
        json.dumps(
            {
                "idf": EXPECTED_IDF,
                "target": "esp32h2",
                "manifest_sha256": marker.hex(),
                "segments": records,
            },
            indent=2,
        )
        + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
