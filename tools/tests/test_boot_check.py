from __future__ import annotations

import importlib.util
import io
import json
import os
import pathlib
import sys
import tempfile
import threading
import unittest
from contextlib import contextmanager, redirect_stdout
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "boot_check", pathlib.Path(__file__).parents[1] / "boot_check.py"
)
assert SPEC is not None and SPEC.loader is not None
boot_check = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = boot_check
SPEC.loader.exec_module(boot_check)

CAPTURE = """ESP-ROM:esp32s3
rst:0x15 (USB_UART_CHIP_RESET)
I (1) app_init: App version:      0.1.2
I (2) esp_psram: SPI SRAM memory test OK
I (3) core_pilot: required encrypted SD storage verified
I (4) esp_netif_handlers: sta ip: 192.0.2.10, mask: 255.255.255.0
I (5) core_pilot: Application startup complete; internal free=38000 largest=29000, DMA free=30000
"""


@contextmanager
def fixture_server(fault: str = ""):
    state = {"exchanges": 0, "authorization_headers": 0}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: object) -> None:
            pass

        def do_GET(self):
            self.respond()

        def do_POST(self):
            self.respond()

        def respond(self):
            state["authorization_headers"] += int("Authorization" in self.headers)
            payload = {}
            status = 200
            if self.path == "/api/v1/health":
                payload = {
                    "product": "Other" if fault == "wrong_product" else "Core Pilot"
                }
                if fault == "redirect":
                    status = 302
            elif self.path == "/api/v1/auth":
                payload = {
                    "authentication_required": fault != "auth_bypass",
                    "session_authenticated": False,
                    "bootstrap_state": "open"
                    if fault == "open_code"
                    or (fault == "remote_issuance" and state["exchanges"])
                    else "idle",
                }
            elif self.path == "/api/v1/system":
                status = (
                    200
                    if fault == "unlocked_system"
                    or (fault == "accepted_cookie" and "Cookie" in self.headers)
                    else 401
                )
            elif self.path == "/api/v1/auth/session":
                self.rfile.read(int(self.headers.get("Content-Length", "0")))
                state["exchanges"] += 1
                status = 503
            else:
                status = 404
            body = json.dumps(payload).encode()
            if fault == "oversized":
                body = b"x" * (boot_check.MAX_RESPONSE_BYTES + 1)
            if fault == "malformed_json" and self.path == "/api/v1/auth":
                body = b"{broken"
            if fault == "array_json" and self.path == "/api/v1/auth":
                body = b"[]"
            self.send_response(status)
            self.send_header(
                "Cache-Control", "max-age=60" if fault == "cacheable" else "no-store"
            )
            self.send_header(
                "Connection", "keep-alive" if fault == "keep_alive" else "close"
            )
            self.send_header("Content-Length", str(len(body)))
            if fault == "redirect":
                self.send_header("Location", "http://invalid.example/")
            if fault == "set_cookie":
                self.send_header("Set-Cookie", "unexpected=fixture")
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                # A rejected status/header may close the client before the fixture body is sent.
                pass
            finally:
                self.close_connection = True

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(
        target=server.serve_forever, kwargs={"poll_interval": 0.01}
    )
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_port}", state
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


class BootCaptureTests(unittest.TestCase):
    def inspect(self, capture=CAPTURE, version="0.1.2", address="192.0.2.10"):
        return boot_check.inspect_boot(capture, version, address)

    def test_complete_boot_retains_memory_evidence(self):
        evidence = self.inspect()
        self.assertEqual(evidence.version, "0.1.2")
        self.assertEqual(evidence.internal_free, 38000)
        self.assertEqual(evidence.dma_free, 30000)

    def test_requires_one_boot_and_one_startup(self):
        for capture in (
            CAPTURE.replace("rst:0x15", "reset"),
            CAPTURE * 2,
            CAPTURE + "rst:0xc (RTC_SW_CPU_RST)\n",
        ):
            with self.subTest(capture=capture[:16]), self.assertRaises(ValueError):
                self.inspect(capture)

    def test_rejects_panics_even_after_startup(self):
        for marker in (
            "Mem alloc fail",
            "Guru Meditation",
            "Backtrace:",
            "panic_abort",
            "Stack smashing",
            "stack overflow",
            "assert failed",
        ):
            with self.subTest(marker=marker), self.assertRaises(ValueError):
                self.inspect(CAPTURE + marker)

    def test_requires_current_version_and_captured_dhcp_target(self):
        for kwargs in ({"version": "0.1.1"}, {"address": "192.0.2.11"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.inspect(**kwargs)

    def test_requires_memory_storage_and_application_completion(self):
        for marker in (
            "SPI SRAM memory test OK",
            "required encrypted SD storage verified",
            "Application startup complete",
        ):
            with self.subTest(marker=marker), self.assertRaises(ValueError):
                self.inspect(CAPTURE.replace(marker, "incomplete"))

    def test_enforces_each_memory_floor(self):
        for counter in ("free=38000", "largest=29000", "free=30000"):
            with self.subTest(counter=counter), self.assertRaises(ValueError):
                self.inspect(CAPTURE.replace(counter, counter.split("=")[0] + "=1024"))

    def test_bounds_capture_size(self):
        with self.assertRaises(ValueError):
            self.inspect("x" * (boot_check.MAX_LOG_BYTES + 1))


class ProductionHttpSmokeTests(unittest.TestCase):
    def test_real_http_sequence_stays_locked_without_using_environment_bearer(self):
        with fixture_server() as (url, state):
            with mock.patch.dict(
                os.environ, {"CORE_PILOT_BEARER": "fixture-not-a-real-token"}
            ):
                boot_check.check_http(url, timeout=1)
            self.assertEqual(state["authorization_headers"], 0)
            self.assertEqual(state["exchanges"], 1)

    def test_rejects_contract_regressions(self):
        for fault in (
            "wrong_product",
            "auth_bypass",
            "open_code",
            "unlocked_system",
            "accepted_cookie",
            "remote_issuance",
            "set_cookie",
            "cacheable",
            "keep_alive",
            "redirect",
            "oversized",
            "malformed_json",
            "array_json",
        ):
            expected = TypeError if fault == "array_json" else ValueError
            with self.subTest(fault=fault), fixture_server(fault) as (url, _):
                with self.assertRaises(expected):
                    boot_check.check_http(url, timeout=1)

    def test_cli_reports_response_type_errors_without_echoing_server_details(self):
        with tempfile.TemporaryDirectory() as directory:
            capture = pathlib.Path(directory) / "boot.log"
            capture.write_text(CAPTURE)
            output = io.StringIO()
            with (
                mock.patch.object(
                    sys,
                    "argv",
                    ["boot_check.py", str(capture), "192.0.2.10", "--version", "0.1.2"],
                ),
                mock.patch.object(
                    boot_check,
                    "check_http",
                    side_effect=TypeError("untrusted response detail"),
                ),
                redirect_stdout(output),
            ):
                self.assertEqual(boot_check.main(), 1)
            self.assertIn("Boot/API check failed", output.getvalue())
            self.assertNotIn("untrusted response detail", output.getvalue())

    def test_does_not_attempt_exchange_when_a_code_is_already_open(self):
        with fixture_server("open_code") as (url, state):
            with self.assertRaises(ValueError):
                boot_check.check_http(url, timeout=1)
            self.assertEqual(state["exchanges"], 0)


if __name__ == "__main__":
    unittest.main()
