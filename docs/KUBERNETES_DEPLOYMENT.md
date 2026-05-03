# Kubernetes Deployment Guide

**ProjectKeystone HMAS - Production Deployment on Kubernetes**

## Overview

This guide covers deploying ProjectKeystone HMAS to Kubernetes with production-ready configurations, health checks, and
monitoring readiness.

---

## Prerequisites

### Required Tools

- **Docker** 20.10+ - For building container images
- **kubectl** 1.24+ - Kubernetes command-line tool
- **Kubernetes cluster** - One of:
  - **Minikube** - Local single-node cluster
  - **kind** - Kubernetes IN Docker
  - **Cloud Kubernetes** - GKE, EKS, AKS

### Verify Installation

```bash
# Check Docker
docker --version

# Check kubectl
kubectl version --client

# Check cluster connection
kubectl cluster-info
```

---

## Quick Start

### 1. Build Production Image

```bash
# Build the production Docker image
docker build --target production -t projectkeystone:latest .

# Verify image
docker images | grep projectkeystone
```

### 2. Deploy to Kubernetes

```bash
# Apply all manifests
kubectl apply -f k8s/

# Verify deployment
kubectl get all -n projectkeystone

# Check pod status
kubectl get pods -n projectkeystone -w
```

### 3. Verify Health

```bash
# Get service endpoints
kubectl get svc -n projectkeystone

# Port-forward to access health check
kubectl port-forward -n projectkeystone svc/hmas 8080:8080

# Test health endpoint (in another terminal)
curl http://localhost:8080/healthz
# Expected: {"status":"healthy"}

curl http://localhost:8080/ready
# Expected: {"status":"ready"}

# Test metrics endpoint
kubectl port-forward -n projectkeystone svc/hmas 9090:9090
curl http://localhost:9090/metrics
```

---

## Architecture

### Deployment Structure

```
projectkeystone namespace
├── hmas Deployment (2 replicas)
│   ├── Pod 1 (hmas-xxxxxxxx-xxxxx)
│   └── Pod 2 (hmas-xxxxxxxx-xxxxx)
├── hmas Service (ClusterIP)
│   ├── Port 8080: Health checks
│   ├── Port 9090: Metrics (Prometheus)
│   └── Port 50051: gRPC (Phase 8)
└── hmas-headless Service (for direct pod access)
```

### Container Specifications

**Image**: `projectkeystone:latest` (production stage)

**Ports**:

- `8080`: HTTP health checks (`/healthz`, `/ready`)
- `9090`: Prometheus metrics (`/metrics`)
- `50051`: gRPC (for distributed deployment in Phase 8)

**Resource Limits**:

- **Requests**: 500m CPU, 512Mi memory
- **Limits**: 2000m CPU (2 cores), 2Gi memory

**Security**:

- Runs as non-root user (UID 1000)
- Read-only root filesystem (future enhancement)
- Minimal capabilities

---

## Manifest Files

### 1. Namespace (`k8s/namespace.yaml`)

Creates `projectkeystone` namespace with resource quotas:

- Max 8 CPU cores (requests)
- Max 16 CPU cores (limits)
- Max 16 GB memory (requests)
- Max 32 GB memory (limits)
- Max 50 pods
- Max 10 PersistentVolumeClaims

### 2. ConfigMap (`k8s/configmap.yaml`)

Configuration for HMAS agents:

- Worker count (default: 4)
- Heartbeat intervals
- Circuit breaker settings
- Retry policy configuration
- Feature flags

**Environment Variables**:

```yaml
WORKER_COUNT: "4"
LOG_LEVEL: "info"
HEARTBEAT_INTERVAL_MS: "1000"
METRICS_ENABLED: "true"
```

### 3. Deployment (`k8s/deployment.yaml`)

**Key Features**:

- **2 replicas** for high availability
- **Rolling update strategy** (maxSurge: 1, maxUnavailable: 0)
- **Pod anti-affinity** (spread across nodes)
- **Liveness probe**: HTTP GET `/healthz` (every 10s)
- **Readiness probe**: HTTP GET `/ready` (every 5s)
- **Startup probe**: 150s max startup time
- **Graceful shutdown**: 30s termination period

