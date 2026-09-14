#!/usr/bin/env bash
# Exercise the real CI launcher with controlled engine/tools and the Markdown CLI.
set -euo pipefail

SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REAL_JUST="$(command -v just)"
REAL_MARKDOWNLINT="$(command -v markdownlint-cli2)"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/keystone-ci-test.XXXXXX")"
trap 'rm -rf "$TEST_ROOT"' EXIT

setup() {
    FIXTURE="$TEST_ROOT/$1"
    mkdir -p "$FIXTURE/repo/scripts" "$FIXTURE/repo/.github/workflows" "$FIXTURE/bin"
    cp "$SOURCE_ROOT/scripts/run_ci_local.sh" "$FIXTURE/repo/scripts/"
    for helper in check-symlinks.sh check-extraction.sh check-ci-policy.sh check-release.py \
        check-workflow-schema.sh test-workflow-schema-validation.sh; do
        if [ -f "$SOURCE_ROOT/scripts/$helper" ]; then
            cp "$SOURCE_ROOT/scripts/$helper" "$FIXTURE/repo/scripts/"
        fi
    done
    mkdir -p "$FIXTURE/repo/tests/fixtures"
    cp -R "$SOURCE_ROOT/tests/fixtures/workflow-schema" "$FIXTURE/repo/tests/fixtures/"
    printf 'name: fixture\non: push\njobs: {}\n' > "$FIXTURE/repo/.github/workflows/test.yml"
    printf 'jobs:\n  release:\n    steps:\n      - run: "true"\n' > "$FIXTURE/repo/.github/workflows/_required.yml"
    printf 'default:\n  echo fixture\n' > "$FIXTURE/repo/justfile"
    for helper in check-install generate_coverage check-merge-queue-readiness; do
        cat > "$FIXTURE/repo/scripts/$helper.sh" <<'HELPER'
#!/usr/bin/env bash
printf '%s\n' "${0##*/}" >> "$CI_TEST_CALLS"
if [ "${0##*/}" = check-merge-queue-readiness.sh ] && [ "${CI_TEST_FAIL:-}" = queue-contract ]; then
    exit 48
fi
HELPER
        chmod +x "$FIXTURE/repo/scripts/$helper.sh"
    done
    git -C "$FIXTURE/repo" init -q
    git -C "$FIXTURE/repo" add .
    cat > "$FIXTURE/bin/engine" <<'ENGINE'
#!/usr/bin/env bash
set -euo pipefail
if [ "$1" = image ]; then exit 0; fi
if [ "$1" != run ]; then exit 92; fi
while [ "$#" -gt 0 ] && [ "$1" != bash ]; do shift; done
if [ "$#" -eq 0 ]; then exit 93; fi
shift
# Do not read host login profiles. The supplied command still runs in real Bash.
if [ "$1" = -lc ]; then shift; exec bash -c "$1"; fi
exec bash "$@"
ENGINE
    cat > "$FIXTURE/bin/uv" <<'UV'
#!/usr/bin/env bash
set -euo pipefail
if [ "$1" = run ]; then
    shift
    while [[ "${1:-}" = --* ]]; do shift; done
    if [ "${1:-}" = check-jsonschema ] && [ "${CI_TEST_FAIL:-}" = missing-validator ]; then
        exec "$(dirname "$0")/missing-validator"
    fi
    exec "$@"
fi
printf 'uv %s\n' "$*" >> "$CI_TEST_CALLS"
exit 0
UV
    cat > "$FIXTURE/bin/tool" <<'TOOL'
#!/usr/bin/env bash
set -euo pipefail
name="${0##*/}"
printf '%s %s\n' "$name" "$*" >> "$CI_TEST_CALLS"
case "$name" in
    podman) exit 41 ;;
    docker) exit 0 ;;
    make)
        mkdir -p build/x86.debug build/x86.release
        if [ "${CI_TEST_FAIL:-}" = make ]; then exit 41; fi
        ;;
    cpack)
        if [ "${CI_TEST_FAIL:-}" = cpack ]; then exit 46; fi
        if [ "${CI_TEST_FAIL:-}" != no-packages ]; then
            case "$2" in
                DEB) touch fixture.deb ;;
                RPM) touch fixture.rpm ;;
                TGZ) touch fixture.tar.gz ;;
                ZIP) touch fixture.zip ;;
                *) exit 47 ;;
            esac
        fi
        ;;
    ctest)
        if [ "${CI_TEST_FAIL:-}" = ctest ]; then
            if [ ! -f "$CI_TEST_CALLS.ctest" ]; then
                touch "$CI_TEST_CALLS.ctest"
                exit 42
            fi
        fi
        if [ "${CI_TEST_FAIL:-}" = empty-tests ] && [[ " $* " != *' --no-tests=error '* ]]; then
            echo 'No tests were found!!!'
            exit 0
        fi
        if [ "${CI_TEST_FAIL:-}" = empty-tests ]; then exit 8; fi
        ;;
    gitleaks)
        if [[ " $* " != *' --redact '* ]]; then exit 94; fi
        if [ "${CI_TEST_FAIL:-}" = gitleaks ]; then echo 'controlled redacted finding'; exit 43; fi
        ;;
    check-jsonschema)
        # The negative-schema harness runs its real helper against a declared
        # invalid workflow. Model that validator result at the tool boundary.
        if [[ " $* " = *' tests/fixtures/workflow-schema/invalid-workflow.yaml '* ]]; then
            if [ "${CI_TEST_FAIL:-}" = accepted-invalid-schema ]; then exit 0; fi
            echo 'Schema validation errors were encountered' >&2
            exit 1
        fi
        if [ "${CI_TEST_FAIL:-}" = schema ]; then echo 'invalid workflow' >&2; exit 44; fi
        if [ "${CI_TEST_FAIL:-}" = schema-contract ]; then
            if [[ " $* " != *' --builtin-schema vendor.github-workflows '* ]]; then exit 45; fi
        fi
        ;;
