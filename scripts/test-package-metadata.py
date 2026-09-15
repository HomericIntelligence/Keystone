"""Exercise the production CPack configuration with a small real ELF payload.

The fixture installs the system `true` executable as data; it never runs a
Keystone binary or builds C++. Only the dependency-inspector failure case
substitutes a tool. Package generation and metadata inspection use real tools.
"""

from __future__ import annotations

import json
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def command(
    argv: list[str], cwd: Path, **kwargs: object
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv, cwd=cwd, text=True, capture_output=True, timeout=15, check=False, **kwargs
    )
    print(
        json.dumps(
            {
                "argv": argv,
                "cwd": str(cwd),
                "exit_code": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
            }
        ),
        flush=True,
    )
    return result


class PackageMetadataTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="keystone-package-metadata-"
        )
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.tools: dict[str, str] = {}
        for name in ("cmake", "cpack", "dpkg-deb", "true"):
            tool = shutil.which(name)
            self.assertIsNotNone(tool, f"Required packaging test tool missing: {name}")
            self.tools[name] = str(tool)
        self.assertTrue(
            Path(self.tools["true"]).read_bytes().startswith(b"\x7fELF"),
            "This dependency test requires a Linux ELF payload",
        )

        # Run the actual public packaging block. The controlled seam replaces
        # target installation only, avoiding FetchContent and the C++ build.
        source = (ROOT / "CMakeLists.txt").read_text()
        start = 'set(CPACK_PACKAGE_NAME "Keystone")'
        end = "include(CPack)"
        self.assertEqual(source.count(start), 1)
        self.assertEqual(source.count(end), 1)
        packaging = start + source.split(start, 1)[1].split(end, 1)[0] + end
        for name in ("LICENSE", "README.md"):
            shutil.copyfile(ROOT / name, self.directory / name)
        shutil.copyfile(self.tools["true"], self.directory / "runtime-payload")
        (self.directory / "marker.txt").write_text("Controlled CPack fixture input.\n")
        (self.directory / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\n"
            "project(PackageMetadataFixture VERSION 0.1.0 LANGUAGES NONE)\n"
            "set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})\n"
            "install(PROGRAMS runtime-payload DESTINATION bin COMPONENT keystone)\n"
            "foreach(component keystone-dev keystone-doc keystone-test keystone-misc)\n"
            "  install(FILES marker.txt DESTINATION share/${component} COMPONENT ${component})\n"
            "endforeach()\n" + packaging + "\n"
        )
        configured = command(
            [
                self.tools["cmake"],
                "-S",
                str(self.directory),
                "-B",
                str(self.directory / "build"),
            ],
            self.directory,
        )
        self.assertEqual(
            configured.returncode, 0, configured.stdout + configured.stderr
        )

    def package(
        self, label: str, *, failing_inspector: bool = False
    ) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        argv = [
            self.tools["cpack"],
            "--config",
            str(self.directory / "build" / "CPackConfig.cmake"),
            "-G",
            "DEB",
            "-B",
            str(self.directory / label),
        ]
        if failing_inspector:
            inspector_tool = shutil.which("dpkg-shlibdeps")
            self.assertIsNotNone(
                inspector_tool, "Required packaging test tool missing: dpkg-shlibdeps"
            )
            tools = self.directory / "controlled-tools"
            tools.mkdir()
            inspector = tools / "dpkg-shlibdeps"
            inspector.write_text(
                "#!/bin/sh\n"
                'case "$1" in\n'
                "  --help|--version) exec "
                + shlex.quote(str(inspector_tool))
                + ' "$@" ;;\n'
                "esac\n"
                "echo 'fixture: dependency inspection failed' >&2\n"
                "exit 42\n"
            )
            inspector.chmod(0o700)
            environment["PATH"] = str(tools) + os.pathsep + environment["PATH"]
            argv.extend(["-D", "CPACK_COMPONENTS_ALL=keystone"])
        return command(argv, self.directory, env=environment)

    def test_grouped_packages_keep_names_and_declare_dependencies(self) -> None:
        result = self.package("normal")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        packages = sorted((self.directory / "normal").glob("*.deb"))
        self.assertEqual(len(packages), 5)
        fields: dict[str, dict[str, str]] = {}
        for package in packages:
            inspected = command(
                [self.tools["dpkg-deb"], "--field", str(package)], self.directory
            )
            self.assertEqual(
                inspected.returncode, 0, inspected.stdout + inspected.stderr
            )
            metadata = dict(
                line.split(": ", 1)
                for line in inspected.stdout.splitlines()
                if line and not line.startswith(" ") and ": " in line
            )
            fields[metadata["Package"]] = metadata
        self.assertEqual(
            set(fields),
            {
                "keystone-runtime",
                "keystone-development",
                "keystone-documentation",
                "keystone-testing",
                "keystone-tools",
            },
        )
        with self.subTest(package="keystone-runtime"):
            self.assertRegex(
                fields["keystone-runtime"].get("Depends", ""), r"\blibc6\b"
            )
        for name in ("keystone-development", "keystone-testing", "keystone-tools"):
            with self.subTest(package=name):
                self.assertIn(
                    "keystone-runtime (= 0.1.0)", fields[name].get("Depends", "")
                )
        with self.subTest(package="development-toolchain"):
            self.assertIn(
                "cmake (>= 3.20)", fields["keystone-development"].get("Depends", "")
            )
        with self.subTest(package="documentation-architecture"):
            self.assertEqual(fields["keystone-documentation"]["Architecture"], "all")

    def test_dependency_inspector_failure_rejects_package(self) -> None:
        result = self.package("failed-inspection", failing_inspector=True)
        self.assertNotEqual(
            result.returncode, 0, "CPack must reject dependency-inspection failure"
        )
        self.assertIn("fixture: dependency inspection failed", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
