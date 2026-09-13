#!/usr/bin/env bash
# Exercise the real queue-contract checker with private configuration fixtures.
set -euo pipefail

SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/keystone-queue-test.XXXXXX")"
trap 'rm -rf "$TEST_ROOT"' EXIT

setup() {
    FIXTURE="$TEST_ROOT/$1"
    mkdir -p "$FIXTURE/.github/workflows" "$FIXTURE/docs" "$FIXTURE/scripts"
    cp "$SOURCE_ROOT"/.github/workflows/{_required,extras,merge-queue-smoke,release-please}.yml \
        "$FIXTURE/.github/workflows/"
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
    local job=$1 field=$2
    awk -v job="$job" -v field="$field" '
        { print }
        $0 == "  " job ":" { print "    " field }
    ' "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
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
    expect_rejection '_required.yml: missing merge_group trigger'
}

coverage_cannot_skip_queue() {
    insert_job_field coverage "if: github.event_name != 'merge_group'"
    expect_rejection 'queue job must be unconditional: coverage'
}

required_dependency_cannot_skip_queue() {
    insert_job_field lint "if: github.event_name != 'merge_group'"
    expect_rejection 'queue job must be unconditional: lint'
}

extra_dependency_cannot_skip_queue() {
    insert_job_field unit-tests 'needs: queue-setup'
    cat >> "$FIXTURE/.github/workflows/_required.yml" <<'YAML'

  queue-setup:
    if: github.event_name != 'merge_group'
    runs-on: ubuntu-latest
    steps:
      - run: echo setup
YAML
    expect_rejection 'queue job must be unconditional: queue-setup'
}

missing_dependency_is_rejected() {
    insert_job_field unit-tests 'needs: absent-setup'
    expect_rejection 'queue dependency is missing: absent-setup'
}

missing_install_context_is_rejected() {
    sed 's/^    name: install$/    name: other-install/' \
        "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection 'required queue context needs exactly one producer: install'
}

duplicate_coverage_context_is_rejected() {
    cat >> "$FIXTURE/.github/workflows/_required.yml" <<'YAML'

  duplicate-coverage:
    name: coverage
    runs-on: ubuntu-latest
    steps:
      - run: echo duplicate
YAML
    expect_rejection 'required queue context needs exactly one producer: coverage'
}

job_failure_cannot_be_ignored() {
    insert_job_field coverage 'continue-on-error: true'
    expect_rejection 'queue job must be unconditional: coverage'
}

all_required_steps_cannot_be_conditional() {
    awk '
        /^  coverage:/ { in_job=1 }
        in_job && /^  [[:alnum:]_-]+:/ && !/^  coverage:/ { in_job=0 }
        { print }
        in_job && /^      - name:/ { print "        if: github.event_name != '\''merge_group'\''" }
    ' "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection 'queue job has no unconditional command: coverage'
}

release_publisher_cannot_join_queue() {
    awk '{ print } /^on:/ { print "  merge_group:\n    types: [checks_requested]" }' \
        "$FIXTURE/.github/workflows/release-please.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/release-please.yml"
    expect_rejection 'release-please.yml: must not trigger on merge_group'
}

unsupported_trigger_filter_is_rejected() {
    awk '{ print } /^  merge_group:/ { print "    paths: [src/**]" }' \
        "$FIXTURE/.github/workflows/_required.yml" > "$FIXTURE/changed.yml"
    mv "$FIXTURE/changed.yml" "$FIXTURE/.github/workflows/_required.yml"
    expect_rejection '_required.yml: unsupported merge_group configuration'
}

smoke_version_check() {
    # Execute the actual smoke step's standard-library Python, with only the
    # declared release metadata copied into the private fixture.
    awk '
        /^          python3 - <<.*PYEOF/ { in_script=1; next }
        in_script && /^          PYEOF/ { exit }
        in_script { sub(/^          /, ""); print }
    ' "$FIXTURE/.github/workflows/merge-queue-smoke.yml" > "$FIXTURE/version.py"
    if [ ! -s "$FIXTURE/version.py" ]; then return 1; fi
    if (cd "$FIXTURE" && python3 version.py) > "$FIXTURE/output" 2>&1; then
        RESULT=0
    else
        RESULT=$?
    fi
}

smoke_release_surfaces_are_current() {
    smoke_version_check || return 1
    if [ "$RESULT" -ne 0 ]; then cat "$FIXTURE/output"; return 1; fi
}

smoke_release_mismatch_is_rejected() {
    printf '{".": "999.0.0"}\n' > "$FIXTURE/.release-please-manifest.json"
    smoke_version_check || return 1
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
    smoke_release_surfaces_are_current smoke_release_mismatch_is_rejected; do
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
