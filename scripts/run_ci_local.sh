#!/bin/bash
# Run the Keystone CI suite locally inside a container.
#
# Mirrors what GitHub Actions runs, using the same CI container image.
# Supports both Podman (rootless, no SU — preferred) and Docker.
#
# Usage:
#   ./scripts/run_ci_local.sh              # Run all CI checks
#   ./scripts/run_ci_local.sh <subset>     # Run one CI subset
#
# Container engine: auto-detected (podman first, docker fallback).
# Override: CONTAINER_ENGINE=docker ./scripts/run_ci_local.sh
#
# Image: requires 'keystone-ci:local' built from the current source.
# Build locally: just ci-build  (or: podman build -f ci/Containerfile -t keystone-ci:local .)

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SUBSET="${1:-all}"

LOCAL_IMAGE="keystone-ci:local"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[CI]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[CI]${NC} $*"; }
log_error() { echo -e "${RED}[CI]${NC} $*" >&2; }
log_step()  { echo -e "
${BLUE}==>${NC} $*"; }

# ============================================================================
# Container engine detection
# ============================================================================

detect_engine() {
    if [ -n "${CONTAINER_ENGINE:-}" ]; then
        if ! command -v "${CONTAINER_ENGINE}" &> /dev/null; then
            log_error "CONTAINER_ENGINE=${CONTAINER_ENGINE} not found in PATH"
            exit 1
        fi
        log_info "Container engine: ${CONTAINER_ENGINE} (from env)"
        return
    fi

    if command -v podman &> /dev/null; then
        CONTAINER_ENGINE="podman"
        log_info "Container engine: podman (rootless)"
    elif command -v docker &> /dev/null; then
        CONTAINER_ENGINE="docker"
        log_info "Container engine: docker"
    else
        log_error "No container engine found. Install podman (recommended) or docker."
        exit 1
    fi
    export CONTAINER_ENGINE
}

# ============================================================================
# Image selection
# ============================================================================

select_image() {
    if "${CONTAINER_ENGINE}" image inspect "${LOCAL_IMAGE}" &> /dev/null; then
        IMAGE="${LOCAL_IMAGE}"
        log_info "Using local image: ${IMAGE}"
    else
        log_error "Image ${LOCAL_IMAGE} not found. Build it with: just ci-build"
        exit 1
    fi
}

# ============================================================================
# Subset execution
# ============================================================================

run_step() {
    local desc="$1"; shift
    log_step "$desc"
    if ! "$@"; then
        log_error "FAILED: $desc"
        exit 1
    fi
    log_info "OK: $desc"
}

run_in_container() {
    local cmd="$1"
    local engine_arguments=(run --rm)
    if [ "${CONTAINER_ENGINE##*/}" != docker ]; then
        engine_arguments+=("--userns=keep-id:uid=1000,gid=1000")
    fi
    "${CONTAINER_ENGINE}" "${engine_arguments[@]}" \
        -v "${PROJECT_ROOT}:/workspace:Z" -w /workspace \
        -e CMAKE_BUILD_PARALLEL_LEVEL=2 \
        "${IMAGE}" bash -lc "set -euo pipefail; $cmd"
}

# ============================================================================
# Subset definitions
# ============================================================================

run_lint() {
    # Lint — Keystone is a pure C++20 library (ADR-015/016): the only Python is
    # conanfile.py + scripts/, so mypy targets those (main's CI dropped ruff)
    # and pre-commit covers clang-format/yamllint/trailing-whitespace.
    run_in_container 'uv run --locked mypy conanfile.py scripts/check-release.py && uv run --locked ruff check scripts/check-release.py && uv run --locked ruff format --check scripts/check-release.py && shellcheck scripts/run_ci_local.sh scripts/test-ci-local.sh scripts/check-ci-policy.sh scripts/check-symlinks.sh scripts/check-merge-queue-readiness.sh scripts/test-merge-queue-readiness.sh ci/install-tool.sh ci/test-install-tool.sh && uv run --locked pre-commit run --all-files --show-diff-on-failure && just check-extraction && just test-ci-local && just fleet-tidy'
}

run_markdownlint() {
    # Markdown lint (markdownlint-cli2, matching the native CI job; the tool is
    # baked into the CI image via nodejs, not a PyPI package)
    run_in_container "markdownlint-cli2 \"**/*.md\" \"!**/.claude/**\" \"!CHANGELOG.md\""
}

run_uv-lock-check() {
    # uv.lock in sync
    run_in_container "uv lock --check && uv sync --all-groups --all-extras --locked"
}

run_typecheck() {
    # Type check
    run_in_container 'uv run --locked mypy conanfile.py scripts/check-release.py'
}

run_unit-tests() {
    # C++ unit tests (ctest) — mirrors the native CI job (make deps +
    # make compile.debug + ctest -L unit). CONTAINER_CHECK/CONTAINER_PREFIX
    # are cleared so the Makefile runs directly in the CI image instead of
    # re-entering the podman-compose dev container.
    run_in_container 'uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= deps && uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= compile.debug && cd build/x86.debug && uv run --locked ctest --output-on-failure --no-tests=error -L unit -j2 --timeout 120'
}

run_integration-tests() {
    # C++ integration/sanitizer matrix (asan/ubsan/tsan/lsan) — mirrors the
    # native CI job, running the Makefile directly inside the CI image.
    run_in_container 'command -v nats-server && uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= deps && uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= CMAKE_FEATURE_FLAGS=-DENABLE_FLEET_INTEGRATION_TESTS=ON compile.debug.asan test.debug.asan compile.debug.ubsan test.debug.ubsan compile.debug.tsan test.debug.tsan compile.debug.lsan test.debug.lsan'
}

run_schema-validation() {
    # Schema validation
    run_in_container 'uv run --locked check-jsonschema --builtin-schema vendor.github-workflows .github/workflows/*.yml && bash scripts/check-merge-queue-readiness.sh'
}

run_security-secrets-scan() {
    # Secrets scan (gitleaks)
    run_in_container 'gitleaks detect --no-banner --redact --source .'
}

run_security-dependency-scan() {
    # Dependency vulnerability scan (pip-audit)
    run_in_container "uv run pip-audit"
}

run_deps-version-sync() {
    # Dependency version sync check
    run_in_container "uv sync --locked"
}

run_forbid-suppressions() {
    # No silent failure suppressions
    run_in_container 'bash scripts/check-ci-policy.sh'
}

run_justfile-check() {
    # justfile syntax check
    run_in_container "just --evaluate > /dev/null"
}

run_symlink-check() {
    # Symlink integrity
    run_in_container 'bash scripts/check-symlinks.sh'
}

run_release() {
    # Execute the same static release checks; no tag/release publication.
    run_in_container 'uv run --locked python scripts/check-release.py'
}

run_build() {
    run_in_container 'uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= deps && uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= compile.release'
}

run_install() {
    run_in_container 'uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= deps && uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= compile.release && uv run --locked bash scripts/check-install.sh'
}

run_package() {
    run_in_container "$(cat <<'COMMAND'
        uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= deps
        uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= compile.release
        cd build/x86.release
        for format in DEB RPM TGZ ZIP; do uv run --locked cpack -G "$format"; done
        shopt -s nullglob
        packages=(*.deb *.rpm *.tar.gz *.zip)
        if [ "${#packages[@]}" -eq 0 ]; then echo "CPack produced no package artifacts" >&2; exit 1; fi
        printf "%s\n" "${packages[@]}"
COMMAND
)"
}

