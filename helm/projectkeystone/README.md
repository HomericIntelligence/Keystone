# Keystone Helm Chart

Helm chart for deploying Keystone HMAS (Hierarchical Multi-Agent System) to Kubernetes.

## Prerequisites

- Kubernetes 1.24+
- Helm 3.8+
- Docker image `keystone:latest` available

## Installing the Chart

### From local chart

```bash
# Install with default values
helm install keystone ./helm/keystone

# Install with custom namespace
helm install keystone ./helm/keystone --namespace keystone --create-namespace

# Install with custom values
helm install keystone ./helm/keystone --values custom-values.yaml
```

### Verify Installation

```bash
# Check deployment status
helm status keystone

# Check pods
kubectl get pods -n keystone

# Check logs
kubectl logs -n keystone -l app.kubernetes.io/name=keystone
```

## Uninstalling the Chart

```bash
# Uninstall the release
helm uninstall keystone --namespace keystone

# Delete namespace (optional)
kubectl delete namespace keystone
```

## Configuration

### Common Configurations

#### Change Replica Count

```bash
helm install keystone ./helm/keystone \
  --set replicaCount=5
```

#### Change Worker Count

```bash
helm install keystone ./helm/keystone \
  --set config.workerCount=8
```

#### Enable Debug Logging

```bash
helm install keystone ./helm/keystone \
  --set config.logLevel=debug
```

#### Adjust Resource Limits

```bash
helm install keystone ./helm/keystone \
  --set resources.requests.cpu=1000m \
  --set resources.requests.memory=1Gi \
  --set resources.limits.cpu=4000m \
  --set resources.limits.memory=4Gi
```

#### Use Different Image Tag

```bash
helm install keystone ./helm/keystone \
  --set image.tag=v1.2.3
```

### Configuration Parameters

The following table lists the configurable parameters of the Keystone chart and their default values.

#### Global Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `namespace.create` | Create the namespace | `true` |
| `namespace.name` | Namespace name | `keystone` |
| `namespace.resourceQuota.enabled` | Enable resource quotas | `true` |

#### Image Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `image.repository` | Image repository | `keystone` |
| `image.tag` | Image tag | `latest` |
| `image.pullPolicy` | Image pull policy | `IfNotPresent` |

#### Deployment Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `replicaCount` | Number of replicas | `2` |
| `updateStrategy.type` | Update strategy type | `RollingUpdate` |
| `terminationGracePeriodSeconds` | Grace period for pod termination | `30` |

#### Service Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `service.type` | Service type | `ClusterIP` |
| `service.ports.health.port` | Health check port | `8080` |
| `service.ports.metrics.port` | Metrics port | `9090` |
| `service.ports.grpc.port` | gRPC port | `50051` |
| `headlessService.enabled` | Enable headless service | `true` |

#### Resource Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `resources.requests.cpu` | CPU requests | `500m` |
| `resources.requests.memory` | Memory requests | `512Mi` |
| `resources.limits.cpu` | CPU limits | `2000m` |
| `resources.limits.memory` | Memory limits | `2Gi` |

#### Probe Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `livenessProbe.enabled` | Enable liveness probe | `true` |
| `livenessProbe.initialDelaySeconds` | Initial delay | `30` |
| `livenessProbe.periodSeconds` | Period | `10` |
| `readinessProbe.enabled` | Enable readiness probe | `true` |
| `readinessProbe.initialDelaySeconds` | Initial delay | `10` |
| `readinessProbe.periodSeconds` | Period | `5` |
| `startupProbe.enabled` | Enable startup probe | `true` |
| `startupProbe.failureThreshold` | Failure threshold | `30` |

#### HMAS Configuration Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `config.workerCount` | Number of worker threads | `"4"` |
| `config.logLevel` | Log level (debug/info/warn/error) | `"info"` |
| `config.heartbeatIntervalMs` | Heartbeat interval in ms | `"1000"` |
| `config.heartbeatTimeoutMs` | Heartbeat timeout in ms | `"5000"` |
| `config.circuitBreakerFailureThreshold` | Circuit breaker failure threshold | `"5"` |
| `config.retryMaxAttempts` | Max retry attempts | `"3"` |
| `config.enableChaosEngineering` | Enable chaos engineering | `"false"` |
| `config.enableAiAgents` | Enable AI agents | `"false"` |

#### Autoscaling Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `autoscaling.enabled` | Enable horizontal pod autoscaler | `false` |
| `autoscaling.minReplicas` | Minimum replicas | `2` |
| `autoscaling.maxReplicas` | Maximum replicas | `10` |
| `autoscaling.targetCPUUtilizationPercentage` | Target CPU utilization | `80` |

## Examples

### Production Deployment