### 4. Service (`k8s/service.yaml`)

**hmas Service** (ClusterIP):

- Exposes pods within cluster
- Load balances across replicas
- Prometheus annotations for scraping

**hmas-headless Service**:

- Direct pod-to-pod communication
- Used for distributed work-stealing (Phase 8)

---

## Health Checks

### Liveness Probe

**Purpose**: Detects if container is alive
**Endpoint**: `GET /healthz`
**Interval**: 10 seconds
**Timeout**: 5 seconds
**Failure Threshold**: 3 (restart after 3 failures)

**Success Response**:

```json
{"status":"healthy"}
```

### Readiness Probe

**Purpose**: Detects if container is ready for traffic
**Endpoint**: `GET /ready`
**Interval**: 5 seconds
**Timeout**: 3 seconds
**Failure Threshold**: 3 (remove from service after 3 failures)

**Success Response**:

```json
{"status":"ready"}
```

### Startup Probe

**Purpose**: Gives slow-starting containers time to initialize
**Endpoint**: `GET /healthz`
**Interval**: 5 seconds
**Failure Threshold**: 30 (150s total)

---

## Metrics

### Prometheus Endpoint

**Endpoint**: `GET /metrics`
**Port**: 9090
**Format**: Prometheus text format

**Available Metrics** (Phase 6.1 placeholder):

```
# HELP hmas_uptime_seconds HMAS uptime in seconds
# TYPE hmas_uptime_seconds counter
hmas_uptime_seconds 1234567890

# HELP hmas_status HMAS status (1=healthy, 0=unhealthy)
# TYPE hmas_status gauge
hmas_status 1
```

**Note**: Full metrics will be implemented in Phase 6.3 (Prometheus Monitoring).

---

## Configuration

### Environment Variables

All configuration is via ConfigMap. To customize:

```bash
# Edit ConfigMap
kubectl edit configmap hmas-config -n projectkeystone

# Restart pods to pick up changes
kubectl rollout restart deployment/hmas -n projectkeystone
```

### Common Configurations

**Increase workers** (more parallelism):

```yaml
WORKER_COUNT: "8"
```

**Enable debug logging**:

```yaml
LOG_LEVEL: "debug"
```

**Adjust heartbeat sensitivity**:

```yaml
HEARTBEAT_INTERVAL_MS: "500"
HEARTBEAT_TIMEOUT_MS: "2000"
```

---

## Scaling

### Horizontal Scaling (More Replicas)

```bash
# Scale to 5 replicas
kubectl scale deployment/hmas -n projectkeystone --replicas=5

# Verify scaling
kubectl get pods -n projectkeystone
```

### Vertical Scaling (More Resources)

Edit `k8s/deployment.yaml`:

```yaml
resources:
  requests:
    cpu: "1000m"      # Increase from 500m
    memory: "1Gi"     # Increase from 512Mi
  limits:
    cpu: "4000m"      # Increase from 2000m
    memory: "4Gi"     # Increase from 2Gi
```

Apply changes:

```bash
kubectl apply -f k8s/deployment.yaml
kubectl rollout restart deployment/hmas -n projectkeystone
```

---

## Troubleshooting

### Pod Won't Start

```bash
# Check pod events
kubectl describe pod -n projectkeystone <pod-name>

# Check pod logs
kubectl logs -n projectkeystone <pod-name>

# Check recent logs
kubectl logs -n projectkeystone <pod-name> --tail=50
```

**Common Issues**:

- **ImagePullBackOff**: Image not found (build and tag correctly)
- **CrashLoopBackOff**: Container crashes on startup (check logs)
- **Pending**: Insufficient resources (check node capacity)

### Health Checks Failing

```bash
# Check liveness/readiness probes
kubectl describe pod -n projectkeystone <pod-name>

# Manually test health endpoint
kubectl exec -n projectkeystone <pod-name> -- curl localhost:8080/healthz
```

### View Logs