esac
exit 0
TOOL
    chmod +x "$FIXTURE/bin/engine" "$FIXTURE/bin/uv" "$FIXTURE/bin/tool"
    # The CI image supplies GNU readlink; Homebrew exposes it as greadlink.
    if command -v greadlink > /dev/null 2>&1; then
        ln -s "$(command -v greadlink)" "$FIXTURE/bin/readlink"
    fi
    for tool in make ctest cpack gitleaks check-jsonschema mypy ruff shellcheck pre-commit just markdownlint-cli2 pip-audit nats-server; do
        ln -s tool "$FIXTURE/bin/$tool"
    done
    export CI_TEST_CALLS="$FIXTURE/calls" CI_TEST_FAIL=""
    : > "$CI_TEST_CALLS"
}

launch() {
    local subset="$1"
    if (cd "$FIXTURE/repo" && PATH="$FIXTURE/bin:$PATH" CONTAINER_ENGINE="$FIXTURE/bin/engine" \
        bash scripts/run_ci_local.sh "$subset") > "$FIXTURE/output" 2>&1; then
        RESULT=0
    else
        RESULT=$?
    fi
}

expect_success() {
    if [ "$RESULT" -ne 0 ]; then cat "$FIXTURE/output"; return 1; fi
}

expect_failure() {
    if [ "$RESULT" -eq 0 ]; then cat "$FIXTURE/output"; return 1; fi
}

all_dispatches() {
    launch all
    expect_success || return 1
    for tool in mypy markdownlint-cli2 check-jsonschema gitleaks pip-audit ctest; do
        grep -q "^$tool " "$CI_TEST_CALLS" || return 1
    done
}

all_includes_install_package_and_coverage() {
    launch all
    expect_success || return 1
    for expected in 'compile.release' 'check-install' 'cpack -G DEB' 'cpack -G RPM' \
        'cpack -G TGZ' 'cpack -G ZIP' 'test.debug.coverage' 'generate_coverage'; do
        grep -qF "$expected" "$CI_TEST_CALLS" || return 1
    done
}