Create a `production-values.yaml`:

```yaml
replicaCount: 5

resources:
  requests:
    cpu: 1000m
    memory: 1Gi
  limits:
    cpu: 4000m
    memory: 4Gi

config:
  workerCount: "8"
  logLevel: "info"
  enableChaosEngineering: "false"

autoscaling:
  enabled: true
  minReplicas: 3
  maxReplicas: 20
  targetCPUUtilizationPercentage: 70

affinity:
  podAntiAffinity:
    requiredDuringSchedulingIgnoredDuringExecution:
    - labelSelector:
        matchExpressions:
        - key: app.kubernetes.io/name
          operator: In
          values:
          - keystone
      topologyKey: kubernetes.io/hostname
```

Install:

```bash
helm install keystone ./helm/keystone \
  --namespace keystone \
  --create-namespace \
  --values production-values.yaml
```

### Development Deployment

Create a `dev-values.yaml`:

```yaml
replicaCount: 1

resources:
  requests:
    cpu: 250m
    memory: 256Mi
  limits:
    cpu: 1000m
    memory: 1Gi

config:
  workerCount: "2"
  logLevel: "debug"
  enableChaosEngineering: "true"
```

Install:

```bash
helm install keystone-dev ./helm/keystone \
  --namespace keystone-dev \
  --create-namespace \
  --values dev-values.yaml
```

## Upgrading

### Upgrade to New Version

```bash
# Upgrade with new image tag
helm upgrade keystone ./helm/keystone \
  --set image.tag=v2.0.0

# Upgrade with new values
helm upgrade keystone ./helm/keystone \
  --values updated-values.yaml

# Check upgrade status
helm status keystone

# View upgrade history
helm history keystone
```

### Rollback

```bash
# Rollback to previous version
helm rollback keystone

# Rollback to specific revision
helm rollback keystone 2

# Verify rollback
helm status keystone
```

## Testing

### Helm Test (Future)

```bash
# Run Helm tests
helm test keystone
```

### Manual Testing

```bash
# Port-forward health check
kubectl port-forward -n keystone svc/keystone 8080:8080

# Test health endpoint
curl http://localhost:8080/healthz

# Test readiness endpoint
curl http://localhost:8080/ready

# Port-forward metrics
kubectl port-forward -n keystone svc/keystone 9090:9090

# Test metrics endpoint
curl http://localhost:9090/metrics
```

## Debugging

### View Logs

```bash
# All pods
kubectl logs -n keystone -l app.kubernetes.io/name=keystone --all-containers=true

# Specific pod
kubectl logs -n keystone <pod-name>

# Follow logs
kubectl logs -n keystone -f deployment/keystone
```

### Describe Resources

```bash
# Describe deployment
kubectl describe deployment -n keystone keystone

# Describe pods
kubectl describe pods -n keystone

# Check events
kubectl get events -n keystone --sort-by='.lastTimestamp'
```

### Shell Access

```bash
# Get a shell in a pod
kubectl exec -it -n keystone <pod-name> -- /bin/bash
```

## Monitoring Integration

### Prometheus

The chart includes Prometheus annotations for automatic service discovery:

```yaml
podAnnotations:
  prometheus.io/scrape: "true"
  prometheus.io/port: "9090"
  prometheus.io/path: "/metrics"
```

### Service Monitor (Prometheus Operator)

Enable ServiceMonitor:

```yaml
serviceMonitor:
  enabled: true
  interval: 30s
  scrapeTimeout: 10s
```

Then install:

```bash
helm install keystone ./helm/keystone \
  --set serviceMonitor.enabled=true
```

## Common Issues

### Image Pull Errors

If you see `ImagePullBackOff`:

1. Check image exists: `docker images | grep keystone`
2. For local testing with Minikube: `eval $(minikube docker-env)`
3. For kind: `kind load docker-image keystone:latest`

### Pods Not Starting

Check pod events:

```bash
kubectl describe pod -n keystone <pod-name>
```

Common causes:

- Insufficient resources (check resource quotas)
- Image not found
- ConfigMap not created
- Health checks failing too quickly

### Health Checks Failing

Check container logs:

```bash
kubectl logs -n keystone <pod-name>
```

Adjust probe timings if needed:

```yaml
livenessProbe:
  initialDelaySeconds: 60  # Increase if slow startup
readinessProbe:
  initialDelaySeconds: 30
startupProbe:
  failureThreshold: 60  # 60 * 5s = 300s max startup
```

## Contributing

For issues or improvements, please contribute to the main repository.

## License

See the main Keystone repository for license information.

---

**Chart Version**: 0.1.0
**App Version**: 1.0.0
**Last Updated**: 2025-11-19
