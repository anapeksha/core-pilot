#!/usr/bin/env python3
"""Run clang-tidy against project-owned firmware translation units."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"
S3 = FIRMWARE / "s3"


def find_tool(name: str) -> str | None:
    installed = shutil.which(name)
    if installed:
        return installed
    candidates = sorted(
        (Path.home() / ".espressif" / "tools" / "esp-clang").glob(f"**/bin/{name}"),
        reverse=True,
    )
    return str(candidates[0]) if candidates else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "build_directory",
        nargs="?",
        default="build-clang",
        help="Compilation database directory relative to firmware/s3/",
    )
    args = parser.parse_args()

    tidy = find_tool("clang-tidy")
    if tidy is None:
        print("clang-tidy is required; install ESP-Clang with EIM", file=sys.stderr)
        return 1
    build_directory = (S3 / args.build_directory).resolve()
    if not (build_directory / "compile_commands.json").is_file():
        print(
            f"missing {build_directory / 'compile_commands.json'}; configure the ESP-Clang build first",
            file=sys.stderr,
        )
        return 1
    sources = sorted(
        str(path)
        for directory in (S3 / "components", S3 / "main")
        for path in directory.rglob("*.cpp")
        if "test" not in path.parts
    )
    command = [
        tidy,
        "-quiet",
        "-p",
        str(build_directory),
        "--extra-arg=-Wno-error=unknown-warning-option",
        "--extra-arg=-Wno-error=vla-cxx-extension",
        "--extra-arg=-Wno-error=inconsistent-missing-override",
        *sources,
    ]
    return subprocess.run(command, cwd=S3, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
