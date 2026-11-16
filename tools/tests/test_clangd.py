from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import shlex
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {name: module}):
        spec.loader.exec_module(module)
    return module


with mock.patch.dict(sys.modules, {"build_gateway_rcp": load("build_gateway_rcp")}):
    configure = load("configure_clangd")
launcher = load("clangd")


class ConfigureTests(unittest.TestCase):
    def test_real_projects_and_exported_native_database(self):
        commands = configure.commands()
        self.assertEqual(len(commands), 5)
        self.assertIn("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", commands[0])
        decoded = [shlex.split(command[-2]) for command in commands[1:]]
        builds = [command[command.index("-B") + 1] for command in decoded]
        self.assertEqual(len(set(builds)), 4)
        self.assertIn("firmware/s3/build-tests-clang", builds)
        self.assertIn("firmware/s3/test/esp32s3/build-clang", builds)
        for command in commands[1:]:
            self.assertEqual(
                command[1:6],
                ["--log-file", os.devnull, "--do-not-track", "true", "run"],
            )
            self.assertEqual(command[-1], "v6.0.2")
        tests = next(
            command for command in decoded if "firmware/s3/build-tests-clang" in command
        )
        selection = next(arg for arg in tests if arg.startswith("TEST_COMPONENTS="))
        self.assertIn("main", selection.split("=", 1)[1].split(";"))
        self.assertIn("core_display", selection)
        self.assertIn("BUILD_TESTS=OFF", tests)
        h2 = decoded[1]
        self.assertIn("GATEWAY_RCP_MANIFEST_HEX=" + configure.recipe_digest().hex(), h2)

    def test_clang_sdkconfigs_are_unique_build_local_and_not_production(self):
        # Include spaces to verify the EIM shell command preserves absolute paths.
        with mock.patch.object(configure, "ROOT", Path("/workspace/core pilot")):
            commands = configure.commands()
            configs = []
            for wrapped in commands[1:]:
                command = shlex.split(wrapped[-2])
                definitions = [arg for arg in command if arg.startswith("SDKCONFIG=")]
                self.assertEqual(len(definitions), 1)
                definition = definitions[0]
                self.assertEqual(command[command.index(definition) - 1], "-D")
                config = Path(definition.split("=", 1)[1])
                build = configure.ROOT / command[command.index("-B") + 1]
                project = configure.ROOT / command[command.index("-C") + 1]
                self.assertTrue(config.is_absolute())
                self.assertEqual(config, build / "sdkconfig")
                self.assertNotEqual(config, project / "sdkconfig")
                self.assertNotEqual(config, configure.ROOT / "firmware/s3/sdkconfig")
                self.assertNotEqual(config, configure.ROOT / "firmware/h2/sdkconfig")
                self.assertFalse(
                    any(arg.startswith("SDKCONFIG_DEFAULTS=") for arg in command)
                )
                configs.append(config)
            self.assertEqual(len(configs), 4)
            self.assertEqual(len(set(configs)), 4)

    def test_stops_on_first_failure(self):
        with mock.patch.object(
            configure.subprocess, "run", return_value=subprocess.CompletedProcess([], 7)
        ) as run:
            self.assertEqual(configure.main(), 7)
        self.assertEqual(run.call_count, 1)

    def test_runs_every_configuration_without_build_or_flash(self):
        with mock.patch.object(
            configure.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)
        ) as run:
            self.assertEqual(configure.main(), 0)
        self.assertEqual(run.call_count, 5)
        for call in run.call_args_list:
            self.assertEqual(call.kwargs["cwd"], configure.ROOT)
            self.assertNotIn("flash", call.args[0])


class LauncherTests(unittest.TestCase):
    def launch(self, stdout, returncode=0, compiler="/sdk/esp-clang/bin/clangd"):
        result = subprocess.CompletedProcess(
            [], returncode, stdout, "activation failed"
        )
        output = io.StringIO()
        errors = io.StringIO()
        with (
            mock.patch.object(
                launcher.shutil, "which", side_effect=["/bin/eim", compiler]
            ),
            mock.patch.object(launcher.subprocess, "run", return_value=result) as run,
            mock.patch.object(launcher.os, "execve") as execute,
            mock.patch.object(sys, "argv", ["clangd.py", "--check=example.cpp"]),
            contextlib.redirect_stdout(output),
            contextlib.redirect_stderr(errors),
        ):
            status = launcher.main()
        self.assertEqual(output.getvalue(), "")
        return status, run, execute, errors.getvalue()

    def test_activation_chatter_never_reaches_protocol(self):
        environment = {"PATH": "/sdk/esp-clang/bin:/usr/bin", "IDF_VERSION": "6.0.2"}
        _, run, execute, _ = self.launch(
            "EIM activation chatter\n" + json.dumps(environment) + "\n"
        )
        self.assertTrue(run.call_args.kwargs["capture_output"])
        self.assertEqual(
            run.call_args.args[0][1:6],
            ["--log-file", os.devnull, "--do-not-track", "true", "run"],
        )
        args = execute.call_args.args
        self.assertEqual(args[2], environment)
        self.assertIn("--check=example.cpp", args[1])
        self.assertIn("/usr/bin/c++", args[1][1])

    def test_rejects_failed_or_malformed_activation(self):
        for stdout, code in [("not json", 0), ("", 5), ("[]", 0)]:
            with self.subTest(stdout=stdout):
                status, _, execute, _ = self.launch(stdout, code)
                self.assertNotEqual(status, 0)
                execute.assert_not_called()

    def test_refuses_host_clangd(self):
        status, _, execute, _ = self.launch(
            '{"PATH":"/usr/bin"}', compiler="/usr/bin/clangd"
        )
        self.assertEqual(status, 1)
        execute.assert_not_called()

    def test_missing_eim(self):
        with (
            mock.patch.object(launcher.shutil, "which", return_value=None),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(launcher.main(), 1)


if __name__ == "__main__":
    unittest.main()
