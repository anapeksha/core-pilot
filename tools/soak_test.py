#!/usr/bin/env python3
"""Run a bounded Core Pilot hardware soak with periodic HTTP stress bursts."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import time
import urllib.error
import urllib.request
from dataclasses import dataclass


@dataclass(frozen=True)
class Sample:
    reset_reason: int
    matter_ready: bool
    thread_ready: bool
    case_ready: bool
    subscriptions_ready: bool


def fetch_json(opener: urllib.request.OpenerDirector, url: str, timeout: float) -> dict:
    request = urllib.request.Request(url, headers={"Accept": "application/json"})
    with opener.open(request, timeout=timeout) as response:
        if response.status != 200:
            raise RuntimeError(f"{url} returned HTTP {response.status}")
        if response.headers.get("Cache-Control") != "no-store":
            raise RuntimeError(f"{url} omitted Cache-Control: no-store")
        if response.headers.get("Connection", "").lower() != "close":
            raise RuntimeError(f"{url} omitted Connection: close")
        return json.load(response)


def sample(
    opener: urllib.request.OpenerDirector, base_url: str, timeout: float
) -> Sample:
    system = fetch_json(opener, f"{base_url}/api/v1/system", timeout)
    matter = fetch_json(opener, f"{base_url}/api/v1/matter", timeout)
    return Sample(
        reset_reason=int(system["reset_reason"]),
        matter_ready=system["matter"] == "ready",
        thread_ready=bool(matter["thread_ready"]),
        case_ready=bool(matter["case_ready"]),
        subscriptions_ready=bool(matter["subscriptions_ready"]),
    )


def stress_burst(
    base_url: str, requests: int, workers: int, timeout: float
) -> tuple[int, int]:
    paths = ("/api/v1/health", "/api/v1/matter", "/api/v1/network", "/")

    def fetch(index: int) -> bool:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            with opener.open(
                f"{base_url}{paths[index % len(paths)]}", timeout=timeout
            ) as response:
                response.read()
                return response.status == 200
        except (OSError, urllib.error.URLError):
            return False

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        results = list(executor.map(fetch, range(requests)))
    succeeded = sum(results)
    return succeeded, len(results) - succeeded


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "host", help="Hostname, IPv4 address, or bracketed IPv6 address"
    )
    parser.add_argument("--duration-hours", type=float, default=8.0)
    parser.add_argument("--sample-interval", type=float, default=30.0)
    parser.add_argument("--stress-interval", type=float, default=300.0)
    parser.add_argument("--stress-requests", type=int, default=200)
    parser.add_argument("--stress-workers", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--sample-attempts", type=int, default=3)
    parser.add_argument("--max-failure-rate", type=float, default=0.02)
    args = parser.parse_args()
    if (
        args.duration_hours <= 0
        or args.sample_interval <= 0
        or args.stress_interval <= 0
        or args.stress_requests < 1
        or args.stress_workers < 1
        or args.timeout <= 0
        or args.sample_attempts < 1
    ):
        parser.error("durations, counts, workers, and timeout must be positive")
    if not 0 <= args.max_failure_rate <= 1:
        parser.error("max-failure-rate must be between 0 and 1")

    base_url = f"http://{args.host}"
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    started = time.monotonic()
    deadline = started + args.duration_hours * 3600
    next_stress = started
    samples = 0
    bursts = 0
    baseline_reset_reason: int | None = None
    transient_failures = 0
    sample_errors = (
        KeyError,
        TypeError,
        ValueError,
        RuntimeError,
        OSError,
        urllib.error.URLError,
    )

    while time.monotonic() < deadline:
        current: Sample | None = None
        last_error: Exception | None = None
        for attempt in range(1, args.sample_attempts + 1):
            try:
                current = sample(opener, base_url, args.timeout)
                break
            except sample_errors as error:
                last_error = error
                transient_failures += 1
                print(
                    f"WARN sample {samples + 1} attempt {attempt}: {error}", flush=True
                )
                if attempt < args.sample_attempts:
                    time.sleep(2)
        if current is None:
            print(
                f"FAIL sample {samples + 1} did not recover: {last_error}", flush=True
            )
            return 1

        if baseline_reset_reason is None:
            baseline_reset_reason = current.reset_reason
        if current.reset_reason != baseline_reset_reason:
            print(
                f"FAIL reset reason changed from {baseline_reset_reason} to {current.reset_reason}",
                flush=True,
            )
            return 1
        if not all(
            (
                current.matter_ready,
                current.thread_ready,
                current.case_ready,
                current.subscriptions_ready,
            )
        ):
            print(f"FAIL protocol readiness regressed: {current}", flush=True)
            return 1

        samples += 1
        now = time.monotonic()
        if now >= next_stress:
            succeeded, failed = stress_burst(
                base_url, args.stress_requests, args.stress_workers, args.timeout
            )
            failure_rate = failed / args.stress_requests
            bursts += 1
            elapsed = int(now - started)
            print(
                f"{elapsed:05d}s sample={samples} burst={bursts} "
                f"success={succeeded} failed={failed}",
                flush=True,
            )
            if failure_rate > args.max_failure_rate:
                print(
                    f"FAIL stress failure rate {failure_rate:.2%} exceeds "
                    f"{args.max_failure_rate:.2%}",
                    flush=True,
                )
                return 1
            next_stress = now + args.stress_interval

        remaining = deadline - time.monotonic()
        if remaining > 0:
            time.sleep(min(args.sample_interval, remaining))

    elapsed = int(time.monotonic() - started)
    print(
        f"PASS duration={elapsed}s samples={samples} stress_bursts={bursts} "
        f"transient_failures={transient_failures}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
