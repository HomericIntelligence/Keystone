#!/usr/bin/env python3
"""Remove a specific entry from the 'include' array in CMakeUserPresets.json."""
import json
import pathlib
import sys

if len(sys.argv) != 3:
    print(f"usage: {sys.argv[0]} <presets-file> <include-to-remove>", file=sys.stderr)
    sys.exit(1)

presets_path = pathlib.Path(sys.argv[1])
entry_to_remove = sys.argv[2]

data = json.loads(presets_path.read_text())
data["include"] = [x for x in data.get("include", []) if x != entry_to_remove]
presets_path.write_text(json.dumps(data, indent=4) + "\n")
