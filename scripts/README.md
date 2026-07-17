# Keystone Scripts

This directory contains utility scripts for Keystone HMAS development and automation.

---

## Available Scripts

### `create_phase_issues.sh`

Creates GitHub issues for future implementation phases (7, 8, and 10) based on the plan documents in `docs/plan/`.

**Usage**:

```bash
# Dry run (preview what will be created)
./scripts/create_phase_issues.sh --dry-run

# Actually create issues
./scripts/create_phase_issues.sh
```

**Requirements**:

- GitHub CLI (`gh`) installed and authenticated
- Must be run from project root directory
- Write access to the repository

**What it does**:

1. Reads plan documents (`PHASE_7_PLAN.md`, `PHASE_8_PLAN.md`, `PHASE_10_PLAN.md`)
2. Generates issue bodies with:
   - Overview and links to plan documents
   - Implementation phase checklist
   - Success criteria references
3. Creates GitHub issues with appropriate labels:
   - Phase 7: `phase-7`, `epic`, `enhancement`, `ai`
   - Phase 8: `phase-8`, `epic`, `enhancement`, `distributed`
   - Phase 10: `phase-10`, `epic`, `enhancement`, `observability`

**Example output**:

```
=== Keystone Phase Issues Creator ===

Creating Phase 7 issues...
✓ Created: https://github.com/user/Keystone/issues/123

Creating Phase 8 issues...
✓ Created: https://github.com/user/Keystone/issues/124

Creating Phase 10 issues...
✓ Created: https://github.com/user/Keystone/issues/125

=== Done! ===
```

**Dry run mode**:

```bash
./scripts/create_phase_issues.sh --dry-run
```

This will show what would be created without actually creating any issues.

---

## Installation Requirements

### GitHub CLI (`gh`)

**macOS**:

```bash
brew install gh
gh auth login
```

**Ubuntu/Debian**:

```bash
curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
  | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg]" \
  "https://cli.github.com/packages stable main" \
  | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
sudo apt update
sudo apt install gh
gh auth login
```

**Docker** (if running in container):

```bash
# Already installed in dev container
gh auth login
```

---

## Contributing

When adding new scripts:

1. Add the script to this directory
2. Make it executable: `chmod +x scripts/your_script.sh`
3. Add documentation to this README
4. Follow the existing naming convention (lowercase with underscores)
5. Include error handling and `--dry-run` mode when appropriate
6. Add usage instructions in script header comments

---

**Last Updated**: 2025-11-19
