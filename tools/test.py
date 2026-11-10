#!/usr/bin/env python3
"""Run bounded Core Pilot hardware and protocol validation."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import time
import urllib.error
import urllib.request
from collections.abc import Callable
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


def read_json_response(response, url: str) -> dict:
    if response.status != 200:
        raise RuntimeError(f"{url} returned HTTP {response.status}")
    if response.headers.get("Cache-Control") != "no-store":
        raise RuntimeError(f"{url} omitted Cache-Control: no-store")
    if response.headers.get("Connection", "").lower() != "close":
        raise RuntimeError(f"{url} omitted Connection: close")
    return json.load(response)


def fetch_json(client: urllib.request.OpenerDirector, url: str, timeout: float) -> dict:
    request = urllib.request.Request(url, headers={"Accept": "application/json"})
    with client.open(request, timeout=timeout) as response:
        return read_json_response(response, url)


def mutation_json(
    client: urllib.request.OpenerDirector,
    url: str,
    body: dict,
    timeout: float,
    method: str = "POST",
) -> dict:
    request = urllib.request.Request(
        url,
        data=json.dumps(body, separators=(",", ":")).encode(),
        headers={
            "Accept": "application/json",
            "Content-Type": "application/json",
            "Origin": url.split("/api/", 1)[0],
        },
        method=method,
    )
    with client.open(request, timeout=timeout) as response:
        return read_json_response(response, url)


def post_json(
    client: urllib.request.OpenerDirector,
    url: str,
    body: dict,
    timeout: float,
) -> dict:
    return mutation_json(client, url, body, timeout)


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


def sample_is_ready(
    current: Sample, baseline_reset_reason: int, require_thread: bool = True
) -> bool:
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
            current.thread_ready or not require_thread,
            current.case_ready,
            current.subscriptions_ready,
        )
    ):
        print(f"FAIL protocol readiness regressed: {current}", flush=True)
        return False
    return True


def run_stress(args: argparse.Namespace, base_url: str) -> int:
    baseline, _ = checked_sample(
        opener(), base_url, args.timeout, args.sample_attempts, 0
    )
    if baseline is None or not sample_is_ready(
        baseline,
        baseline.reset_reason,
        require_thread=args.matter_transport == "thread",
    ):
        return 1

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
    if current is None or not sample_is_ready(
        current,
        baseline.reset_reason,
        require_thread=args.matter_transport == "thread",
    ):
        return 1
    print("PASS stress and recovery", flush=True)
    return 0


def wait_for(
    description: str,
    timeout: float,
    poll: Callable[[], dict],
) -> dict:
    deadline = time.monotonic() + timeout
    last: dict = {}
    while time.monotonic() < deadline:
        last = poll()
        if last.get("complete"):
            return last
        time.sleep(1)
    raise RuntimeError(f"timed out waiting for {description}: {last}")


def device_nodes(
    client: urllib.request.OpenerDirector, base_url: str, timeout: float
) -> set[str]:
    response = fetch_json(client, f"{base_url}/api/v1/devices", timeout)
    devices = response.get("devices")
    if not isinstance(devices, list):
        raise TypeError("device registry response is malformed")
    nodes = {
        device.get("node_id")
        for device in devices
        if device.get("protocol") == "matter"
    }
    if None in nodes or any(not isinstance(node, str) for node in nodes):
        raise RuntimeError("Matter device registry contains an invalid node ID")
    return nodes


def canonical_node_id(value: str) -> str:
    node_id = value.lower()
    if re.fullmatch(r"[0-9a-f]{16}", node_id) is None:
        raise RuntimeError("Matter node ID must be exactly 16 hexadecimal digits")
    return node_id


def matter_devices(
    client: urllib.request.OpenerDirector, base_url: str, timeout: float
) -> list[dict]:
    response = fetch_json(client, f"{base_url}/api/v1/devices", timeout)
    devices = response.get("devices")
    if not isinstance(devices, list) or not all(
        isinstance(item, dict) for item in devices
    ):
        raise RuntimeError("device registry response is malformed")
    return [item for item in devices if item.get("protocol") == "matter"]


def on_off_endpoint(devices: list[dict], node_id: str) -> tuple[int, bool]:
    for device in devices:
        capabilities = device.get("capabilities")
        if (
            device.get("node_id") == node_id
            and device.get("availability") == "online"
            and isinstance(device.get("endpoint_id"), int)
            and isinstance(capabilities, list)
            and "on_off" in capabilities
        ):
            state = device.get("state")
            if isinstance(state, list):
                for value in state:
                    if isinstance(value, dict) and isinstance(value.get("OnOff"), bool):
                        return device["endpoint_id"], value["OnOff"]
            return device["endpoint_id"], False
    raise RuntimeError("selected Matter node has no online On/Off endpoint")


def wait_for_command(
    client: urllib.request.OpenerDirector,
    base_url: str,
    timeout: float,
    operation_timeout: float,
    operation_id: int,
    kind: str,
    node_id: str,
    endpoint_id: int | None = None,
) -> dict:
    def status() -> dict:
        matter = fetch_json(client, f"{base_url}/api/v1/matter", timeout)
        command = matter.get("command", {})
        matches = all(
            (
                command.get("id") == operation_id,
                command.get("kind") == kind,
                command.get("device_id") == node_id,
                endpoint_id is None or command.get("endpoint_id") == endpoint_id,
            )
        )
        if not matches:
            return {"complete": False, "phase": command.get("phase")}
        if command.get("phase") == "failed":
            raise RuntimeError(f"Matter {kind} failed: {command.get('last_error')}")
        return {"complete": command.get("phase") == "complete", **command}

    return wait_for(f"Matter {kind}", operation_timeout, status)


def exercise_on_off(
    client: urllib.request.OpenerDirector,
    base_url: str,
    args: argparse.Namespace,
    node_id: str,
) -> None:
    endpoint_id, current = on_off_endpoint(
        matter_devices(client, base_url, args.timeout), node_id
    )
    response = mutation_json(
        client,
        f"{base_url}/api/v1/devices",
        {"device_id": node_id, "endpoint_id": endpoint_id, "on": current},
        args.timeout,
        method="PATCH",
    )
    operation_id = response.get("operation_id")
    if not response.get("accepted") or not isinstance(operation_id, int):
        raise RuntimeError(f"On/Off command rejected: {response.get('status')}")
    wait_for_command(
        client,
        base_url,
        args.timeout,
        args.operation_timeout,
        operation_id,
        "set_on_off",
        node_id,
        endpoint_id,
    )


def wait_for_reboot(
    client: urllib.request.OpenerDirector, base_url: str, args: argparse.Namespace
) -> None:
    response = post_json(client, f"{base_url}/api/v1/system/reboot", {}, args.timeout)
    if not response.get("accepted"):
        raise RuntimeError("Core Pilot rejected the reboot request")

    deadline = time.monotonic() + args.operation_timeout
    disconnected = False
    while time.monotonic() < deadline:
        try:
            fetch_json(client, f"{base_url}/api/v1/health", min(args.timeout, 1.0))
            if disconnected:
                return
        except (OSError, RuntimeError, urllib.error.URLError):
            disconnected = True
        time.sleep(0.25)
    raise RuntimeError(
        "Core Pilot reboot was not observed through disconnect and recovery"
    )


def wait_for_matter_recovery(
    client: urllib.request.OpenerDirector,
    base_url: str,
    args: argparse.Namespace,
    expected_node: str | None,
) -> str:
    def status() -> dict:
        matter = fetch_json(client, f"{base_url}/api/v1/matter", args.timeout)
        nodes = sorted(device_nodes(client, base_url, args.timeout))
        selected = expected_node if expected_node in nodes else None
        if expected_node is None and len(nodes) == 1:
            selected = nodes[0]
        ready = bool(
            selected
            and matter.get("case_ready")
            and matter.get("subscriptions_ready")
            and (args.matter_transport != "thread" or matter.get("thread_ready"))
        )
        return {"complete": ready, "node": selected, "nodes": nodes}

    recovered = wait_for("Matter recovery", args.operation_timeout, status)
    return recovered["node"]


def commission(
    client: urllib.request.OpenerDirector,
    base_url: str,
    args: argparse.Namespace,
    setup_payload: str,
) -> str:
    response = post_json(
        client,
        f"{base_url}/api/v1/matter/commission",
        {"transport": args.matter_transport, "setup_payload": setup_payload},
        args.timeout,
    )
    operation_id = response.get("operation_id")
    if not response.get("accepted") or not isinstance(operation_id, int):
        raise RuntimeError(f"commissioning rejected: {response.get('status')}")

    def status() -> dict:
        matter = fetch_json(client, f"{base_url}/api/v1/matter", args.timeout)
        terminal = matter.get("operation", {}).get("last_terminal")
        if not isinstance(terminal, dict) or terminal.get("id") != operation_id:
            return {"complete": False}
        if terminal.get("phase") != "commissioning_complete":
            failure = terminal.get("failure") or {}
            raise RuntimeError(f"commissioning failed: {failure.get('code')}")
        return {"complete": True}

    wait_for("Matter commissioning", args.operation_timeout, status)
    return wait_for_matter_recovery(client, base_url, args, None)


def run_lifecycle(args: argparse.Namespace, base_url: str) -> int:
    if not args.confirm_destructive:
        raise RuntimeError("lifecycle testing requires --confirm-destructive")
    setup_payload = os.environ.get("CORE_PILOT_MATTER_SETUP_PAYLOAD") or os.environ.get(
        "MATTER_SETUP_CODE", ""
    )
    selected_node = os.environ.get("CORE_PILOT_MATTER_NODE_ID", "")
    if not setup_payload or not selected_node:
        raise RuntimeError(
            "set MATTER_SETUP_CODE and CORE_PILOT_MATTER_NODE_ID in the environment"
        )
    selected_node = canonical_node_id(selected_node)

    client = opener()
    auth = fetch_json(client, f"{base_url}/api/v1/auth", args.timeout)
    if auth.get("authentication_required") is not False:
        raise RuntimeError(
            "lifecycle reboot testing requires the development auth override"
        )
    system = fetch_json(client, f"{base_url}/api/v1/system", args.timeout)
    if system.get("product") != "Core Pilot":
        raise RuntimeError("target did not identify itself as Core Pilot")
    if selected_node not in device_nodes(client, base_url, args.timeout):
        raise RuntimeError(
            "selected Matter node is not in the current encrypted registry"
        )

    node_id = selected_node
    for cycle in range(1, args.lifecycle_cycles + 1):
        exercise_on_off(client, base_url, args, node_id)
        wait_for_reboot(client, base_url, args)
        node_id = wait_for_matter_recovery(client, base_url, args, node_id)
        exercise_on_off(client, base_url, args, node_id)
        print(f"cycle={cycle} reboot=complete command=complete", flush=True)

        removal = post_json(
            client,
            f"{base_url}/api/v1/matter/devices/remove",
            {"device_id": node_id},
            args.timeout,
        )
        operation_id = removal.get("operation_id")
        if not removal.get("accepted") or not isinstance(operation_id, int):
            raise RuntimeError(f"removal rejected: {removal.get('status')}")
        wait_for_command(
            client,
            base_url,
            args.timeout,
            args.operation_timeout,
            operation_id,
            "unpair_device",
            node_id,
        )
        wait_for(
            "encrypted registry removal",
            args.operation_timeout,
            lambda node_id=node_id: {
                "complete": node_id not in device_nodes(client, base_url, args.timeout)
            },
        )
        print(f"cycle={cycle} unpair=complete registry=removed", flush=True)

        node_id = commission(client, base_url, args, setup_payload)
        exercise_on_off(client, base_url, args, node_id)
        print(
            f"cycle={cycle} commission=complete case=ready subscriptions=ready",
            flush=True,
        )

    print(f"PASS lifecycle cycles={args.lifecycle_cycles}", flush=True)
    return 0


def validate_rate_limit(error: urllib.error.HTTPError) -> int:
    if error.code != 429:
        raise RuntimeError(f"rate probe returned unexpected HTTP {error.code}")
    if error.headers.get_content_type() != "application/json":
        raise RuntimeError("rate limit response is not JSON")
    if error.headers.get("Cache-Control") != "no-store":
        raise RuntimeError("rate limit response omitted Cache-Control: no-store")
    if error.headers.get("Connection", "").lower() != "close":
        raise RuntimeError("rate limit response omitted Connection: close")
    retry_value = error.headers.get("Retry-After", "")
    try:
        retry_after = int(retry_value)
    except ValueError as exception:
        raise RuntimeError("rate limit response has invalid Retry-After") from exception
    if retry_after <= 0:
        raise RuntimeError("rate limit response has non-positive Retry-After")
    try:
        payload = json.load(error)
    except (UnicodeDecodeError, json.JSONDecodeError) as exception:
        raise RuntimeError("rate limit response is not valid JSON") from exception
    if payload != {
        "error": {
            "code": "rate_limited",
            "message": "Too many requests. Retry later.",
        }
    }:
        raise RuntimeError("rate limit response body does not match the API contract")
    return retry_after


def run_rate_limit(args: argparse.Namespace, base_url: str) -> int:
    client = opener()
    baseline = read_sample(client, base_url, args.timeout)
    retry_after = 0
    accepted = 0
    url = f"{base_url}/api/v1/health"
    for _ in range(args.rate_limit_requests):
        request = urllib.request.Request(url, headers={"Accept": "application/json"})
        try:
            with client.open(request, timeout=args.timeout) as response:
                response.read()
                if response.status != 200:
                    raise RuntimeError(f"rate probe returned HTTP {response.status}")
                accepted += 1
        except urllib.error.HTTPError as error:
            retry_after = validate_rate_limit(error)
            break
    if retry_after == 0:
        raise RuntimeError(
            f"rate limiter did not reject {args.rate_limit_requests} requests"
        )

    time.sleep(retry_after)
    fetch_json(client, url, args.timeout)
    recovered = read_sample(client, base_url, args.timeout)
    if not sample_is_ready(
        recovered,
        baseline.reset_reason,
        require_thread=args.matter_transport == "thread",
    ):
        return 1
    print(
        f"PASS rate-limit accepted={accepted} retry_after={retry_after}s recovery=ready",
        flush=True,
    )
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
        if not sample_is_ready(
            current,
            baseline_reset_reason,
            require_thread=args.matter_transport == "thread",
        ):
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
    parser.add_argument(
        "mode", choices=("stress", "rate-limit", "soak", "both", "lifecycle")
    )
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
    parser.add_argument("--lifecycle-cycles", type=int, default=3)
    parser.add_argument("--operation-timeout", type=float, default=180.0)
    parser.add_argument("--rate-limit-requests", type=int, default=700)
    parser.add_argument("--confirm-destructive", action="store_true")
    parser.add_argument(
        "--matter-transport", choices=("wifi", "thread"), default="wifi"
    )
    args = parser.parse_args()

    positive = (
        args.duration_hours,
        args.sample_interval,
        args.sample_attempts,
        args.stress_interval,
        args.stress_requests,
        args.stress_workers,
        args.timeout,
        args.lifecycle_cycles,
        args.operation_timeout,
        args.rate_limit_requests,
    )
    if any(value <= 0 for value in positive):
        parser.error("durations, counts, workers, and timeout must be positive")
    if not 0 <= args.max_failure_rate <= 1:
        parser.error("max-failure-rate must be between 0 and 1")

    if not os.environ.get("CORE_PILOT_BEARER"):
        parser.error(
            "set CORE_PILOT_BEARER to a RAM session token; never pass it on the command line"
        )
    setup_payload = os.environ.get("CORE_PILOT_MATTER_SETUP_PAYLOAD") or os.environ.get(
        "MATTER_SETUP_CODE"
    )
    if args.mode == "lifecycle" and (
        not setup_payload or not os.environ.get("CORE_PILOT_MATTER_NODE_ID")
    ):
        parser.error(
            "set MATTER_SETUP_CODE and CORE_PILOT_MATTER_NODE_ID for lifecycle testing"
        )
    base_url = f"{args.scheme}://{args.host}"
    if args.mode == "stress":
        return run_stress(args, base_url)
    if args.mode == "lifecycle":
        return run_lifecycle(args, base_url)
    if args.mode == "rate-limit":
        return run_rate_limit(args, base_url)
    return run_soak(args, base_url, with_stress=args.mode == "both")


if __name__ == "__main__":
    raise SystemExit(main())
