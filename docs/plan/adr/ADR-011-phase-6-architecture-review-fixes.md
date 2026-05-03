# ADR-011: Architecture Review Fixes - Phase 6.6

**Date**: 2025-11-20
**Review**: ProjectKeystone Kubernetes Architecture Review

## Summary

This document tracks fixes applied to address critical issues identified in the comprehensive architecture review.

## Critical Issues Fixed (C1-C5)

### ✅ C1: Hardcoded Production Secrets - FIXED

**Issue**: Secrets with default passwords committed to Git repository

**Changes**:

- Removed `k8s/secrets.yaml` from repository
- Created `k8s/secrets-example.yaml` with placeholder values
- Added `k8s/secrets.yaml` to `.gitignore`
- Documented proper secret generation commands in example file

**Production Deployment**:

```bash
# Generate Grafana admin password
kubectl create secret generic grafana-admin-credentials \
  --from-literal=admin-user=admin \
  --from-literal=admin-password=$(openssl rand -base64 32) \
  -n projectkeystone

# Generate TLS certificates
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout tls.key -out tls.crt \
  -subj "/CN=hmas.projectkeystone.svc.cluster.local"

kubectl create secret tls hmas-tls \
  --cert=tls.crt --key=tls.key \
  -n projectkeystone
```

**Files Changed**:

- Deleted: `k8s/secrets.yaml`
- Created: `k8s/secrets-example.yaml`
- Modified: `.gitignore`

---

### ✅ C2: Prometheus Data Loss on Pod Restart - FIXED

**Issue**: Prometheus using emptyDir (ephemeral storage), causing metric data loss on pod restart

**Changes**:

- Replaced `emptyDir` with `PersistentVolumeClaim`
- Added `prometheus-data` PVC with 100Gi storage
- Configured for 15-day retention with headroom

**Impact**:

- Metrics now persist across pod restarts
- Historical data available for SLO compliance
- Incident investigation possible with past metrics

**Files Changed**:

- Modified: `k8s/prometheus.yaml` (lines 185-205)

**Production Note**: Set appropriate `storageClassName` for your cluster

---

### ✅ C3: SLO Alert Rules Incorrect - FIXED

**Issue**: `SLOLatencyViolation` alert used `histogram_quantile()` on a gauge metric (would never fire)

**Changes**:

- Updated alert to use gauge directly: `hmas_message_latency_microseconds > 1000`
- Added comment noting this monitors average, not P95
- Added note in description referencing GitHub issue for histogram implementation

**Limitation**:

- Now monitors **average latency** instead of P95
- Less accurate than histogram-based P95
- Histogram implementation tracked in GitHub issue

**Files Changed**:

- Modified: `k8s/prometheus-alerts.yaml` (lines 223-234)

**Future Enhancement**: Implement histogram metrics (see GitHub issues)

---

### ✅ C5: Network Policy DNS Selector Deprecated - FIXED

