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
    local configuration
    configuration=$(awk '
        /^  merge_group:$/ { in_group=1; next }
        in_group && /^[^ ]|^  [^ ]/ { exit }
        in_group && NF && !/^ *#/ { print }
    ' <<<"$trigger_block")
    if [ "$configuration" != '    types: [checks_requested]' ]; then
        fail "$file: unsupported merge_group configuration"
    fi
}

forbid_merge_group_trigger() {
    local file=$1
    local trigger_block

    trigger_block=$(sed -n '/^on:/,/^[^[:space:]]/p' "$file")
    if grep -Eq '^  merge_group:$' <<<"$trigger_block"; then
        fail "$file: must not trigger on merge_group"
    fi
}

# The canonical workflow runs every required context, including real coverage.
require_merge_group_trigger .github/workflows/_required.yml
require_merge_group_trigger .github/workflows/merge-queue-smoke.yml

for workflow in \
    .github/workflows/extras.yml \
    .github/workflows/release-please.yml; do
    forbid_merge_group_trigger "$workflow"
done

# The separate smoke context remains advisory; it cannot replace required jobs.
require_line .github/workflows/merge-queue-smoke.yml \
    '^    name: merge-queue-smoke$' \
    'merge-queue-smoke context in the smoke workflow'

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
    install
    release
    coverage
)

job_block() {
    awk -v job="$1" '
        /^jobs:$/ { in_jobs=1; next }
        in_jobs && /^  [[:alnum:]_-]+:$/ { selected=($0 == "  " job ":") }
        selected { print }
    ' .github/workflows/_required.yml
}

checked_jobs=' '
check_job() {
    local job=$1 ancestry=$2 block dependencies dependency
    if [[ "$ancestry" == *" $job "* ]]; then
        fail "queue dependency cycle: $job"
        return
    fi
    if [[ "$checked_jobs" == *" $job "* ]]; then return; fi
    checked_jobs+="$job "
    block=$(job_block "$job")
    if [ -z "$block" ]; then
        fail "queue dependency is missing: $job"
        return
    fi
    if grep -Eq '^    (if|continue-on-error):' <<<"$block"; then
        fail "queue job must be unconditional: $job"
    fi
    if ! awk '
        function finish_step() { if (command && !conditional) runnable=1 }
        /^      - / { finish_step(); command=0; conditional=0 }
        /^      - run:|^        run:/ { command=1 }
        /^        (if|continue-on-error):/ { conditional=1 }
        END { finish_step(); exit(runnable ? 0 : 1) }
    ' <<<"$block"; then
        fail "queue job has no unconditional command: $job"
    fi
    dependencies=$(sed -n 's/^    needs: //p' <<<"$block")
    if [ -n "$dependencies" ]; then
        # Current jobs use a literal job ID or an inline list. A different
        # expression needs an explicit contract update; never guess eligibility.
        if [[ ! "$dependencies" =~ ^[[:alnum:]_-]+$ &&
              ! "$dependencies" =~ ^\[[[:alnum:]_,[:space:]-]+\]$ ]]; then
            fail "unsupported queue dependencies: $job"
            return
        fi
        dependencies=${dependencies//[/}
        dependencies=${dependencies//]/}
        dependencies=${dependencies//,/ }
        for dependency in $dependencies; do
            check_job "$dependency" "$ancestry$job "
        done
    elif grep -Eq '^    needs:' <<<"$block"; then
        fail "unsupported queue dependencies: $job"
    fi
}

for context in "${required_contexts[@]}"; do
    producers=$(awk -v context="$context" '
        /^jobs:$/ { in_jobs=1; next }
        in_jobs && /^  [[:alnum:]_-]+:$/ { job=substr($0, 3, length($0)-3) }
        in_jobs && $0 == "    name: " context { print job }
    ' .github/workflows/_required.yml)
    if [ -z "$producers" ] || [[ "$producers" == *$'\n'* ]]; then
        fail "required queue context needs exactly one producer: $context"
    else
        check_job "$producers" ' '
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
# The canonical `release` required-check is emitted by the PR-time validation
# job in _required.yml (which triggers on push/pull_request/merge_group), NOT by
# release-please.yml — a second release-please.yml job named `release` would
# collide with it on the `release` check name. The generic required_contexts
# loop above already confirms the `release` context is emitted by a
# queue-enabled workflow; assert here that its sole producer is _required.yml.
require_line .github/workflows/_required.yml \
    '^    name: release$' \
    'canonical release required-check gate in _required.yml'
require_line .github/workflows/_required.yml \
    '^        run: bash scripts/run_ci_local\.sh schema-validation$' \
    'canonical schema-validation caller'
require_line scripts/run_ci_local.sh \
    'bash scripts/check-merge-queue-readiness\.sh' \
    'local and hosted schema-validation invocation of this contract'

# Preserve optional extras and their queue guards. Required coverage runs in
# _required.yml; a skipped extras job is not its replacement.
for job in benchmarks nats-integration coverage; do
    if ! awk -v job="$job" '
        $0 == "  " job ":" { in_job=1; next }
        in_job && /^  [[:alnum:]_-]+:/ { exit }
        in_job && /if: github\.event_name != '\''merge_group'\''/ { found=1 }
        END { exit(found ? 0 : 1) }
    ' .github/workflows/extras.yml; then
        fail ".github/workflows/extras.yml: $job must skip merge_group builds"
    fi
done

# The repository records the observed live policy without writing protection.
for policy in \
    '"check_response_timeout_minutes": 60' \
    '"grouping_strategy": "HEADGREEN"' \
    '"max_entries_to_build": 2' \
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
