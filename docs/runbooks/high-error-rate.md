# Runbook: High Error Rate

**Alert**: `HighErrorRate`
**Severity**: Critical (P0)
**Response Time**: Immediate (5 minutes)

---

## Alert Details

**Trigger**: `rate(hmas_deadline_misses_total[5m]) > 10`
**Duration**: 5 minutes
**Impact**: Degraded service quality, potential data loss

---

## Symptoms

- Deadline miss rate exceeding 10 messages/second
- Messages not processed within SLA
- Queue backlog building up
- Worker threads saturated

---

## Diagnosis

### 1. Check Current Error Rate

```bash
# Port-forward Prometheus
kubectl port-forward -n projectkeystone svc/prometheus 9090:9090

# Query current deadline miss rate
curl 'http://localhost:9090/api/v1/query?query=rate(hmas_deadline_misses_total[5m])'

# Check per-agent breakdown
curl 'http://localhost:9090/api/v1/query?query=rate(hmas_deadline_misses_total[5m])' \
  | jq '.data.result[] | {agent: .metric.agent_id, rate: .value[1]}'
```

### 2. Check System Health

```bash
# Pod status and resource usage
kubectl top pods -n projectkeystone

# Worker utilization
curl 'http://localhost:9090/api/v1/query?query=hmas_worker_utilization_percent'

# Queue depth
curl 'http://localhost:9090/api/v1/query?query=hmas_queue_depth_max'

# Message latency
curl 'http://localhost:9090/api/v1/query?query=hmas_message_latency_microseconds'
```

### 3. Check for Patterns

```bash
# View recent logs
kubectl logs -n projectkeystone -l app=hmas --tail=200 | grep -i "deadline\|timeout\|error"

# Check for specific error patterns
kubectl logs -n projectkeystone -l app=hmas --tail=500 | grep "Deadline miss"
```

---

## Impact Assessment

### Severity Determination

**SEV-0** (Error rate >50/sec):

- Massive deadline misses
- System overloaded
- Potential data loss
- Page incident commander

**SEV-1** (Error rate 10-50/sec):

- Significant degradation
- SLA at risk
- Immediate action needed

**SEV-2** (Error rate <10/sec):

- Minor degradation
- Monitor closely
- Investigate during business hours

---

## Immediate Actions

### 1. Assess Load

```bash
# Check incoming message rate
curl 'http://localhost:9090/api/v1/query?query=rate(hmas_messages_received_total[5m])'

# Compare to normal baseline (should be <100 msg/sec normally)
```

**If Load Spike** (>200 msg/sec):

- This is a capacity issue
- Scale up immediately (see Step 2)

**If Normal Load** (<100 msg/sec):

- This is a performance issue
- Investigate resource contention (see Step 3)

### 2. Scale Up Capacity

```bash
# Increase HMAS replicas
kubectl scale deployment/hmas --replicas=5 -n projectkeystone

# Watch rollout
kubectl get pods -n projectkeystone -l app=hmas -w

# Verify scaling helped (wait 2-3 minutes)
curl 'http://localhost:9090/api/v1/query?query=rate(hmas_deadline_misses_total[5m])'
```

### 3. Check Resource Contention

```bash
# CPU throttling
kubectl top pods -n projectkeystone
# If CPU at limit, increase CPU resources

# Memory pressure
kubectl top nodes
# If memory high, check for memory leaks

# Disk I/O (if using persistent storage)
kubectl exec -n projectkeystone <pod-name> -- iostat -x 1 5
```

---

## Root Cause Investigation

### Common Causes

#### 1. Traffic Spike

**Symptom**: Sudden increase in message rate

**Diagnosis**:

```bash
# Check message rate trend (last hour)
curl 'http://localhost:9090/api/v1/query_range?query=rate(hmas_messages_received_total[5m])&start=-1h&step=60s' | jq
```

**Resolution**:

- Scale horizontally: `kubectl scale deployment/hmas --replicas=10`
- Enable HPA (Horizontal Pod Autoscaler):

  ```yaml
  apiVersion: autoscaling/v2
  kind: HorizontalPodAutoscaler
  metadata:
    name: hmas-hpa
  spec:
    scaleTargetRef:
      apiVersion: apps/v1
      kind: Deployment
      name: hmas
    minReplicas: 2
    maxReplicas: 10
    metrics:
      - type: Resource
        resource:
          name: cpu
          target:
            type: Utilization
            averageUtilization: 70
  ```

#### 2. Resource Exhaustion

**Symptom**: High CPU/memory usage

**Diagnosis**:

```bash
kubectl top pods -n projectkeystone

# Check CPU throttling
kubectl describe pod <pod-name> -n projectkeystone | grep -A 10 "Limits\|Requests"
```

**Resolution**:

- Increase resource limits:

  ```bash
  kubectl patch deployment hmas -n projectkeystone -p '
  {
    "spec": {
      "template": {
        "spec": {
          "containers": [{
            "name": "hmas",
            "resources": {
              "limits": {"cpu": "4000m", "memory": "4Gi"},
              "requests": {"cpu": "2000m", "memory": "2Gi"}
            }
          }]
        }
      }
    }
  }'
  ```

#### 3. Slow Downstream Service

**Symptom**: High latency, normal CPU/memory

**Diagnosis**:

