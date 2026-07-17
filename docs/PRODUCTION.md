# Keystone HMAS - Production Deployment Guide

**Version**: 1.0
**Last Updated**: 2025-11-19
**Status**: Production Ready ✅

This document provides comprehensive production deployment procedures, operational
runbooks, and incident response playbooks for Keystone HMAS.

---

## Table of Contents

1. [Production Readiness Checklist](#production-readiness-checklist)
2. [Deployment Procedures](#deployment-procedures)
3. [Rollback Procedures](#rollback-procedures)
4. [Security Hardening](#security-hardening)
5. [Monitoring and Alerting](#monitoring-and-alerting)
6. [Incident Response](#incident-response)
7. [SLO/SLA Definitions](#slosla-definitions)
8. [Performance Testing](#performance-testing)
9. [Disaster Recovery](#disaster-recovery)
10. [Troubleshooting](#troubleshooting)

---

## Production Readiness Checklist

Use this checklist before deploying to production:

### Infrastructure

- [ ] **Kubernetes cluster** is running and accessible (kubectl context set)
- [ ] **Namespace** `keystone` created
- [ ] **Resource quotas** configured (CPU: 8-16, Memory: 16-32GB)
- [ ] **StorageClass** configured for PersistentVolumes
- [ ] **Load balancer** or Ingress controller configured (if exposing externally)
- [ ] **DNS** records configured (if using custom domains)

### Security

- [ ] **Secrets** created and properly configured (no default passwords)
- [ ] **RBAC policies** applied (least privilege access)
- [ ] **Network policies** applied (zero-trust networking)
- [ ] **TLS certificates** generated and configured
- [ ] **Service accounts** created with minimal permissions
- [ ] **Pod security standards** enforced (non-root users)
- [ ] **Image scanning** completed (no critical vulnerabilities)

### Application

- [ ] **Docker images** built and pushed to registry
- [ ] **ConfigMaps** created with production configuration
- [ ] **Health checks** configured (liveness, readiness, startup probes)
- [ ] **Resource limits** set (CPU, memory)
- [ ] **Horizontal Pod Autoscaler** configured (if needed)
- [ ] **Anti-affinity rules** configured (for HA)

### Monitoring

- [ ] **Prometheus** deployed and scraping metrics
- [ ] **Loki** deployed and collecting logs
- [ ] **Promtail** DaemonSet running on all nodes
- [ ] **Grafana** deployed with all dashboards
- [ ] **Alerting rules** configured in Prometheus
- [ ] **Alertmanager** configured (optional, for notifications)
- [ ] **Dashboards** accessible and showing data

### Testing

- [ ] **Smoke tests** passed (basic functionality)
- [ ] **Integration tests** passed (end-to-end flows)
- [ ] **Load testing** completed (performance baseline)
- [ ] **Failover testing** completed (HA validation)
- [ ] **Monitoring** validated (alerts firing correctly)

### Documentation

- [ ] **Runbooks** created for all alerts
- [ ] **Deployment procedures** documented
- [ ] **Rollback procedures** documented
- [ ] **Incident response** playbooks created
- [ ] **SLO/SLA** targets defined
- [ ] **Architecture diagrams** up-to-date

### Operational Readiness

- [ ] **On-call rotation** established
- [ ] **Escalation procedures** defined
- [ ] **Communication channels** set up (Slack, PagerDuty, etc.)
- [ ] **Backup and recovery** procedures tested
- [ ] **Disaster recovery plan** documented and tested

---

## Deployment Procedures

### Initial Deployment

**Prerequisites**:

- Kubernetes cluster (1.24+)
- kubectl configured
- Helm 3.0+ (optional)
- Access to container registry

**Step 1: Create Namespace and Secrets**

```bash
# Create namespace with resource quotas
kubectl apply -f k8s/namespace.yaml

# Create secrets (update with production values first!)
# IMPORTANT: Edit k8s/secrets.yaml and change default passwords
kubectl apply -f k8s/secrets.yaml

# Verify secrets created
kubectl get secrets -n keystone
```

**Step 2: Apply Security Policies**

```bash
# Apply RBAC policies
kubectl apply -f k8s/rbac.yaml

# Apply network policies
kubectl apply -f k8s/networkpolicy.yaml

# Verify policies
kubectl get networkpolicies -n keystone
kubectl get roles,rolebindings -n keystone
```

**Step 3: Deploy Application**

```bash
# Deploy HMAS application
kubectl apply -f k8s/configmap.yaml
kubectl apply -f k8s/deployment.yaml
kubectl apply -f k8s/service.yaml

# Wait for deployment to be ready
kubectl wait --for=condition=available --timeout=300s deployment/hmas -n keystone

# Verify deployment
kubectl get pods -n keystone -l app=hmas
kubectl logs -n keystone -l app=hmas --tail=50
```

**Step 4: Deploy Monitoring Stack**

```bash
# Deploy Prometheus
kubectl apply -f k8s/prometheus.yaml
kubectl apply -f k8s/prometheus-alerts.yaml

# Deploy Loki
kubectl apply -f k8s/loki.yaml
kubectl apply -f k8s/promtail.yaml

# Deploy Grafana
kubectl apply -f k8s/grafana.yaml
kubectl apply -f k8s/grafana-dashboards-configmap.yaml

# Wait for all monitoring components
kubectl wait --for=condition=ready pod -l app=prometheus -n keystone --timeout=120s
kubectl wait --for=condition=ready pod -l app=loki -n keystone --timeout=120s
kubectl wait --for=condition=ready pod -l app=grafana -n keystone --timeout=120s

# Verify monitoring stack
kubectl get pods -n keystone
```

**Step 5: Validate Deployment**

```bash
# Run smoke tests
kubectl exec -n keystone deployment/hmas -- /usr/local/bin/hmas-smoke-test

# Check health endpoints
kubectl port-forward -n keystone svc/hmas 8080:8080 &
curl http://localhost:8080/healthz
curl http://localhost:8080/ready

# Verify metrics
curl http://localhost:9090/metrics | grep hmas_

# Access Grafana
kubectl port-forward -n keystone svc/grafana 3000:3000
open http://localhost:3000  # Login: admin/admin (change password!)
```

**Step 6: Configure Ingress (Optional)**

```bash
# Example: Using NGINX Ingress Controller
kubectl apply -f k8s/ingress.yaml

# Verify ingress
kubectl get ingress -n keystone
```

### Helm Deployment (Alternative)

```bash
# Install via Helm
helm install keystone ./helm/keystone \
  --namespace keystone \
  --create-namespace \
  --set image.tag=v1.0.0 \
  --set replicaCount=2 \
  --values helm/keystone/values-production.yaml

# Verify installation
helm status keystone -n keystone
helm test keystone -n keystone
```

### Upgrade Deployment

```bash
# Update image tag or configuration
kubectl set image deployment/hmas hmas=keystone:v1.1.0 -n keystone

# Or apply updated manifests
kubectl apply -f k8s/deployment.yaml

# Monitor rollout status
kubectl rollout status deployment/hmas -n keystone

# Verify new version
kubectl get pods -n keystone -l app=hmas -o jsonpath='{.items[0].spec.containers[0].image}'
```

---

## Rollback Procedures

### Rollback Deployment

**Scenario**: New deployment is causing issues, need to revert to previous version.

**Method 1: Kubernetes Rollout Undo**

```bash
# Check rollout history
kubectl rollout history deployment/hmas -n keystone

# Rollback to previous version
kubectl rollout undo deployment/hmas -n keystone

# Rollback to specific revision
kubectl rollout undo deployment/hmas -n keystone --to-revision=3

# Monitor rollback
kubectl rollout status deployment/hmas -n keystone

# Verify rollback
kubectl get pods -n keystone -l app=hmas
kubectl logs -n keystone -l app=hmas --tail=50
```

**Method 2: Helm Rollback**

```bash
# Check release history
helm history keystone -n keystone

# Rollback to previous release
helm rollback keystone -n keystone

# Rollback to specific revision
helm rollback keystone 3 -n keystone

# Verify rollback
helm status keystone -n keystone
```

**Method 3: GitOps Rollback**

```bash
# Revert commit in Git
git revert <commit-hash>
git push origin main

# ArgoCD/Flux will automatically sync and rollback
# Verify sync status
argocd app get keystone
```

### Rollback Verification Checklist

After rollback, verify:

- [ ] Pods are running (kubectl get pods)
- [ ] Health checks passing (curl /healthz, /ready)
- [ ] Metrics being collected (Prometheus targets)
- [ ] Logs flowing (Loki queries)
- [ ] No active alerts (Prometheus alerts)
- [ ] Performance metrics normal (latency, throughput)

---

## Security Hardening

### Secrets Management

**Production Best Practice**: Use external secret management (e.g., HashiCorp Vault, AWS Secrets Manager).

**Option 1: External Secrets Operator**

```bash
# Install External Secrets Operator
helm repo add external-secrets https://charts.external-secrets.io
helm install external-secrets external-secrets/external-secrets \
  --namespace external-secrets-system \
  --create-namespace

# Configure SecretStore (example: Vault)
kubectl apply -f - <<EOF
apiVersion: external-secrets.io/v1beta1
kind: SecretStore
metadata:
  name: vault-backend
  namespace: keystone
spec:
  provider:
    vault:
      server: "https://vault.example.com"
      path: "secret"
      version: "v2"
      auth:
        kubernetes:
          mountPath: "kubernetes"
          role: "keystone"
EOF

# Create ExternalSecret
kubectl apply -f - <<EOF
apiVersion: external-secrets.io/v1beta1
kind: ExternalSecret
metadata:
  name: hmas-credentials
  namespace: keystone
spec:
  refreshInterval: 1h
  secretStoreRef:
    name: vault-backend
    kind: SecretStore
  target:
    name: hmas-credentials
    creationPolicy: Owner
  data:
  - secretKey: admin-password
    remoteRef:
      key: keystone/grafana
      property: admin-password
EOF
```

**Option 2: Sealed Secrets**

```bash
# Install Sealed Secrets controller
kubectl apply -f https://github.com/bitnami-labs/sealed-secrets/releases/download/v0.24.0/controller.yaml

# Encrypt secret
kubeseal --format=yaml < k8s/secrets.yaml > k8s/sealed-secrets.yaml

# Apply sealed secret
kubectl apply -f k8s/sealed-secrets.yaml
```

### RBAC Hardening

All RBAC policies are defined in `k8s/rbac.yaml`:

- **Principle of Least Privilege**: Each service account has minimal required permissions
- **Read-only access**: Monitoring components have read-only cluster access
- **Namespace isolation**: Roles scoped to keystone namespace

### Network Policies

All network policies are defined in `k8s/networkpolicy.yaml`:

- **Default deny**: All ingress/egress denied by default
- **Whitelist approach**: Only explicitly allowed traffic permitted
- **Zero-trust**: Pods can only communicate with authorized pods

**Verify network policies**:

```bash
# Check applied policies
kubectl get networkpolicies -n keystone

# Test network connectivity
kubectl run test-pod --image=busybox -n keystone -- sleep 3600
kubectl exec -n keystone test-pod -- wget -O- http://hmas:8080/healthz
```

### Pod Security Standards

**Pod Security Admission** (PSA) is enabled for the namespace:

```bash
# Label namespace with pod security standard
kubectl label namespace keystone \
  pod-security.kubernetes.io/enforce=restricted \
  pod-security.kubernetes.io/audit=restricted \
  pod-security.kubernetes.io/warn=restricted
```

**Pod security features in use**:

- Non-root users (UID 1000 for HMAS, 472 for Grafana, etc.)
- Read-only root filesystem (where possible)
- Drop all capabilities
- No privilege escalation
- Security context constraints

### Image Security

**Best practices**:

```bash
# Scan images for vulnerabilities (using Trivy)
trivy image keystone:latest

# Sign images (using Cosign)
cosign sign keystone:latest

# Verify image signatures in admission controller
# Configure OPA Gatekeeper or Kyverno policy
```

---

## Monitoring and Alerting

### Prometheus Alerts

**Alert Rules**: Defined in `k8s/prometheus-alerts.yaml`

**Alert Categories**:

1. **Application Alerts** (hmas_application group)
   - HMASPodsDown
   - HighErrorRate
   - HighMessageLatency
   - HighQueueDepth
   - HighWorkerUtilization

2. **Infrastructure Alerts** (hmas_infrastructure group)
   - PodRestartLoop
   - PodOOMKilled
   - HighCPUUsage
   - HighMemoryUsage
   - PVCAlmostFull

3. **Monitoring Stack Alerts** (monitoring_stack group)
   - PrometheusDown
   - LokiDown
   - PromtailNotRunningOnAllNodes
   - GrafanaDown

4. **SLO/SLA Alerts** (slo_sla group)
   - SLOAvailabilityViolation
   - SLOLatencyViolation
   - SLOErrorRateViolation

**View Active Alerts**:

```bash
# Port-forward to Prometheus
kubectl port-forward -n keystone svc/prometheus 9090:9090

# Open in browser
open http://localhost:9090/alerts
```

### Alertmanager Setup (Optional)

For production alerting notifications:

```bash
# Deploy Alertmanager
kubectl apply -f k8s/alertmanager.yaml

# Configure notification channels (Slack, PagerDuty, email)
kubectl apply -f k8s/alertmanager-config.yaml
```

Example Alertmanager config:

```yaml
global:
  slack_api_url: 'https://hooks.slack.com/services/YOUR/SLACK/WEBHOOK'

route:
  group_by: ['alertname', 'cluster', 'service']
  group_wait: 10s
  group_interval: 10s
  repeat_interval: 12h
  receiver: 'slack-notifications'
  routes:
  - match:
      severity: critical
    receiver: 'pagerduty-critical'

receivers:
- name: 'slack-notifications'
  slack_configs:
  - channel: '#alerts'
    title: '{{ .GroupLabels.alertname }}'
    text: '{{ range .Alerts }}{{ .Annotations.description }}{{ end }}'

- name: 'pagerduty-critical'
  pagerduty_configs:
  - service_key: 'YOUR_PAGERDUTY_SERVICE_KEY'
```

---

## Incident Response

### Incident Response Workflow

```
1. DETECT → 2. ASSESS → 3. MITIGATE → 4. RESOLVE → 5. POSTMORTEM
```

### 1. Detection

**Alert fires** → Check Grafana dashboards → Assess severity

**Severity Levels**:

- **Critical (P0)**: Total service outage, data loss risk
- **High (P1)**: Major functionality impaired, SLO violation
- **Medium (P2)**: Degraded performance, non-critical features down
- **Low (P3)**: Minor issues, no user impact

### 2. Assessment

**Gather information**:

```bash
# Check pod status
kubectl get pods -n keystone

# Check recent events
kubectl get events -n keystone --sort-by='.lastTimestamp'

# Check logs
kubectl logs -n keystone -l app=hmas --tail=100 --since=10m

# Check metrics
kubectl port-forward -n keystone svc/grafana 3000:3000
# Open dashboards

# Check resource usage
kubectl top pods -n keystone
kubectl top nodes
```

### 3. Mitigation

**Common mitigation actions**:

**Scenario: High Error Rate**

```bash
# Check alert: HighErrorRate
# Runbook: https://github.com/keystone/runbooks/high-error-rate.md

# 1. Check logs for errors
kubectl logs -n keystone -l app=hmas | grep ERROR

# 2. Check if related to recent deployment
kubectl rollout history deployment/hmas -n keystone

# 3. If recent deployment, consider rollback
kubectl rollout undo deployment/hmas -n keystone

# 4. Scale down if load-related
kubectl scale deployment/hmas --replicas=4 -n keystone

# 5. Monitor recovery
watch kubectl get pods -n keystone
```

**Scenario: Pod Crashing**

```bash
# Check pod status
kubectl get pods -n keystone

# Describe pod
kubectl describe pod <pod-name> -n keystone

# Check logs
kubectl logs <pod-name> -n keystone --previous

# Check events
kubectl get events -n keystone | grep <pod-name>

# Common fixes:
# - OOMKilled: Increase memory limits
# - CrashLoopBackOff: Fix application bug, check ConfigMap/Secrets
# - ImagePullBackOff: Fix image registry access
```

**Scenario: High Latency**

```bash
# Check metrics
kubectl port-forward -n keystone svc/grafana 3000:3000
# Open Agent Details dashboard

# Check worker utilization
# If >90%, scale horizontally

# Scale deployment
kubectl scale deployment/hmas --replicas=4 -n keystone

# Or enable HPA
kubectl autoscale deployment hmas --min=2 --max=10 --cpu-percent=70 -n keystone

# Monitor latency improvement
# Check Grafana dashboard
```

### 4. Resolution

**Verify mitigation worked**:

- [ ] Alert has cleared (check Prometheus /alerts)
- [ ] Metrics returned to normal (check Grafana dashboards)
- [ ] No errors in logs (check Loki)
- [ ] Health checks passing (curl /healthz)

### 5. Postmortem

**After incident resolved**, conduct postmortem:

**Template**:

```markdown
# Incident Postmortem: [Title]

**Date**: 2025-11-19
**Duration**: 15 minutes (14:00 - 14:15 UTC)
**Severity**: P1 (High)
**Root Cause**: Memory leak in HMAS pod

## Timeline

- 14:00 UTC: Alert "HighMemoryUsage" fired
- 14:02 UTC: On-call engineer paged
- 14:05 UTC: Investigation started
- 14:10 UTC: Root cause identified (memory leak in message processing)
- 14:12 UTC: Pod restarted
- 14:15 UTC: Alert cleared, incident resolved

## Root Cause

Memory leak in message processing loop caused pod to consume >90% memory limit.

## Impact

- 15 minutes of degraded performance
- ~500 messages delayed (average delay: 2 seconds)
- No data loss

## Action Items

- [ ] Fix memory leak (Issue #123)
- [ ] Add memory profiling to CI/CD
- [ ] Increase memory limits from 2Gi to 4Gi
- [ ] Add alert for gradual memory increase

## Lessons Learned

- Health checks didn't catch gradual memory leak
- Need better memory profiling in development
```

---

## SLO/SLA Definitions

### Service Level Objectives (SLOs)

**Availability SLO**: 99.9% uptime (43.2 minutes downtime per month)

```promql
# Availability calculation
avg_over_time(hmas_up[30d]) >= 0.999
```

**Latency SLO**: P95 message processing latency < 1ms

```promql
# P95 latency
histogram_quantile(0.95, hmas_message_latency_microseconds) < 1000
```

**Error Rate SLO**: Error rate < 0.1% (1 in 1000 messages)

```promql
# Error rate
rate(hmas_deadline_misses_total[5m]) / rate(hmas_messages_processed_total[5m]) < 0.001
```

**Throughput SLO**: Support 1000 messages/sec

```promql
# Throughput
rate(hmas_messages_processed_total[5m]) >= 1000
```

### Service Level Agreements (SLAs)

**External SLA** (for customers/stakeholders):

| Metric | Target | Measurement | Consequence |
|--------|--------|-------------|-------------|
| Availability | 99.9% | Monthly uptime percentage | Service credit: 10% for <99.9%, 25% for <99% |
| Latency (P95) | < 5ms | 95th percentile over 5min | Service credit: 5% if exceeded 3 times/month |
| Error Rate | < 1% | Errors / total requests | Service credit: 10% if exceeded |

**Monitoring SLO Compliance**:

```bash
# Check SLO dashboard in Grafana
kubectl port-forward -n keystone svc/grafana 3000:3000
# Navigate to: /d/hmas-slo
```

---

## Performance Testing

### Load Testing

**Tool**: k6 (<https://k6.io/>)

**Test Script** (`tests/load/hmas-load-test.js`):

```javascript
import http from 'k6/http';
import { check, sleep } from 'k6';

export let options = {
  stages: [
    { duration: '2m', target: 100 }, // Ramp up to 100 users
    { duration: '5m', target: 100 }, // Stay at 100 users
    { duration: '2m', target: 200 }, // Ramp up to 200 users
    { duration: '5m', target: 200 }, // Stay at 200 users
    { duration: '2m', target: 0 },   // Ramp down
  ],
  thresholds: {
    http_req_duration: ['p(95)<1000'], // 95% of requests < 1s
    http_req_failed: ['rate<0.01'],    // Error rate < 1%
  },
};

export default function () {
  let response = http.post('http://hmas:8080/api/message', JSON.stringify({
    priority: 'normal',
    payload: { test: 'data' }
  }), {
    headers: { 'Content-Type': 'application/json' },
  });

  check(response, {
    'status is 200': (r) => r.status === 200,
    'latency < 100ms': (r) => r.timings.duration < 100,
  });

  sleep(1);
}
```

**Run load test**:

```bash
# From within cluster
kubectl run k6 --image=grafana/k6 -it --rm -- run - < tests/load/hmas-load-test.js
```

**Expected Results** (2 replicas, 4 workers each):

- **Throughput**: 1000+ req/sec
- **P95 Latency**: < 1ms
- **Error Rate**: < 0.1%
- **CPU Usage**: < 70%
- **Memory Usage**: < 80%

### Stress Testing

**Test failure scenarios**:

```bash
# Kill random pods (chaos testing)
kubectl delete pod -n keystone -l app=hmas \
  --field-selector status.phase=Running --dry-run=client -o name | \
  shuf | head -1 | xargs kubectl delete

# Limit CPU
kubectl set resources deployment/hmas -n keystone --limits=cpu=100m

# Fill disk (test PVC limits)
kubectl exec -n keystone deployment/hmas -- dd if=/dev/zero of=/tmp/testfile bs=1M count=1000

# Network partition (with chaos engineering tools)
# Use Chaos Mesh or Litmus
```

---

## Disaster Recovery

### Backup Strategy

**What to backup**:

1. **Persistent data** (PVCs)
2. **Configuration** (ConfigMaps, Secrets)
3. **State** (if stateful)

**Backup with Velero**:

```bash
# Install Velero
velero install \
  --provider aws \
  --bucket keystone-backups \
  --secret-file ./credentials-velero

# Create backup schedule (daily backups, 30-day retention)
velero schedule create keystone-daily \
  --schedule="0 2 * * *" \
  --include-namespaces keystone \
  --ttl 720h

# Manual backup
velero backup create keystone-manual \
  --include-namespaces keystone

# Check backup status
velero backup get
velero backup describe keystone-manual
```

### Restore Procedures

**Restore from backup**:

```bash
# List available backups
velero backup get

# Restore from specific backup
velero restore create --from-backup keystone-daily-20251119

# Monitor restore
velero restore describe <restore-name>

# Verify restore
kubectl get all -n keystone
kubectl get pvc -n keystone
```

### Disaster Scenarios

**Scenario 1: Entire Cluster Lost**

```bash
# 1. Provision new cluster
# 2. Install Velero with same backup location
# 3. Restore from latest backup
velero restore create --from-backup keystone-daily-latest

# 4. Verify all resources
kubectl get all -n keystone

# 5. Run smoke tests
kubectl exec -n keystone deployment/hmas -- /usr/local/bin/hmas-smoke-test
```

**Scenario 2: Data Corruption**

```bash
# 1. Stop application (scale to 0)
kubectl scale deployment/hmas --replicas=0 -n keystone

# 2. Restore PVC from backup
velero restore create --from-backup keystone-daily-20251118 \
  --include-resources pvc

# 3. Restart application
kubectl scale deployment/hmas --replicas=2 -n keystone

# 4. Verify data integrity
# Run application-specific data validation
```

**Recovery Time Objective (RTO)**: < 1 hour
**Recovery Point Objective (RPO)**: < 24 hours (daily backups)

---

## Troubleshooting

### Common Issues

#### Issue: Pods in CrashLoopBackOff

**Symptoms**: Pods repeatedly restarting

**Diagnosis**:

```bash
kubectl get pods -n keystone
kubectl describe pod <pod-name> -n keystone
kubectl logs <pod-name> -n keystone --previous
```

**Common Causes & Fixes**:

- **Application error**: Check logs, fix code
- **Missing ConfigMap/Secret**: Verify secrets exist
- **Port conflict**: Check port configuration
- **Health check failure**: Increase initialDelaySeconds

#### Issue: High Memory Usage

**Symptoms**: Pods being OOMKilled

**Diagnosis**:

```bash
kubectl top pods -n keystone
kubectl describe pod <pod-name> -n keystone | grep -A 5 "Limits"
```

**Fixes**:

- Increase memory limits in deployment.yaml
- Fix memory leaks in application
- Enable memory profiling

#### Issue: Network Policy Blocking Traffic

**Symptoms**: Pods can't communicate

**Diagnosis**:

```bash
kubectl get networkpolicies -n keystone
kubectl describe networkpolicy <policy-name> -n keystone

# Test connectivity
kubectl run test-pod --image=busybox -n keystone -- sleep 3600
kubectl exec -n keystone test-pod -- wget -O- http://hmas:8080/healthz
```

**Fixes**:

- Review and update network policies
- Temporarily disable for testing (not in production!)

#### Issue: Prometheus Not Scraping Metrics

**Symptoms**: No metrics in Grafana

**Diagnosis**:

```bash
# Check Prometheus targets
kubectl port-forward -n keystone svc/prometheus 9090:9090
open http://localhost:9090/targets

# Check service endpoints
kubectl get endpoints hmas -n keystone

# Test metrics endpoint directly
kubectl exec -n keystone deployment/hmas -- curl localhost:9090/metrics
```

**Fixes**:

- Verify service selector matches pod labels
- Check network policy allows Prometheus → HMAS traffic
- Verify pod annotations (prometheus.io/scrape=true)

### Emergency Contacts

**On-Call Rotation**:

- Primary: <ops-oncall@example.com>
- Secondary: <sre-team@example.com>
- Escalation: <engineering-lead@example.com>

**Communication Channels**:

- Slack: #keystone-incidents
- PagerDuty: Keystone service
- War room: Zoom link in PagerDuty alert

---

**Document Version**: 1.0
**Last Review**: 2025-11-19
**Next Review**: 2025-12-19
**Owner**: SRE Team

**Phase**: 6.6 - Production Readiness
**Status**: Complete ✅
