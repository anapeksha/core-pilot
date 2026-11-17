#!/usr/bin/env python3
"""Resolve only locked cJSON with the ESP-IDF 6.0.2 Python environment."""

import argparse
from pathlib import Path

import yaml
from idf_component_tools.hash_tools.validate import validate_hash_eq_hashdir
from idf_component_tools.manifest import SolvedComponent


def prepare_component(component, destination):
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        # ComponentFetcher changed its directory argument between manager 2.x and
        # 3.x. Source.download consistently accepts the exact component directory.
        component.source.download(component, destination.as_posix())
    # Recompute content against the lock, never trust a downloader's marker file.
    validate_hash_eq_hashdir(destination, component.component_hash)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    lock = Path(__file__).resolve().parents[2] / "dependencies.lock"
    entry = yaml.safe_load(lock.read_text())["dependencies"]["espressif/cjson"]
    component = SolvedComponent.model_validate({"espressif/cjson": entry})
    destination = args.build_dir.resolve() / "deps" / "espressif__cjson"
    prepare_component(component, destination)
    print(f"Verified {component.name}@{component.version}: {destination}")


if __name__ == "__main__":
    main()
