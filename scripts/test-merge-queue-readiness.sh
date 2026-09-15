#!/usr/bin/env bash
# Exercise the real queue-contract checker with private configuration fixtures.
set -euo pipefail

SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Keep the real parser in the repository's locked environment while each
# checker invocation reads only its private workflow fixture.
export UV_PROJECT="$SOURCE_ROOT"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/keystone-queue-test.XXXXXX")"
trap 'rm -rf "$TEST_ROOT"' EXIT

setup() {
    FIXTURE="$TEST_ROOT/$1"
    mkdir -p "$FIXTURE/.github/workflows" "$FIXTURE/docs" "$FIXTURE/scripts"
    cp "$SOURCE_ROOT"/.github/workflows/{_required,extras,release-please}.yml \
        "$FIXTURE/.github/workflows/"
    mkdir -p "$FIXTURE/tests/fixtures"
    cp -R "$SOURCE_ROOT/tests/fixtures/merge-queue-readiness" "$FIXTURE/tests/fixtures/"
    cp "$SOURCE_ROOT/docs/CICD_QUALITY_GATES.md" "$FIXTURE/docs/"
    cp "$SOURCE_ROOT/scripts/check-merge-queue-readiness.sh" \
        "$SOURCE_ROOT/scripts/run_ci_local.sh" "$FIXTURE/scripts/"
    cp "$SOURCE_ROOT"/{.release-please-manifest.json,CMakeLists.txt,conanfile.py} "$FIXTURE/"
}

check_fixture() {
    if (cd "$FIXTURE" && bash scripts/check-merge-queue-readiness.sh) > "$FIXTURE/output" 2>&1; then
        RESULT=0
    else
        RESULT=$?
    fi
}

expect_rejection() {
    check_fixture
    if [ "$RESULT" -eq 0 ] || ! grep -qF "$1" "$FIXTURE/output"; then
        cat "$FIXTURE/output"
        return 1
    fi
}

insert_job_field() {
    local job=$1 field=$2 workflow=${3:-_required}
    awk -v job="$job" -v field="$field" '
        { print }
        $0 == "  " job ":" { print "    " field }
    ' "$FIXTURE/.github/workflows/$workflow.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/$workflow.yml"
}

current_queue_contract_passes() {
    check_fixture
    if [ "$RESULT" -ne 0 ]; then cat "$FIXTURE/output"; return 1; fi
}

missing_required_trigger_is_rejected() {
    awk '
        /^  merge_group:$/ { skip=1; next }
        skip && /^    types:/ { skip=0; next }
        { print }
    ' "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection '_required.yml: missing merge_group event mapping'
}

coverage_cannot_skip_queue() {
    insert_job_field coverage "if: github.event_name != 'merge_group'"
    expect_rejection 'required context coverage (coverage) must be unconditional'
}

required_dependency_cannot_skip_queue() {
    insert_job_field lint "if: github.event_name != 'merge_group'"
    expect_rejection 'required context lint (lint) must be unconditional'
}

extra_dependency_cannot_skip_queue() {
    insert_job_field unit-tests 'needs: queue-setup'
    cat >> "$FIXTURE/.github/workflows/_required.yml" <<'YAML'

  queue-setup:
    name: queue-setup
    if: vars.QUEUE_SETUP_ENABLED
    runs-on: ubuntu-latest
    steps:
      - run: echo setup
YAML
    expect_rejection 'queue dependency queue-setup must be unconditional'
}

missing_dependency_is_rejected() {
    insert_job_field unit-tests 'needs: absent-setup'
    expect_rejection 'queue dependency is missing: absent-setup'
}

missing_install_context_is_rejected() {
    sed 's/^    name: install$/    name: other-install/' \
        "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection 'live required contexts missing from real producers: install'
}

duplicate_coverage_context_is_rejected() {
    cat >> "$FIXTURE/.github/workflows/_required.yml" <<'YAML'

  duplicate-coverage:
    name: coverage
    runs-on: ubuntu-latest
    steps:
      - run: echo duplicate
YAML
    expect_rejection 'required context coverage needs exactly one producer per workflow'
}

job_failure_cannot_be_ignored() {
    insert_job_field coverage 'continue-on-error: true'
    expect_rejection 'queue job coverage must not ignore failure'
}

all_required_steps_cannot_be_conditional() {
    awk '
        /^  coverage:/ { in_job=1 }
        in_job && /^  [[:alnum:]_-]+:/ && !/^  coverage:/ { in_job=0 }
        { print }
        in_job && /^      - name:/ { print "        if: vars.RUN_VALIDATION" }
    ' "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection 'queue job has no unconditional command: coverage'
}

release_publisher_cannot_join_queue() {
    awk '{ print } /^on:/ { print "  merge_group:\n    types: [checks_requested]" }' \
        "$FIXTURE/.github/workflows/release-please.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/release-please.yml"
    expect_rejection 'release-please.yml: artifact publisher must not run on merge_group'
}

unsupported_trigger_filter_is_rejected() {
    awk '{ print } /^  merge_group:/ { print "    paths: [src/**]" }' \
        "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection '_required.yml: unsupported merge_group configuration'
}

