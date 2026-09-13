#!/usr/bin/env bash

set -euo pipefail

# PyYAML is already present in Keystone's locked development environment via
# Conan and pre-commit.  Use the lock so this check behaves the same locally
# and in the CI image.
exec uv run --frozen python - <<'PYEOF'
from __future__ import annotations

import copy
import re
import sys
from pathlib import Path
from typing import Any

import yaml


class WorkflowLoader(yaml.SafeLoader):
    """YAML 1.2-style booleans for GitHub workflow keys such as `on`."""


WorkflowLoader.yaml_implicit_resolvers = copy.deepcopy(
    yaml.SafeLoader.yaml_implicit_resolvers
)
for first_character, resolvers in WorkflowLoader.yaml_implicit_resolvers.items():
    WorkflowLoader.yaml_implicit_resolvers[first_character] = [
        resolver for resolver in resolvers if resolver[0] != "tag:yaml.org,2002:bool"
    ]
WorkflowLoader.add_implicit_resolver(
    "tag:yaml.org,2002:bool",
    re.compile(r"^(?:true|false)$", re.IGNORECASE),
    list("tTfF"),
)


PRODUCERS = (
    Path(".github/workflows/_required.yml"),
    Path(".github/workflows/extras.yml"),
)
PUBLISHER = Path(".github/workflows/release-please.yml")
WORKFLOW_PATTERNS = ("*.yml", "*.yaml")
SMOKE_FILENAMES = frozenset({"merge-queue-smoke.yml", "merge-queue-smoke.yaml"})

# Each exception must bind the workflow path, job id, step name, and exact
# condition. There are currently no intentional event guards in a required
# validator; publication-only exceptions can be added here when evidence calls
# for one without weakening the scan or validation step that precedes it.
ALLOWED_REQUIRED_STEP_EVENT_GUARDS: frozenset[tuple[str, str, str, str]] = frozenset()

# Define the live required-context contract once.  Producer ownership is
# derived from each workflow's job names below, including the duplicate
# `coverage` producer that currently exists in both real workflows.
REQUIRED_CONTEXTS = frozenset(
    {
        "build",
        "coverage",
        "deps/version-sync",
        "install",
        "integration-tests",
        "lint",
        "package",
        "release",
        "schema-validation",
        "security/dependency-scan",
        "security/secrets-scan",
        "test",
        "unit-tests",
    }
)

failures: list[str] = []
observed_step_guard_allowlist: set[tuple[str, str, str, str]] = set()


def fail(message: str) -> None:
    failures.append(message)
    print(f"ERROR: {message}", file=sys.stderr)


def load_workflow(path: Path) -> dict[str, Any]:
    try:
        document = yaml.load(path.read_text(encoding="utf-8"), Loader=WorkflowLoader)
    except (OSError, UnicodeError, yaml.YAMLError) as error:
        fail(f"{path}: cannot parse workflow YAML: {error}")
        return {}

    if not isinstance(document, dict):
        fail(f"{path}: workflow root must be a mapping")
        return {}
    if "on" not in document:
        fail(f"{path}: parser did not preserve the literal `on` key")
    return document


def discover_workflow_files(directory: Path) -> list[Path]:
    paths = {
        path
        for pattern in WORKFLOW_PATTERNS
        for path in directory.glob(pattern)
        if path.is_file()
    }
    return sorted(paths)


def smoke_carrier_violations(paths: list[Path]) -> list[str]:
    violations: list[str] = []
    for path in paths:
        if path.name in SMOKE_FILENAMES:
            violations.append(f"{path}: smoke-only merge-group carrier remains")

        workflow = load_workflow(path)
        jobs = workflow.get("jobs")
        if not isinstance(jobs, dict):
            continue
        for job_id, job in jobs.items():
            name = job.get("name") if isinstance(job, dict) else None
            if job_id == "merge-queue-smoke" or name == "merge-queue-smoke":
                violations.append(f"{path}: smoke-only merge-queue job remains")
    return violations


def require_event_parity(path: Path, workflow: dict[str, Any]) -> None:
    events = workflow.get("on")
    if not isinstance(events, dict):
        fail(f"{path}: `on` must be an event mapping")
        return

    push = events.get("push")
    if not isinstance(push, dict) or push.get("branches") != [
        "main",
        "develop",
        "claude/**",
    ]:
        fail(f"{path}: existing push branches changed or are missing")

    pull_request = events.get("pull_request")
    if not isinstance(pull_request, dict) or pull_request.get("branches") != [
        "main",
        "develop",
    ]:
        fail(f"{path}: existing pull_request branches changed or are missing")

    merge_group = events.get("merge_group")
    if not isinstance(merge_group, dict):
        fail(f"{path}: missing merge_group event mapping")
    elif merge_group.get("types") != ["checks_requested"]:
        fail(f"{path}: merge_group must be limited to checks_requested")


def require_event_sha_concurrency(path: Path, workflow: dict[str, Any]) -> None:
    concurrency = workflow.get("concurrency")
    if not isinstance(concurrency, dict):
        fail(f"{path}: concurrency must be a mapping")
        return

    group = concurrency.get("group")
    if not isinstance(group, str):
        fail(f"{path}: concurrency.group must be a string")
        return
    for expression in ("${{ github.event_name }}", "${{ github.sha }}"):
        if expression not in group:
            fail(f"{path}: concurrency.group must include {expression}")

    if concurrency.get("cancel-in-progress") is not True:
        fail(f"{path}: cancel-in-progress must remain true")


