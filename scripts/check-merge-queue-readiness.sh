#!/usr/bin/env bash

set -euo pipefail

failures=0

fail() {
    echo "ERROR: $*" >&2
    failures=$((failures + 1))
}

require_line() {
    local file=$1
    local pattern=$2
    local description=$3

    if ! grep -Eq "$pattern" "$file"; then
        fail "$file: missing $description"
    fi
}

require_merge_group_trigger() {
    local file=$1
    local trigger_block

    trigger_block=$(sed -n '/^on:/,/^[^[:space:]]/p' "$file")
    if ! grep -Eq '^  merge_group:$' <<<"$trigger_block"; then
        fail "$file: missing merge_group trigger"
    fi
    if ! grep -Eq '^    types: \[checks_requested\]$' <<<"$trigger_block"; then
        fail "$file: merge_group must be limited to checks_requested"
    fi
}

for workflow in \
    .github/workflows/_required.yml \
    .github/workflows/extras.yml \
    .github/workflows/release-please.yml; do
    require_merge_group_trigger "$workflow"
done

required_contexts=(
    lint
    unit-tests
    integration-tests
    security/dependency-scan
    security/secrets-scan
    build
    schema-validation
    deps/version-sync
    test
    package
    release
    coverage
)

for context in "${required_contexts[@]}"; do
    if ! grep -Ehq "^[[:space:]]+name: ${context}$" \
        .github/workflows/_required.yml \
        .github/workflows/extras.yml \
        .github/workflows/release-please.yml; then
        fail "required context is not emitted by a queue-enabled workflow: $context"
    fi
done

# Preserve the existing push and pull-request surfaces while adding queue builds.
for workflow in .github/workflows/_required.yml .github/workflows/extras.yml; do
    require_line "$workflow" '^  push:$' 'push trigger'
    require_line "$workflow" '^  pull_request:$' 'pull_request trigger'
    require_line "$workflow" '^    branches: \[main, develop, "claude/\*\*"\]$' \
        'existing push branches'
    require_line "$workflow" '^    branches: \[main, develop\]$' \
        'existing pull-request branches'
done

# Merge-group refs must never execute release-please or publish release artifacts.
require_line .github/workflows/release-please.yml \
    "^    if: github\.event_name != 'merge_group'$" \
    'merge-group guard on release-please'
require_line .github/workflows/release-please.yml \
    '^    name: release$' \
    'stable release required-check gate'
require_line .github/workflows/release-please.yml \
    "github\.event_name == 'merge_group'" \
    'merge-group release validation path'
require_line .github/workflows/_required.yml \
    '^        run: \./scripts/check-merge-queue-readiness\.sh$' \
    'required schema-validation invocation of this contract'

# extras.yml supplies the required coverage context. Its advisory heavy jobs stay
# available on push/PR, but do not consume merge-queue build capacity.
for job in benchmarks nats-integration; do
    if ! awk -v job="$job" '
        $0 == "  " job ":" { in_job=1; next }
        in_job && /^  [[:alnum:]_-]+:/ { exit }
        in_job && /if: github\.event_name != '\''merge_group'\''/ { found=1 }
        END { exit(found ? 0 : 1) }
    ' .github/workflows/extras.yml; then
        fail ".github/workflows/extras.yml: $job must skip merge_group builds"
    fi
done

# The repository has no canonical ruleset file, so the activation contract must
# remain exact and reviewable in version-controlled documentation.
for policy in \
    '"check_response_timeout_minutes": 60' \
    '"grouping_strategy": "ALLGREEN"' \
    '"max_entries_to_build": 10' \
    '"max_entries_to_merge": 5' \
    '"merge_method": "SQUASH"' \
    '"min_entries_to_merge": 1' \
    '"min_entries_to_merge_wait_minutes": 5'; do
    if ! grep -Fq "$policy" docs/CICD_QUALITY_GATES.md; then
        fail "docs/CICD_QUALITY_GATES.md: missing queue policy $policy"
    fi
done

if ((failures > 0)); then
    echo "Merge-queue readiness validation failed with $failures error(s)." >&2
    exit 1
fi

echo "Merge-queue readiness validation passed."
