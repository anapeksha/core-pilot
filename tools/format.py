#!/usr/bin/env python3
"""Check or apply formatting to Core Pilot C and C++ sources."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"
S3 = FIRMWARE / "s3"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}


def clang_format_version(executable: str) -> int | None:
    result = subprocess.run(
        [executable, "--version"], capture_output=True, check=False, text=True
    )
    match = re.search(r"version (\d+)", result.stdout)
    return int(match.group(1)) if match else None


def find_formatter() -> str | None:
    candidates = [shutil.which("clang-format")]
    candidates.extend(
        str(path)
        for path in sorted(
            (Path.home() / ".espressif" / "tools" / "esp-clang").glob(
                "**/bin/clang-format"
            ),
            reverse=True,
        )
    )
    for candidate in candidates:
        if (
            candidate
            and Path(candidate).is_file()
            and clang_format_version(candidate) == 20
        ):
            return candidate
    return None


def source_files() -> list[str]:
    files: list[str] = []
    for directory in (
        S3 / "components",
        S3 / "main",
        FIRMWARE / "h2" / "main",
    ):
        files.extend(
            str(path)
            for path in directory.rglob("*")
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        )
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write", action="store_true", help="Apply formatting in place"
    )
    args = parser.parse_args()

    formatter = find_formatter()
    if formatter is None:
        print("ESP-Clang clang-format 20 is required", file=sys.stderr)
        return 1
    files = source_files()
    if not files:
        print("no firmware C or C++ sources found", file=sys.stderr)
        return 1
    command = [formatter, "-i" if args.write else "--dry-run"]
    if not args.write:
        command.append("--Werror")
    return subprocess.run(
        command + files, cwd=ROOT, check=False, env=os.environ.copy()
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
