# Operational Runbooks

**Keystone HMAS - Production Incident Response Procedures**

## Overview

This directory contains operational runbooks for responding to alerts and incidents in Keystone HMAS production deployments.

**Phase 6.8 N1**: Comprehensive runbooks for production operations

---

## What is a Runbook?

A runbook is a step-by-step guide for responding to a specific alert or incident. It provides:

- **Diagnosis**: How to confirm and understand the issue
- **Impact Assessment**: What's affected and severity
- **Immediate Actions**: Steps to mitigate or resolve
- **Root Cause Analysis**: How to identify underlying causes
- **Prevention**: How to avoid recurrence

---

## Runbook Index

### Critical Alerts (P0)

- [HMASPodsDown](hmas-pods-down.md) - HMAS pod down for >1 minute
- [HighErrorRate](high-error-rate.md) - Deadline miss rate >10/sec
- [CriticalWorkerUtilization](high-worker-utilization.md) - Worker utilization >95%
- [PodOOMKilled](pod-oomkilled.md) - Pod killed due to out-of-memory
- [SLOAvailabilityViolation](slo-availability-violation.md) - Availability <99.9%
- [PrometheusDown](prometheus-down.md) - Monitoring stack unavailable

### Warning Alerts (P1)

- [HighMessageLatency](high-latency.md) - Message latency >5ms
- [HighQueueDepth](high-queue-depth.md) - Queue depth >1000 messages
- [HighWorkerUtilization](high-worker-utilization.md) - Worker utilization >80%
- [PodRestartLoop](pod-restart-loop.md) - Frequent pod restarts
- [HighCPUUsage](high-cpu-usage.md) - CPU usage >90%
- [HighMemoryUsage](high-memory-usage.md) - Memory usage >90%

### Infrastructure Alerts (P1)

- [PVCAlmostFull](pvc-almost-full.md) - Persistent volume >90% full
- [LokiDown](loki-down.md) - Log collection unavailable
- [GrafanaDown](grafana-down.md) - Dashboard unavailable

---

## Using Runbooks

### 1. Alert Notification

When you receive an alert (Slack, PagerDuty, email):

```
Alert: HighErrorRate
Severity: critical
Instance: hmas-5d8f7c9b6-xk2lm
Description: Deadline miss rate is 12.5/sec over the last 5 minutes.
Runbook: https://github.com/keystone/runbooks/high-error-rate.md
```

### 2. Open Runbook

Click the runbook URL or navigate to `docs/runbooks/<alert-name>.md`

### 3. Follow Steps

Execute each step in order:

1. **Diagnose** - Confirm the issue
2. **Assess Impact** - Understand severity
3. **Mitigate** - Take immediate action
4. **Investigate** - Find root cause
5. **Resolve** - Fix the issue
6. **Prevent** - Avoid recurrence

### 4. Document

Record actions taken in incident ticket or postmortem document.

---

## Quick Reference Commands

### Check Pod Status

```bash
# List all pods
kubectl get pods -n keystone

# Describe specific pod
kubectl describe pod <pod-name> -n keystone

# View pod logs
kubectl logs <pod-name> -n keystone --tail=100

# Follow logs
kubectl logs -f <pod-name> -n keystone
```

### Check Metrics

```bash
# Port-forward Prometheus
kubectl port-forward -n keystone svc/prometheus 9090:9090

# Access Prometheus UI
open http://localhost:9090

# Query specific metric
curl 'http://localhost:9090/api/v1/query?query=hmas_worker_utilization_percent'
```

### Check Resource Usage

```bash
# Pod resource usage
kubectl top pods -n keystone

# Node resource usage
kubectl top nodes

# Check resource quotas
kubectl describe resourcequota -n keystone
```

### Restart Services

```bash
# Restart HMAS deployment
kubectl rollout restart deployment/hmas -n keystone

# Restart Prometheus
kubectl rollout restart deployment/prometheus -n keystone

# Scale deployment
kubectl scale deployment/hmas --replicas=3 -n keystone
```

### Access Logs

```bash
# HMAS logs (all replicas)
kubectl logs -n keystone -l app=hmas --tail=100

# Prometheus logs
kubectl logs -n keystone -l app=prometheus --tail=50

# Alertmanager logs
kubectl logs -n keystone -l app=alertmanager --tail=50
```

