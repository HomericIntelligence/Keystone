#!/usr/bin/env bash
# Verify tracked symlinks; generated build and dependency caches are not source.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd -P)"
ROOT="$(cd "$ROOT" && pwd -P)"
cd "$ROOT"
files="$(mktemp)"
trap 'rm -f "$files"' EXIT
git ls-files --stage -z > "$files"
fail=0

while IFS= read -r -d '' entry; do
  case "$entry" in 120000\ *) ;; *) continue ;; esac
  link="${entry#*$'\t'}"
  if [ ! -L "$link" ]; then
    echo "ERROR: tracked symlink is not a symlink: $link"
    fail=1
    continue
  fi
  target="$(readlink -- "$link")"
  case "$target" in
    /*) abs="$target" ;;
    *)  abs="$(cd "$(dirname -- "$link")" && pwd -P)/$target" ;;
  esac
  resolved="$(readlink -m -- "$abs")"
  case "$resolved" in
    "$ROOT"|"$ROOT"/*) ;;
    *) echo "ERROR: symlink escapes repo: $link -> $target"; fail=1 ;;
  esac
  if [ ! -e "$link" ]; then
    echo "ERROR: broken symlink: $link -> $target"; fail=1
  fi
done < "$files"

[ "$fail" -eq 0 ] && echo "symlink-check: OK" || exit 1