```bash
# Check message latency
curl 'http://localhost:9090/api/v1/query?query=hmas_message_latency_microseconds'

# Check logs for slow operations
kubectl logs -n projectkeystone -l app=hmas --tail=100 | grep -i "slow\|timeout"
```

**Resolution**:

- Identify slow dependency (database, API, etc.)
- Add timeout or circuit breaker
- Scale downstream service

#### 4. Queue Backlog

**Symptom**: High queue depth, normal error rate

**Diagnosis**:

```bash
# Check queue depth per priority
curl 'http://localhost:9090/api/v1/query?query=hmas_queue_depth_max'

# Check for starvation
curl 'http://localhost:9090/api/v1/query?query=rate(hmas_low_priority_processed_total[5m])'
```

**Resolution**:

- Drain queue by scaling up
- Adjust priority fairness interval:

  ```cpp
  agent->setLowPriorityCheckInterval(std::chrono::milliseconds{50});  // More frequent low-priority checks
  ```

#### 5. Worker Thread Saturation

**Symptom**: Worker utilization >95%

**Diagnosis**:

```bash
curl 'http://localhost:9090/api/v1/query?query=hmas_worker_utilization_percent'
```

**Resolution**:

- Increase worker count via ConfigMap:

  ```bash
  kubectl edit configmap hmas-config -n projectkeystone
  # Change WORKER_COUNT from 4 to 8
  kubectl rollout restart deployment/hmas -n projectkeystone
  ```

---

## Resolution Steps

### Step 1: Immediate Mitigation

```bash
# Scale up to handle load
kubectl scale deployment/hmas --replicas=5 -n projectkeystone

# Monitor for improvement (2-3 minutes)
watch 'curl -s "http://localhost:9090/api/v1/query?query=rate(hmas_deadline_misses_total[5m])" | jq ".data.result[0].value[1]"'
```

### Step 2: Identify Bottleneck

Use diagnosis steps to determine if bottleneck is:

- CPU (top shows high CPU%)
- Memory (OOM events, high memory%)
- I/O (slow disk operations)
- Network (high latency to dependencies)
- Code (inefficient algorithms)

### Step 3: Apply Fix

Based on bottleneck:

- **CPU**: Increase CPU limits or optimize code
- **Memory**: Increase memory limits or fix memory leaks
- **I/O**: Use faster storage or cache more
- **Network**: Optimize network calls or add circuit breakers
- **Code**: Profile and optimize hot paths

### Step 4: Verify Resolution

```bash
# Error rate should drop below 10/sec
curl 'http://localhost:9090/api/v1/query?query=rate(hmas_deadline_misses_total[5m])'

# Worker utilization should drop below 80%
curl 'http://localhost:9090/api/v1/query?query=hmas_worker_utilization_percent'

# Queue depth should stabilize
curl 'http://localhost:9090/api/v1/query?query=hmas_queue_depth_max'
```

---

## Performance Tuning

### 1. Optimize Worker Configuration

```yaml
# ConfigMap: hmas-config
data:
  WORKER_COUNT: "8"  # Increase from 4
  LOW_PRIORITY_CHECK_INTERVAL_MS: "100"  # Tune fairness
```

### 2. Tune Resource Limits

```yaml
resources:
  requests:
    cpu: "2000m"      # 2 cores
    memory: "2Gi"
  limits:
    cpu: "4000m"      # 4 cores max
    memory: "4Gi"
```

### 3. Enable Autoscaling

```bash
kubectl autoscale deployment hmas \
  --cpu-percent=70 \
  --min=2 \
  --max=10 \
  -n projectkeystone
```

---

## Prevention

### 1. Capacity Planning

- Run load tests regularly (see `docs/LOAD_TESTING.md`)
- Determine max sustainable throughput
- Set HPA target at 70% of max capacity

### 2. Monitoring

- Alert on trending toward limits:
  - Worker utilization >80% for 15min (warning)
  - Queue depth >500 for 10min (warning)
- Monitor P95 latency, not just average

### 3. Circuit Breakers

Implement circuit breakers for downstream dependencies:

```cpp
// Pseudo-code
if (downstream_error_rate > 0.5) {
  circuit_breaker.open();
  return_cached_response();
}
```

### 4. Graceful Degradation

When overloaded:

- Drop low-priority messages
- Return cached responses
- Shed non-critical features

---

## Escalation

### Escalate If

- Error rate not improving after 15 minutes
- Multiple mitigation attempts failed
- Root cause unclear
- Requires code changes

### Contact

- **On-Call Engineer**: Check PagerDuty rotation
- **Performance Team**: `@perf-team` in Slack
- **Incident Commander**: For SEV-0

---

## Related Alerts

- `CriticalWorkerUtilization` - Often co-occurs
- `HighQueueDepth` - May precede this alert
- `HighMessageLatency` - Related performance issue

---

## Metrics to Monitor

```promql
# Error rate (should be <10/sec)
rate(hmas_deadline_misses_total[5m])

# Worker utilization (should be <80%)
hmas_worker_utilization_percent

# Queue depth (should be <1000)
hmas_queue_depth_max

# Message latency (should be <5000µs)
hmas_message_latency_microseconds

# Throughput
rate(hmas_messages_processed_total[5m])
```

---

**Last Updated**: 2025-11-21
**Tested**: 2025-11-21 (staging)
**Owner**: SRE Team
