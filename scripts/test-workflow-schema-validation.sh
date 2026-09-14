#!/usr/bin/env bash

set -euo pipefail

fixture_dir=tests/fixtures/workflow-schema

set +e
output=$(./scripts/check-workflow-schema.sh "$fixture_dir" 2>&1)
status=$?
set -e

if ((status == 0)); then
    echo "ERROR: invalid .yaml workflow fixture passed schema validation" >&2
    exit 1
fi

if ((status != 1)); then
    echo "ERROR: schema validator exited $status instead of rejecting the fixture" >&2
    printf '%s\n' "$output" >&2
    exit 1
fi

if [[ "$output" != *"Schema validation errors were encountered"* ]]; then
    echo "ERROR: invalid fixture did not produce a schema-validation error" >&2
    printf '%s\n' "$output" >&2
    exit 1
fi

echo "Invalid .yaml workflow fixture was rejected as expected."
