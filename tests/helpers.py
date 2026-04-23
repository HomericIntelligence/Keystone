from __future__ import annotations

from typing import Optional

from src.keystone.models import Agent, Task


def make_agent(
    id: str = "agent-1",
    name: str = "Agent 1",
    host: str = "host-1",
    status: str = "active",
    session_status: str = "online",
    task_description: str = "",
    program: str = "",
    current_task_id: Optional[str] = None,
) -> Agent:
    return Agent(
        id=id,
        name=name,
        host=host,
        status=status,
        session_status=session_status,
        task_description=task_description,
        program=program,
        current_task_id=current_task_id,
    )


def make_task(
    id: str = "task-1",
    title: str = "Task 1",
    status: str = "pending",
    dependencies: Optional[list[str]] = None,
    assigned_agent_id: Optional[str] = None,
) -> Task:
    return Task(
        id=id,
        title=title,
        status=status,
        dependencies=dependencies or [],
        assigned_agent_id=assigned_agent_id,
    )
