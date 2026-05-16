#!/bin/bash
# setup-notes.sh
# Bootstrap the local `notes/` scratch workspace used by the .claude/agents/
# instructions. The `notes/` tree is in .gitignore (it is per-developer
# scratch space, intentionally not version-controlled), so a fresh clone
# has no `notes/` directory and agent workflows that `cd /notes/issues/$N`
# would fail without this bootstrap.
#
# Run once after cloning, or whenever the directory is missing:
#   ./scripts/setup-notes.sh
# or via:
#   just setup-notes
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mkdir -p notes/issues notes/review notes/blog notes/plan

echo "Initialized local notes/ workspace at $ROOT/notes"
echo "  notes/issues  notes/review  notes/blog  notes/plan"
echo "(notes/ is in .gitignore — local-only scratch space.)"
