#!/usr/bin/env python3
"""Exercise HTTP socket exhaustion and verify that the listener recovers."""

from __future__ import annotations

import argparse
import concurrent.futures
import time
import urllib.error
import urllib.request


def fetch(url: str, timeout: float) -> tuple[bool, str]:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            response.read()
            return response.status == 200, f"HTTP {response.status}"
    except (OSError, urllib.error.URLError) as error:
        return False, str(error)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host", help="Core Pilot IPv4 address or hostname")
    parser.add_argument("--requests", type=int, default=200)
    parser.add_argument("--workers", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    base_url = f"http://{args.host}"
    paths = ("/api/v1/health", "/api/v1/matter", "/api/v1/network", "/")
    urls = [base_url + paths[index % len(paths)] for index in range(args.requests)]

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
        results = list(executor.map(lambda url: fetch(url, args.timeout), urls))

    succeeded = sum(success for success, _ in results)
    failed = len(results) - succeeded
    print(f"load: {succeeded} succeeded, {failed} rejected or timed out")

    deadline = time.monotonic() + 10
    health_url = base_url + "/api/v1/health"
    detail = "health check did not run"
    while time.monotonic() < deadline:
        healthy, detail = fetch(health_url, 2)
        if healthy:
            print("recovery: health endpoint returned HTTP 200")
            return 0
        time.sleep(0.25)

    print(f"recovery failed: {detail}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
