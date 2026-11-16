#!/usr/bin/env python3
"""Regenerate native and ESP-Clang databases after setup or relocation."""

from __future__ import annotations

import os
import shlex
import subprocess
from pathlib import Path

from build_gateway_rcp import recipe_digest

ROOT = Path(__file__).resolve().parents[1]


def commands() -> list[list[str]]:
    test_components = [
        "main",
        *sorted(
            path.parent.parent.name
            for path in (ROOT / "firmware/s3/components").glob(
                "core_*/test/CMakeLists.txt"
            )
        ),
    ]
    configurations = [
        ("firmware/s3", "firmware/s3/build-clang", []),
        (
            "firmware/h2",
            "firmware/h2/build-clang",
            [
                "-D",
                "IDF_TARGET=esp32h2",
                "-D",
                f"GATEWAY_RCP_MANIFEST_HEX={recipe_digest().hex()}",
            ],
        ),
        (
            "firmware/s3",
            "firmware/s3/build-tests-clang",
            [
                "-D",
                "BUILD_TESTS=OFF",
                "-D",
                "TEST_COMPONENTS=" + ";".join(test_components),
            ],
        ),
        (
            "firmware/s3/test/esp32s3",
            "firmware/s3/test/esp32s3/build-clang",
            [
                "-D",
                "IDF_TARGET=esp32s3",
            ],
        ),
    ]
    result = [
        [
            "cmake",
            "-S",
            "firmware/s3/test/host",
            "-B",
            "firmware/s3/test/host/build",
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ]
    ]
    for project, build, extra in configurations:
        command = [
            "idf.py",
            "-C",
            project,
            "-B",
            build,
            "-D",
            "IDF_TOOLCHAIN=clang",
            "-D",
            f"SDKCONFIG={(ROOT / build / 'sdkconfig').resolve()}",
            *extra,
            "reconfigure",
        ]
        result.append(
            [
                "eim",
                "--log-file",
                os.devnull,
                "--do-not-track",
                "true",
                "run",
                shlex.join(command),
                "v6.0.2",
            ]
        )
    return result


def main() -> int:
    for command in commands():
        result = subprocess.run(command, cwd=ROOT, check=False)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
