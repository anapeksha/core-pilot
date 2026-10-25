#!/usr/bin/env python3
"""Run Core Pilot hardware stress, soak, or combined validation."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
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


def opener() -> urllib.request.OpenerDirector:
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    token = os.environ.get("CORE_PILOT_BEARER", "")
    if token:
        client.addheaders = [("Authorization", f"Bearer {token}")]
    return client


def fetch_json(client: urllib.request.OpenerDirector, url: str, timeout: float) -> dict:
    request = urllib.request.Request(url, headers={"Accept": "application/json"})
    with client.open(request, timeout=timeout) as response:
        if response.status != 200:
            raise RuntimeError(f"{url} returned HTTP {response.status}")
        if response.headers.get("Cache-Control") != "no-store":
            raise RuntimeError(f"{url} omitted Cache-Control: no-store")
        if response.headers.get("Connection", "").lower() != "close":
            raise RuntimeError(f"{url} omitted Connection: close")
        return json.load(response)


def read_sample(
    client: urllib.request.OpenerDirector, base_url: str, timeout: float
) -> Sample:
    system = fetch_json(client, f"{base_url}/api/v1/system", timeout)
    matter = fetch_json(client, f"{base_url}/api/v1/matter", timeout)
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
        try:
            with opener().open(
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


def checked_sample(
    client: urllib.request.OpenerDirector,
    base_url: str,
    timeout: float,
    attempts: int,
    number: int,
) -> tuple[Sample | None, int]:
    errors = (
        KeyError,
        TypeError,
        ValueError,
        RuntimeError,
        OSError,
        urllib.error.URLError,
    )
    last_error: Exception | None = None
    failures = 0
    for attempt in range(1, attempts + 1):
        try:
            return read_sample(client, base_url, timeout), failures
        except errors as error:
            last_error = error
            failures += 1
            print(f"WARN sample {number} attempt {attempt}: {error}", flush=True)
            if attempt < attempts:
                time.sleep(2)
    print(f"FAIL sample {number} did not recover: {last_error}", flush=True)
    return None, failures


def sample_is_ready(current: Sample, baseline_reset_reason: int) -> bool:
    if current.reset_reason != baseline_reset_reason:
        print(
            f"FAIL reset reason changed from {baseline_reset_reason} "
            f"to {current.reset_reason}",
            flush=True,
        )
        return False
    if not all(
        (
            current.matter_ready,
            current.thread_ready,
            current.case_ready,
            current.subscriptions_ready,
        )
    ):
        print(f"FAIL protocol readiness regressed: {current}", flush=True)
        return False
    return True


def run_stress(args: argparse.Namespace, base_url: str) -> int:
    succeeded, failed = stress_burst(
        base_url, args.stress_requests, args.stress_workers, args.timeout
    )
    rate = failed / args.stress_requests
    print(f"stress success={succeeded} failed={failed} rate={rate:.2%}", flush=True)
    if rate > args.max_failure_rate:
        print(
            f"FAIL stress failure rate exceeds {args.max_failure_rate:.2%}", flush=True
        )
        return 1
    current, _ = checked_sample(
        opener(), base_url, args.timeout, args.sample_attempts, 1
    )
    if current is None or not sample_is_ready(current, current.reset_reason):
        return 1
    print("PASS stress and recovery", flush=True)
    return 0


def run_soak(args: argparse.Namespace, base_url: str, with_stress: bool) -> int:
    client = opener()
    started = time.monotonic()
    deadline = started + args.duration_hours * 3600
    next_stress = started
    samples = 0
    bursts = 0
    transient_failures = 0
    baseline_reset_reason: int | None = None

    while time.monotonic() < deadline:
        current, failures = checked_sample(
            client, base_url, args.timeout, args.sample_attempts, samples + 1
        )
        transient_failures += failures
        if current is None:
            return 1
        if baseline_reset_reason is None:
            baseline_reset_reason = current.reset_reason
        if not sample_is_ready(current, baseline_reset_reason):
            return 1

        samples += 1
        now = time.monotonic()
        if with_stress and now >= next_stress:
            succeeded, failed = stress_burst(
                base_url, args.stress_requests, args.stress_workers, args.timeout
            )
            rate = failed / args.stress_requests
            bursts += 1
            print(
                f"{int(now - started):05d}s sample={samples} burst={bursts} "
                f"success={succeeded} failed={failed}",
                flush=True,
            )
            if rate > args.max_failure_rate:
                print(
                    f"FAIL stress failure rate {rate:.2%} exceeds "
                    f"{args.max_failure_rate:.2%}",
                    flush=True,
                )
                return 1
            next_stress = now + args.stress_interval

        remaining = deadline - time.monotonic()
        if remaining > 0:
            time.sleep(min(args.sample_interval, remaining))

    print(
        f"PASS duration={int(time.monotonic() - started)}s samples={samples} "
        f"stress_bursts={bursts} transient_failures={transient_failures}",
        flush=True,
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("stress", "soak", "both"))
    parser.add_argument(
        "host", help="Hostname, IPv4 address, or bracketed IPv6 address"
    )
    parser.add_argument("--scheme", choices=("http", "https"), default="http")
    parser.add_argument("--duration-hours", type=float, default=8.0)
    parser.add_argument("--sample-interval", type=float, default=30.0)
    parser.add_argument("--sample-attempts", type=int, default=3)
    parser.add_argument("--stress-interval", type=float, default=300.0)
    parser.add_argument("--stress-requests", type=int, default=200)
    parser.add_argument("--stress-workers", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--max-failure-rate", type=float, default=0.05)
    args = parser.parse_args()

    positive = (
        args.duration_hours,
        args.sample_interval,
        args.sample_attempts,
        args.stress_interval,
        args.stress_requests,
        args.stress_workers,
        args.timeout,
    )
    if any(value <= 0 for value in positive):
        parser.error("durations, counts, workers, and timeout must be positive")
    if not 0 <= args.max_failure_rate <= 1:
        parser.error("max-failure-rate must be between 0 and 1")

    if not os.environ.get("CORE_PILOT_BEARER"):
        parser.error(
            "set CORE_PILOT_BEARER to a RAM session token; never pass it on the command line"
        )
    base_url = f"{args.scheme}://{args.host}"
    if args.mode == "stress":
        return run_stress(args, base_url)
    return run_soak(args, base_url, with_stress=args.mode == "both")


if __name__ == "__main__":
    raise SystemExit(main())
