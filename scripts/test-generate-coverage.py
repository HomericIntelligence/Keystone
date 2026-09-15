"""Exercise the public coverage gate with controlled lcov/genhtml processes."""

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().with_name("generate_coverage.sh")
# Synthetic coverage data in the format emitted by lcov 2.3.1. These numbers
# exercise the parser; they are not a measurement of Keystone coverage.
SUMMARY = """Summary coverage rate:
  source files: 46
  lines.......: 84.0% (2233 of 2657 lines)
  functions...: 92.7% (471 of 508 functions)
Message summary:
  no messages were reported
"""
TOOL = r"""
import json
import os
from pathlib import Path
import sys

tool = Path(sys.argv[0]).name
operation = sys.argv[1] if tool == "lcov" else tool
with Path(os.environ["COVERAGE_FIXTURE_CALLS"]).open("a") as log:
    log.write(json.dumps([tool, *sys.argv[1:]]) + "\n")
if operation == "--summary":
    stream = sys.stderr if os.environ["COVERAGE_FIXTURE_STREAM"] == "stderr" else sys.stdout
    print(os.environ["COVERAGE_FIXTURE_SUMMARY"], end="", file=stream)
if operation == os.environ.get("COVERAGE_FIXTURE_FAIL"):
    print("coverage fixture tool failure: " + operation, file=sys.stderr)
    raise SystemExit(42)
if operation in ("--capture", "--remove"):
    target = Path(sys.argv[sys.argv.index("--output-file") + 1])
    target.write_text("TN:controlled coverage fixture\n")
elif operation == "genhtml":
    target = Path(sys.argv[sys.argv.index("--output-directory") + 1])
    target.mkdir(parents=True)
    (target / "index.html").write_text("<p>Controlled coverage fixture</p>\n")
elif operation != "--summary":
    raise SystemExit("Unexpected coverage fixture operation: " + operation)
"""


def run_coverage(
    summary: str = SUMMARY,
    *,
    fail: str = "",
    stream: str = "stdout",
    html_only: bool = False,
) -> tuple[subprocess.CompletedProcess[str], list[list[str]]]:
    """Run the unchanged script; replace only the external coverage tools."""
    with tempfile.TemporaryDirectory(prefix="keystone-coverage-fixture-") as temporary:
        root = Path(temporary)
        binary = root / "bin"
        binary.mkdir()
        # Do not inherit browser launchers, compiler tools, or an ambient repo.
        # Bash, its text tools, and the threshold calculator are real executables.
        for name in ("bash", "mkdir", "chmod", "rm", "grep", "awk", "bc"):
            executable = shutil.which(name)
            if executable is None:
                raise RuntimeError(f"Required coverage test tool is missing: {name}")
            (binary / name).symlink_to(executable)
        for name in ("lcov", "genhtml"):
            tool = binary / name
            tool.write_text(f"#!{sys.executable}\n" + TOOL)
            tool.chmod(0o700)
        build = root / "build"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("# Controlled prebuilt fixture\n")
        if html_only:
            reports = build / "reports/coverage"
            reports.mkdir(parents=True)
            (reports / "coverage_filtered.info").write_text("TN:controlled fixture\n")
        log = root / "calls.jsonl"
        env = {
            "PATH": str(binary),
            "HOME": str(root),
            "LANG": "C",
            "LC_ALL": "C",
            "BUILD_DIR": str(build),
            "COVERAGE_FIXTURE_CALLS": str(log),
            "COVERAGE_FIXTURE_SUMMARY": summary,
            "COVERAGE_FIXTURE_STREAM": stream,
            "COVERAGE_FIXTURE_FAIL": fail,
        }
        argv = [str(binary / "bash"), str(SCRIPT)]
        if html_only:
            argv.append("--html-only")
        result = subprocess.run(
            argv,
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        # Retain real subprocess results in test output, including passing cases.
        print(
            json.dumps(
                {
                    "fixture": {"summary": summary, "fail": fail, "stream": stream},
                    "argv": argv,
                    "exit_code": result.returncode,
                    "stdout": result.stdout,
                    "stderr": result.stderr,
                    "calls": calls,
                }
            ),
            flush=True,
        )
        return result, calls


class GenerateCoverage(unittest.TestCase):
    def test_supported_lcov_summaries_pass(self) -> None:
        for dots in (6, 7):
            for stream in ("stdout", "stderr"):
                with self.subTest(dots=dots, stream=stream):
                    summary = SUMMARY.replace("lines.......:", f"lines{'.' * dots}:")
                    result, calls = run_coverage(summary, stream=stream)
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    self.assertIn("Coverage meets threshold", result.stdout)
                    self.assertEqual(
                        sum(
                            call[1] == "--summary"
                            for call in calls
                            if call[0] == "lcov"
                        ),
                        1,
                        "The gate must evaluate the same summary it reports",
                    )

    def test_threshold_boundaries(self) -> None:
        for percent, expected in (("74.9", 1), ("75.0", 0), ("75.1", 0), ("100", 0)):
            with self.subTest(percent=percent):
                result, _ = run_coverage(SUMMARY.replace("84.0%", percent + "%"))
                self.assertEqual(
                    result.returncode, expected, result.stdout + result.stderr
                )
                if expected:
                    self.assertIn("Coverage below threshold", result.stdout)
                    self.assertNotIn("Coverage meets threshold", result.stdout)

    def test_invalid_or_missing_line_coverage_fails_closed(self) -> None:
        for line in (
            "",
            "  lines.......: n/a (0 of 0 lines)\n",
            "  lines.......: 84.0oops% (84 of 100 lines)\n",
            "  lines.......: 101% (101 of 100 lines)\n",
            "  lines.......: -1% (-1 of 100 lines)\n",
            "  lines.......: 84.0%\n  lines.......: 90.0%\n",
        ):
            with self.subTest(line=line):
                result, _ = run_coverage("Summary coverage rate:\n" + line)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Invalid or missing line coverage", result.stderr)
                self.assertNotIn("Coverage meets threshold", result.stdout)

    def test_coverage_tool_failures_propagate(self) -> None:
        for operation in ("--capture", "--remove", "genhtml", "--summary"):
            with self.subTest(operation=operation):
                result, _ = run_coverage(fail=operation)
                self.assertEqual(result.returncode, 42, result.stdout + result.stderr)
                self.assertIn("coverage fixture tool failure", result.stderr)
                self.assertNotIn("Coverage meets threshold", result.stdout)

    def test_html_only_uses_the_same_threshold_gate(self) -> None:
        for percent, expected in (("84.0", 0), ("74.9", 1)):
            with self.subTest(percent=percent):
                result, calls = run_coverage(
                    SUMMARY.replace("84.0%", percent + "%"), html_only=True
                )
                self.assertEqual(
                    result.returncode, expected, result.stdout + result.stderr
                )
                self.assertEqual(
                    [call[1] for call in calls if call[0] == "lcov"], ["--summary"]
                )


if __name__ == "__main__":
    unittest.main()
