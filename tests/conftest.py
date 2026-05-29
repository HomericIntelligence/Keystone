"""Shared pytest fixtures and configuration for ProjectKeystone tests.

asyncio_mode = "auto" is configured in pyproject.toml [tool.pytest.ini_options],
so @pytest.mark.asyncio decorators are not required on individual async tests.

Note: The keystone Python orchestration modules (models, daemon, dag_walker, etc.)
were extracted to ProjectAgamemnon per ADR-015/016. Fixtures that depended on those
modules have been removed along with the src/keystone/ package.
"""

from __future__ import annotations
