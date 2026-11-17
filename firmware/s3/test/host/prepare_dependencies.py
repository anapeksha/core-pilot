#!/usr/bin/env python3
"""Resolve only locked cJSON with the ESP-IDF 6.0.2 Python environment."""

import argparse
from pathlib import Path

import yaml
from idf_component_tools.hash_tools.validate import validate_hash_eq_hashdir
from idf_component_tools.manifest import SolvedComponent
from idf_component_tools.sources.fetcher import ComponentFetcher


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    lock = Path(__file__).resolve().parents[2] / "dependencies.lock"
    entry = yaml.safe_load(lock.read_text())["dependencies"]["espressif/cjson"]
    component = SolvedComponent.model_validate({"espressif/cjson": entry})
    destination = args.build_dir.resolve() / "deps" / "espressif__cjson"
    if not destination.exists():
        ComponentFetcher(component, destination).download()
    # Validate content against the lock, not merely the downloader's marker file.
    validate_hash_eq_hashdir(destination, component.component_hash)
    print(f"Verified {component.name}@{component.version}: {destination}")


if __name__ == "__main__":
    main()
