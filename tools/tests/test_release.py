from __future__ import annotations

import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest
from argparse import Namespace
from contextlib import redirect_stdout
from io import StringIO

MODULE_PATH = pathlib.Path(__file__).parents[1] / "release.py"
SPEC = importlib.util.spec_from_file_location("release_tool", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
release_tool = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = release_tool
SPEC.loader.exec_module(release_tool)


class ReleaseToolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.partitions = self.root / "partitions.csv"
        self.partitions.write_text(
            "ota_0,app,ota_0,0x20000,0x400000,\n"
            "ota_stage,data,undefined,0x940000,0x400000,\n",
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_rejects_application_without_release_headroom(self) -> None:
        image = self.root / "core_pilot.bin"
        image.write_bytes(b"x" * (0x400000 - release_tool.MIN_APP_HEADROOM + 1))
        with self.assertRaisesRegex(ValueError, "at least"):
            release_tool.validate_app(image, release_tool.partitions(self.partitions))

    def test_build_inspection_validates_flash_layout(self) -> None:
        build = self.root / "build"
        build.mkdir()
        (build / "core_pilot.bin").write_bytes(b"app")
        (build / "flasher_args.json").write_text(
            json.dumps(
                {
                    "flash_files": {
                        "0x0": "bootloader/bootloader.bin",
                        "0x8000": "partition_table/partition-table.bin",
                        "0xf000": "ota_data_initial.bin",
                        "0x20000": "core_pilot.bin",
                    }
                }
            ),
            encoding="utf-8",
        )
        with redirect_stdout(StringIO()) as output:
            release_tool.inspect_build(
                Namespace(build_dir=str(build), partitions=str(self.partitions))
            )
        self.assertEqual(json.loads(output.getvalue())["headroom"], 0x400000 - 3)

    def test_manifest_records_release_artifacts(self) -> None:
        release = self.root / "release"
        release.mkdir()
        (release / "core-pilot-0.1.0-rc.3.bin").write_bytes(b"merged")
        (release / "core-pilot-0.1.0-rc.3-ota.bin").write_bytes(b"signed")
        with redirect_stdout(StringIO()):
            release_tool.package_manifest(
                Namespace(
                    release_dir=str(release),
                    partitions=str(self.partitions),
                    version="0.1.0-rc.3",
                    source_commit="a" * 40,
                    idf_version="6.0.2",
                )
            )
        manifest = json.loads(
            (release / "core-pilot-0.1.0-rc.3-manifest.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(manifest["tag"], "v0.1.0-rc.3")
        self.assertEqual([item["role"] for item in manifest["artifacts"]], ["initial", "ota"])
        self.assertTrue(manifest["signature_verified"])


if __name__ == "__main__":
    unittest.main()