run_coverage() {
    run_in_container 'uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= deps && uv run --locked make NPROC=2 CONTAINER_CHECK= CONTAINER_PREFIX= compile.debug.coverage test.debug.coverage && BUILD_DIR=build/x86.coverage.debug uv run --locked bash scripts/generate_coverage.sh'
}

# ============================================================================
# Dispatch
# ============================================================================

detect_engine
if [ "$SUBSET" = image-build ]; then
    cd "$PROJECT_ROOT"
    build_arguments=(build)
    if [ "${CONTAINER_ENGINE##*/}" != docker ]; then
        build_arguments+=(--ignorefile ci/.dockerignore)
    fi
    "${CONTAINER_ENGINE}" "${build_arguments[@]}" -f ci/Containerfile -t "$LOCAL_IMAGE" .
    exit 0
fi
select_image

case "${SUBSET}" in
    all)
        for subset in forbid-suppressions uv-lock-check lint markdownlint justfile-check \
            symlink-check schema-validation deps-version-sync release unit-tests integration-tests \
            build install package coverage security-secrets-scan security-dependency-scan; do
            run_step "$subset" "run_${subset}"
        done
        ;;
    lint) run_lint ;;
    markdownlint) run_markdownlint ;;
    uv-lock-check) run_uv-lock-check ;;
    uv-check) run_uv-lock-check ;;
    typecheck) run_typecheck ;;
    unit-tests) run_unit-tests ;;
    integration-tests) run_integration-tests ;;
    schema-validation) run_schema-validation ;;
    security-secrets-scan) run_security-secrets-scan ;;
    security-dependency-scan) run_security-dependency-scan ;;
    deps-version-sync) run_deps-version-sync ;;
    forbid-suppressions) run_forbid-suppressions ;;
    justfile-check) run_justfile-check ;;
    symlink-check) run_symlink-check ;;
    release) run_release ;;
    build) run_build ;;
    install) run_install ;;
    package) run_package ;;
    coverage) run_coverage ;;

    *)
    log_error "Unknown subset '${SUBSET}'"
    exit 1
    ;;
esac

log_info "All CI checks passed (${SUBSET})."
