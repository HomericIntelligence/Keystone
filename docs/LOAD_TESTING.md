# Load Testing Strategy - Keystone HMAS

## Overview

This document outlines the load testing strategy for Keystone's Hierarchical Multi-Agent System (HMAS) to ensure
production readiness at Phase 6.7 milestone M1 (P1).

## Objectives

1. **Determine resource requirements** for production deployment
2. **Validate system stability** under sustained load
3. **Identify bottlenecks** in the 4-layer hierarchy (L0→L1→L2→L3)
4. **Measure throughput** and latency under various load profiles
5. **Verify metrics collection** accuracy under load
6. **Establish baseline performance** for regression testing

## Load Test Scenarios

### Scenario 1: Sustained Load (Baseline Capacity)

**Goal**: Determine maximum sustainable throughput

**Configuration**:

- Duration: 10 minutes
- Agent topology: 1 Chief → 2 ComponentLeads → 4 ModuleLeads → 16 TaskAgents (23 agents total)
- Message rate: Start at 100 msg/s, increase by 50 msg/s every 60s
- Message priority distribution: 20% HIGH, 70% NORMAL, 10% LOW
- Task complexity: Simple echo commands (latency: 1-5ms)

**Success Criteria**:

- Average message latency < 100ms at max sustainable rate
- No message drops
- Queue depth < 1000 messages per agent
- CPU utilization < 80%
- Memory stable (no leaks)

**Metrics to Collect**:

- Messages per second (total and per priority)
- End-to-end latency (Chief → TaskAgent → Chief)
- Queue depth per agent
- CPU and memory usage
- Deadline miss rate

### Scenario 2: Burst Load (Peak Capacity)

**Goal**: Validate handling of traffic spikes

**Configuration**:

- Duration: 5 minutes
- Agent topology: Same as Scenario 1
- Message pattern: Alternating bursts
  - 30s burst: 500 msg/s
  - 30s idle: 10 msg/s
- 5 burst cycles total

**Success Criteria**:

- Average latency < 200ms during bursts
- Recovery time < 10s after burst
- No crashes or deadlocks
- Backpressure mechanism activates correctly
- Queue depth returns to baseline after burst

### Scenario 3: Large Hierarchy (Scalability)

**Goal**: Test scalability with maximum agent count

**Configuration**:

- Duration: 5 minutes
- Agent topology: 1 Chief → 4 ComponentLeads → 12 ModuleLeads → 48 TaskAgents (65 agents total)
- Message rate: Constant 200 msg/s
- Message priority: 100% NORMAL

**Success Criteria**:

- System remains stable with 65 agents
- Average latency < 500ms (accounts for longer delegation chains)
- Memory usage < 2GB total
- No agent starvation (all agents process at least 1 message)

### Scenario 4: Priority Fairness (Quality of Service)

**Goal**: Verify priority-based scheduling works under load

**Configuration**:

- Duration: 3 minutes
- Agent topology: Same as Scenario 1
- Message rate: Constant 300 msg/s
- Message priority distribution: 50% HIGH, 40% NORMAL, 10% LOW

**Success Criteria**:

- HIGH priority latency < 50ms (p99)
- NORMAL priority latency < 150ms (p99)
- LOW priority latency < 500ms (p99)
- No LOW priority starvation (time-based fairness working)
- HIGH messages processed first when queues have mixed priorities

### Scenario 5: Chaos + Load (Resilience)

**Goal**: Validate graceful degradation under failures

**Configuration**:

- Duration: 5 minutes
- Agent topology: Same as Scenario 1
- Message rate: Constant 200 msg/s
- Failure injection: Kill 1 TaskAgent every 30s, respawn after 10s

**Success Criteria**:

- Circuit breakers activate for failed agents
- Messages rerouted to healthy agents
- Overall throughput degradation < 30% during failures
- Full recovery within 15s of agent restart
- No cascading failures

## Resource Sizing Recommendations

Based on load test results, document:

1. **Minimum requirements** (conservative):
   - CPU cores
   - RAM
   - Network bandwidth

2. **Recommended requirements** (production):
   - CPU cores (with 50% headroom)
   - RAM (with 50% headroom)
   - Network bandwidth

