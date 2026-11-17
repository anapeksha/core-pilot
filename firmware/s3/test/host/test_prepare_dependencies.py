"""Run with the SDK Python: python -m unittest discover -s firmware/s3/test/host -p 'test_*.py'.

Uses a temporary, empty registry cache and real downloads, not managed_components.
"""

import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import yaml
from idf_component_tools.hash_tools.errors import HashNotEqualError
from idf_component_tools.manifest import SolvedComponent
from prepare_dependencies import prepare_component


class PrepareDependenciesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        lock = Path(__file__).resolve().parents[2] / "dependencies.lock"
        entry = yaml.safe_load(lock.read_text())["dependencies"]["espressif/cjson"]
        cls.component = SolvedComponent.model_validate({"espressif/cjson": entry})
        # Isolate the SDK cache as well as the destination: a new build directory
        # alone can silently reuse the contributor's already-downloaded package.
        cls.component.source._system_cache_path = str(cls.root / "registry-cache")
        cls.cold = cls.root / "cold" / "deps" / "espressif__cjson"
        assert not (cls.root / "registry-cache").exists()
        prepare_component(cls.component, cls.cold)

    def setUp(self):
        self.temporary_case = tempfile.TemporaryDirectory(dir=self.root)
        self.addCleanup(self.temporary_case.cleanup)
        self.destination = Path(self.temporary_case.name) / "espressif__cjson"
        shutil.copytree(self.cold, self.destination)

    def test_cold_fetch_has_exact_layout(self):
        self.assertTrue((self.cold / "cJSON" / "cJSON.c").is_file())
        self.assertFalse((self.cold / "espressif__cjson").exists())

    def test_cached_content_is_verified_without_download(self):
        with patch.object(type(self.component.source), "download") as download:
            prepare_component(self.component, self.destination)
            download.assert_not_called()

    def test_modified_source_is_rejected_even_with_locked_marker(self):
        (self.destination / ".component_hash").write_text(self.component.component_hash)
        with (self.destination / "cJSON" / "cJSON.c").open("a") as source:
            source.write("\n/* tampered */\n")
        with self.assertRaises(HashNotEqualError):
            prepare_component(self.component, self.destination)

    def test_missing_source_is_rejected(self):
        (self.destination / "cJSON" / "cJSON.c").unlink()
        with self.assertRaises(HashNotEqualError):
            prepare_component(self.component, self.destination)

    def test_extra_source_is_rejected(self):
        (self.destination / "unexpected.c").write_text("/* unexpected */\n")
        with self.assertRaises(HashNotEqualError):
            prepare_component(self.component, self.destination)


if __name__ == "__main__":
    unittest.main()
