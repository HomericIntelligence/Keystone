"""Tests for DAGWalker.get_available_agents() and advance_dag()."""
from __future__ import annotations

import pytest

from src.keystone.dag_walker import DAGWalker
from tests.helpers import make_agent, make_task


class TestGetAvailableAgents:
    def test_online_active_agent_available(self) -> None:
        agent = make_agent()
        walker = DAGWalker(tasks=[], agents=[agent])
        assert agent in walker.get_available_agents()

    def test_busy_agent_not_available(self) -> None:
        agent = make_agent(current_task_id="some-task")
        walker = DAGWalker(tasks=[], agents=[agent])
        assert agent not in walker.get_available_agents()

    def test_inactive_agent_not_available(self) -> None:
        agent = make_agent(status="inactive")
        walker = DAGWalker(tasks=[], agents=[agent])
        assert agent not in walker.get_available_agents()

    def test_offline_agent_not_available(self) -> None:
        agent = make_agent(session_status="offline")
        walker = DAGWalker(tasks=[], agents=[agent])
        assert agent not in walker.get_available_agents()

    def test_mixed_agents_only_idle_returned(self) -> None:
        idle = make_agent(id="a1")
        busy = make_agent(id="a2", current_task_id="t-99")
        offline = make_agent(id="a3", session_status="offline")
        walker = DAGWalker(tasks=[], agents=[idle, busy, offline])
        result = walker.get_available_agents()
        assert result == [idle]


class TestGetReadyTasks:
    def test_task_with_no_deps_is_ready(self) -> None:
        task = make_task()
        walker = DAGWalker(tasks=[task], agents=[])
        assert task in walker.get_ready_tasks()

    def test_task_with_completed_dep_is_ready(self) -> None:
        dep = make_task(id="dep-1", status="completed")
        task = make_task(id="task-2", dependencies=["dep-1"])
        walker = DAGWalker(tasks=[dep, task], agents=[])
        assert task in walker.get_ready_tasks()

    def test_task_with_pending_dep_not_ready(self) -> None:
        dep = make_task(id="dep-1", status="pending")
        task = make_task(id="task-2", dependencies=["dep-1"])
        walker = DAGWalker(tasks=[dep, task], agents=[])
        assert task not in walker.get_ready_tasks()

    def test_already_assigned_task_not_ready(self) -> None:
        task = make_task(assigned_agent_id="agent-99")
        walker = DAGWalker(tasks=[task], agents=[])
        assert task not in walker.get_ready_tasks()


class TestAdvanceDag:
    @pytest.mark.asyncio
    async def test_assigns_ready_task_to_available_agent(self) -> None:
        task = make_task()
        agent = make_agent()
        walker = DAGWalker(tasks=[task], agents=[agent])
        assignments = await walker.advance_dag()
        assert len(assignments) == 1
        assert assignments[0] == (task, agent)
        assert agent.current_task_id == task.id
        assert task.assigned_agent_id == agent.id

    @pytest.mark.asyncio
    async def test_multiple_assignments_use_different_agents(self) -> None:
        task1 = make_task(id="t1")
        task2 = make_task(id="t2")
        agent1 = make_agent(id="a1")
        agent2 = make_agent(id="a2")
        walker = DAGWalker(tasks=[task1, task2], agents=[agent1, agent2])
        assignments = await walker.advance_dag()
        assert len(assignments) == 2
        assigned_agents = {a.id for _, a in assignments}
        assert assigned_agents == {"a1", "a2"}

    @pytest.mark.asyncio
    async def test_busy_agent_not_double_assigned(self) -> None:
        """A single idle agent with two ready tasks should only get one assignment."""
        task1 = make_task(id="t1")
        task2 = make_task(id="t2")
        agent = make_agent(id="a1")
        walker = DAGWalker(tasks=[task1, task2], agents=[agent])
        assignments = await walker.advance_dag()
        assert len(assignments) == 1
        # Agent is now marked busy
        assert agent.current_task_id is not None

    @pytest.mark.asyncio
    async def test_agent_marked_busy_immediately(self) -> None:
        """Within advance_dag, agent.current_task_id is set before next iteration."""
        task1 = make_task(id="t1")
        task2 = make_task(id="t2")
        agent = make_agent(id="a1")
        walker = DAGWalker(tasks=[task1, task2], agents=[agent])
        await walker.advance_dag()
        # Only one task should be assigned since agent was marked busy after first
        assert sum(1 for t in walker.tasks if t.assigned_agent_id is not None) == 1

    @pytest.mark.asyncio
    async def test_no_available_agents_no_assignments(self) -> None:
        task = make_task()
        agent = make_agent(current_task_id="other-task")
        walker = DAGWalker(tasks=[task], agents=[agent])
        assignments = await walker.advance_dag()
        assert assignments == []

    @pytest.mark.asyncio
    async def test_no_ready_tasks_no_assignments(self) -> None:
        # dep is in-progress (not pending, not terminal) — t1 depends on it and is blocked
        dep = make_task(id="dep", status="in_progress")
        task = make_task(id="t1", dependencies=["dep"])
        agent = make_agent()
        walker = DAGWalker(tasks=[dep, task], agents=[agent])
        assignments = await walker.advance_dag()
        assert assignments == []
