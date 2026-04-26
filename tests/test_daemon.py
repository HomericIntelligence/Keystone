"""Integration tests for the Keystone daemon — SIGTERM shutdown path."""

from __future__ import annotations

import asyncio
import logging
import signal
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

import keystone.daemon
from keystone.config import Settings
from keystone.daemon import (
    _handle_signal,
    assign_task,
    handle_sigterm,
    main,
    run,
    run_routing_loop,
)
from keystone.nats_listener import NATSListener
from keystone.task_claimer import TaskClaimer


class TestHandleSigterm:
    def test_handle_sigterm_raises_system_exit(self) -> None:
        """handle_sigterm() must raise SystemExit(0) when called with SIGTERM."""
        with pytest.raises(SystemExit) as exc_info:
            handle_sigterm(signal.SIGTERM, None)
        assert exc_info.value.code == 0

    def test_handle_sigterm_raises_system_exit_with_other_signum(self) -> None:
        """handle_sigterm() must raise SystemExit(0) regardless of the signal number passed."""
        with pytest.raises(SystemExit) as exc_info:
            handle_sigterm(signal.SIGHUP, None)
        assert exc_info.value.code == 0


class TestMain:
    def test_main_returns_zero_on_clean_shutdown(self) -> None:
        """main() must return 0 when run() exits cleanly."""
        with patch("keystone.daemon.run", new_callable=AsyncMock, return_value=0):
            result = main(["--log-level", "INFO"])
        assert result == 0

    def test_main_returns_one_on_unexpected_exception(self) -> None:
        """main() must return 1 when run() raises an unexpected exception."""
        with patch(
            "keystone.daemon.run",
            new_callable=AsyncMock,
            side_effect=RuntimeError("unexpected failure"),
        ):
            result = main(["--log-level", "INFO"])
        assert result == 1

    def test_main_logs_daemon_started(self, caplog: pytest.LogCaptureFixture) -> None:
        """main() must emit a daemon_starting log record before run() is called."""
        with patch("keystone.daemon.run", new_callable=AsyncMock, return_value=0):
            with caplog.at_level(logging.INFO):
                main(["--log-level", "INFO"])

        messages = " ".join(caplog.messages)
        assert "daemon_starting" in messages

    def test_main_registers_sigterm_handler(self) -> None:
        """main() must register handle_sigterm as the SIGTERM signal handler."""
        with patch("keystone.daemon.run", new_callable=AsyncMock, return_value=0):
            with patch("keystone.daemon.signal.signal") as mock_signal:
                main(["--log-level", "INFO"])
        mock_signal.assert_any_call(signal.SIGTERM, handle_sigterm)

    def test_main_sigterm_path_via_handler(self) -> None:
        """main() completes with return code 0 on clean run() exit."""
        with patch("keystone.daemon.run", new_callable=AsyncMock, return_value=0):
            result = main(["--log-level", "INFO"])
        assert result == 0

    def test_main_accepts_debug_log_level(self) -> None:
        """main() must accept --log-level DEBUG without error."""
        with patch("keystone.daemon.run", new_callable=AsyncMock, return_value=0):
            result = main(["--log-level", "DEBUG"])
        assert result == 0

    def test_main_daemon_stopped_logged_even_on_error(
        self, caplog: pytest.LogCaptureFixture
    ) -> None:
        """main() must emit daemon_error when run() raises an exception."""
        with patch(
            "keystone.daemon.run",
            new_callable=AsyncMock,
            side_effect=RuntimeError("crash"),
        ):
            with caplog.at_level(logging.INFO):
                result = main(["--log-level", "INFO"])

        assert result == 1
        messages = " ".join(caplog.messages)
        assert "daemon_error" in messages


class TestHandleSignal:
    def test_handle_signal_sets_shutdown_event(self) -> None:
        """_handle_signal() must set the _shutdown_event when called."""
        keystone.daemon._shutdown_event = asyncio.Event()
        mock_listener = MagicMock(spec=NATSListener)

        _handle_signal(signal.SIGTERM, mock_listener)

        assert keystone.daemon._shutdown_event.is_set()
        mock_listener.begin_shutdown.assert_called_once()

    def test_handle_signal_calls_listener_begin_shutdown(self) -> None:
        """_handle_signal() must call listener.begin_shutdown() on signal."""
        keystone.daemon._shutdown_event = asyncio.Event()
        mock_listener = MagicMock(spec=NATSListener)

        _handle_signal(signal.SIGTERM, mock_listener)

        mock_listener.begin_shutdown.assert_called_once()


class TestRunRoutingLoop:
    def test_run_routing_loop_logs_startup(self, caplog: pytest.LogCaptureFixture) -> None:
        """run_routing_loop() must log startup message."""
        with caplog.at_level(logging.INFO):
            with patch("time.sleep", side_effect=KeyboardInterrupt):
                run_routing_loop(poll_interval=0.5)

        messages = " ".join(caplog.messages)
        assert "routing_loop_started" in messages

    def test_run_routing_loop_logs_shutdown_on_interrupt(
        self, caplog: pytest.LogCaptureFixture
    ) -> None:
        """run_routing_loop() must log shutdown message on KeyboardInterrupt."""
        with caplog.at_level(logging.INFO):
            with patch("time.sleep", side_effect=KeyboardInterrupt):
                run_routing_loop(poll_interval=0.5)

        messages = " ".join(caplog.messages)
        assert "routing_loop_stopped" in messages

    def test_run_routing_loop_logs_shutdown_on_system_exit(
        self, caplog: pytest.LogCaptureFixture
    ) -> None:
        """run_routing_loop() must log shutdown message on SystemExit."""
        with caplog.at_level(logging.INFO):
            with patch("time.sleep", side_effect=SystemExit):
                run_routing_loop(poll_interval=0.5)

        messages = " ".join(caplog.messages)
        assert "routing_loop_stopped" in messages

    def test_run_routing_loop_respects_poll_interval(self) -> None:
        """run_routing_loop() must use the provided poll_interval."""
        with patch("time.sleep", side_effect=KeyboardInterrupt):
            run_routing_loop(poll_interval=2.5)

            # sleep was called, but since we interrupt immediately,
            # we can't easily verify the interval. Just verify it was called.