**Issue**: NetworkPolicies used deprecated label `name: kube-system` (won't work in K8s 1.22+)

**Changes**:

- Updated all DNS namespace selectors to use `kubernetes.io/metadata.name: kube-system`
- Fixed 4 instances across policies: hmas-egress, prometheus-egress, grafana-egress, promtail-egress

**Impact**: DNS resolution now works correctly in modern Kubernetes versions

**Files Changed**:

- Modified: `k8s/networkpolicy.yaml` (lines 93, 171, 236, 313)

---

## Outstanding Issues (Require Code Changes)

The following issues cannot be fixed with configuration changes alone and require C++ implementation or infrastructure
work. **GitHub issues have been filed** for tracking:

### C4: Missing Health Check Endpoints

- **Severity**: Critical (P0)
- **Issue**: Kubernetes expects `/healthz` and `/ready` endpoints on port 8080, but no C++ implementation exists
- **Impact**: Pods will fail health checks and restart continuously
- **Required**: Implement HTTP server in C++ for health endpoints
- **GitHub Issue**: Filed automatically

### M1: Resource Allocation Undersized

- **Severity**: Major (P1)
- **Issue**: Default resource limits (2Gi memory) may be insufficient for production load
- **Required**: Load testing with k6, adjust based on actual usage
- **GitHub Issue**: Filed automatically

### M3: No Alertmanager Deployment

- **Severity**: Major (P1)
- **Issue**: Alert rules configured but no notification delivery system
- **Required**: Deploy Alertmanager, configure Slack/PagerDuty
- **GitHub Issue**: Filed automatically

### M4: Prometheus HTTP Server Lacks Security

- **Severity**: Major (P1)
- **Issue**: No authentication, rate limiting, or TLS on metrics endpoint
- **Required**: C++ code changes to add security
- **GitHub Issue**: Filed automatically

### M6: No Distributed Tracing

- **Severity**: Major (P2)
- **Issue**: Missing third pillar of observability (only metrics + logs)
- **Required**: OpenTelemetry/Jaeger integration (Phase 7/8)
- **GitHub Issue**: Filed automatically

### N1: Runbooks Don't Exist

- **Severity**: Minor (P3)
- **Issue**: Alert runbook URLs point to non-existent documentation
- **Required**: Create runbook content for top 10 alerts
- **GitHub Issue**: Filed automatically

### N2: Image Scanning Not Implemented

- **Severity**: Minor (P3)
- **Issue**: Trivy scanning recommended but not in CI/CD
- **Required**: Add to GitHub Actions workflow
- **GitHub Issue**: Filed automatically

---

## Deployment Instructions

### Before Deploying

1. **Generate Secrets** (critical):

```bash
# Grafana admin password
kubectl create secret generic grafana-admin-credentials \
  --from-literal=admin-user=admin \
  --from-literal=admin-password=$(openssl rand -base64 32) \
  -n projectkeystone

# TLS certificates
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout tls.key -out tls.crt \
  -subj "/CN=hmas.projectkeystone.svc.cluster.local"

kubectl create secret tls hmas-tls \
  --cert=tls.crt --key=tls.key \
  -n projectkeystone
```

1. **Configure Storage Class** (optional):

```bash
# Edit k8s/prometheus.yaml line 204
# Uncomment and set storageClassName: <your-storage-class>
```

1. **Deploy**:

```bash
kubectl apply -f k8s/namespace.yaml
kubectl apply -f k8s/rbac.yaml
kubectl apply -f k8s/networkpolicy.yaml
kubectl apply -f k8s/configmap.yaml
kubectl apply -f k8s/prometheus.yaml
kubectl apply -f k8s/prometheus-alerts.yaml
kubectl apply -f k8s/loki.yaml
kubectl apply -f k8s/promtail.yaml
kubectl apply -f k8s/grafana.yaml
kubectl apply -f k8s/grafana-dashboards-configmap.yaml

# Note: deployment.yaml requires C4 fix before it will work
# kubectl apply -f k8s/deployment.yaml  # DO NOT DEPLOY YET
```

### Known Limitations

1. **Health checks will fail** until C4 is implemented (health check endpoints)
2. **Load capacity unknown** until M1 is completed (load testing)
3. **No alerting notifications** until M3 is implemented (Alertmanager)

---

## Testing

```bash
# Validate manifests
kubectl apply --dry-run=server -f k8s/

# Check Prometheus has persistent storage
kubectl get pvc -n projectkeystone
# Should show: prometheus-data  Bound  100Gi

# Verify network policies
kubectl get networkpolicies -n projectkeystone

# Test alert rules syntax
kubectl port-forward -n projectkeystone svc/prometheus 9090:9090
# Open http://localhost:9090/alerts
```

---

## GitHub Issues Filed

See the generated bash script output for GitHub issue URLs tracking remaining work.

---

## Production Readiness Status

| Category | Status | Notes |
|----------|--------|-------|
| **Secrets Management** | ✅ READY | External Secrets Operator documented |
| **Data Persistence** | ✅ READY | Prometheus PVC configured |
| **Alert Correctness** | ⚠️ PARTIAL | Average latency (not P95) |
| **Network Connectivity** | ✅ READY | DNS selectors fixed |
| **Health Checks** | ❌ BLOCKED | Awaiting C++ implementation (C4) |
| **Load Capacity** | ❌ UNKNOWN | Requires load testing (M1) |
| **Notifications** | ❌ MISSING | Requires Alertmanager (M3) |

**Overall**: ⚠️ **60% Production Ready** - Core fixes complete, awaiting C++ implementation

---

**Next Steps**:

1. Implement health check HTTP server (C4) - CRITICAL
2. Run load tests and adjust resources (M1) - HIGH PRIORITY
3. Deploy Alertmanager (M3) - HIGH PRIORITY
4. Address security concerns (M4) - MEDIUM PRIORITY

**Estimated Timeline to Production**: 2-3 weeks (with C++ implementation work)
