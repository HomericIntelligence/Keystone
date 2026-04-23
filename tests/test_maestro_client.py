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

    @pytest.mark.asyncio
    async def test_get_agents_returns_active_agents(self) -> None:
        client = self._make_client([self._raw_entry("a1")])
        agents = await client.get_agents()
        assert len(agents) == 1
        assert agents[0].id == "a1"

    @pytest.mark.asyncio
    async def test_get_agents_filters_soft_deleted(self) -> None:
        client = self._make_client([
            self._raw_entry("a1"),
            self._raw_entry("a2", deleted_at="2026-01-01T00:00:00Z"),
        ])
        agents = await client.get_agents()
        assert len(agents) == 1
        assert agents[0].id == "a1"

    @pytest.mark.asyncio
    async def test_get_agents_populates_current_task_id(self) -> None:
        client = self._make_client([
            self._raw_entry("a1", current_task_id="task-99"),
        ])
        agents = await client.get_agents()
        assert agents[0].current_task_id == "task-99"

    @pytest.mark.asyncio
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
