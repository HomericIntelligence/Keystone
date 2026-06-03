"""Integration test: TaskClaimer against a real (stub) ProjectAgamemnon REST endpoint."""

from __future__ import annotations

import asyncio
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from threading import Thread
from typing import Any
from unittest.mock import patch

import pytest

# Allow importing from src/keystone without an installed package
sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from keystone.task_claimer import TaskClaimer


class TaskClaimerStubHandler(BaseHTTPRequestHandler):
    """Minimal HTTP stub implementing ProjectAgamemnon task endpoints."""

    # Class-level state for the stub server
    _claimed_tasks: dict[str, set[str]] = {}
    _get_tasks_data: dict[str, list[dict[str, Any]]] = {}
    _lock = asyncio.Lock()  # Guards concurrent access

    def log_message(self, format: str, *args: Any) -> None:
        """Suppress default logging."""
        pass

    def do_GET(self) -> None:
        """Handle GET /api/v1/teams/{team_id}/tasks."""
        if self.path.startswith("/api/v1/teams/") and self.path.endswith("/tasks"):
            team_id = self.path.split("/")[-2]
            tasks = self._get_tasks_data.get(team_id, [])
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(
                json.dumps({"tasks": tasks}).encode(),
            )
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self) -> None:
        """Handle POST /api/v1/teams/{team_id}/tasks/{task_id}/claim."""
        if "/claim" in self.path:
            parts = self.path.split("/")
            if len(parts) >= 6 and parts[-1] == "claim":
                team_id = parts[-4]
                task_id = parts[-2]

                if team_id not in self._claimed_tasks:
                    self._claimed_tasks[team_id] = set()

                if task_id in self._claimed_tasks[team_id]:
                    # Task already claimed — return 409 Conflict
                    self.send_response(409)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(
                        json.dumps(
                            {
                                "error": "task_already_claimed",
                                "message": f"Task {task_id} is already claimed",
                            }
                        ).encode(),
                    )
                else:
                    # Claim succeeds
                    self._claimed_tasks[team_id].add(task_id)
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(
                        json.dumps({"claimed": True}).encode(),
                    )
                return

        self.send_response(404)
        self.end_headers()

    @classmethod
    def reset(cls) -> None:
        """Reset stub state for test isolation."""
        cls._claimed_tasks.clear()
        cls._get_tasks_data.clear()

    @classmethod
    def set_tasks_for_team(cls, team_id: str, tasks: list[dict[str, Any]]) -> None:
        """Set the task list returned for a team."""
        cls._get_tasks_data[team_id] = tasks


@pytest.fixture
def stub_server() -> tuple[str, Thread]:
    """Start a minimal stub HTTP server and return its URL."""
    # Use port 0 to let the OS choose an available port
    server = HTTPServer(("127.0.0.1", 0), TaskClaimerStubHandler)
    host, port = server.server_address
    url = f"http://{host}:{port}"

    def run_server() -> None:
        server.serve_forever()

    thread = Thread(target=run_server, daemon=True)
    thread.start()

    yield url, thread

    server.shutdown()
    server.server_close()
    TaskClaimerStubHandler.reset()


class RestApiClient:
    """Async HTTP client for stub server endpoints (using urllib)."""

    def __init__(self, base_url: str) -> None:
        self.base_url = base_url

    async def get_tasks(self, team_id: str) -> list[dict[str, Any]]:
        """Fetch tasks for a team."""
        import urllib.request

        url = f"{self.base_url}/api/v1/teams/{team_id}/tasks"
        try:
            with urllib.request.urlopen(url, timeout=5) as response:
                data = json.loads(response.read().decode())
                return data.get("tasks", [])
        except Exception as e:
            raise RuntimeError(f"Failed to fetch tasks: {e}") from e

    async def claim_task(self, team_id: str, task_id: str) -> bool:
        """Attempt to claim a task. Returns True if successful, False if 409."""
        import urllib.error
        import urllib.request

        url = f"{self.base_url}/api/v1/teams/{team_id}/tasks/{task_id}/claim"
        try:
            with urllib.request.urlopen(
                urllib.request.Request(url, method="POST"),
                timeout=5,
            ) as response:
                if response.status == 200:
                    return True
        except urllib.error.HTTPError as e:
            if e.code == 409:
                # Task already claimed
                return False
            raise RuntimeError(f"Unexpected HTTP error: {e}") from e
        except Exception as e:
            raise RuntimeError(f"Failed to claim task: {e}") from e
        return False


@pytest.mark.asyncio
@pytest.mark.integration
async def test_integration_single_claim_succeeds(
    stub_server: tuple[str, Thread],
) -> None:
    """Verify a single claim succeeds via the REST API."""
    url, _ = stub_server
    client = RestApiClient(url)

    tasks = [{"id": "task-1"}, {"id": "task-2"}]
    TaskClaimerStubHandler.set_tasks_for_team("team-A", tasks)

    claimer = TaskClaimer(get_tasks=client.get_tasks, claim_task=client.claim_task)

    result = await claimer.advance_dag("team-A")

    assert sorted(result) == ["task-1", "task-2"]


