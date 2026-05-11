#!/bin/bash
# create_phase_issues.sh
# Creates GitHub issues for Phase 7, 8, and 10 based on plan documents
#
# Usage: ./scripts/create_phase_issues.sh [--dry-run]
#
# Requirements:
# - gh CLI installed and authenticated
# - Must be run from project root directory

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || echo ".")"
DOCS_PLAN_DIR="$REPO_ROOT/docs/plan"
DRY_RUN=false

# Parse arguments
if [[ "$1" == "--dry-run" ]]; then
    DRY_RUN=true
    echo -e "${YELLOW}DRY RUN MODE - No issues will be created${NC}"
fi

# Check if gh CLI is installed
if ! command -v gh &> /dev/null; then
    echo -e "${RED}Error: gh CLI is not installed${NC}"
    echo "Install from: https://cli.github.com/"
    exit 1
fi

# Check if authenticated
if ! gh auth status &> /dev/null; then
    echo -e "${RED}Error: gh CLI is not authenticated${NC}"
    echo "Run: gh auth login"
    exit 1
fi

# Function to create a GitHub issue
create_issue() {
    local phase=$1
    local title=$2
    local body=$3
    local labels=$4
    local milestone=$5

    if [[ "$DRY_RUN" == "true" ]]; then
        echo -e "${BLUE}[DRY RUN] Would create issue:${NC}"
        echo -e "  Title: ${title}"
        echo -e "  Labels: ${labels}"
        echo -e "  Milestone: ${milestone}"
        echo ""
        return 0
    fi

    echo -e "${GREEN}Creating issue: ${title}${NC}"

    # Create the issue
    local issue_url
    if [[ -n "$milestone" ]]; then
        issue_url=$(gh issue create \
            --title "$title" \
            --body "$body" \
            --label "$labels" \
            --milestone "$milestone" 2>&1)
    else
        issue_url=$(gh issue create \
            --title "$title" \
            --body "$body" \
            --label "$labels" 2>&1)
    fi

    if [[ $? -eq 0 ]]; then
        echo -e "${GREEN}✓ Created: ${issue_url}${NC}"
    else
        echo -e "${RED}✗ Failed to create issue${NC}"
        echo "$issue_url"
    fi
    echo ""
}

# Function to generate issue body from plan document
generate_issue_body() {
    local plan_file=$1
    local phase_num=$2

    cat <<EOF
## Overview

This epic tracks the implementation of Phase ${phase_num} as detailed in [\`${plan_file}\`](../docs/plan/${plan_file}).

Please refer to the plan document for:
- Detailed architecture diagrams
- Implementation tasks and estimates
- Success criteria
- Test plan
- Risk mitigation strategies

## Plan Document

See [\`docs/plan/${plan_file}\`](../docs/plan/${plan_file}) for the complete plan.

## Subtasks

This is an epic issue. Create sub-issues for each implementation phase and link them here.

### Implementation Phases

EOF

    # Extract implementation phases from plan document (sections starting with "Phase X.Y:")
    if [[ -f "$DOCS_PLAN_DIR/$plan_file" ]]; then
        grep -E "^### Phase ${phase_num}\.[0-9]+:" "$DOCS_PLAN_DIR/$plan_file" | sed 's/^### /- [ ] /'
    fi

    cat <<EOF

## Checklist

- [ ] Read and understand the plan document
- [ ] Create sub-issues for each implementation phase
- [ ] Implement all sub-phases
- [ ] Write E2E tests for all scenarios
- [ ] Update documentation
- [ ] Run all tests (unit + E2E)
- [ ] Create PR and get reviewed
- [ ] Merge to main

## Labels

\`phase-${phase_num}\`, \`epic\`, \`enhancement\`

## References

- Plan Document: [\`docs/plan/${plan_file}\`](../docs/plan/${plan_file})
- TDD Roadmap: [\`docs/plan/TDD_FOUR_LAYER_ROADMAP.md\`](../docs/plan/TDD_FOUR_LAYER_ROADMAP.md)
- Architecture: [\`docs/plan/FOUR_LAYER_ARCHITECTURE.md\`](../docs/plan/FOUR_LAYER_ARCHITECTURE.md)
EOF
}

# Function to create milestone if it doesn't exist
create_milestone_if_needed() {
    local milestone_title=$1
    local milestone_due_date=$2  # Format: YYYY-MM-DD

    # Check if milestone exists
    if gh api repos/:owner/:repo/milestones --jq ".[] | select(.title == \"$milestone_title\")" | grep -q "$milestone_title"; then
        echo -e "${BLUE}Milestone '$milestone_title' already exists${NC}"
        return 0
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        echo -e "${BLUE}[DRY RUN] Would create milestone: $milestone_title${NC}"
        return 0
    fi

    echo -e "${GREEN}Creating milestone: $milestone_title${NC}"
    if ! gh api repos/:owner/:repo/milestones -f title="$milestone_title" -f due_on="$milestone_due_date"; then
        echo -e "${RED}warn: failed to create milestone '$milestone_title'${NC}" >&2
        return 1
    fi
}

# Main script
echo -e "${BLUE}=== ProjectKeystone Phase Issues Creator ===${NC}"
echo ""

# Create milestones (optional - comment out if not needed)
# create_milestone_if_needed "Phase 7: AI Integration" "2025-03-31T23:59:59Z"
# create_milestone_if_needed "Phase 8: Real Distributed System" "2025-05-31T23:59:59Z"
# create_milestone_if_needed "Phase 10: Advanced Features" "2025-07-31T23:59:59Z"

# Phase 7: AI Integration
echo -e "${YELLOW}Creating Phase 7 issues...${NC}"
PHASE7_BODY=$(generate_issue_body "PHASE_7_PLAN.md" "7")
create_issue \
    "7" \
    "Phase 7: AI Integration (LLM-powered task agents)" \
    "$PHASE7_BODY" \
    "phase-7,epic,enhancement,ai" \
    ""  # Empty string = no milestone

# Phase 8: Real Distributed System
echo -e "${YELLOW}Creating Phase 8 issues...${NC}"
PHASE8_BODY=$(generate_issue_body "PHASE_8_PLAN.md" "8")
create_issue \
    "8" \
    "Phase 8: Real Distributed System (gRPC + etcd + Raft)" \
    "$PHASE8_BODY" \
    "phase-8,epic,enhancement,distributed" \
    ""  # Empty string = no milestone

# Phase 10: Advanced Features
echo -e "${YELLOW}Creating Phase 10 issues...${NC}"
PHASE10_BODY=$(generate_issue_body "PHASE_10_PLAN.md" "10")
create_issue \
    "10" \
    "Phase 10: Advanced Features (Persistence + Tracing + A/B Testing + Auto-Scaling)" \
    "$PHASE10_BODY" \
    "phase-10,epic,enhancement,observability" \
    ""  # Empty string = no milestone

echo -e "${GREEN}=== Done! ===${NC}"
echo ""

if [[ "$DRY_RUN" == "true" ]]; then
    echo -e "${YELLOW}This was a dry run. No issues were created.${NC}"
    echo -e "${YELLOW}Run without --dry-run to create actual issues.${NC}"
else
    echo -e "${GREEN}GitHub issues created successfully!${NC}"
    echo ""
    echo "View all issues: gh issue list --label epic"
fi