### Silence Alerts

```bash
# Port-forward Alertmanager
kubectl port-forward -n keystone svc/alertmanager 9093:9093

# Access Alertmanager UI
open http://localhost:9093

# Create silence via UI or API
curl -X POST http://localhost:9093/api/v2/silences \
  -H 'Content-Type: application/json' \
  -d '{
    "matchers": [{"name": "alertname", "value": "HighErrorRate", "isRegex": false}],
    "startsAt": "2025-11-21T10:00:00Z",
    "endsAt": "2025-11-21T12:00:00Z",
    "comment": "Planned maintenance",
    "createdBy": "ops-team"
  }'
```

---

## Escalation Path

### Level 1: On-Call Engineer (15 min response)

- Check runbook
- Execute diagnosis steps
- Attempt immediate mitigation
- Document actions

**Escalate if**:

- Issue not resolved in 30 minutes
- Impact is wider than expected
- Root cause unclear

### Level 2: Senior Engineer (30 min response)

- Review L1 findings
- Deep dive into logs and metrics
- Coordinate with development team
- Make architectural decisions

**Escalate if**:

- System-wide outage
- Data loss risk
- Requires code changes

### Level 3: Engineering Manager + Architect (1 hour response)

- Incident command
- Cross-team coordination
- Customer communication
- Post-incident review

---

## Incident Severity Levels

### SEV-0: Critical Outage

- **Impact**: Complete service outage
- **Example**: All HMAS pods down
- **Response**: Immediate, all hands on deck
- **Notification**: Page on-call + manager

### SEV-1: Major Degradation

- **Impact**: Significant feature loss or performance degradation
- **Example**: 50% pod failure, extreme latency
- **Response**: Within 15 minutes
- **Notification**: Page on-call

### SEV-2: Minor Degradation

- **Impact**: Reduced capacity or intermittent issues
- **Example**: High worker utilization, queue backlog
- **Response**: Within 1 hour
- **Notification**: Alert on-call (no page)

### SEV-3: Warning

- **Impact**: Potential future issue
- **Example**: Trending toward resource limits
- **Response**: Within business hours
- **Notification**: Ticket created

---

## Postmortem Template

After resolving a SEV-0 or SEV-1 incident, create a postmortem:

```markdown
# Incident Postmortem: [TITLE]

**Date**: YYYY-MM-DD
**Duration**: X hours Y minutes
**Severity**: SEV-X
**Responders**: [Names]

## Summary

[Brief description of incident]

## Timeline

- HH:MM - Alert triggered
- HH:MM - Engineer acknowledged
- HH:MM - Root cause identified
- HH:MM - Fix deployed
- HH:MM - Service restored

## Root Cause

[Detailed explanation]

## Impact

- Users affected: X
- Requests failed: Y
- Revenue impact: $Z

## Resolution

[What fixed the issue]

## Action Items

1. [ ] Fix root cause (owner, ETA)
2. [ ] Add monitoring (owner, ETA)
3. [ ] Update runbook (owner, ETA)

## Lessons Learned

[What we learned and how to prevent recurrence]
```

---

## Runbook Maintenance

### When to Update

- After each incident (add new findings)
- When alert thresholds change
- When architecture changes
- Quarterly review

### How to Update

1. Create PR with changes
2. Review with team
3. Test procedures in staging
4. Merge and communicate

### Quality Checklist

- [ ] Steps are clear and actionable
- [ ] Commands are copy-paste ready
- [ ] Expected outputs documented
- [ ] Rollback procedures included
- [ ] Tested in staging environment

---

## Additional Resources

- [Prometheus Alerts](../k8s/prometheus-alerts.yaml) - Alert rule definitions
- [Metrics Guide](../METRICS_SECURITY.md) - Metrics access and security
- [Kubernetes Deployment](../KUBERNETES_DEPLOYMENT.md) - Deployment procedures
- [Load Testing](../LOAD_TESTING.md) - Performance testing

---

## Contributing

To add a new runbook:

1. Copy `template.md` to `<alert-name>.md`
2. Fill in all sections
3. Test procedures in staging
4. Submit PR for review
5. Update this README index

---

**Last Updated**: 2025-11-21
**Maintained By**: SRE Team
**Contact**: <sre@keystone.io>
