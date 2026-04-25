"""Tests for MaestroClient._agent_from_api() and get_agents()."""
from __future__ import annotations

import pytest
from unittest.mock import AsyncMock, MagicMock

from src.keystone.maestro_client import _agent_from_api, MaestroClient


class TestAgentFromApi:
    def _entry(self, **overrides: object) -> dict:
        base = {
            "id": "agent-abc",
            "name": "TestAgent",
            "hostId": "host-1",
            "status": "active",
            "session": {"status": "online"},
            "taskDescription": "some description",
            "program": "researcher",
            "currentTaskId": None,
            "deletedAt": None,
        }
        base.update(overrides)
        return base

    def test_agent_from_api_basic_fields(self) -> None:
        agent = _agent_from_api(self._entry())
        assert agent.id == "agent-abc"
        assert agent.name == "TestAgent"
        assert agent.host == "host-1"
        assert agent.status == "active"
        assert agent.session_status == "online"
        assert agent.program == "researcher"

    def test_agent_from_api_current_task_id_populated(self) -> None:
        agent = _agent_from_api(self._entry(currentTaskId="task-xyz"))
        assert agent.current_task_id == "task-xyz"

    def test_agent_from_api_null_current_task_id(self) -> None:
        agent = _agent_from_api(self._entry(currentTaskId=None))
        assert agent.current_task_id is None

    def test_agent_from_api_missing_current_task_id(self) -> None:
        data = self._entry()
        data.pop("currentTaskId")
        agent = _agent_from_api(data)
        assert agent.current_task_id is None

    def test_agent_from_api_task_description_populated(self) -> None:
        agent = _agent_from_api(self._entry(taskDescription="working on task A"))
        assert agent.task_description == "working on task A"

    def test_agent_from_api_missing_task_description(self) -> None:
        data = self._entry()
        data.pop("taskDescription")
        agent = _agent_from_api(data)
        assert agent.task_description == ""

    def test_agent_from_api_nested_agent_key(self) -> None:
        """API may wrap agent data under an 'agent' key."""
        inner = self._entry(currentTaskId="task-nested")
        agent = _agent_from_api({"agent": inner})
        assert agent.current_task_id == "task-nested"
        assert agent.id == "agent-abc"

    def test_agent_from_api_session_status_from_nested_session(self) -> None:
        data = self._entry()
        data["session"] = {"status": "offline"}
        agent = _agent_from_api(data)
        assert agent.session_status == "offline"


class TestGetAgents:
    def _make_client(self, raw_agents: list) -> MaestroClient:
        resp = MagicMock()
        resp.json.return_value = {"agents": raw_agents}
        http = MagicMock()
        http.get = AsyncMock(return_value=resp)
        client = MaestroClient(http)
        return client

    def _raw_entry(self, id: str = "a1", deleted_at: object = None, current_task_id: object = None) -> dict:
        return {
            "id": id,
            "name": "Agent",
            "hostId": "h1",
            "status": "active",
            "session": {"status": "online"},
            "taskDescription": "",
            "program": "",
            "currentTaskId": current_task_id,
            "deletedAt": deleted_at,
        }

    async def test_get_agents_returns_active_agents(self) -> None:
        client = self._make_client([self._raw_entry("a1")])
        agents = await client.get_agents()
        assert len(agents) == 1
        assert agents[0].id == "a1"

    async def test_get_agents_filters_soft_deleted(self) -> None:
        client = self._make_client([
            self._raw_entry("a1"),
            self._raw_entry("a2", deleted_at="2026-01-01T00:00:00Z"),
        ])
        agents = await client.get_agents()
        assert len(agents) == 1
        assert agents[0].id == "a1"

    async def test_get_agents_populates_current_task_id(self) -> None:
        client = self._make_client([
            self._raw_entry("a1", current_task_id="task-99"),
        ])
        agents = await client.get_agents()
        assert agents[0].current_task_id == "task-99"

    async def test_get_agents_nested_agent_key_soft_delete(self) -> None:
        """Handles responses where agent data is nested under 'agent' key."""
        inner = self._raw_entry("a1", deleted_at="2026-01-01T00:00:00Z")
        raw = [{"agent": inner}]
        client = self._make_client(raw)
        # Patch _make_client to use nested form
        resp = MagicMock()
        resp.json.return_value = {"agents": raw}
        http = MagicMock()
        http.get = AsyncMock(return_value=resp)
        client = MaestroClient(http)
        agents = await client.get_agents()
        assert agents == []