package_failure_is_not_success() { CI_TEST_FAIL=cpack; launch package; expect_failure; }
no_packages_is_not_success() { CI_TEST_FAIL=no-packages; launch package; expect_failure; }

image_build_failure_does_not_switch_engine() {
    cp "$SOURCE_ROOT/justfile" "$FIXTURE/repo/justfile"
    ln -s tool "$FIXTURE/bin/podman"
    ln -s tool "$FIXTURE/bin/docker"
    if (cd "$FIXTURE/repo" && PATH="$FIXTURE/bin:$PATH" "$REAL_JUST" ci-build) \
        > "$FIXTURE/output" 2>&1; then RESULT=0; else RESULT=$?; fi
    expect_failure || return 1
    if grep -q '^docker ' "$CI_TEST_CALLS"; then return 1; fi
}

launch_docker() {
    local recipe="$1"
    cp "$SOURCE_ROOT/justfile" "$FIXTURE/repo/justfile"
    cat > "$FIXTURE/bin/docker" <<'DOCKER'
#!/usr/bin/env bash
set -euo pipefail
printf 'docker %s\n' "$*" >> "$CI_TEST_CALLS"
for argument in "$@"; do
    case "$argument" in
        --ignorefile|--userns=keep-id*)
            echo "unsupported Docker option: $argument" >&2
            exit 95
            ;;
    esac
done
if [ "$1" = build ]; then exit 0; fi
exec "$(dirname "$0")/engine" "$@"
DOCKER
    chmod +x "$FIXTURE/bin/docker"
    if (cd "$FIXTURE/repo" && PATH="$FIXTURE/bin:$PATH" CONTAINER_ENGINE="$FIXTURE/bin/docker" \
        "$REAL_JUST" "$recipe") > "$FIXTURE/output" 2>&1; then RESULT=0; else RESULT=$?; fi
}

docker_build_accepts_its_cli_arguments() {
    launch_docker ci-build
    expect_success || return 1
    grep -q '^docker build ' "$CI_TEST_CALLS"
}

docker_run_accepts_its_cli_arguments() {
    launch_docker ci-schema-validation
    expect_success || return 1
    grep -q '^check-jsonschema ' "$CI_TEST_CALLS"
}