missing_extras_trigger_is_rejected() {
    awk '
        /^  merge_group:$/ { skip=1; next }
        skip && /^    types:/ { skip=0; next }
        { print }
    ' "$FIXTURE/.github/workflows/extras.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/extras.yml"
    expect_rejection 'extras.yml: missing merge_group event mapping'
}

extras_coverage_cannot_skip_queue() {
    insert_job_field coverage "if: github.event_name != 'merge_group'" extras
    expect_rejection 'extras.yml: required context coverage (coverage) must be unconditional'
}

dependency_cycle_is_rejected() {
    insert_job_field unit-tests 'needs: test'
    expect_rejection 'queue dependency cycle:'
}

unsupported_dependency_is_rejected() {
    insert_job_field unit-tests 'needs: {job: lint}'
    expect_rejection 'unsupported queue dependencies: unit-tests'
}

valid_extra_dependency_passes() {
    insert_job_field unit-tests 'needs: [queue-setup]'
    cat >> "$FIXTURE/.github/workflows/_required.yml" <<'YAML'

  queue-setup:
    name: queue-setup
    runs-on: ubuntu-latest
    steps:
      - run: echo setup
YAML
    current_queue_contract_passes
}

dependency_failure_cannot_be_ignored() {
    valid_extra_dependency_passes || return 1
    insert_job_field queue-setup 'continue-on-error: true'
    expect_rejection 'queue job queue-setup must not ignore failure'
}

all_required_step_failures_cannot_be_ignored() {
    awk '
        /^  coverage:/ { in_job=1 }
        in_job && /^  [[:alnum:]_-]+:/ && !/^  coverage:/ { in_job=0 }
        { print }
        in_job && /^      - name:/ { print "        continue-on-error: true" }
    ' "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection 'queue job has no unconditional command: coverage'
}

required_step_event_guard_is_rejected() {
    awk '
        { print }
        /^      - name: Run tests for coverage$/ { print "        if: github.event_name != '\''merge_group'\''" }
    ' "$FIXTURE/.github/workflows/extras.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/extras.yml"
    expect_rejection 'has an unapproved event guard:'
}

ref_only_concurrency_is_rejected() {
    sed 's/group: required-.*/group: required-${{ github.ref }}/' \
        "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection 'concurrency.group must include'
}

smoke_carrier_is_rejected() {
    cp "$FIXTURE/tests/fixtures/merge-queue-readiness/merge-queue-smoke.yaml" \
        "$FIXTURE/.github/workflows/merge-queue-smoke.yaml"
    expect_rejection 'smoke-only merge-group carrier remains'
}

release_version_check() {
    # Execute the canonical release step's standard-library Python, with only the
    # declared release metadata copied into the private fixture.
    awk '
        /^      - name: Verify version consistency across release surfaces$/ { selected=1 }
        selected && /^          python3 - <<.*PYEOF/ { in_script=1; next }
        in_script && /^          PYEOF/ { exit }
        in_script { sub(/^          /, ""); print }
    ' "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/version.py"
    if [ ! -s "$FIXTURE/version.py" ]; then return 1; fi
    if (cd "$FIXTURE" && python3 version.py) > "$FIXTURE/output" 2>&1; then
        RESULT=0
    else
        RESULT=$?
    fi
}

release_surfaces_are_current() {
    release_version_check || return 1
    if [ "$RESULT" -ne 0 ]; then cat "$FIXTURE/output"; return 1; fi
}

release_mismatch_is_rejected() {
    printf '{".": "999.0.0"}\n' > "$FIXTURE/.release-please-manifest.json"
    release_version_check || return 1
    if [ "$RESULT" -eq 0 ] || ! grep -qF 'Release surfaces disagree' "$FIXTURE/output"; then
        cat "$FIXTURE/output"
        return 1
    fi
}

passed=0
failed=0
for test in current_queue_contract_passes missing_required_trigger_is_rejected \
    coverage_cannot_skip_queue required_dependency_cannot_skip_queue \
    extra_dependency_cannot_skip_queue missing_dependency_is_rejected \
    missing_install_context_is_rejected duplicate_coverage_context_is_rejected \
    job_failure_cannot_be_ignored all_required_steps_cannot_be_conditional \
    release_publisher_cannot_join_queue unsupported_trigger_filter_is_rejected \
    missing_extras_trigger_is_rejected extras_coverage_cannot_skip_queue \
    dependency_cycle_is_rejected unsupported_dependency_is_rejected \
    valid_extra_dependency_passes dependency_failure_cannot_be_ignored \
    all_required_step_failures_cannot_be_ignored required_step_event_guard_is_rejected \
    ref_only_concurrency_is_rejected smoke_carrier_is_rejected \
    release_surfaces_are_current release_mismatch_is_rejected; do
    setup "$test"
    if "$test"; then
        echo "PASS $test"
        passed=$((passed + 1))
    else
        echo "FAIL $test"
        failed=$((failed + 1))
    fi
done
echo "$passed passed; $failed failed"
[ "$failed" -eq 0 ]