class TestAssignTask:
    def _make_client(self) -> tuple[MaestroClient, AsyncMock]:
        resp = MagicMock()
        http = MagicMock()
        put_mock = AsyncMock(return_value=resp)
        http.put = put_mock
        client = MaestroClient(http)
        return client, put_mock

    @pytest.mark.asyncio
    async def test_assign_task_calls_put_with_correct_args(self) -> None:
        client, put_mock = self._make_client()
        await client.assign_task("task-123", "agent-456")
        put_mock.assert_called_once_with(
            "/api/tasks/task-123/assign",
            json={"agentId": "agent-456"},
        )

    @pytest.mark.asyncio
    async def test_assign_task_task_id_in_url(self) -> None:
        client, put_mock = self._make_client()
        await client.assign_task("task-abc", "agent-xyz")
        call_args = put_mock.call_args
        assert "/api/tasks/task-abc/assign" in call_args[0][0]

    @pytest.mark.asyncio
    async def test_assign_task_agent_id_in_body(self) -> None:
        client, put_mock = self._make_client()
        await client.assign_task("task-1", "agent-999")
        call_kwargs = put_mock.call_args[1]
        assert call_kwargs["json"] == {"agentId": "agent-999"}

    @pytest.mark.asyncio
    async def test_assign_task_different_ids(self) -> None:
        client, put_mock = self._make_client()
        await client.assign_task("task-XYZ", "agent-ABC")
        put_mock.assert_called_once_with(
            "/api/tasks/task-XYZ/assign",
            json={"agentId": "agent-ABC"},
        )


class TestGetTasks:
    def _make_client(self, raw_tasks: list) -> MaestroClient:
        resp = MagicMock()
        resp.json.return_value = {"tasks": raw_tasks}
        http = MagicMock()
        http.get = AsyncMock(return_value=resp)
        return MaestroClient(http)

    def _raw_task(
        self,
        id: str = "t1",
        title: str = "Task One",
        status: str = "pending",
        dependencies: list | None = None,
        assigned_agent_id: str | None = None,
    ) -> dict:
        return {
            "id": id,
            "title": title,
            "status": status,
            "dependencies": dependencies or [],
            "assignedAgentId": assigned_agent_id,
        }

    @pytest.mark.asyncio
    async def test_get_tasks_returns_list(self) -> None:
        client = self._make_client([self._raw_task("t1"), self._raw_task("t2")])
        tasks = await client.get_tasks()
        assert len(tasks) == 2
        assert tasks[0].id == "t1"
        assert tasks[1].id == "t2"

    @pytest.mark.asyncio
    async def test_get_tasks_handles_dict_with_tasks_key(self) -> None:
        client = self._make_client([self._raw_task("t-wrap")])
        tasks = await client.get_tasks()
        assert len(tasks) == 1
        assert tasks[0].id == "t-wrap"

    @pytest.mark.asyncio
    async def test_get_tasks_returns_empty_list(self) -> None:
        client = self._make_client([])
        tasks = await client.get_tasks()
        assert tasks == []

    @pytest.mark.asyncio
    async def test_get_tasks_populates_title(self) -> None:
        client = self._make_client([self._raw_task("t1", title="My Task")])
        tasks = await client.get_tasks()
        assert tasks[0].title == "My Task"

    @pytest.mark.asyncio
    async def test_get_tasks_populates_status(self) -> None:
        client = self._make_client([self._raw_task("t1", status="completed")])
        tasks = await client.get_tasks()
        assert tasks[0].status == "completed"

    @pytest.mark.asyncio
    async def test_get_tasks_handles_list_response_directly(self) -> None:
        raw = [self._raw_task("t1")]
        resp = MagicMock()
        resp.json.return_value = raw
        http = MagicMock()
        http.get = AsyncMock(return_value=resp)
        client = MaestroClient(http)
        tasks = await client.get_tasks()
        assert len(tasks) == 1
        assert tasks[0].id == "t1"

    @pytest.mark.asyncio
    async def test_get_tasks_assigned_agent_id(self) -> None:
        client = self._make_client([
            self._raw_task("t1", assigned_agent_id="agent-42")
        ])
        tasks = await client.get_tasks()
        assert tasks[0].assigned_agent_id == "agent-42"


class TestWithRetries:
    def _make_client(self) -> MaestroClient:
        http = MagicMock()
        return MaestroClient(http)

    @pytest.mark.asyncio
    async def test_with_retries_calls_fn_and_returns_result(self) -> None:
        client = self._make_client()
        expected = object()
        fn = AsyncMock(return_value=expected)
        result = await client._with_retries(fn)
        fn.assert_called_once()
        assert result is expected

    @pytest.mark.asyncio
    async def test_with_retries_propagates_exception(self) -> None:
        client = self._make_client()
        fn = AsyncMock(side_effect=RuntimeError("connection failed"))
        with pytest.raises(RuntimeError, match="connection failed"):
            await client._with_retries(fn)

    @pytest.mark.asyncio
    async def test_with_retries_returns_none_on_none_result(self) -> None:
        client = self._make_client()
        fn = AsyncMock(return_value=None)
        result = await client._with_retries(fn)
        assert result is None
