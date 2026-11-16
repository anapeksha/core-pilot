"""Check a captured CoreS3 boot and its unauthenticated production API."""

from __future__ import annotations

import argparse
import ipaddress
import json
import re
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

MAX_RESPONSE_BYTES = 4096
MAX_LOG_BYTES = 2 * 1024 * 1024


@dataclass(frozen=True)
class BootEvidence:
    version: str
    address: str
    internal_free: int
    internal_largest: int
    dma_free: int


def inspect_boot(text: str, version: str, address: str) -> BootEvidence:
    if len(text.encode("utf-8")) > MAX_LOG_BYTES:
        raise ValueError("boot capture exceeds the size limit")
    if len(re.findall(r"\brst:0x[0-9a-fA-F]+", text)) != 1:
        raise ValueError("capture must contain exactly one boot, with no later reset")
    if any(marker in text for marker in (
        "Mem alloc fail", "Guru Meditation", "Backtrace:", "panic_abort",
        "Stack smashing", "stack overflow", "assert failed",
    )):
        raise ValueError("boot capture contains a panic or allocation failure")
    versions = re.findall(r"App version:\s+(\S+)", text)
    addresses = re.findall(r"sta ip:\s+([0-9.]+)", text)
    if versions != [version]:
        raise ValueError("boot version does not match the expected firmware")
    if not addresses or addresses[-1] != address:
        raise ValueError("target does not match the DHCP address in this capture")
    for marker in ("SPI SRAM memory test OK", "required encrypted SD storage verified"):
        if marker not in text:
            raise ValueError("boot memory/storage self-tests did not complete")
    memory = re.findall(
        r"Application startup complete; internal free=(\d+) largest=(\d+), DMA free=(\d+)",
        text,
    )
    if len(memory) != 1:
        raise ValueError("application startup did not complete exactly once")
    free, largest, dma = map(int, memory[0])
    if free < 16 * 1024 or largest < 8 * 1024 or dma < 8 * 1024:
        raise ValueError("startup internal/DMA memory is below the regression floor")
    return BootEvidence(version, address, free, largest, dma)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def check_http(base_url: str, timeout: float = 5) -> None:
    """Use no credentials; a session exchange is attempted only while auth is idle."""
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    def request(path: str, expected: int, *, cookie: str | None = None,
                body: dict[str, str] | None = None, json_response: bool = True) -> dict:
        headers = {"Accept": "application/json"}
        if cookie is not None:
            headers["Cookie"] = cookie
        payload = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(body).encode("utf-8")
        req = urllib.request.Request(base_url + path, data=payload, headers=headers)
        try:
            response = client.open(req, timeout=timeout)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            if response.status != expected:
                raise ValueError(f"unexpected HTTP status on {path}")
            if "no-store" not in response.headers.get("Cache-Control", "").lower():
                raise ValueError(f"missing no-store policy on {path}")
            if response.headers.get("Connection", "").lower() != "close":
                raise ValueError(f"missing connection-close policy on {path}")
            if response.headers.get("Set-Cookie"):
                raise ValueError("unauthenticated probe unexpectedly created a session")
            data = response.read(MAX_RESPONSE_BYTES + 1)
        if len(data) > MAX_RESPONSE_BYTES:
            raise ValueError("HTTP response exceeds the size limit")
        if not json_response:
            return {}
        try:
            result = json.loads(data)
        except (ValueError, UnicodeError):
            raise ValueError("HTTP response is not valid JSON") from None
        if not isinstance(result, dict):
            raise ValueError("HTTP response must be an object")
        return result

    def require_idle() -> None:
        auth = request("/api/v1/auth", 200)
        if (auth.get("authentication_required") is not True or
                auth.get("session_authenticated") is not False or
                auth.get("bootstrap_state") != "idle"):
            raise ValueError("production authorization must remain idle and locked")

    if request("/api/v1/health", 200).get("product") != "Core Pilot":
        raise ValueError("HTTP target did not identify as Core Pilot")
    require_idle()
    request("/api/v1/system", 401, json_response=False)
    request("/api/v1/system", 401, cookie="core_pilot_session=" + "0" * 64, json_response=False)
    request("/api/v1/auth/session", 503, body={"code": "0" * 16, "mode": "cookie"})
    require_idle()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="Fresh boot capture from the identified CoreS3")
    parser.add_argument("address", help="Current CoreS3 DHCP IPv4 address")
    parser.add_argument("--version", required=True)
    parser.add_argument("--timeout", type=float, default=5)
    args = parser.parse_args()
    try:
        ipaddress.IPv4Address(args.address)
        if not 0 < args.timeout <= 60:
            raise ValueError("timeout must be greater than zero and at most 60 seconds")
        if args.capture.stat().st_size > MAX_LOG_BYTES:
            raise ValueError("boot capture exceeds the size limit")
        evidence = inspect_boot(args.capture.read_text(), args.version, args.address)
        check_http("http://" + args.address, args.timeout)
    except (OSError, ValueError, urllib.error.URLError):
        # Do not echo response bodies, captured logs, or server-controlled error text.
        print("Boot/API check failed; verify the capture, memory margin, and production access state.")
        return 1
    print(f"Boot/API check passed for {evidence.version}; internal free={evidence.internal_free}, "
          f"largest={evidence.internal_largest}, DMA free={evidence.dma_free} bytes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
