#!/usr/bin/env python3
"""Launch the ESP-IDF 6.0.2 clangd, independent of the editor's PATH."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys


def main() -> int:
    eim = shutil.which("eim")
    if not eim:
        print("Install EIM and ESP-IDF 6.0.2 (including ESP-Clang).", file=sys.stderr)
        return 1
    # Capture EIM's activation chatter: stdout belongs exclusively to LSP.
    result = subprocess.run(
        [
            eim,
            "--log-file",
            os.devnull,
            "--do-not-track",
            "true",
            "run",
            "python -c 'import json, os; print(json.dumps(dict(os.environ)))'",
            "v6.0.2",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode:
        print(result.stderr, file=sys.stderr)
        return result.returncode
    try:
        environment = json.loads(result.stdout.strip().splitlines()[-1])
        if not isinstance(environment, dict) or not all(
            isinstance(key, str) and isinstance(value, str)
            for key, value in environment.items()
        ):
            raise ValueError("invalid environment")
    except (ValueError, IndexError):
        print("EIM did not return the ESP-IDF environment.", file=sys.stderr)
        return 1
    clangd = shutil.which("clangd", path=environment.get("PATH", ""))
    if not clangd or "esp-clang" not in clangd:
        print(
            "Install ESP-Clang for ESP-IDF 6.0.2; refusing host clangd.",
            file=sys.stderr,
        )
        return 1
    # Query only trusted system drivers, not arbitrary executables from a database.
    drivers = "**/esp-clang/bin/clang,**/esp-clang/bin/clang++,/usr/bin/c++,/usr/bin/g++,/usr/bin/clang++"
    os.execve(
        clangd,
        [clangd, "--query-driver=" + drivers, *sys.argv[1:]],
        environment,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
