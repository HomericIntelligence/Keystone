"""Exercise repository GoogleTest registration with real CMake/CTest, no build."""

import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
UNIT_TARGETS = (
    "unit_tests",
    "concurrency_unit_tests",
    "scheduler_backoff_tests",
    "simulation_unit_tests",
    "transport_unit_tests",
    "bridge_unit_tests",
)
NONUNIT_TARGETS = ("distributed_hierarchy_tests", "scheduler_sigterm_tests")
THREAD_POOL_CASES = (
    "ThreadPoolTest.CreateAndDestroy",
    "ThreadPoolTest.HardwareConcurrency",
)


def case_names(target: str) -> tuple[str, ...]:
    if target == "concurrency_unit_tests":
        return THREAD_POOL_CASES
    return (f"{target}.Selected",)


def discovery_call(target: str) -> str:
    # These calls contain literal target names and property lists, not nested
    # CMake expressions. Preserve the entire call; never synthesize its labels.
    matches: list[str] = re.findall(
        rf"(?m)^\s*(gtest_discover_tests\(\s*{re.escape(target)}\b[^()]*\))",
        (ROOT / "CMakeLists.txt").read_text(),
    )
    if len(matches) != 1:
        raise ValueError(f"Expected one literal discovery call for {target}")
    return matches[0]


def discover(calls: dict[str, str]) -> tuple[dict[str, Any], dict[str, Any]]:
    """Use imported listing fixtures to isolate discovery from C++ dependencies."""
    cmake = shutil.which("cmake")
    ctest = shutil.which("ctest")
    if cmake is None or ctest is None:
        raise RuntimeError("CMake and CTest must be on PATH (use the uv toolchain)")
    with tempfile.TemporaryDirectory(prefix="keystone-unit-selection-") as temporary:
        root = Path(temporary)
        project = [
            "cmake_minimum_required(VERSION 3.20)",
            "project(UnitSelection NONE)",
            "enable_testing()",
            "include(GoogleTest)",
            "set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)",
        ]
        for target, call in calls.items():
            executable = root / target
            listing = "".join(
                f"{suite}.\n  {name}\n"
                for suite, name in (case.split(".") for case in case_names(target))
            )
            executable.write_text(
                "#!/bin/sh\n"
                'test "$1" = --gtest_list_tests || exit 99\n'
                f"cat <<'TEST_LIST'\n{listing}TEST_LIST\n"
            )
            executable.chmod(0o700)
            project.extend(
                (
                    f"add_executable({target} IMPORTED)",
                    (
                        f"set_target_properties({target} PROPERTIES "
                        f"IMPORTED_LOCATION [==[{executable}]==])"
                    ),
                    call,
                )
            )
        (root / "CMakeLists.txt").write_text("\n".join(project) + "\n")
        build = root / "build"

        def run(argv: list[str]) -> str:
            result = subprocess.run(
                argv, capture_output=True, text=True, timeout=15, check=False
            )
            if result.returncode:
                raise RuntimeError(
                    f"Discovery setup failed: {argv!r}\n"
                    f"{result.stdout}\n{result.stderr}"
                )
            return result.stdout

        run([cmake, "-S", str(root), "-B", str(build)])
        command = [ctest, "--test-dir", str(build), "--show-only=json-v1"]
        all_tests: dict[str, Any] = json.loads(run(command))
        unit_tests: dict[str, Any] = json.loads(run([*command, "-L", "unit"]))
        return all_tests, unit_tests


def tests_by_name(inventory: dict[str, Any]) -> dict[str, Any]:
    return {test["name"]: test for test in inventory["tests"]}


class UnitTestSelection(unittest.TestCase):
    def test_imported_discovery_boundary(self) -> None:
        """Prove PRE_TEST support before attributing a selection failure."""
        all_tests, unit_tests = discover(
            {
                "control": "gtest_discover_tests(control PROPERTIES LABELS unit "
                "RUN_SERIAL TRUE)",
                "nonunit_control": "gtest_discover_tests(nonunit_control)",
            }
        )
        self.assertEqual(
            set(tests_by_name(all_tests)),
            {"control.Selected", "nonunit_control.Selected"},
        )
        selected = tests_by_name(unit_tests)
        self.assertEqual(set(selected), {"control.Selected"})
        properties = {
            item["name"]: item["value"]
            for item in selected["control.Selected"]["properties"]
        }
        self.assertIs(properties["RUN_SERIAL"], True)

    def test_repository_unit_selection(self) -> None:
        for profiling in (False, True):
            targets: tuple[str, ...] = (*UNIT_TARGETS, *NONUNIT_TARGETS)
            if profiling:
                targets += ("profiling_tests", "tls_integration_tests")
            all_tests, unit_tests = discover(
                {target: discovery_call(target) for target in targets}
            )
            discovered = tests_by_name(all_tests)
            selected = tests_by_name(unit_tests)
            # Missing discoveries are setup failures, distinct from lost labels.
            self.assertEqual(
                set(discovered),
                {case for target in targets for case in case_names(target)},
                "The imported executables did not produce the expected inventory",
            )
            for target in targets:
                for case in case_names(target):
                    with self.subTest(profiling=profiling, case=case):
                        self.assertEqual(
                            case in selected,
                            target in UNIT_TARGETS or target == "profiling_tests",
                            f"ctest -L unit selected {sorted(selected)}",
                        )
            properties = {
                item["name"]: item["value"]
                for item in discovered["scheduler_backoff_tests.Selected"]["properties"]
            }
            self.assertIs(properties["RUN_SERIAL"], True)

    def test_thread_pool_controls_match_real_cases(self) -> None:
        source = (ROOT / "tests/unit/test_thread_pool.cpp").read_text()
        for case in THREAD_POOL_CASES:
            suite, name = case.split(".")
            with self.subTest(case=case):
                self.assertRegex(source, rf"TEST\({suite},\s*{name}\)")


if __name__ == "__main__":
    unittest.main()
