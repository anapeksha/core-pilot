"""Tests for safe OpenOCD command construction."""

from __future__ import annotations

import argparse
import unittest
from pathlib import Path

from tools import flash_jtag


class AdapterSerialTest(unittest.TestCase):
    def test_accepts_normal_serial(self) -> None:
        self.assertEqual(
            flash_jtag.adapter_serial("80:45:6B:4D:54:94"),
            "80:45:6B:4D:54:94",
        )

    def test_rejects_tcl_metacharacters(self) -> None:
        for serial in (
            "80:45:6B:4D:54:94; shutdown",
            "80:45:6B:4D:54:[exec]",
            "80:45:6B:{4D}:54:94",
            "80:45:6B:4D:54 94",
            r"80:45:6B:4D:54\94",
        ):
            with (
                self.subTest(serial=serial),
                self.assertRaises(argparse.ArgumentTypeError),
            ):
                flash_jtag.adapter_serial(serial)


class TclArgumentQuotingTest(unittest.TestCase):
    def test_quotes_normal_path(self) -> None:
        self.assertEqual(
            flash_jtag.quote_tcl_argument("/tmp/core-pilot/image.bin"),
            '"/tmp/core-pilot/image.bin"',
        )

    def test_preserves_whitespace_and_braces(self) -> None:
        self.assertEqual(
            flash_jtag.quote_tcl_argument("/tmp/Core Pilot/{image}.bin"),
            '"/tmp/Core Pilot/{image}.bin"',
        )

    def test_escapes_semicolons_brackets_and_backslashes(self) -> None:
        self.assertEqual(
            flash_jtag.quote_tcl_argument(r"/tmp/a;shutdown/[exec]/x\image.bin"),
            '"/tmp/a;shutdown/\\[exec\\]/x\\\\image.bin"',
        )

    def test_rejects_control_characters(self) -> None:
        with self.assertRaises(ValueError):
            flash_jtag.quote_tcl_argument("/tmp/image.bin\nshutdown")

    def test_flash_command_quotes_each_image_path(self) -> None:
        command = flash_jtag.flash_command(
            "/usr/bin/openocd",
            "80:45:6B:4D:54:94",
            ((Path("/tmp/Core Pilot/a;[danger]{x}\\image.bin"), "0x0"),),
        )

        self.assertEqual(command[2], 'adapter serial "80:45:6B:4D:54:94"')
        self.assertEqual(
            command[6],
            'init; reset halt; program_esp "/tmp/Core Pilot/a;\\[danger\\]{x}\\\\image.bin" 0x0 verify; reset run; shutdown',
        )


if __name__ == "__main__":
    unittest.main()