```bash
# Follow logs (live tail)
kubectl logs -n projectkeystone -f deployment/hmas

# Logs from all replicas
kubectl logs -n projectkeystone -l app=hmas --all-containers=true

# Logs from specific pod
kubectl logs -n projectkeystone <pod-name>
```

### Access Shell

```bash
# Get a shell in a running pod
kubectl exec -it -n projectkeystone <pod-name> -- /bin/bash

# Note: Container runs as non-root user (hmas)
```

### Resource Usage

```bash
# Check resource usage
kubectl top pods -n projectkeystone

# Check node capacity
kubectl top nodes

# Describe resource quotas
kubectl describe resourcequota -n projectkeystone
```

---

## Testing Local Deployment

### Option 1: Minikube

```bash
# Start Minikube
minikube start --cpus=4 --memory=8192

# Build image inside Minikube
eval $(minikube docker-env)
docker build --target production -t projectkeystone:latest .

# Deploy
kubectl apply -f k8s/

# Access service
minikube service hmas -n projectkeystone
```

### Option 2: kind (Kubernetes IN Docker)

```bash
# Create kind cluster
kind create cluster --name projectkeystone

# Load image into kind
kind load docker-image projectkeystone:latest --name projectkeystone

# Deploy
kubectl apply -f k8s/

# Port-forward to access
kubectl port-forward -n projectkeystone svc/hmas 8080:8080
```

### Option 3: Docker Desktop Kubernetes

```bash
# Enable Kubernetes in Docker Desktop settings

# Build image
docker build --target production -t projectkeystone:latest .

# Deploy
kubectl apply -f k8s/

# Port-forward
kubectl port-forward -n projectkeystone svc/hmas 8080:8080 9090:9090
```

---

## Cleanup

### Delete Deployment

```bash
# Delete all resources in projectkeystone namespace
kubectl delete -f k8s/

# Verify deletion
kubectl get all -n projectkeystone
```

### Delete Namespace

```bash
# Delete entire namespace (WARNING: deletes all resources)
kubectl delete namespace projectkeystone
```

---

## Rollout and Rollback

### Rolling Update

```bash
# Update image tag
kubectl set image deployment/hmas hmas=projectkeystone:v2 -n projectkeystone

# Watch rollout
kubectl rollout status deployment/hmas -n projectkeystone

# Check rollout history
kubectl rollout history deployment/hmas -n projectkeystone
```

### Rollback

```bash
# Rollback to previous version
kubectl rollout undo deployment/hmas -n projectkeystone

# Rollback to specific revision
kubectl rollout undo deployment/hmas -n projectkeystone --to-revision=2

# Verify rollback
kubectl rollout status deployment/hmas -n projectkeystone
```

---

## Monitoring Stack (Phase 6.7 M3)

### Prometheus and Alertmanager

The monitoring stack includes Prometheus for metrics collection and Alertmanager for alert management.

**Deploy Monitoring Stack**:

```bash
# Deploy Prometheus and alert rules
kubectl apply -f k8s/prometheus.yaml
kubectl apply -f k8s/prometheus-alerts.yaml

# Deploy Alertmanager
kubectl apply -f k8s/alertmanager.yaml

# Verify deployments
kubectl get pods -n projectkeystone | grep -E 'prometheus|alertmanager'
```

**Access Prometheus**:

```bash
# Port-forward Prometheus UI
kubectl port-forward -n projectkeystone svc/prometheus 9090:9090

# Access UI: http://localhost:9090
# View alerts: http://localhost:9090/alerts
# View targets: http://localhost:9090/targets
```

**Access Alertmanager**:

```bash
# Port-forward Alertmanager UI
kubectl port-forward -n projectkeystone svc/alertmanager 9093:9093

# Access UI: http://localhost:9093
# View active alerts: http://localhost:9093/#/alerts
# View silences: http://localhost:9093/#/silences
```

**Alert Configuration**:

Alertmanager is configured with routing for:

- **Critical alerts** - Grouped and sent every 1 hour
- **Warning alerts** - Grouped and sent every 6 hours
- **SLO violations** - Dedicated channel for SLA tracking
- **Infrastructure alerts** - Pod/node issues
- **Monitoring alerts** - Stack health

