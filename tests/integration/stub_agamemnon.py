"""In-process FastAPI stub simulating the ProjectAgamemnon REST API.

Used exclusively by the integration tests in this package.  No real port
binding occurs — tests wire an httpx.AsyncClient with ASGITransport so the
stub runs entirely in-process.

State:
    The stub tracks which tasks have been claimed via an asyncio.Lock-protected
    set.  The ``/reset`` endpoint clears all state between test cases.

Endpoints:
    GET  /teams/{team_id}/tasks
        Returns the list configured by configure_tasks().
    POST /teams/{team_id}/tasks/{task_id}/claim
        First call for (team_id, task_id) → 200 {"claimed": true}
        Subsequent calls for the same pair  → 409 {"detail": "Task already claimed"}
    POST /reset
        Clears claimed-task state and the configured task list.
"""

from __future__ import annotations

import asyncio
from typing import Any

from fastapi import FastAPI, HTTPException

app: FastAPI = FastAPI()

# ---------------------------------------------------------------------------
# Mutable in-process state (reset between tests via POST /reset)
# ---------------------------------------------------------------------------

_claim_lock: asyncio.Lock = asyncio.Lock()
_claimed_tasks: set[str] = set()
_configured_tasks: list[dict[str, Any]] = []


def configure_tasks(tasks: list[dict[str, Any]]) -> None:
    """Seed the task list returned by GET /teams/{team_id}/tasks.

    Call this from a test fixture *before* the first HTTP request.

    Args:
        tasks: List of task dicts (must each have an "id" key).
    """
    global _configured_tasks
    _configured_tasks = list(tasks)


# ---------------------------------------------------------------------------
# Endpoints
# ---------------------------------------------------------------------------


@app.get("/teams/{team_id}/tasks")
async def get_tasks(team_id: str) -> list[dict[str, Any]]:
    """Return all configured tasks for the given team.

    The stub does not filter by team_id — all tasks are returned regardless.
    Tests that need per-team isolation should configure distinct stubs or reset
    between calls.
    """
    return list(_configured_tasks)


@app.post("/teams/{team_id}/tasks/{task_id}/claim")
async def claim_task(team_id: str, task_id: str) -> dict[str, Any]:
    """Claim a task.

    The first call for a given (team_id, task_id) pair succeeds (200).
    Subsequent calls for the same pair return 409 Conflict.
    """
    key = f"{team_id}:{task_id}"
    async with _claim_lock:
        if key in _claimed_tasks:
            raise HTTPException(status_code=409, detail="Task already claimed")
        _claimed_tasks.add(key)
    return {"claimed": True}


@app.post("/reset")
async def reset() -> dict[str, str]:
    """Reset all stub state.  Call between test cases to ensure isolation."""
    global _configured_tasks
    async with _claim_lock:
        _claimed_tasks.clear()
    _configured_tasks = []
    return {"status": "reset"}