schema_failure() { CI_TEST_FAIL=schema; launch schema-validation; expect_failure; }
schema_contract() {
    CI_TEST_FAIL=schema-contract
    launch schema-validation
    expect_success || return 1
    grep -q '^check-jsonschema --builtin-schema vendor.github-workflows ' "$CI_TEST_CALLS"
}
schema_missing() {
    CI_TEST_FAIL=missing-validator
    launch schema-validation
    expect_failure
}
queue_contract_failure() {
    CI_TEST_FAIL=queue-contract
    launch schema-validation
    if [ "$RESULT" -ne 48 ]; then cat "$FIXTURE/output"; return 1; fi
    grep -q '^check-merge-queue-readiness.sh$' "$CI_TEST_CALLS"
}
all_schema_failure_stops_queue() {
    CI_TEST_FAIL=schema
    launch all
    expect_failure || return 1
    grep -q 'invalid workflow' "$FIXTURE/output" || return 1
    if grep -q '^check-merge-queue-readiness.sh$' "$CI_TEST_CALLS"; then return 1; fi
    if grep -qE 'compile.release|^cpack ' "$CI_TEST_CALLS"; then return 1; fi
}
all_negative_schema_failure_stops_queue() {
    CI_TEST_FAIL=accepted-invalid-schema
    launch all
    expect_failure || return 1
    grep -q 'invalid .yaml workflow fixture passed schema validation' "$FIXTURE/output" || return 1
    if grep -q '^check-merge-queue-readiness.sh$' "$CI_TEST_CALLS"; then return 1; fi
    if grep -qE 'compile.release|^cpack ' "$CI_TEST_CALLS"; then return 1; fi
}
scanner_failure() { CI_TEST_FAIL=gitleaks; launch security-secrets-scan; expect_failure; }
scanner_success() { launch security-secrets-scan; expect_success; }
suppression_failure() {
    printf 'false %s\n' '|| true' > "$FIXTURE/repo/bad.sh"
    git -C "$FIXTURE/repo" add bad.sh
    launch forbid-suppressions
    expect_failure
}
suppression_success() { launch forbid-suppressions; expect_success; }
required_workflow_opt_out_fails() {
    cat > "$FIXTURE/repo/.github/workflows/_required.yml" <<'WORKFLOW'
jobs:
  release:
    steps:
      - run: exit 1
        continue-on-error: true
WORKFLOW
    launch forbid-suppressions
    expect_failure
}
failed_build_stops_tests() {
    CI_TEST_FAIL="make"
    launch unit-tests
    expect_failure || return 1
    if grep -q '^ctest ' "$CI_TEST_CALLS"; then return 1; fi
}
failed_tests_cannot_fall_back() { CI_TEST_FAIL=ctest; launch unit-tests; expect_failure; }
empty_tests_fail() { CI_TEST_FAIL=empty-tests; launch unit-tests; expect_failure; }
broken_symlink_fails() {
    ln -s absent "$FIXTURE/repo/broken"
    git -C "$FIXTURE/repo" add broken
    launch symlink-check
    expect_failure || return 1
    grep -q 'broken symlink' "$FIXTURE/output"
}

valid_tracked_symlink_passes() {
    ln -s justfile "$FIXTURE/repo/valid"
    git -C "$FIXTURE/repo" add valid
    launch symlink-check
    expect_success
}

build_cache_symlink_is_not_a_source_failure() {
    mkdir "$FIXTURE/repo/build"
    ln -s absent "$FIXTURE/repo/build/old-cache"
    launch symlink-check
    expect_success
}

markdown_fixture() {
    cp "$SOURCE_ROOT/.markdownlint.yaml" "$SOURCE_ROOT/.markdownlint-cli2.jsonc" \
        "$SOURCE_ROOT/.markdownlintignore" "$FIXTURE/repo/"
    ln -sf "$REAL_MARKDOWNLINT" "$FIXTURE/bin/markdownlint-cli2"
    mkdir -p "$FIXTURE/repo/build/fleet-tidy/_deps/fixture-src" "$FIXTURE/repo/docs/build"
    printf '# Source\n\nValid source documentation.\n' > "$FIXTURE/repo/docs/build/source.md"
    printf '# Generated dependency\n\n## Duplicate\n\n## Duplicate\n' \
        > "$FIXTURE/repo/build/fleet-tidy/_deps/fixture-src/README.md"
}

markdown_ignores_generated_build_dependencies() {
    markdown_fixture
    launch markdownlint
    expect_success
}

markdown_rejects_invalid_source_beside_build_dependencies() {
    markdown_fixture
    printf '# Source\n\n## Duplicate\n\n## Duplicate\n' > "$FIXTURE/repo/docs/build/source.md"
    launch markdownlint
    expect_failure || return 1
    grep -q 'docs/build/source.md:.*MD024' "$FIXTURE/output"
}

release_checks_execute_declared_steps() {
    cat > "$FIXTURE/repo/.github/workflows/_required.yml" <<'WORKFLOW'
jobs:
  release:
    steps:
      - uses: actions/checkout@fixture
      - uses: actions/setup-python@fixture
      - run: printf 'validated\n' > first-step
      - run: test -f first-step
WORKFLOW
    launch release
    expect_success || return 1
    test -f "$FIXTURE/repo/first-step"
}

