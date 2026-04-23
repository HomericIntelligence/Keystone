"""Tests for TaskClaimer concurrency guard."""

from __future__ import annotations

import asyncio
import sys
from pathlib import Path
from unittest.mock import AsyncMock, call

import pytest

# Allow importing from src/keystone without an installed package
sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from keystone.task_claimer import TaskClaimer


def _make_claimer(
    tasks_by_team: dict[str, list[dict]] | None = None,
    claim_result: bool = True,
) -> tuple[TaskClaimer, AsyncMock, AsyncMock]:
    tasks_by_team = tasks_by_team or {}

    async def get_tasks(team_id: str) -> list[dict]:
        return tasks_by_team.get(team_id, [])

    get_tasks_mock = AsyncMock(side_effect=get_tasks)
    claim_mock = AsyncMock(return_value=claim_result)
    claimer = TaskClaimer(get_tasks=get_tasks_mock, claim_task=claim_mock)
    return claimer, get_tasks_mock, claim_mock


@pytest.mark.asyncio
async def test_advance_dag_claims_ready_tasks() -> None:
    tasks = [{"id": "t1"}, {"id": "t2"}]
    claimer, get_tasks_mock, claim_mock = _make_claimer({"team-A": tasks})

    result = await claimer.advance_dag("team-A")

    assert result == ["t1", "t2"]
    get_tasks_mock.assert_awaited_once_with("team-A")
    assert claim_mock.await_count == 2
    claim_mock.assert_any_await("team-A", "t1")
    claim_mock.assert_any_await("team-A", "t2")


@pytest.mark.asyncio
async def test_advance_dag_empty_team_returns_empty() -> None:
    claimer, get_tasks_mock, claim_mock = _make_claimer()

    result = await claimer.advance_dag("team-empty")

    assert result == []
    get_tasks_mock.assert_awaited_once_with("team-empty")
    claim_mock.assert_not_awaited()


@pytest.mark.asyncio
async def test_advance_dag_partial_claim_failures() -> None:
    tasks = [{"id": "t1"}, {"id": "t2"}]

    async def get_tasks(team_id: str) -> list[dict]:
        return tasks

    get_tasks_mock = AsyncMock(side_effect=get_tasks)
    # First claim succeeds, second fails
    claim_mock = AsyncMock(side_effect=[True, False])
    claimer = TaskClaimer(get_tasks=get_tasks_mock, claim_task=claim_mock)

    result = await claimer.advance_dag("team-A")

    assert result == ["t1"]


@pytest.mark.asyncio
async def test_concurrent_advance_dag_coalesced() -> None:
    """Second advance_dag for same team is skipped while first is in-flight."""
    first_call_started = asyncio.Event()
    first_call_proceed = asyncio.Event()

    async def slow_get_tasks(team_id: str) -> list[dict]:
        first_call_started.set()
        await first_call_proceed.wait()
        return [{"id": "t1"}]

    get_tasks_mock = AsyncMock(side_effect=slow_get_tasks)
    claim_mock = AsyncMock(return_value=True)
    claimer = TaskClaimer(get_tasks=get_tasks_mock, claim_task=claim_mock)

    async def first_call() -> list[str]:
        return await claimer.advance_dag("team-X")

    async def second_call() -> list[str]:
        # Wait until first call is inside get_tasks (in-flight), then try
        await first_call_started.wait()
        result = await claimer.advance_dag("team-X")
        # Now ungate the first call so the gather can complete
        first_call_proceed.set()
        return result

    result1, result2 = await asyncio.gather(first_call(), second_call())

    # First call completes normally; second was coalesced
    assert result1 == ["t1"]
    assert result2 == []
    # get_tasks called only once — second call was skipped before calling it
    assert get_tasks_mock.await_count == 1


@pytest.mark.asyncio
async def test_concurrent_advance_dag_different_teams_parallel() -> None:
    """Concurrent calls for different teams both proceed independently."""
    tasks = {"team-A": [{"id": "a1"}], "team-B": [{"id": "b1"}]}
    claimer, get_tasks_mock, claim_mock = _make_claimer(tasks)

    result_a, result_b = await asyncio.gather(
        claimer.advance_dag("team-A"),
        claimer.advance_dag("team-B"),
    )

    assert sorted(result_a) == ["a1"]
    assert sorted(result_b) == ["b1"]
    assert get_tasks_mock.await_count == 2


@pytest.mark.asyncio
async def test_advance_dag_lock_released_on_error() -> None:
    """Lock and _advancing set are cleaned up even when get_tasks raises."""
    call_count = 0

    async def flaky_get_tasks(team_id: str) -> list[dict]:
        nonlocal call_count
        call_count += 1
        if call_count == 1:
            raise RuntimeError("transient failure")
        return [{"id": "t1"}]

    get_tasks_mock = AsyncMock(side_effect=flaky_get_tasks)
    claim_mock = AsyncMock(return_value=True)
    claimer = TaskClaimer(get_tasks=get_tasks_mock, claim_task=claim_mock)

    with pytest.raises(RuntimeError, match="transient failure"):
        await claimer.advance_dag("team-Z")

    # Second call must not deadlock and must succeed
    result = await claimer.advance_dag("team-Z")
    assert result == ["t1"]
    assert get_tasks_mock.await_count == 2


@pytest.mark.asyncio
async def test_sequential_calls_same_team_both_execute() -> None:
    """Sequential (non-concurrent) calls for the same team both run normally."""
    tasks = [{"id": "t1"}]
    claimer, get_tasks_mock, claim_mock = _make_claimer({"team-A": tasks})

    result1 = await claimer.advance_dag("team-A")
    result2 = await claimer.advance_dag("team-A")

    assert result1 == ["t1"]
    assert result2 == ["t1"]
    assert get_tasks_mock.await_count == 2
