#!/usr/bin/env python3
"""Validate CHANGELOG.md follows Keep a Changelog format.

See https://keepachangelog.com/en/1.1.0/ for the specification.
"""

import re
import sys


# Standard section types defined by Keep a Changelog
VALID_SECTIONS = {
    "added",
    "changed",
    "deprecated",
    "removed",
    "fixed",
    "security",
}

# Required header text
HEADER_PATTERN = re.compile(
    r"\[Keep a Changelog\]\(https://keepachangelog\.com", re.IGNORECASE
)

# Version entry patterns
UNRELEASED_PATTERN = re.compile(r"^## \[Unreleased\]", re.IGNORECASE | re.MULTILINE)
RELEASE_PATTERN = re.compile(
    r"^## \[(\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?)\]", re.MULTILINE
)
SECTION_PATTERN = re.compile(r"^### (.+)$", re.MULTILINE)

# Reference links at bottom
UNRELEASED_LINK_PATTERN = re.compile(
    r"^\[Unreleased\]:\s+https?://", re.IGNORECASE | re.MULTILINE
)


def lint(path: str) -> list[str]:
    """Return a list of lint errors for the given CHANGELOG file."""
    errors: list[str] = []

    try:
        content = open(path, encoding="utf-8").read()
    except OSError as exc:
        return [f"Cannot read {path}: {exc}"]

    # 1. Must reference Keep a Changelog
    if not HEADER_PATTERN.search(content):
        errors.append(
            "Missing 'Keep a Changelog' reference link "
            "(https://keepachangelog.com/en/1.1.0/)"
        )

    # 2. Must have an [Unreleased] section
    if not UNRELEASED_PATTERN.search(content):
        errors.append("Missing '## [Unreleased]' section")

    # 3. Must have at least one versioned release
    releases = RELEASE_PATTERN.findall(content)
    if not releases:
        errors.append(
            "No versioned release found — expected at least one '## [x.y.z]' entry"
        )

    # 4. All ### subsections must use standard Keep a Changelog names
    for section in SECTION_PATTERN.findall(content):
        name = section.strip()
        if name.lower() not in VALID_SECTIONS:
            errors.append(
                f"Non-standard section '### {name}' — "
                f"valid sections are: {', '.join(sorted(VALID_SECTIONS))}"
            )

    # 5. Must have a comparison link for [Unreleased]
    if not UNRELEASED_LINK_PATTERN.search(content):
        errors.append(
            "Missing '[Unreleased]: https://...' comparison link at end of file"
        )

    return errors


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: lint-changelog.py CHANGELOG.md", file=sys.stderr)
        return 1

    all_ok = True
    for path in sys.argv[1:]:
        errors = lint(path)
        if errors:
            all_ok = False
            print(f"CHANGELOG lint errors in {path}:", file=sys.stderr)
            for err in errors:
                print(f"  - {err}", file=sys.stderr)
        else:
            print(f"{path}: OK")

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
