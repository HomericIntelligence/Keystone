# Runbook: HMAS Pods Down

**Alert**: `HMASPodsDown`
**Severity**: Critical (P0)
**Response Time**: Immediate (5 minutes)

---

## Alert Details

**Trigger**: `up{job="hmas"} == 0`
**Duration**: 1 minute
**Impact**: Complete or partial service outage

---

## Symptoms

- Prometheus scrape target shows as DOWN
- HMAS metrics endpoint unreachable
- User-facing services may be unavailable
- Queue processing stopped

---

## Diagnosis

### 1. Confirm Pod Status

```bash
# Check all HMAS pods
kubectl get pods -n keystone -l app=hmas

# Look for:
# - Pending (not scheduled)
# - CrashLoopBackOff (restart failures)
# - Error (startup failure)
# - ImagePullBackOff (image not found)
```

**Expected Output** (healthy):

```
NAME                    READY   STATUS    RESTARTS   AGE
hmas-5d8f7c9b6-abc12    1/1     Running   0          10m
hmas-5d8f7c9b6-def34    1/1     Running   0          10m
```

### 2. Check Pod Events

```bash
# Get recent events for failed pods
kubectl describe pod <pod-name> -n keystone | tail -20

# Check namespace events
kubectl get events -n keystone --sort-by='.lastTimestamp' | tail -20
```

### 3. Check Logs

```bash
# View logs from crashed pod
kubectl logs <pod-name> -n keystone --tail=50

# View previous container logs if pod restarted
kubectl logs <pod-name> -n keystone --previous
```

---

## Impact Assessment

### Severity Determination

**SEV-0** (All pods down):

- Complete service outage
- All message processing stopped
- Page incident commander

**SEV-1** (Partial outage):

- 50%+ pods down
- Degraded performance
- Risk of cascade failure

**SEV-2** (Single pod down):

- Reduced capacity
- Other pods handling load
- Monitor closely

---

## Immediate Actions

### If All Pods Down (SEV-0)

1. **Check Cluster Health**:

   ```bash
   kubectl get nodes
   kubectl top nodes
   ```

2. **Check Resource Quotas**:

   ```bash
   kubectl describe resourcequota -n keystone
   ```

3. **Attempt Pod Recreation**:

   ```bash
   # Force recreate pods
   kubectl rollout restart deployment/hmas -n keystone

   # Watch rollout status
   kubectl rollout status deployment/hmas -n keystone
   ```

4. **Scale Down if Resources Exhausted**:

   ```bash
   # Temporarily reduce replicas
   kubectl scale deployment/hmas --replicas=1 -n keystone
   ```

### If Single Pod Down (SEV-2)

1. **Delete Failed Pod** (Kubernetes will recreate):

   ```bash
   kubectl delete pod <pod-name> -n keystone
   ```

2. **Monitor Recreation**:

   ```bash
   kubectl get pods -n keystone -l app=hmas -w
   ```

---

## Root Cause Investigation

### Common Causes

#### 1. Image Pull Failure

**Symptom**: `ImagePullBackOff` or `ErrImagePull`

**Diagnosis**:

```bash
kubectl describe pod <pod-name> -n keystone | grep -A 5 "Failed"
```

**Resolution**:

- Verify image exists: `docker pull keystone:latest`
- Check image pull secrets: `kubectl get secrets -n keystone`
- Fix image tag in deployment

#### 2. Resource Exhaustion

**Symptom**: `Pending` status with "Insufficient cpu" or "Insufficient memory" events

**Diagnosis**:

```bash
kubectl top nodes
kubectl describe nodes | grep -A 5 "Allocated resources"
```

**Resolution**:

- Scale down other workloads
- Add more nodes to cluster
- Reduce HMAS resource requests

#### 3. Crash on Startup

**Symptom**: `CrashLoopBackOff` with restart count > 0

**Diagnosis**:

```bash
kubectl logs <pod-name> -n keystone --previous
```

**Resolution**:

- Check for segmentation faults, exceptions, or panics
- Verify configuration: `kubectl get configmap hmas-config -o yaml`
- Check secrets: `kubectl get secrets -n keystone`
- Rollback if recent deployment: `kubectl rollout undo deployment/hmas`

#### 4. Health Check Failure

