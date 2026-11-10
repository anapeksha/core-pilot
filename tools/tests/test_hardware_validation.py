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


if __name__ == "__main__":
    unittest.main()
