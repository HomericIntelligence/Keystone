"""Tests for DAGWalker — cycle detection, ready tasks, available agents, and advance_dag."""
from __future__ import annotations

from unittest.mock import AsyncMock

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


class TestValidateNoCycles:
    def test_empty_dag_is_acyclic(self) -> None:
        walker = DAGWalker(tasks=[], agents=[])
        assert walker.validate_no_cycles() is True

    def test_single_task_no_deps_is_acyclic(self) -> None:
        task = make_task(id="t1")
        walker = DAGWalker(tasks=[task], agents=[])
        assert walker.validate_no_cycles() is True

    def test_linear_chain_is_acyclic(self) -> None:
        # t1 -> t2 -> t3
        t1 = make_task(id="t1")
        t2 = make_task(id="t2", dependencies=["t1"])
        t3 = make_task(id="t3", dependencies=["t2"])
        walker = DAGWalker(tasks=[t1, t2, t3], agents=[])
        assert walker.validate_no_cycles() is True

    def test_diamond_dag_is_acyclic(self) -> None:
        # t1 -> t2, t1 -> t3, t2 -> t4, t3 -> t4
        t1 = make_task(id="t1")
        t2 = make_task(id="t2", dependencies=["t1"])
        t3 = make_task(id="t3", dependencies=["t1"])
        t4 = make_task(id="t4", dependencies=["t2", "t3"])
        walker = DAGWalker(tasks=[t1, t2, t3, t4], agents=[])
        assert walker.validate_no_cycles() is True

    def test_two_node_cycle_detected(self) -> None:
        # t1 -> t2 -> t1
        t1 = make_task(id="t1", dependencies=["t2"])
        t2 = make_task(id="t2", dependencies=["t1"])
        walker = DAGWalker(tasks=[t1, t2], agents=[])
        assert walker.validate_no_cycles() is False

    def test_three_node_cycle_detected(self) -> None:
        # t1 -> t2 -> t3 -> t1
        t1 = make_task(id="t1", dependencies=["t3"])
        t2 = make_task(id="t2", dependencies=["t1"])
        t3 = make_task(id="t3", dependencies=["t2"])
        walker = DAGWalker(tasks=[t1, t2, t3], agents=[])
        assert walker.validate_no_cycles() is False

    def test_self_cycle_detected(self) -> None:
        t1 = make_task(id="t1", dependencies=["t1"])
        walker = DAGWalker(tasks=[t1], agents=[])
        assert walker.validate_no_cycles() is False

    def test_cycle_in_subgraph_detected(self) -> None:
        # t1 is clean; t2 <-> t3 form a cycle
        t1 = make_task(id="t1")
        t2 = make_task(id="t2", dependencies=["t3"])
        t3 = make_task(id="t3", dependencies=["t2"])
        walker = DAGWalker(tasks=[t1, t2, t3], agents=[])
        assert walker.validate_no_cycles() is False

    def test_deep_chain_no_recursion_error(self) -> None:
        """2000-node linear chain must not raise RecursionError (regression for #100)."""
        n = 2000
        tasks = [make_task(id="t0")]
        for i in range(1, n):
            tasks.append(make_task(id=f"t{i}", dependencies=[f"t{i - 1}"]))
        walker = DAGWalker(tasks=tasks, agents=[])
        assert walker.validate_no_cycles() is True

    def test_external_dependency_ignored(self) -> None:
        """Dependencies referencing task IDs not in the graph are silently ignored."""
        t1 = make_task(id="t1", dependencies=["external-task-not-in-graph"])
        walker = DAGWalker(tasks=[t1], agents=[])
        assert walker.validate_no_cycles() is True


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
        assert agent.current_task_id is not None

    @pytest.mark.asyncio
    async def test_agent_marked_busy_immediately(self) -> None:
        task1 = make_task(id="t1")
        task2 = make_task(id="t2")
        agent = make_agent(id="a1")
        walker = DAGWalker(tasks=[task1, task2], agents=[agent])
        await walker.advance_dag()
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
        dep = make_task(id="dep", status="in_progress")
        task = make_task(id="t1", dependencies=["dep"])
        agent = make_agent()
        walker = DAGWalker(tasks=[dep, task], agents=[agent])
        assignments = await walker.advance_dag()
        assert assignments == []

    @pytest.mark.asyncio
    async def test_advance_dag_calls_client_assign_task(self) -> None:
        """advance_dag() must call client.assign_task(task_id, agent_id) when a client is set."""
        task = make_task()
        agent = make_agent()
        mock_client = AsyncMock()
        walker = DAGWalker(tasks=[task], agents=[agent], client=mock_client)

        assignments = await walker.advance_dag()

        assert len(assignments) == 1
        mock_client.assign_task.assert_awaited_once_with(task.id, agent.id)

    @pytest.mark.asyncio
    async def test_advance_dag_no_client_no_api_call(self) -> None:
        """advance_dag() must succeed and return assignments when no client is provided."""
        task = make_task()
        agent = make_agent()
        walker = DAGWalker(tasks=[task], agents=[agent], client=None)

        assignments = await walker.advance_dag()

        assert len(assignments) == 1
        assert assignments[0] == (task, agent)

    @pytest.mark.asyncio
    async def test_advance_dag_marks_agent_busy(self) -> None:
        """advance_dag() must set agent.current_task_id after assignment."""
        task = make_task()
        agent = make_agent()
        walker = DAGWalker(tasks=[task], agents=[agent])

        await walker.advance_dag()

        assert agent.current_task_id == task.id
        assert task.assigned_agent_id == agent.id

    @pytest.mark.asyncio
    async def test_advance_dag_no_ready_tasks_returns_empty_with_client(self) -> None:
        """advance_dag() must return [] and never call assign_task when no tasks are ready."""
        dep = make_task(id="dep", status="in_progress")
        task = make_task(id="t1", dependencies=["dep"])
        agent = make_agent()
        mock_client = AsyncMock()
        walker = DAGWalker(tasks=[dep, task], agents=[agent], client=mock_client)

        assignments = await walker.advance_dag()

        assert assignments == []
        mock_client.assign_task.assert_not_awaited()

    @pytest.mark.asyncio
    async def test_advance_dag_client_called_for_each_assignment(self) -> None:
        """assign_task() must be called once per task-agent pair when multiple assignments occur."""
        task1 = make_task(id="t1")
        task2 = make_task(id="t2")
        agent1 = make_agent(id="a1")
        agent2 = make_agent(id="a2")
        mock_client = AsyncMock()
        walker = DAGWalker(
            tasks=[task1, task2], agents=[agent1, agent2], client=mock_client
        )

        assignments = await walker.advance_dag()

        assert len(assignments) == 2
        assert mock_client.assign_task.await_count == 2
        awaited_calls = {call.args for call in mock_client.assign_task.await_args_list}
        assert (task1.id, agent1.id) in awaited_calls or (
            task1.id,
            agent2.id,
        ) in awaited_calls
        assert (task2.id, agent1.id) in awaited_calls or (
            task2.id,
            agent2.id,
        ) in awaited_calls