**Symptom**: Pod running but marked as not ready

**Diagnosis**:

```bash
kubectl describe pod <pod-name> -n keystone | grep -A 10 "Liveness\|Readiness"
```

**Resolution**:

- Manually test health endpoint:

  ```bash
  kubectl exec <pod-name> -n keystone -- curl -v localhost:8080/healthz
  ```

- Increase `initialDelaySeconds` if slow startup
- Check health check server implementation

#### 5. OOMKilled (Out of Memory)

**Symptom**: Pod restart with `OOMKilled` reason

**Diagnosis**:

```bash
kubectl describe pod <pod-name> -n keystone | grep -i oom
```

**Resolution**:

- Increase memory limits in deployment
- Investigate memory leak: `kubectl top pod <pod-name>`
- Check for memory-intensive operations

---

## Resolution Steps

### Step 1: Identify Root Cause

Follow diagnosis steps above to determine why pods are failing.

### Step 2: Apply Fix

Based on root cause:

- **Image issue**: Update deployment with correct image tag
- **Resources**: Increase limits or add nodes
- **Code bug**: Rollback deployment or apply hotfix
- **Config**: Fix ConfigMap or Secrets
- **Health checks**: Adjust probe parameters

### Step 3: Verify Recovery

```bash
# Check pod status
kubectl get pods -n keystone -l app=hmas

# Check metrics endpoint
kubectl port-forward -n keystone svc/hmas 9090:9090
curl http://localhost:9090/metrics

# Check Prometheus targets
# Access http://localhost:9090/targets (should show UP)
```

### Step 4: Monitor

```bash
# Watch metrics for 15 minutes
kubectl logs -f deployment/hmas -n keystone

# Check error rate
curl 'http://localhost:9090/api/v1/query?query=rate(hmas_deadline_misses_total[5m])'
```

---

## Rollback Procedure

If new deployment caused the issue:

```bash
# View rollout history
kubectl rollout history deployment/hmas -n keystone

# Rollback to previous version
kubectl rollout undo deployment/hmas -n keystone

# Rollback to specific revision
kubectl rollout undo deployment/hmas --to-revision=2 -n keystone

# Verify rollback
kubectl rollout status deployment/hmas -n keystone
```

---

## Prevention

### 1. Pre-Deployment Checks

- Test deployment in staging first
- Run smoke tests: `./tests/smoke_test.sh`
- Verify resource requests/limits
- Check image availability

### 2. Gradual Rollouts

```yaml
# Use rolling update strategy in deployment
spec:
  strategy:
    type: RollingUpdate
    rollingUpdate:
      maxSurge: 1
      maxUnavailable: 0  # Ensures zero downtime
```

### 3. Pod Disruption Budgets

```yaml
apiVersion: policy/v1
kind: PodDisruptionBudget
metadata:
  name: hmas-pdb
spec:
  minAvailable: 1
  selector:
    matchLabels:
      app: hmas
```

### 4. Resource Monitoring

- Set up alerts for resource exhaustion:
  - Node CPU >90%
  - Node memory >90%
  - Pod pending >1 minute

### 5. Health Check Tuning

```yaml
livenessProbe:
  httpGet:
    path: /healthz
    port: 8080
  initialDelaySeconds: 30  # Adjust for slow startup
  periodSeconds: 10
  failureThreshold: 3       # Allow 3 failures before restart
```

---

## Escalation

### Escalate Immediately If

- All pods down for >5 minutes
- Repeated crashes after rollback
- Cluster-wide resource exhaustion
- Unknown root cause after 15 minutes

### Contact

- **On-Call Engineer**: Check PagerDuty rotation
- **Senior Engineer**: `@sre-team` in Slack
- **Incident Commander**: For SEV-0 only

---

## Related Alerts

- `PodRestartLoop` - May precede this alert
- `HighCPUUsage` - May indicate resource pressure
- `HighMemoryUsage` - May indicate OOM coming

---

## Postmortem Checklist

After resolution:

- [ ] Document root cause
- [ ] Identify preventive measures
- [ ] Update this runbook if new learnings
- [ ] Create tickets for long-term fixes
- [ ] Communicate to stakeholders

---

**Last Updated**: 2025-11-21
**Tested**: 2025-11-21 (staging)
**Owner**: SRE Team
