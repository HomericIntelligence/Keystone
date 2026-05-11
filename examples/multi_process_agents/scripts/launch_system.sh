#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

# Clean up any previous runs
rm -f /tmp/keystone_queues.sock
for pattern in process1_chief process2_component_lead process3_module_lead process4_task_agent; do
    # pkill exits 1 when no processes match — that is the expected case for a clean run.
    # Treat exit codes >=2 as real errors (permission denied, syntax error, etc.).
    rc=0
    pkill -f "$pattern" || rc=$?
    if [[ $rc -ge 2 ]]; then
        echo "warn: pkill -f $pattern failed with rc=$rc" >&2
    fi
done

sleep 1

echo "=== Starting Multi-Process Agent System ==="
echo ""

# Create logs directory
mkdir -p "$SCRIPT_DIR/../logs"

# Start background processes
echo "Starting ComponentLead (Process 2)..."
"$BUILD_DIR/process2_component_lead" > "$SCRIPT_DIR/../logs/component_lead.log" 2>&1 &
COMP_PID=$!
echo "  PID: $COMP_PID"

echo "Starting ModuleLead (Process 3)..."
"$BUILD_DIR/process3_module_lead" > "$SCRIPT_DIR/../logs/module_lead.log" 2>&1 &
MOD_PID=$!
echo "  PID: $MOD_PID"

echo "Starting TaskAgent (Process 4)..."
"$BUILD_DIR/process4_task_agent" > "$SCRIPT_DIR/../logs/task_agent.log" 2>&1 &
TASK_PID=$!
echo "  PID: $TASK_PID"

echo ""
echo "Starting ChiefArchitect (Process 1)..."
# ChiefArchitect runs in foreground
"$BUILD_DIR/process1_chief" "$1"

echo ""
echo "=== System Complete ==="
echo ""
echo "Process logs available in:"
echo "  - logs/component_lead.log"
echo "  - logs/module_lead.log"
echo "  - logs/task_agent.log"

# Cleanup background processes
echo ""
echo "Cleaning up background processes..."
for pid in "$COMP_PID" "$MOD_PID" "$TASK_PID"; do
    if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
        if ! kill "$pid" 2>/dev/null; then
            echo "warn: kill failed for $pid" >&2
        fi
    fi
done
for pid in "$COMP_PID" "$MOD_PID" "$TASK_PID"; do
    # wait returns 127 if pid is unknown to shell, 128+N if signalled, 0 on clean
    # exit; we don't care which — only that the process is reaped before we exit.
    rc=0
    wait "$pid" 2>/dev/null || rc=$?
    if [[ $rc -gt 128 ]]; then
        : # killed by signal as expected
    fi
done
echo "Done"
