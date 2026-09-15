"""Run the declared, read-only release validation steps in the local CI image."""

import subprocess
from pathlib import Path

import yaml


def main() -> int:
    """Reuse the hosted release checks, refusing unsupported hosted context."""
    workflow = yaml.safe_load(Path(".github/workflows/_required.yml").read_text())
    job = workflow["jobs"]["release"]
    if any(key in workflow for key in ("env", "defaults")) or any(
        key in job
        for key in ("if", "env", "defaults", "container", "services", "needs")
    ):
        raise ValueError("Release validation requires unsupported hosted job context")
    steps = job["steps"]
    commands: list[str] = []
    for step in steps:
        if any(
            key in step
            for key in ("if", "env", "working-directory", "continue-on-error")
        ):
            raise ValueError("Release validation requires unsupported hosted context")
        if "uses" in step:
            action = step["uses"].split("@", 1)[0]
            if action not in {"actions/checkout", "actions/setup-python"}:
                raise ValueError(
                    f"Release action needs an explicit local equivalent: {action}"
                )
            continue
        command = step.get("run")
        if not isinstance(command, str) or not command.strip() or "${{" in command:
            raise ValueError("Release validation requires a literal shell command")
        if step.get("shell", "bash") != "bash":
            raise ValueError("Release validation requires an unsupported shell")
        commands.append(command)
    if not commands:
        raise ValueError("No release validation commands were selected")
    for index, command in enumerate(commands, 1):
        print(f"Release validation step {index}/{len(commands)}", flush=True)
        result = subprocess.run(
            ["bash", "-euo", "pipefail", "-c", command], check=False
        )
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