release_checks_stop_on_failure() {
    cat > "$FIXTURE/repo/.github/workflows/_required.yml" <<'WORKFLOW'
jobs:
  release:
    steps:
      - run: exit 31
      - run: touch should-not-run
WORKFLOW
    launch release
    expect_failure || return 1
    test ! -e "$FIXTURE/repo/should-not-run"
}

release_job_context_cannot_be_ignored() {
    cat > "$FIXTURE/repo/.github/workflows/_required.yml" <<'WORKFLOW'
jobs:
  release:
    env:
      DIFFERENT_INPUT: value
    steps:
      - run: "true"
WORKFLOW
    launch release
    expect_failure
}

failures=0
index=0
dispatch_test() {
    case "$1" in
        all_dispatches) all_dispatches ;;
        schema_failure) schema_failure ;;
        schema_contract) schema_contract ;;
        schema_missing) schema_missing ;;
        queue_contract_failure) queue_contract_failure ;;
        all_schema_failure_stops_queue) all_schema_failure_stops_queue ;;
        all_negative_schema_failure_stops_queue) all_negative_schema_failure_stops_queue ;;
        scanner_failure) scanner_failure ;;
        scanner_success) scanner_success ;;
        suppression_failure) suppression_failure ;;
        suppression_success) suppression_success ;;
        required_workflow_opt_out_fails) required_workflow_opt_out_fails ;;
        failed_build_stops_tests) failed_build_stops_tests ;;
        failed_tests_cannot_fall_back) failed_tests_cannot_fall_back ;;
        empty_tests_fail) empty_tests_fail ;;
        broken_symlink_fails) broken_symlink_fails ;;
        valid_tracked_symlink_passes) valid_tracked_symlink_passes ;;
        build_cache_symlink_is_not_a_source_failure) build_cache_symlink_is_not_a_source_failure ;;
        markdown_ignores_generated_build_dependencies) markdown_ignores_generated_build_dependencies ;;
        markdown_rejects_invalid_source_beside_build_dependencies) markdown_rejects_invalid_source_beside_build_dependencies ;;
        release_checks_execute_declared_steps) release_checks_execute_declared_steps ;;
        release_checks_stop_on_failure) release_checks_stop_on_failure ;;
        release_job_context_cannot_be_ignored) release_job_context_cannot_be_ignored ;;
        all_includes_install_package_and_coverage) all_includes_install_package_and_coverage ;;
        package_failure_is_not_success) package_failure_is_not_success ;;
        no_packages_is_not_success) no_packages_is_not_success ;;
        image_build_failure_does_not_switch_engine) image_build_failure_does_not_switch_engine ;;
        docker_build_accepts_its_cli_arguments) docker_build_accepts_its_cli_arguments ;;
        docker_run_accepts_its_cli_arguments) docker_run_accepts_its_cli_arguments ;;
        *) return 1 ;;
    esac
}
for test in all_dispatches schema_failure schema_contract schema_missing queue_contract_failure scanner_failure scanner_success \
    all_schema_failure_stops_queue all_negative_schema_failure_stops_queue \
    suppression_failure suppression_success failed_build_stops_tests failed_tests_cannot_fall_back \
    empty_tests_fail broken_symlink_fails valid_tracked_symlink_passes build_cache_symlink_is_not_a_source_failure \
    markdown_ignores_generated_build_dependencies markdown_rejects_invalid_source_beside_build_dependencies \
    release_checks_execute_declared_steps release_checks_stop_on_failure \
    all_includes_install_package_and_coverage package_failure_is_not_success no_packages_is_not_success \
    image_build_failure_does_not_switch_engine required_workflow_opt_out_fails \
    release_job_context_cannot_be_ignored docker_build_accepts_its_cli_arguments \
    docker_run_accepts_its_cli_arguments; do
    index=$((index + 1))
    setup "$test"
    if dispatch_test "$test"; then
        printf 'ok %s - %s\n' "$index" "$test"
    else
        printf 'not ok %s - %s\n' "$index" "$test"
        failures=$((failures + 1))
    fi
done
printf '1..%s\n' "$index"
exit "$failures"
