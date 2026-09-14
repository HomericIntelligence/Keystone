#!/usr/bin/env bash

set -euo pipefail

readonly schema_name="vendor.github-workflows"

declare -a roots=()
if (($# > 0)); then
    roots=("$@")
else
    roots=(.github/workflows)
fi
declare -a workflow_files=()

while IFS= read -r -d '' workflow; do
    workflow_files+=("$workflow")
done < <(
    find "${roots[@]}" -type f \
        \( -name '*.yml' -o -name '*.yaml' \) \
        -print0 | sort -z
)

if ((${#workflow_files[@]} == 0)); then
    echo "ERROR: no .yml or .yaml workflow files found" >&2
    exit 1
fi

uv run --frozen check-jsonschema \
    --builtin-schema "$schema_name" \
    "${workflow_files[@]}"