To configure notification channels (Slack, PagerDuty), edit `k8s/alertmanager.yaml` and update the receiver
configurations.

**Available Alert Rules**:

- `HMASPodsDown` - Critical: Pod down for >1 minute
- `HighErrorRate` - Critical: Deadline miss rate >10/sec
- `HighMessageLatency` - Warning: Latency >5ms
- `HighQueueDepth` - Warning: Queue >1000 messages
- `CriticalWorkerUtilization` - Critical: >95% utilization
- `PodRestartLoop` - Warning: Frequent restarts
- `PodOOMKilled` - Critical: Out of memory
- `SLOAvailabilityViolation` - Critical: <99.9% uptime
- `PrometheusDown` - Critical: Monitoring stack down

See `k8s/prometheus-alerts.yaml` for complete list.

---

## Security (Phase 6.8 M4)

### Metrics Security

ProjectKeystone implements comprehensive security for metrics endpoints including authentication, encryption, and access
control.

**Security Layers**:

1. **Network Isolation**: NetworkPolicy restricts access to Prometheus pods only
2. **Authentication**: HTTP Basic Auth with bcrypt-hashed passwords
3. **Encryption**: TLS 1.2+ for metrics in transit
4. **Authorization**: RBAC with principle of least privilege
5. **Secrets Management**: Kubernetes secrets for credentials and certificates

**Deploy Metrics Security**:

```bash
# Generate authentication credentials
htpasswd -nBc auth prometheus

# Generate TLS certificate (development)
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout tls.key -out tls.crt \
  -subj "/CN=hmas-metrics.projectkeystone.svc.cluster.local"

# Create secrets
kubectl create secret generic prometheus-scrape-credentials \
  --from-file=htpasswd=auth -n projectkeystone

kubectl create secret tls metrics-tls \
  --cert=tls.crt --key=tls.key -n projectkeystone

# Deploy security configuration
kubectl apply -f k8s/metrics-security.yaml

# Restart Prometheus to pick up changes
kubectl rollout restart deployment/prometheus -n projectkeystone
```

**Access Secured Metrics**:

```bash
# Test with credentials
curl -k -u prometheus:PASSWORD https://hmas:9443/metrics

# Expected: Prometheus metrics output
```

**Security Features**:

- **TLS 1.2+ encryption** for metrics scraping
- **Basic Auth** with bcrypt password hashing
- **NetworkPolicy** restricts metrics port access
- **RBAC** limits Prometheus ServiceAccount permissions
- **PodSecurityPolicy** enforces security standards
- **Security headers** (HSTS, X-Frame-Options, CSP)

For detailed security configuration, see [METRICS_SECURITY.md](METRICS_SECURITY.md).

---

## Next Steps

### Phase 6.2: Helm Chart

- Template Kubernetes manifests
- Version management
- Easy upgrades

### Phase 6.3: Prometheus Monitoring

- Full metrics implementation
- Prometheus integration
- Grafana dashboards

### Phase 6.4: Centralized Logging

- Loki log aggregation
- Structured logging
- Log retention policies

---

## Architecture Notes

### Current Limitations (Phase 6.1)

1. **No persistent storage**: Pods are stateless
2. **Simple health checks**: Python-based HTTP server
3. **Placeholder metrics**: Will be replaced in Phase 6.3
4. **Single-node optimized**: Distributed features in Phase 8

### Future Enhancements

- **Phase 6.3**: Real Prometheus metrics exporter
- **Phase 7**: AI-powered agents
- **Phase 8**: Distributed multi-node cluster with gRPC
- **Phase 10**: Production hardening (TLS, auth, security)

---

## Support

For issues or questions:

- Check logs: `kubectl logs -n projectkeystone deployment/hmas`
- Check events: `kubectl get events -n projectkeystone`
- Refer to Phase 6 plan: `docs/plan/PHASE_6_PLAN.md`

---

**Last Updated**: 2025-11-19
**Phase**: 6.1 - Kubernetes Manifests
**Status**: Ready for local testing
