"""Tests for NATSListener ID validation in _on_task_event."""
from __future__ import annotations

import json

import pytest
from unittest.mock import MagicMock

from src.keystone.nats_listener import NATSListener


def _make_listener() -> tuple[NATSListener, MagicMock]:
    claimer = MagicMock()
    claimer.advance_dag_tracked = MagicMock()
    listener = NATSListener(claimer)
    return listener, claimer


class TestOnTaskEventValidIds:
    async def test_valid_ids_dispatches(self) -> None:
        listener, claimer = _make_listener()
        await listener._on_task_event("hi.tasks.team-1.task-42.completed", "team-1", "task-42")
        claimer.advance_dag_tracked.assert_called_once_with("team-1")

    async def test_valid_uuid_ids_dispatches(self) -> None:
        listener, claimer = _make_listener()
        team = "550e8400-e29b-41d4-a716-446655440000"
        task = "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
        await listener._on_task_event(f"hi.tasks.{team}.{task}.completed", team, task)
        claimer.advance_dag_tracked.assert_called_once_with(team)


class TestOnTaskEventInvalidIds:
    async def test_empty_team_id_dropped(self) -> None:
        listener, claimer = _make_listener()
        await listener._on_task_event("hi.tasks...task-1.completed", "", "task-1")
        claimer.advance_dag_tracked.assert_not_called()

    async def test_slash_in_team_id_dropped(self) -> None:
        listener, claimer = _make_listener()
        await listener._on_task_event("hi.tasks.../admin.task-1.completed", "../admin", "task-1")
        claimer.advance_dag_tracked.assert_not_called()

    async def test_empty_task_id_dropped(self) -> None:
        listener, claimer = _make_listener()
        await listener._on_task_event("hi.tasks.team-1..completed", "team-1", "")
        claimer.advance_dag_tracked.assert_not_called()

    async def test_path_traversal_task_id_dropped(self) -> None:
        listener, claimer = _make_listener()
        await listener._on_task_event("hi.tasks.team-1.../etc.completed", "team-1", "../etc")
        claimer.advance_dag_tracked.assert_not_called()

    async def test_overlong_team_id_dropped(self) -> None:
        listener, claimer = _make_listener()
        bad_team = "a" * 257
        await listener._on_task_event("hi.tasks.AAA.task-1.completed", bad_team, "task-1")
        claimer.advance_dag_tracked.assert_not_called()


class TestOnTaskEventShutdown:
    async def test_event_dropped_during_shutdown(self) -> None:
        listener, claimer = _make_listener()
        listener.begin_shutdown()
        await listener._on_task_event("hi.tasks.team-1.task-1.completed", "team-1", "task-1")
        claimer.advance_dag_tracked.assert_not_called()

    async def test_shutdown_checked_before_validation(self) -> None:
        listener, claimer = _make_listener()
        listener.begin_shutdown()
        # Even with invalid IDs, no error raised — shutdown takes priority
        await listener._on_task_event("bad", "", "")
        claimer.advance_dag_tracked.assert_not_called()


class TestOnTaskEventRawPayload:
    @pytest.mark.asyncio
    async def test_valid_json_payload_dispatches(self) -> None:
        listener, claimer = _make_listener()
        payload = json.dumps({"status": "completed"}).encode()
        await listener._on_task_event(
            "hi.tasks.team-1.task-42.completed", "team-1", "task-42", raw_payload=payload
        )
        claimer.advance_dag_tracked.assert_called_once_with("team-1")

    @pytest.mark.asyncio
    async def test_invalid_json_payload_drops_event(self) -> None:
        listener, claimer = _make_listener()
        await listener._on_task_event(
            "hi.tasks.team-1.task-42.completed",
            "team-1",
            "task-42",
            raw_payload=b"not-valid-json",
        )
        claimer.advance_dag_tracked.assert_not_called()

    @pytest.mark.asyncio
    async def test_payload_missing_status_still_dispatches(self) -> None:
        listener, claimer = _make_listener()
        payload = json.dumps({}).encode()
        await listener._on_task_event(
            "hi.tasks.team-1.task-42.completed", "team-1", "task-42", raw_payload=payload
        )
        claimer.advance_dag_tracked.assert_called_once_with("team-1")

    @pytest.mark.asyncio
    async def test_nested_data_status_resolves_effective_status(self) -> None:
        listener, claimer = _make_listener()
        payload = json.dumps({"data": {"status": "completed"}}).encode()
        await listener._on_task_event(
            "hi.tasks.team-1.task-42.completed", "team-1", "task-42", raw_payload=payload
        )
        claimer.advance_dag_tracked.assert_called_once_with("team-1")

    @pytest.mark.asyncio
    async def test_none_payload_dispatches_without_parsing(self) -> None:
        listener, claimer = _make_listener()
        await listener._on_task_event(
            "hi.tasks.team-1.task-42.completed", "team-1", "task-42", raw_payload=None
        )
        claimer.advance_dag_tracked.assert_called_once_with("team-1")
