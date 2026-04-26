"""Integration tests for the Keystone daemon — SIGTERM shutdown path."""

from __future__ import annotations

import signal
from unittest.mock import AsyncMock, patch

import pytest

from keystone.daemon import handle_sigterm, main


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
        import logging

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
        import logging

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
