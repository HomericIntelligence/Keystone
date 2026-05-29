"""Integration tests: TaskClaimer wired against an in-process ProjectAgamemnon stub.

These tests verify behaviour at the HTTP boundary — not at the unit/mock level:

1. A 409 Conflict from the stub propagates correctly when two independent
   TaskClaimer instances both attempt to claim the same task.
2. After a 409, the _advancing set in TaskClaimer is cleaned up (not leaked),
   so subsequent calls can proceed without deadlock.
3. The full get_tasks → claim_task → advance_dag lifecycle works end-to-end.
4. Happy-path single-claimer claim succeeds and returns the claimed task IDs.

Design notes
------------
* The coalesce guard in TaskClaimer (``if team_id in self._advancing``) fires
  *before* any HTTP call is made, so a single TaskClaimer instance will never
  produce two concurrent HTTP requests for the same team.  To exercise the
  HTTP-level 409, we create **two independent TaskClaimer instances** that
  share the same stub — each bypasses the other's guard while still hitting the
  same claim endpoint.
* ``AgamemnonAPIError`` is defined here (test layer only) per the architectural
  constraint in CLAUDE.md that ``src/keystone/`` must not grow new production
  responsibilities.
* The ``integration`` marker is registered in ``pyproject.toml``; run with::

    pixi run python -m pytest tests/integration/ -v --override-ini="addopts="
"""

from __future__ import annotations

import asyncio
from typing import Any

import httpx
import pytest
from fastapi import FastAPI

from keystone.task_claimer import TaskClaimer
from tests.integration.stub_agamemnon import app as stub_app
from tests.integration.stub_agamemnon import configure_tasks

pytestmark = pytest.mark.integration

# ---------------------------------------------------------------------------
# Test-local error hierarchy (intentionally NOT in src/keystone/)
# ---------------------------------------------------------------------------


class AgamemnonError(Exception):
    """Base class for Agamemnon REST errors."""


class AgamemnonAPIError(AgamemnonError):
    """Raised when the Agamemnon stub returns a non-2xx response.

    Attributes:
        status_code: HTTP status code returned by the stub.
        response_body: Raw response text.
    """

    def __init__(self, status_code: int, response_body: str) -> None:
        super().__init__(f"Agamemnon API error {status_code}: {response_body}")
        self.status_code = status_code
        self.response_body = response_body


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture
def anyio_backend() -> str:
    return "asyncio"


@pytest.fixture
async def async_client() -> httpx.AsyncClient:  # type: ignore[misc]
    """Yield an httpx.AsyncClient backed by the in-process FastAPI stub.

    Uses ASGITransport — no real TCP port is bound.
    """
    transport = httpx.ASGITransport(app=stub_app)  # type: ignore[arg-type]
    async with httpx.AsyncClient(
        transport=transport, base_url="http://test"
    ) as client:
        # Reset stub state before every test
        await client.post("/reset")
        yield client


def _make_real_get_tasks(
    client: httpx.AsyncClient,
    team_id: str,
) -> Any:
    """Return an async callable that fetches tasks from the stub for team_id."""

    async def get_tasks(tid: str) -> list[dict[str, Any]]:
        response = await client.get(f"/teams/{tid}/tasks")
        response.raise_for_status()
        return response.json()  # type: ignore[no-any-return]

    return get_tasks


def _make_real_claim_task(client: httpx.AsyncClient) -> Any:
    """Return an async callable that claims a task via the stub.

    Raises:
        AgamemnonAPIError: If the stub returns a non-2xx status (e.g. 409).
    """

    async def claim_task(team_id: str, task_id: str) -> bool:
        response = await client.post(f"/teams/{team_id}/tasks/{task_id}/claim")
        if response.status_code == 200:
            return True
        raise AgamemnonAPIError(response.status_code, response.text)

    return claim_task


