# Multi-Process Cline Agent Execution System

Demonstration of 4 separate processes executing Cline agents using one-shot non-interactive mode, coordinating through
lock-free queues to perform hierarchical code review.

## Architecture

```
Process 1: ChiefArchitect (L0)
    ↓ decomposes PR into domains
Process 2: ComponentLead (L1)
    ↓ decomposes domains into modules
Process 3: ModuleLead (L2)
    ↓ selects agents & creates tasks
Process 4: TaskAgent (L3)
    ↓ executes specialized agents
```

## Quick Start

```bash
cd examples/multi_process_agents
mkdir build && cd build
cmake ..
make -j$(nproc)
cd ..
./scripts/run_demo.sh
```

## Key Features

- **Request ID Tracking**: Unique ID propagates through all levels for tracing
- **Agent Selection**: ModuleLead automatically selects appropriate agents based on file patterns
- **Lock-Free Queues**: Uses moodycamel::ConcurrentQueue for high-performance IPC
- **Work Stealing**: Each process steals work from queues dynamically

## Agent Selection Logic

ModuleLead selects agents based on file patterns:

- **All files**: `code-review-specialist`
- **Auth/crypto files**: `security-specialist`
- **Test files**: `test-engineer`
- **Documentation**: `documentation-engineer`

## Files

- `include/data_structures.hpp` - Task/Result structures with request_id
- `include/queue_manager.hpp` - IPC queue management
- `src/process1_chief.cpp` - Generates request_id, orchestrates workflow
- `src/process2_component_lead.cpp` - Domain coordination
- `src/process3_module_lead.cpp` - Module coordination + agent selection
- `src/process4_task_agent.cpp` - Task execution (simulated Cline calls)
- `scripts/launch_system.sh` - Starts all 4 processes
- `scripts/run_demo.sh` - End-to-end demo with sample PR
- `scripts/sample_pr.json` - Sample PR for demo

## How It Works

1. **ChiefArchitect** reads PR JSON and generates unique request_id
2. **ChiefArchitect** decomposes PR into domain tasks, pushes to DomainQueue
3. **ComponentLead** steals domains, creates module tasks, pushes to ModuleQueue
4. **ModuleLead** steals modules, determines review types, creates task items, pushes to TaskQueue
5. **TaskAgent** steals tasks, executes simulated Cline agents, pushes results
6. Results aggregate back up through hierarchy

## Customization

### Add New Agent Type

1. Update `determineReviewTypes()` in `process3_module_lead.cpp`
2. Add agent configuration to `.claude/agents/`
3. Rebuild

### Run with Real PR

```bash
# Create PR JSON file
cat > my_pr.json <<EOF
{
  "pr_id": "456",
  "title": "My PR",
  "files": ["src/foo.cpp", "src/bar.cpp"],
  "domains": [
    {
      "name": "my_domain",
      "files": ["src/foo.cpp", "src/bar.cpp"],
      "modules": ["foo", "bar"]
    }
  ]
}
EOF

# Run
./scripts/launch_system.sh my_pr.json
```

## Logs

Each process logs to `logs/<process>.log`:

- `logs/component_lead.log`
- `logs/module_lead.log`
- `logs/task_agent.log`

ChiefArchitect logs to stdout.

## Requirements

- C++20 compiler (GCC 13+ or Clang 17+)
- CMake 3.20+
- nlohmann/json library
- Unix-like OS (Linux/macOS)

## Implementation Status

✅ Core infrastructure complete
✅ All 4 processes implemented
✅ Queue management with request_id tracking
✅ Agent selection based on file patterns
✅ Demo scripts and sample data
⚠️ Cline execution currently simulated (not calling real `claude --print`)

## Next Steps

- Integrate real `claude --print` calls in process4_task_agent.cpp
- Add structured logging (spdlog)
- Add state storage for debugging
- Add debug utilities (trace_request.sh, filter_logs.sh)

## Reference

See `/home/mvillmow/.claude/plans/quizzical-baking-newt.md` for complete design document.