3. **Scaling guidelines**:
   - Horizontal scaling: Agents per CPU core
   - Vertical scaling: Maximum messages per second per core
   - Kubernetes HPA thresholds

## Load Test Implementation

### Tools

- **Load Generator**: Custom C++20 harness using existing `ChiefArchitectAgent`
- **Metrics Collection**: Prometheus + existing `Metrics` class
- **Visualization**: Grafana dashboards
- **Orchestration**: Docker Compose for multi-agent deployment

### Load Test Harness

Location: `tests/load/hmas_load_test.cpp`

Key components:

1. `LoadTestHarness` class - manages agent lifecycle
2. `MessageGenerator` - produces messages at specified rate
3. `MetricsCollector` - samples metrics during test
4. `ResultsAnalyzer` - analyzes and reports results

### Metrics Collection

**Real-time metrics** (via Prometheus):

- `hmas_messages_sent_total{priority}`
- `hmas_messages_processed_total`
- `hmas_message_latency_seconds{quantile}`
- `hmas_queue_depth{agent_id}`
- `hmas_agent_cpu_seconds_total{agent_id}`
- `hmas_deadline_misses_total`

**Post-test analysis**:

- Latency percentiles (p50, p90, p95, p99)
- Throughput over time
- Memory usage trend
- Error rate

## Running Load Tests

### Prerequisites

```bash
# Build release mode for accurate performance measurement
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -G Ninja ..
ninja hmas_load_test

# Start Prometheus and Grafana
docker-compose -f docker-compose-monitoring.yaml up -d
```

### Execute Load Tests

```bash
# Run all scenarios
./tests/load/run_all_scenarios.sh

# Run specific scenario
./build/hmas_load_test --scenario=sustained_load --duration=600

# Run with custom parameters
./build/hmas_load_test \
  --scenario=burst \
  --duration=300 \
  --message-rate=500 \
  --agents=23 \
  --output=results/burst_test_$(date +%Y%m%d_%H%M%S).json
```

### Analyze Results

```bash
# View real-time metrics
open http://localhost:3000/d/hmas-load-test

# Generate performance report
./scripts/analyze_load_test.py results/sustained_load_*.json

# Compare against baseline
./scripts/compare_load_tests.py \
  --baseline=results/baseline.json \
  --current=results/latest.json \
  --threshold=10  # 10% regression threshold
```

## Acceptance Criteria (Phase 6.7 M1)

Load testing is considered complete when:

- ✅ All 5 scenarios pass success criteria
- ✅ Resource sizing recommendations documented
- ✅ Baseline performance metrics established
- ✅ Load test results published to `benchmarks/results/`
- ✅ Grafana dashboards configured for monitoring
- ✅ No memory leaks detected (valgrind clean under load)
- ✅ No data races detected (ThreadSanitizer clean under load)

## Integration with CI/CD

**Nightly regression testing**:

```yaml
# .github/workflows/nightly-load-test.yml
name: Nightly Load Test
on:
  schedule:
    - cron: '0 2 * * *'  # 2 AM UTC daily
  workflow_dispatch:

jobs:
  load-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Run sustained load test
        run: |
          ./tests/load/run_scenario.sh sustained_load
      - name: Compare against baseline
        run: |
          ./scripts/compare_load_tests.py \
            --baseline=benchmarks/baseline/sustained_load.json \
            --current=results/sustained_load_latest.json \
            --threshold=10
      - name: Upload results
        uses: actions/upload-artifact@v3
        with:
          name: load-test-results
          path: results/*.json
```

## Next Steps After Load Testing

1. **Update resource limits** in Kubernetes manifests based on sizing recommendations
2. **Configure Horizontal Pod Autoscaler (HPA)** based on throughput metrics
3. **Set up Alertmanager** (M3) with thresholds from load tests
4. **Document operational runbooks** (Phase 6.8 N1) with load patterns

## References

- [Metrics Documentation](./METRICS.md)
- [Kubernetes Deployment](./KUBERNETES_DEPLOYMENT.md)
- [Health Checks](./HEALTH_CHECKS.md)
- [Benchmark Script](../scripts/run_benchmarks.sh)

---

**Last Updated**: 2025-11-22
**Status**: In Progress (Phase 6.7 M1)
**Owner**: Load Testing Team