@pytest.fixture
async def claimer(async_client: httpx.AsyncClient) -> TaskClaimer:
    """TaskClaimer wired to the in-process stub — single instance."""
    return TaskClaimer(
        get_tasks=_make_real_get_tasks(async_client, "team-1"),
        claim_task=_make_real_claim_task(async_client),
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


async def test_get_tasks_returns_real_data(
    async_client: httpx.AsyncClient,
) -> None:
    """GET /teams/{team_id}/tasks returns the stub's configured task list."""
    configure_tasks([{"id": "t1"}, {"id": "t2"}])

    response = await async_client.get("/teams/team-1/tasks")

    assert response.status_code == 200
    data = response.json()
    assert isinstance(data, list)
    assert len(data) == 2
    ids = {item["id"] for item in data}
    assert ids == {"t1", "t2"}


async def test_single_successful_claim(
    async_client: httpx.AsyncClient,
) -> None:
    """Happy path: a single TaskClaimer claims all available tasks successfully."""
    configure_tasks([{"id": "t1"}, {"id": "t2"}])

    local_claimer = TaskClaimer(
        get_tasks=_make_real_get_tasks(async_client, "team-1"),
        claim_task=_make_real_claim_task(async_client),
    )

    result = await local_claimer.advance_dag("team-1")

    assert sorted(result) == ["t1", "t2"]
    # _advancing is cleaned up after the call
    assert "team-1" not in local_claimer._advancing


async def test_concurrent_claim_rejected_at_api_layer(
    async_client: httpx.AsyncClient,
) -> None:
    """409 Conflict propagates from stub when two independent claimers race.

    Two *independent* TaskClaimer instances share the same stub.  Each
    instance has its own _advancing set, so neither coalesces the other at
    the Python level.  The stub's claim-deduplication fires instead, and the
    second claimer receives a 409 → AgamemnonAPIError.

    This verifies criterion 1 from issue #297: the 409 is rejected at the
    HTTP / API layer, not merely at the Python guard level.
    """
    configure_tasks([{"id": "t-race"}])

    # Gate to ensure both claimers start get_tasks roughly simultaneously
    both_started = asyncio.Event()
    first_count = 0

    original_get_tasks = _make_real_get_tasks(async_client, "team-1")

    async def gated_get_tasks(team_id: str) -> list[dict[str, Any]]:
        nonlocal first_count
        first_count += 1
        if first_count == 1:
            # First claimer signals it's inside get_tasks, then waits briefly
            both_started.set()
            await asyncio.sleep(0)  # yield to let second claimer start
        return await original_get_tasks(team_id)

    claimer_a = TaskClaimer(
        get_tasks=gated_get_tasks,
        claim_task=_make_real_claim_task(async_client),
    )
    claimer_b = TaskClaimer(
        get_tasks=_make_real_get_tasks(async_client, "team-1"),
        claim_task=_make_real_claim_task(async_client),
    )

    results: list[Any] = []

    async def run_a() -> None:
        try:
            r = await claimer_a.advance_dag("team-1")
            results.append(("ok", r))
        except AgamemnonAPIError as exc:
            results.append(("err", exc.status_code))

    async def run_b() -> None:
        # Wait until claimer_a has entered get_tasks, then fire
        await both_started.wait()
        try:
            r = await claimer_b.advance_dag("team-1")
            results.append(("ok", r))
        except AgamemnonAPIError as exc:
            results.append(("err", exc.status_code))

    await asyncio.gather(run_a(), run_b())

    statuses = {r[0] for r in results}
    assert "ok" in statuses, "At least one claimer should succeed"
    assert "err" in statuses, "At least one claimer should receive a 409"

    error_codes = [r[1] for r in results if r[0] == "err"]
    assert all(code == 409 for code in error_codes), (
        f"Expected 409, got: {error_codes}"
    )


async def test_advance_dag_cleanup_not_discarded_on_409(
    async_client: httpx.AsyncClient,
) -> None:
    """_advancing is cleaned up after a 409 AgamemnonAPIError — no lock leak.

    This verifies criterion 2 from issue #297: error handling in advance_dag
    does not discard the _advancing cleanup when claim_task raises.

    After the error, a subsequent sequential call for the same team must
    complete without deadlock.
    """
    configure_tasks([{"id": "t-fail"}])

    # Claim the task directly first so the claimer's call produces a 409
    pre_claim = await async_client.post("/teams/team-1/tasks/t-fail/claim")
    assert pre_claim.status_code == 200

    local_claimer = TaskClaimer(
        get_tasks=_make_real_get_tasks(async_client, "team-1"),
        claim_task=_make_real_claim_task(async_client),
    )

    with pytest.raises(AgamemnonAPIError) as exc_info:
        await local_claimer.advance_dag("team-1")

    assert exc_info.value.status_code == 409

    # _advancing must be cleaned up — team-1 must NOT be in the set
    assert "team-1" not in local_claimer._advancing, (
        "_advancing was not cleaned up after 409; potential lock leak"
    )

    # Now free the stub claim state and verify a subsequent call can proceed
    await async_client.post("/reset")
    configure_tasks([{"id": "t-retry"}])

    result = await local_claimer.advance_dag("team-1")
    assert result == ["t-retry"], (
        "Subsequent advance_dag call deadlocked or failed after 409 cleanup"
    )


async def test_full_lifecycle_get_tasks_claim_advance(
    async_client: httpx.AsyncClient,
) -> None:
    """Full get_tasks → claim_task → advance_dag lifecycle via real HTTP.

    Verifies criterion 3 from issue #297: the end-to-end flow works against
    a real HTTP transport, not just AsyncMock.
    """
    configure_tasks([{"id": "task-a"}, {"id": "task-b"}, {"id": "task-c"}])

    local_claimer = TaskClaimer(
        get_tasks=_make_real_get_tasks(async_client, "team-x"),
        claim_task=_make_real_claim_task(async_client),
    )

    claimed = await local_claimer.advance_dag("team-x")

    assert sorted(claimed) == ["task-a", "task-b", "task-c"]

    # Verify stub state: all three tasks are now claimed
    for task_id in ["task-a", "task-b", "task-c"]:
        response = await async_client.post(f"/teams/team-x/tasks/{task_id}/claim")
        assert response.status_code == 409, (
            f"Expected task {task_id} to already be claimed in stub"
        )


async def test_advance_dag_tracked_cleanup_after_409(
    async_client: httpx.AsyncClient,
) -> None:
    """advance_dag_tracked in_flight count returns to 0 after a 409 error.

    Ensures the done-callback on the Task cleans up _in_flight even when the
    task raises.
    """
    configure_tasks([{"id": "t-tracked"}])

    # Pre-claim so the claimer gets a 409
    await async_client.post("/teams/team-1/tasks/t-tracked/claim")

    local_claimer = TaskClaimer(
        get_tasks=_make_real_get_tasks(async_client, "team-1"),
        claim_task=_make_real_claim_task(async_client),
    )

    task = local_claimer.advance_dag_tracked("team-1")
    assert local_claimer.in_flight_count == 1

    with pytest.raises(AgamemnonAPIError):
        await task

    assert local_claimer.in_flight_count == 0, (
        "_in_flight count was not decremented after the tracked task raised"
    )
