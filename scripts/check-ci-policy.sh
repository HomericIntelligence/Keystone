#!/usr/bin/env bash
# Match the required workflow's three suppression checks over tracked sources.
set -euo pipefail

files="$(mktemp)"
trap 'rm -f "$files"' EXIT
git ls-files -z -- '*.sh' '*.bash' '*.yml' '*.yaml' '*.hcl' 'Dockerfile*' '**/Dockerfile*' \
    'justfile' '**/justfile' 'Justfile' '**/Justfile' > "$files"
failed=0

reject() {
    local expression="$1" path="$2" result
    if grep -nE "$expression" "$path"; then
        printf 'ERROR: CI suppression in %s\n' "$path" >&2
        failed=1
    else
        result=$?
        if [ "$result" -ne 1 ]; then exit "$result"; fi
    fi
}

while IFS= read -r -d '' path; do
    if [ "$path" != .github/workflows/_required.yml ]; then
        reject '\|\|[[:space:]]*true([[:space:]]*$|[[:space:]]+#)' "$path"
    fi
    case "$path" in
        .github/workflows/*.yml|.github/workflows/*.yaml)
            reject '^[[:space:]]*continue-on-error:[[:space:]]*true[[:space:]]*$' "$path"
            if [ "$path" != .github/workflows/_required.yml ]; then
                reject '::warning::' "$path"
            fi
            ;;
    esac
done < "$files"

exit "$failed"
