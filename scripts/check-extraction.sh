#!/usr/bin/env bash
# Enforces ADR-015 (C++ agent hierarchy extraction) and ADR-016 (Python
# orchestration extraction). Fails CI if any extracted artifact reappears.
#
# Layer 1 — directory + file structural checks (cheap, catches re-extraction).
# Layer 2 — usage-form symbol check: ANY occurrence of the four extracted
#           class names in src/, include/, tests/. The four classes all live
#           in ProjectAgamemnon now; any mention in this repo (including
#           `std::make_shared<X>`, `#include <agents/X.hpp>`, Doxygen @code
#           examples, or comments) is a regression.
set -euo pipefail

fail=0
report() { printf 'FAIL: %s\n' "$*" >&2; fail=1; }

# --- ADR-015: agent layer directories must not exist ---------------------
[[ -d src/agents ]]     && report "src/agents/ exists (ADR-015: extracted to ProjectAgamemnon)"
[[ -d include/agents ]] && report "include/agents/ exists (ADR-015: extracted to ProjectAgamemnon)"

# --- ADR-015 layer 1: strict class-definition check ----------------------
defs=$(git grep -nE \
  'class[[:space:]]+(ChiefArchitectAgent|ComponentLeadAgent|ModuleLeadAgent|TaskAgent)\b' \
  -- src include tests || true)
if [[ -n "$defs" ]]; then
  report "agent class definitions present in tree (ADR-015):"
  printf '%s\n' "$defs" >&2
fi

# --- ADR-015 layer 2: broad usage-form check -----------------------------
# Matches ANY occurrence: std::make_shared<X>, includes, types, comments,
# Doxygen examples. This catches the exact form the prior plan would have
# missed (message_bus.hpp:117). Intentional aggressive match — all four
# classes live in ProjectAgamemnon and have no legitimate use in this repo.
uses=$(git grep -nE \
  '\b(ChiefArchitectAgent|ComponentLeadAgent|ModuleLeadAgent|TaskAgent)\b' \
  -- src include tests || true)
if [[ -n "$uses" ]]; then
  report "agent class names referenced in tree (ADR-015 — extracted to ProjectAgamemnon):"
  printf '%s\n' "$uses" >&2
fi

# --- ADR-016: Python orchestration package must not exist ----------------
# ADR-016 forbids re-introducing the extracted Python *orchestration package*.
# A tooling-only pyproject.toml that merely locks the uv-managed build toolchain
# (cmake/ninja/conan/gcovr — ADR-018) ships NO Python package and is allowed: it
# must have `package = false` and declare no shipped modules/scripts. Fail only
# if pyproject.toml actually ships Python (a real package/scripts, or omits the
# `package = false` marker). The orchestration modules themselves are still
# banned unconditionally by the src/keystone/*.py checks below.
if [[ -f pyproject.toml ]]; then
  if ! grep -Eq '^\s*package\s*=\s*false' pyproject.toml \
     || grep -Eq '^\s*\[project\.scripts\]|^\s*packages\s*=|^\s*\[tool\.(setuptools|hatch)\.packages' pyproject.toml; then
    report "top-level pyproject.toml ships Python (ADR-016: Keystone ships no Python; only a package=false build-tooling pyproject is allowed per ADR-018)"
  fi
fi
for f in daemon.py dag_walker.py task_claimer.py nats_listener.py \
         models.py config.py logging.py validation.py maestro_client.py \
         __init__.py __main__.py; do
  [[ -f "src/keystone/$f" ]] && report "src/keystone/$f exists (ADR-016)"
done

# --- ADR-016: tests/*.py orchestration tests must not exist --------------
shopt -s nullglob
py_tests=( tests/test_*.py )
shopt -u nullglob
if (( ${#py_tests[@]} > 0 )); then
  report "tests/*.py orchestration tests present (ADR-016):"
  printf '  %s\n' "${py_tests[@]}" >&2
fi

exit "$fail"