def job_names(path: Path, workflow: dict[str, Any]) -> dict[str, str]:
    jobs = workflow.get("jobs")
    if not isinstance(jobs, dict):
        fail(f"{path}: jobs must be a mapping")
        return {}

    names: dict[str, str] = {}
    for job_id, job in jobs.items():
        if not isinstance(job_id, str) or not isinstance(job, dict):
            fail(f"{path}: malformed job entry {job_id!r}")
            continue
        name = job.get("name")
        if not isinstance(name, str):
            fail(f"{path}: job {job_id} must have a stable name")
            continue
        names[job_id] = name

        condition = job.get("if")
        if isinstance(condition, str) and (
            "github.event" in condition
            or "merge_group" in condition
            or "pull_request" in condition
        ):
            fail(
                f"{path}: job {job_id} has an event condition that breaks "
                "pull-request/merge-group parity"
            )
    return names


def required_step_event_guard_violations(
    path: Path,
    workflow: dict[str, Any],
    names: dict[str, str],
) -> list[str]:
    jobs = workflow.get("jobs", {})
    violations: list[str] = []
    for job_id, name in names.items():
        if name not in REQUIRED_CONTEXTS:
            continue
        job = jobs[job_id]
        steps = job.get("steps")
        if not isinstance(steps, list):
            continue
        for index, step in enumerate(steps):
            if not isinstance(step, dict):
                continue
            condition = step.get("if")
            if not isinstance(condition, str) or not (
                "github.event" in condition
                or "merge_group" in condition
                or "pull_request" in condition
            ):
                continue
            step_name = step.get("name")
            if not isinstance(step_name, str):
                step_name = f"step[{index}]"
            key = (path.as_posix(), job_id, step_name, condition.strip())
            if key in ALLOWED_REQUIRED_STEP_EVENT_GUARDS:
                observed_step_guard_allowlist.add(key)
                continue
            violations.append(
                f"{path}: required context {name} ({job_id}) step "
                f"{step_name!r} has an unapproved event guard: {condition}"
            )
    return violations


def require_context_reachability(
    path: Path,
    workflow: dict[str, Any],
    names: dict[str, str],
) -> set[str]:
    owned_contexts = REQUIRED_CONTEXTS.intersection(names.values())
    if not owned_contexts:
        fail(f"{path}: producer emits no live required context")
        return set()

    jobs = workflow.get("jobs", {})
    for context in sorted(owned_contexts):
        matching_jobs = [job_id for job_id, name in names.items() if name == context]
        for job_id in matching_jobs:
            job = jobs[job_id]
            if "if" in job:
                fail(
                    f"{path}: required context {context} ({job_id}) must be "
                    "unconditional for pull_request and merge_group"
                )
    for violation in required_step_event_guard_violations(path, workflow, names):
        fail(violation)
    return set(owned_contexts)


for violation in smoke_carrier_violations(
    discover_workflow_files(Path(".github/workflows"))
):
    fail(violation)

emitted_required_contexts: set[str] = set()
for producer in PRODUCERS:
    workflow = load_workflow(producer)
    require_event_parity(producer, workflow)
    require_event_sha_concurrency(producer, workflow)
    names = job_names(producer, workflow)
    emitted_required_contexts.update(
        require_context_reachability(producer, workflow, names)
    )

missing_contexts = REQUIRED_CONTEXTS.difference(emitted_required_contexts)
if missing_contexts:
    fail(
        "live required contexts missing from real producers: "
        + ", ".join(sorted(missing_contexts))
    )

unused_step_guard_allowlist = (
    ALLOWED_REQUIRED_STEP_EVENT_GUARDS - observed_step_guard_allowlist
)
if unused_step_guard_allowlist:
    fail(
        "unused required-step event-guard allowlist entries: "
        + repr(sorted(unused_step_guard_allowlist))
    )

schema_runner = Path("scripts/run_ci_local.sh").read_text(encoding="utf-8")
function_start = schema_runner.find("run_schema-validation() {")
function_end = schema_runner.find("\n}", function_start)
if function_start < 0 or function_end < 0:
    fail("scripts/run_ci_local.sh: missing run_schema-validation function")
else:
    schema_body = schema_runner[function_start:function_end]
    for command in (
        "./scripts/check-workflow-schema.sh",
        "./scripts/test-workflow-schema-validation.sh",
        "./scripts/check-merge-queue-readiness.sh",
    ):
        if command not in schema_body:
            fail(f"scripts/run_ci_local.sh: schema-validation must execute {command}")
    if "|| true" in schema_body:
        fail("scripts/run_ci_local.sh: schema-validation must fail closed")

step_guard_fixture_path = Path(
    "tests/fixtures/merge-queue-readiness/step-event-guard.yml"
)
step_guard_fixture = load_workflow(step_guard_fixture_path)
fixture_names = job_names(step_guard_fixture_path, step_guard_fixture)
fixture_guard_violations = required_step_event_guard_violations(
    step_guard_fixture_path,
    step_guard_fixture,
    fixture_names,
)
if not fixture_guard_violations:
    fail("step-level event-guard negative fixture was not rejected")

alternate_fixture_dir = Path("tests/fixtures/merge-queue-readiness")
alternate_fixture_violations = smoke_carrier_violations(
    discover_workflow_files(alternate_fixture_dir)
)
if not any(
    "merge-queue-smoke.yaml" in violation for violation in alternate_fixture_violations
):
    fail("alternate-extension smoke-carrier negative fixture was not discovered")

publisher = load_workflow(PUBLISHER)
publisher_events = publisher.get("on")
if isinstance(publisher_events, dict) and "merge_group" in publisher_events:
    fail(f"{PUBLISHER}: artifact publisher must not run on merge_group")

if failures:
    print(
        f"Merge-queue readiness validation failed with {len(failures)} error(s).",
        file=sys.stderr,
    )
    raise SystemExit(1)

print("Merge-queue readiness validation passed.")
PYEOF
