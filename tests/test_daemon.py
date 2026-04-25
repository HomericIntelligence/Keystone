"""Integration tests for the Keystone daemon — SIGTERM shutdown path."""

from __future__ import annotations

import signal
from unittest.mock import patch

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
        """main() must return 0 when run_routing_loop exits cleanly (returns normally)."""
        # run_routing_loop catches KeyboardInterrupt/SystemExit internally and returns
        # normally, so main() should then return 0.
        with patch("keystone.daemon.run_routing_loop", return_value=None):
            result = main(["--log-level", "INFO"])
        assert result == 0

    def test_main_returns_one_on_unexpected_exception(self) -> None:
        """main() must return 1 when run_routing_loop raises an unexpected exception."""
        with patch(
            "keystone.daemon.run_routing_loop",
            side_effect=RuntimeError("unexpected failure"),
        ):
            result = main(["--log-level", "INFO"])
        assert result == 1

    def test_main_logs_daemon_started(self, caplog: pytest.LogCaptureFixture) -> None:
        """main() must emit a daemon_starting log record before the routing loop runs."""
        import logging

        with patch("keystone.daemon.run_routing_loop", return_value=None):
            with caplog.at_level(logging.INFO):
                main(["--log-level", "INFO"])

        messages = " ".join(caplog.messages)
        assert "daemon_starting" in messages

    def test_main_logs_daemon_stopped(self, caplog: pytest.LogCaptureFixture) -> None:
        """main() must emit a daemon_stopped log record in the finally block."""
        import logging

        with patch("keystone.daemon.run_routing_loop", return_value=None):
            with caplog.at_level(logging.INFO):
                main(["--log-level", "INFO"])

        messages = " ".join(caplog.messages)
        assert "daemon_stopped" in messages

    def test_main_registers_sigterm_handler(self) -> None:
        """main() must register handle_sigterm as the SIGTERM signal handler."""
        with patch("keystone.daemon.run_routing_loop", return_value=None):
            with patch("keystone.daemon.signal.signal") as mock_signal:
                main(["--log-level", "INFO"])
        mock_signal.assert_called_once_with(signal.SIGTERM, handle_sigterm)

    def test_main_sigterm_path_via_handler(self) -> None:
        """Simulates the SIGTERM path: the routing loop catches SystemExit and returns,
        then main() completes with return code 0 — matching real SIGTERM behavior."""
        # In production: handle_sigterm raises SystemExit(0), which is caught inside
        # run_routing_loop's own try/except block, so run_routing_loop returns normally.
        # We simulate this by having the mock return normally (no raise).
        with patch("keystone.daemon.run_routing_loop", return_value=None):
            result = main(["--log-level", "INFO"])
        assert result == 0

    def test_main_accepts_debug_log_level(self) -> None:
        """main() must accept --log-level DEBUG without error."""
        with patch("keystone.daemon.run_routing_loop", return_value=None):
            result = main(["--log-level", "DEBUG"])
        assert result == 0

    def test_main_accepts_poll_interval_arg(self) -> None:
        """main() must forward --poll-interval to run_routing_loop."""
        with patch("keystone.daemon.run_routing_loop") as mock_loop:
            mock_loop.return_value = None
            main(["--poll-interval", "0.1"])
        mock_loop.assert_called_once_with(poll_interval=0.1)

    def test_main_daemon_stopped_logged_even_on_error(
        self, caplog: pytest.LogCaptureFixture
    ) -> None:
        """main() must emit daemon_stopped even when the routing loop raises an exception."""
        import logging

        with patch(
            "keystone.daemon.run_routing_loop",
            side_effect=RuntimeError("crash"),
        ):
            with caplog.at_level(logging.INFO):
                result = main(["--log-level", "INFO"])

        assert result == 1
        messages = " ".join(caplog.messages)
        assert "daemon_stopped" in messages