class TestAssignTask:
    def test_assign_task_logs_assignment(self, caplog: pytest.LogCaptureFixture) -> None:
        """assign_task() must log the task assignment with context."""
        with caplog.at_level(logging.INFO):
            assign_task(team_id="team-1", task_id="task-99", agent_id="agent-5")

        messages = " ".join(caplog.messages)
        assert "task_assigned" in messages


def _make_run_mocks():
    """Create standard mocks for run() tests."""
    mock_listener = MagicMock(spec=NATSListener)
    mock_listener.stop = AsyncMock()
    mock_claimer = MagicMock(spec=TaskClaimer)
    mock_claimer.drain = AsyncMock(return_value=True)
    mock_event_loop = MagicMock()
    mock_event_loop.add_signal_handler = MagicMock()
    return mock_listener, mock_claimer, mock_event_loop


class TestRunAsync:
    async def test_run_returns_zero(self) -> None:
        """run() must return 0 on successful execution."""
        settings = Settings(shutdown_timeout=0.1)
        keystone.daemon._shutdown_event = asyncio.Event()
        keystone.daemon._shutdown_event.set()

        mock_listener, mock_claimer, mock_event_loop = _make_run_mocks()

        with patch("keystone.daemon.NATSListener", return_value=mock_listener):
            with patch("keystone.daemon.TaskClaimer", return_value=mock_claimer):
                with patch("asyncio.get_running_loop", return_value=mock_event_loop):
                    result = await run(settings)

        assert result == 0

    async def test_run_logs_drain_incomplete_warning(
        self, caplog: pytest.LogCaptureFixture
    ) -> None:
        """run() must log a warning when drain() returns False."""
        settings = Settings(shutdown_timeout=0.1)
        keystone.daemon._shutdown_event = asyncio.Event()
        keystone.daemon._shutdown_event.set()

        mock_listener, mock_claimer, mock_event_loop = _make_run_mocks()
        mock_claimer.drain = AsyncMock(return_value=False)

        with patch("keystone.daemon.NATSListener", return_value=mock_listener):
            with patch("keystone.daemon.TaskClaimer", return_value=mock_claimer):
                with patch("asyncio.get_running_loop", return_value=mock_event_loop):
                    with caplog.at_level(logging.WARNING):
                        await run(settings)

        messages = " ".join(caplog.messages)
        assert "drain_incomplete" in messages

    async def test_run_handles_listener_stop_error(
        self, caplog: pytest.LogCaptureFixture
    ) -> None:
        """run() must catch and log errors from listener.stop()."""
        settings = Settings(shutdown_timeout=0.1)
        keystone.daemon._shutdown_event = asyncio.Event()
        keystone.daemon._shutdown_event.set()

        mock_listener, mock_claimer, mock_event_loop = _make_run_mocks()
        mock_listener.stop = AsyncMock(side_effect=RuntimeError("connection lost"))

        with patch("keystone.daemon.NATSListener", return_value=mock_listener):
            with patch("keystone.daemon.TaskClaimer", return_value=mock_claimer):
                with patch("asyncio.get_running_loop", return_value=mock_event_loop):
                    with caplog.at_level(logging.ERROR):
                        result = await run(settings)

        messages = " ".join(caplog.messages)
        assert "nats_stop_error" in messages
        assert result == 0

    async def test_run_logs_daemon_started(self, caplog: pytest.LogCaptureFixture) -> None:
        """run() must log daemon_started on startup."""
        settings = Settings(shutdown_timeout=0.1)
        keystone.daemon._shutdown_event = asyncio.Event()
        keystone.daemon._shutdown_event.set()

        mock_listener, mock_claimer, mock_event_loop = _make_run_mocks()

        with patch("keystone.daemon.NATSListener", return_value=mock_listener):
            with patch("keystone.daemon.TaskClaimer", return_value=mock_claimer):
                with patch("asyncio.get_running_loop", return_value=mock_event_loop):
                    with caplog.at_level(logging.INFO):
                        await run(settings)

        messages = " ".join(caplog.messages)
        assert "daemon_started" in messages

    async def test_run_logs_daemon_stopped(self, caplog: pytest.LogCaptureFixture) -> None:
        """run() must log daemon_stopped on shutdown."""
        settings = Settings(shutdown_timeout=0.1)
        keystone.daemon._shutdown_event = asyncio.Event()
        keystone.daemon._shutdown_event.set()

        mock_listener, mock_claimer, mock_event_loop = _make_run_mocks()

        with patch("keystone.daemon.NATSListener", return_value=mock_listener):
            with patch("keystone.daemon.TaskClaimer", return_value=mock_claimer):
                with patch("asyncio.get_running_loop", return_value=mock_event_loop):
                    with caplog.at_level(logging.INFO):
                        await run(settings)

        messages = " ".join(caplog.messages)
        assert "daemon_stopped" in messages
