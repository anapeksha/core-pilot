from __future__ import annotations

import importlib.util
import io
import pathlib
import sys
import unittest
import urllib.error
from contextlib import redirect_stdout
from email.message import Message
from unittest import mock

MODULE_PATH = pathlib.Path(__file__).parents[1] / "test.py"
SPEC = importlib.util.spec_from_file_location("hardware_validation", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
hardware_validation = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = hardware_validation
SPEC.loader.exec_module(hardware_validation)


class HardwareValidationTests(unittest.TestCase):
    def test_wifi_readiness_does_not_require_thread(self) -> None:
        sample = hardware_validation.Sample(1, True, False, True, True)

        self.assertTrue(
            hardware_validation.sample_is_ready(sample, 1, require_thread=False)
        )
        with redirect_stdout(io.StringIO()):
            self.assertFalse(
                hardware_validation.sample_is_ready(sample, 1, require_thread=True)
            )

    def test_readiness_detects_reset(self) -> None:
        sample = hardware_validation.Sample(2, True, True, True, True)

        with redirect_stdout(io.StringIO()):
            self.assertFalse(hardware_validation.sample_is_ready(sample, 1))

    def test_canonicalizes_valid_node_id(self) -> None:
        self.assertEqual(
            hardware_validation.canonical_node_id("000000000000ABCD"),
            "000000000000abcd",
        )

    def test_rejects_noncanonical_node_id_length(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exactly 16 hexadecimal digits"):
            hardware_validation.canonical_node_id("abcd")

    def test_selects_online_on_off_endpoint_and_preserves_state(self) -> None:
        devices = [
            {
                "protocol": "matter",
                "node_id": "0000000000001234",
                "endpoint_id": 3,
                "availability": "online",
                "capabilities": ["on_off", "level"],
                "state": [{"OnOff": True}],
            }
        ]

        self.assertEqual(
            hardware_validation.on_off_endpoint(devices, "0000000000001234"),
            (3, True),
        )

    def test_rejects_unavailable_on_off_endpoint(self) -> None:
        devices = [
            {
                "node_id": "0000000000001234",
                "endpoint_id": 3,
                "availability": "unknown",
                "capabilities": ["on_off"],
                "state": [],
            }
        ]

        with self.assertRaisesRegex(RuntimeError, "no online On/Off endpoint"):
            hardware_validation.on_off_endpoint(devices, "0000000000001234")

    def test_validates_rate_limit_contract(self) -> None:
        headers = Message()
        headers["Content-Type"] = "application/json"
        headers["Cache-Control"] = "no-store"
        headers["Connection"] = "close"
        headers["Retry-After"] = "2"
        error = urllib.error.HTTPError(
            "http://corepilot.local/api/v1/health",
            429,
            "Too Many Requests",
            headers,
            io.BytesIO(
                b'{"error":{"code":"rate_limited",'
                b'"message":"Too many requests. Retry later."}}'
            ),
        )

        self.assertEqual(hardware_validation.validate_rate_limit(error), 2)

    def test_rejects_malformed_rate_limit_contract(self) -> None:
        headers = Message()
        headers["Content-Type"] = "application/json"
        headers["Cache-Control"] = "no-store"
        headers["Connection"] = "close"
        headers["Retry-After"] = "0"
        error = urllib.error.HTTPError(
            "http://corepilot.local/api/v1/health",
            429,
            "Too Many Requests",
            headers,
            io.BytesIO(b"{}"),
        )

        with self.assertRaisesRegex(RuntimeError, "non-positive Retry-After"):
            hardware_validation.validate_rate_limit(error)

    def test_rejects_rate_limit_response_without_security_headers(self) -> None:
        headers = Message()
        headers["Content-Type"] = "application/json"
        headers["Retry-After"] = "2"
        error = urllib.error.HTTPError(
            "http://corepilot.local/api/v1/health",
            429,
            "Too Many Requests",
            headers,
            io.BytesIO(
                b'{"error":{"code":"rate_limited",'
                b'"message":"Too many requests. Retry later."}}'
            ),
        )

        with self.assertRaisesRegex(RuntimeError, "omitted Cache-Control"):
            hardware_validation.validate_rate_limit(error)

    def test_command_wait_ignores_stale_status(self) -> None:
        responses = iter(
            (
                {
                    "command": {
                        "id": 7,
                        "kind": "set_on_off",
                        "device_id": "0000000000001234",
                        "endpoint_id": 1,
                        "phase": "complete",
                    }
                },
                {
                    "command": {
                        "id": 8,
                        "kind": "set_on_off",
                        "device_id": "0000000000001234",
                        "endpoint_id": 1,
                        "phase": "complete",
                    }
                },
            )
        )

        def immediate_wait(_description, _timeout, poll):
            first = poll()
            self.assertFalse(first["complete"])
            return poll()

        with (
            mock.patch.object(
                hardware_validation,
                "fetch_json",
                side_effect=lambda *_: next(responses),
            ),
            mock.patch.object(
                hardware_validation, "wait_for", side_effect=immediate_wait
            ),
        ):
            result = hardware_validation.wait_for_command(
                object(),
                "http://corepilot.local",
                1,
                5,
                8,
                "set_on_off",
                "0000000000001234",
                1,
            )

        self.assertTrue(result["complete"])

    def test_command_wait_reports_matching_failure(self) -> None:
        response = {
            "command": {
                "id": 8,
                "kind": "set_on_off",
                "device_id": "0000000000001234",
                "endpoint_id": 1,
                "phase": "failed",
                "last_error": "timeout",
            }
        }

        def immediate_wait(_description, _timeout, poll):
            return poll()

        with (
            mock.patch.object(hardware_validation, "fetch_json", return_value=response),
            mock.patch.object(
                hardware_validation, "wait_for", side_effect=immediate_wait
            ),
            self.assertRaisesRegex(RuntimeError, "set_on_off failed: timeout"),
        ):
            hardware_validation.wait_for_command(
                object(),
                "http://corepilot.local",
                1,
                5,
                8,
                "set_on_off",
                "0000000000001234",
                1,
            )

class RawHttpSecurityTests(unittest.TestCase):
    def test_parses_raw_http_response_without_collapsing_headers(self) -> None:
        response = hardware_validation.parse_raw_http_response(
            b"HTTP/1.1 403 Forbidden\r\nX-Test: one\r\nX-Test: two\r\n\r\nno"
        )
        self.assertEqual(response.status, 403)
        self.assertEqual(response.headers, (("x-test", "one"), ("x-test", "two")))
        self.assertEqual(response.body, b"no")

    def test_rejects_incomplete_raw_http_response(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "header terminator"):
            hardware_validation.parse_raw_http_response(b"HTTP/1.1 200 OK\r\n")

    def test_raw_exchange_sends_exact_bytes_and_reads_to_close(self) -> None:
        class FakeSocket:
            def __init__(self) -> None:
                self.sent = b""
                self.chunks = [b"HTTP/1.1 404 Not Found\r\n", b"Connection: close\r\n\r\n", b""]

            def __enter__(self):
                return self

            def __exit__(self, *_args) -> None:
                return None

            def settimeout(self, _timeout: float) -> None:
                return None

            def sendall(self, data: bytes) -> None:
                self.sent = data

            def recv(self, _size: int) -> bytes:
                return self.chunks.pop(0)

        connection = FakeSocket()
        with mock.patch.object(
            hardware_validation.socket, "create_connection", return_value=connection
        ):
            response = hardware_validation.raw_http_exchange(
                "192.0.2.1", b"GET / HTTP/1.1\r\n\r\n", 1
            )
        self.assertEqual(response.status, 404)
        self.assertEqual(connection.sent, b"GET / HTTP/1.1\r\n\r\n")

    def test_builds_raw_request_without_rewriting_duplicate_headers(self) -> None:
        request = hardware_validation.raw_request_bytes(
            "POST",
            "/api/v1/test%2fpath",
            (("Host", "one"), ("Host", "two"), ("Content-Length", "2")),
            b"{}",
        )
        self.assertEqual(
            request,
            b"POST /api/v1/test%2fpath HTTP/1.1\r\n"
            b"Host: one\r\nHost: two\r\nContent-Length: 2\r\n\r\n{}",
        )

    def test_prompt_close_without_response_is_rejection(self) -> None:
        result = hardware_validation.RawSocketResult(b"", True, False, 0.01)
        self.assertTrue(hardware_validation.rejected_or_closed(result))

    def test_delayed_close_is_not_prompt_rejection(self) -> None:
        result = hardware_validation.RawSocketResult(b"", True, False, 2.1)
        self.assertFalse(hardware_validation.rejected_or_closed(result))

    def test_timeout_without_response_is_not_rejection(self) -> None:
        result = hardware_validation.RawSocketResult(b"", False, True, 1.0)
        self.assertFalse(hardware_validation.rejected_or_closed(result))
        with self.assertRaisesRegex(RuntimeError, "was not rejected: timeout"):
            hardware_validation.require_rejected("probe", result)

    def test_explicit_client_error_is_rejection(self) -> None:
        result = hardware_validation.RawSocketResult(
            b"HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n",
            True,
            False,
            0.01,
        )
        self.assertTrue(hardware_validation.rejected_or_closed(result))

    def test_receive_bounded_preserves_chunks_and_close(self) -> None:
        class FakeSocket:
            def __init__(self) -> None:
                self.chunks = [b"abc", b"def", b""]
                self.timeout = 0.0

            def settimeout(self, timeout: float) -> None:
                self.timeout = timeout

            def recv(self, _size: int) -> bytes:
                return self.chunks.pop(0)

        connection = FakeSocket()
        result = hardware_validation.receive_bounded(connection, 2)
        self.assertEqual(result.data, b"abcdef")
        self.assertTrue(result.closed)
        self.assertFalse(result.timed_out)
        self.assertEqual(connection.timeout, 2)

    def test_receive_bounded_reports_socket_timeout(self) -> None:
        class FakeSocket:
            def settimeout(self, _timeout: float) -> None:
                return None

            def recv(self, _size: int) -> bytes:
                raise hardware_validation.socket.timeout()

        result = hardware_validation.receive_bounded(FakeSocket(), 1)
        self.assertEqual(result.data, b"")
        self.assertFalse(result.closed)
        self.assertTrue(result.timed_out)

    def test_raw_socket_exchange_treats_connection_reset_as_prompt_close(self) -> None:
        with mock.patch.object(
            hardware_validation.socket,
            "create_connection",
            side_effect=ConnectionResetError(),
        ):
            result = hardware_validation.raw_socket_exchange(
                "192.0.2.1", (b"request",), 1
            )
        self.assertTrue(result.closed)
        self.assertFalse(result.timed_out)
        self.assertEqual(result.data, b"")

    def test_raw_socket_exchange_sends_parts_and_half_closes(self) -> None:
        class FakeSocket:
            def __init__(self) -> None:
                self.sent: list[bytes] = []
                self.shutdown_how = None

            def __enter__(self):
                return self

            def __exit__(self, *_args) -> None:
                return None

            def sendall(self, data: bytes) -> None:
                self.sent.append(data)

            def shutdown(self, how: int) -> None:
                self.shutdown_how = how

        connection = FakeSocket()
        expected = hardware_validation.RawSocketResult(b"ok", True, False, 0.1)
        with (
            mock.patch.object(
                hardware_validation.socket, "create_connection", return_value=connection
            ),
            mock.patch.object(
                hardware_validation, "receive_bounded", return_value=expected
            ) as receive,
        ):
            result = hardware_validation.raw_socket_exchange(
                "192.0.2.1", (b"one", b"two"), 3, port=8080, half_close=True
            )
        self.assertEqual(result, expected)
        self.assertEqual(connection.sent, [b"one", b"two"])
        self.assertEqual(connection.shutdown_how, hardware_validation.socket.SHUT_WR)
        receive.assert_called_once_with(connection, 3)


if __name__ == "__main__":
    unittest.main()