@pytest.mark.asyncio
@pytest.mark.integration
async def test_integration_concurrent_claim_409_rejected(
    stub_server: tuple[str, Thread],
) -> None:
    """Verify that a second concurrent claim is rejected at the API layer (409).

    This test uses two independent TaskClaimer instances to exercise the 409
    rejection path at the REST API level, which is different from the in-memory
    coalesce guard that prevents concurrent _calls_ to the same TaskClaimer.
    """
    url, _ = stub_server
    client = RestApiClient(url)

    tasks = [{"id": "task-X"}]
    TaskClaimerStubHandler.set_tasks_for_team("team-Z", tasks)

    # Create two independent TaskClaimer instances (simulating different
    # processes or threads that both see the same backend)
    claimer1 = TaskClaimer(
        get_tasks=client.get_tasks,
        claim_task=client.claim_task,
    )
    claimer2 = TaskClaimer(
        get_tasks=client.get_tasks,
        claim_task=client.claim_task,
    )

    # Start both claimers concurrently
    result1, result2 = await asyncio.gather(
        claimer1.advance_dag("team-Z"),
        claimer2.advance_dag("team-Z"),
    )

    # One claimer succeeds, the other gets 409 and returns empty
    results = sorted([result1, result2])
    assert results[0] == []  # One got 409 (empty list)
    assert results[1] == ["task-X"]  # One succeeded


@pytest.mark.asyncio
@pytest.mark.integration
async def test_integration_error_handling_cleanup(
    stub_server: tuple[str, Thread],
) -> None:
    """Verify that _advancing cleanup happens even when claim_task fails.

    Simulates a transient network failure (e.g., timeout during claim) to ensure
    the finally block in advance_dag properly cleans up _advancing.
    """
    url, _ = stub_server
    client = RestApiClient(url)

    tasks = [{"id": "task-A"}]
    TaskClaimerStubHandler.set_tasks_for_team("team-B", tasks)

    call_count = 0

    async def flaky_claim_task(team_id: str, task_id: str) -> bool:
        nonlocal call_count
        call_count += 1
        if call_count == 1:
            # First attempt fails (e.g., network timeout)
            raise RuntimeError("network timeout")
        # Second attempt succeeds
        return await client.claim_task(team_id, task_id)

    claimer = TaskClaimer(
        get_tasks=client.get_tasks,
        claim_task=flaky_claim_task,
    )

    # First call fails
    with pytest.raises(RuntimeError, match="network timeout"):
        await claimer.advance_dag("team-B")

    # Verify _advancing was cleaned up: second call must not deadlock
    result = await claimer.advance_dag("team-B")
    assert result == ["task-A"]


@pytest.mark.asyncio
@pytest.mark.integration
async def test_integration_coalesce_guard_at_api_layer(
    stub_server: tuple[str, Thread],
) -> None:
    """Verify that the coalesce guard works at the API layer.

    When a single TaskClaimer has two concurrent advance_dag calls for the same
    team, the second call should be coalesced (skipped) before even calling
    get_tasks. This test verifies that behavior with a real REST API underneath.
    """
    url, _ = stub_server
    client = RestApiClient(url)

    tasks = [{"id": "task-1"}]
    TaskClaimerStubHandler.set_tasks_for_team("team-C", tasks)

    get_tasks_call_count = 0
    original_get_tasks = client.get_tasks

    async def counting_get_tasks(team_id: str) -> list[dict[str, Any]]:
        nonlocal get_tasks_call_count
        get_tasks_call_count += 1
        return await original_get_tasks(team_id)

    claimer = TaskClaimer(
        get_tasks=counting_get_tasks,
        claim_task=client.claim_task,
    )

    first_call_started = asyncio.Event()
    first_call_proceed = asyncio.Event()

    async def slow_get_tasks(team_id: str) -> list[dict[str, Any]]:
        nonlocal get_tasks_call_count
        get_tasks_call_count += 1
        first_call_started.set()
        await first_call_proceed.wait()
        return await original_get_tasks(team_id)

    claimer = TaskClaimer(
        get_tasks=slow_get_tasks,
        claim_task=client.claim_task,
    )

    async def first_call() -> list[str]:
        return await claimer.advance_dag("team-C")

    async def second_call() -> list[str]:
        await first_call_started.wait()
        result = await claimer.advance_dag("team-C")
        first_call_proceed.set()
        return result

    result1, result2 = await asyncio.gather(first_call(), second_call())

    # First call completes; second is coalesced (empty result)
    assert result1 == ["task-1"]
    assert result2 == []
    # get_tasks should only be called once (second call was skipped)
    assert get_tasks_call_count == 1


@pytest.mark.asyncio
@pytest.mark.integration
async def test_integration_empty_team(stub_server: tuple[str, Thread]) -> None:
    """Verify behavior when a team has no tasks."""
    url, _ = stub_server
    client = RestApiClient(url)

    # No tasks set for team-empty
    TaskClaimerStubHandler.set_tasks_for_team("team-empty", [])

    claimer = TaskClaimer(get_tasks=client.get_tasks, claim_task=client.claim_task)

    result = await claimer.advance_dag("team-empty")

    assert result == []
