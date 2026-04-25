"""Shared pytest fixtures and configuration for ProjectKeystone tests.

asyncio_mode = "auto" is configured in pyproject.toml [tool.pytest.ini_options],
so @pytest.mark.asyncio decorators are not required on individual async tests.
"""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock

import pytest

from tests.helpers import make_agent, make_task

__all__ = ["make_agent", "make_task"]


@pytest.fixture
def mock_http_client() -> MagicMock:
    """Return a mock HTTP client with an async ``get`` method."""
    http = MagicMock()
    http.get = AsyncMock()
    return http
