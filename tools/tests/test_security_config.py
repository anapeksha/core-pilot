import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class SecurityConfigurationTests(unittest.TestCase):
    def test_production_memory_hardening_is_enabled(self) -> None:
        defaults = (ROOT / "firmware/s3/sdkconfig.defaults").read_text()
        required = (
            "CONFIG_COMPILER_STACK_CHECK_MODE_STRONG=y",
            "CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y",
            "CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y",
            "CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y",
        )
        for setting in required:
            with self.subTest(setting=setting):
                self.assertIn(setting, defaults)
        self.assertNotIn("CONFIG_CORE_PILOT_DEVELOPMENT_AUTH_BYPASS=y", defaults)

    def test_development_build_enables_light_heap_poisoning(self) -> None:
        defaults = (ROOT / "firmware/s3/sdkconfig.development.defaults").read_text()
        self.assertIn("CONFIG_HEAP_POISONING_LIGHT=y", defaults)

    def test_development_code_zero_initializes_stack_variables(self) -> None:
        project = (ROOT / "firmware/s3/CMakeLists.txt").read_text()
        self.assertIn("if(CONFIG_CORE_PILOT_DEVELOPMENT_AUTH_BYPASS)", project)
        self.assertIn("-ftrivial-auto-var-init=zero", project)


if __name__ == "__main__":
    unittest.main()
