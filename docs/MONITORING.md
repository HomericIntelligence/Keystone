# Keystone Monitoring Guide

**Prometheus Integration for HMAS Metrics**

## Overview

Keystone HMAS exports metrics in Prometheus format via an HTTP endpoint on port 9090. This
document describes the available metrics, how to deploy Prometheus, and how to query metrics.

---

## Available Metrics

### Message Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `hmas_messages_total` | Counter | `priority` | Total messages sent by priority (high/normal/low) |
| `hmas_messages_processed_total` | Counter | - | Total messages processed |
| `hmas_messages_per_second` | Gauge | - | Current message throughput |

### Latency Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `hmas_message_latency_microseconds` | Gauge | - | Average message processing latency (µs) |

### Queue Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `hmas_queue_depth_max` | Gauge | - | Maximum queue depth across all agents |

### Worker Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `hmas_worker_utilization_percent` | Gauge | - | Worker utilization percentage (0-100) |

### Deadline Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `hmas_deadline_misses_total` | Counter | - | Total number of deadline misses |
| `hmas_deadline_miss_milliseconds` | Gauge | - | Average deadline miss time (ms) |

### System Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `hmas_uptime_seconds` | Gauge | - | HMAS uptime in seconds |
| `hmas_up` | Gauge | - | Health status (1=up, 0=down) |

---

## Deployment Options

### Option 1: Standalone Prometheus (Recommended for Testing)

Deploy Prometheus using the provided Kubernetes manifests:

```bash
# Deploy Prometheus
kubectl apply -f k8s/prometheus.yaml

# Verify deployment
kubectl get pods -n keystone -l app=prometheus

# Port-forward to access Prometheus UI
kubectl port-forward -n keystone svc/prometheus 9091:9090

# Open in browser
open http://localhost:9091
```

**What This Creates**:

- Prometheus deployment (1 replica)
- ConfigMap with scrape configuration
- Service for Prometheus UI
- RBAC permissions for service discovery

### Option 2: Prometheus Operator (Recommended for Production)

If using Prometheus Operator, deploy the ServiceMonitor:

```bash
# Install Prometheus Operator (if not already installed)
helm install prometheus-operator prometheus-community/kube-prometheus-stack \
  --namespace monitoring \
  --create-namespace

# Deploy ServiceMonitor
kubectl apply -f k8s/servicemonitor.yaml

# Prometheus Operator will automatically discover and scrape HMAS metrics
```

### Option 3: External Prometheus

If running Prometheus outside Kubernetes, add this scrape config to `/etc/prometheus/prometheus.yml`:

```yaml
scrape_configs:
  - job_name: 'keystone-hmas'
    static_configs:
      - targets: ['<hmas-service-ip>:9090']
        labels:
          app: 'hmas'
```

---

## Accessing Metrics

### Direct HTTP Access

Test metrics endpoint directly:

```bash
# Port-forward HMAS metrics port
kubectl port-forward -n keystone svc/hmas 9090:9090

# Fetch metrics
curl http://localhost:9090/metrics
```

**Expected Output**:

```
# HELP hmas_messages_total Total number of messages sent by priority
# TYPE hmas_messages_total counter
hmas_messages_total{priority="high"} 1523
hmas_messages_total{priority="normal"} 8471
hmas_messages_total{priority="low"} 2947

# HELP hmas_messages_processed_total Total number of messages processed
# TYPE hmas_messages_processed_total counter
hmas_messages_processed_total 12941

# HELP hmas_message_latency_microseconds Average message processing latency
# TYPE hmas_message_latency_microseconds gauge
hmas_message_latency_microseconds 342.5

... (more metrics)
```

### Prometheus UI

Access Prometheus UI to query and visualize metrics:

```bash
# Port-forward Prometheus UI
kubectl port-forward -n keystone svc/prometheus 9091:9090

# Open in browser
open http://localhost:9091
```

Navigate to:

- **Graph**: Query and visualize metrics
- **Targets**: View scrape targets and health
- **Alerts**: View active alerts (if configured)

---

## Example Queries (PromQL)

### Message Throughput

```promql
# Messages per second (5-minute rate)
rate(hmas_messages_processed_total[5m])

# Messages per second by priority
rate(hmas_messages_total[5m])

# Total messages sent in last hour
increase(hmas_messages_total[1h])
```

### Latency

```promql
# Current average latency (microseconds)
hmas_message_latency_microseconds

# Latency over time (5-minute average)
avg_over_time(hmas_message_latency_microseconds[5m])

# Latency in milliseconds
hmas_message_latency_microseconds / 1000
```

### Queue Depth

```promql
# Maximum queue depth
hmas_queue_depth_max

# Queue depth over time
hmas_queue_depth_max

# Alert if queue depth > 1000
hmas_queue_depth_max > 1000
```

### Worker Utilization

```promql
# Current worker utilization
hmas_worker_utilization_percent

# Average utilization over last 5 minutes
avg_over_time(hmas_worker_utilization_percent[5m])

# Alert if utilization > 90%
hmas_worker_utilization_percent > 90
```

### Deadline Misses

```promql
# Deadline miss rate (per second)
rate(hmas_deadline_misses_total[5m])

# Total deadline misses in last hour
increase(hmas_deadline_misses_total[1h])

# Average miss time
hmas_deadline_miss_milliseconds
```

### System Health

```promql
# Uptime in hours
hmas_uptime_seconds / 3600

# Health status (1=up, 0=down)
hmas_up

# Alert if HMAS is down
hmas_up == 0
```

---

## Alerting Rules

Create alerting rules in Prometheus for operational monitoring.

**Example**: `monitoring/alerts.yml`

```yaml
groups:
- name: hmas_alerts
  interval: 30s
  rules:
  # High queue depth
  - alert: HMASHighQueueDepth
    expr: hmas_queue_depth_max > 1000
    for: 5m
    labels:
      severity: warning
    annotations:
      summary: "HMAS queue depth is high"
      description: "Queue depth is {{ $value }}, threshold is 1000"

  # Critical queue depth
  - alert: HMASCriticalQueueDepth
    expr: hmas_queue_depth_max > 10000
    for: 2m
    labels:
      severity: critical
    annotations:
      summary: "HMAS queue depth is critical"
      description: "Queue depth is {{ $value }}, threshold is 10000"

  # High worker utilization
  - alert: HMASHighWorkerUtilization
    expr: hmas_worker_utilization_percent > 90
    for: 10m
    labels:
      severity: warning
    annotations:
      summary: "HMAS worker utilization is high"
      description: "Worker utilization is {{ $value }}%, consider scaling up"

  # High latency
  - alert: HMASHighLatency
    expr: hmas_message_latency_microseconds > 5000
    for: 5m
    labels:
      severity: warning
    annotations:
      summary: "HMAS message latency is high"
      description: "Latency is {{ $value }}µs ({{ humanize $value | div 1000 }}ms)"

  # HMAS down
  - alert: HMASDown
    expr: hmas_up == 0
    for: 1m
    labels:
      severity: critical
    annotations:
      summary: "HMAS is down"
      description: "HMAS health check failed"

  # High deadline miss rate
  - alert: HMASHighDeadlineMissRate
    expr: rate(hmas_deadline_misses_total[5m]) > 10
    for: 5m
    labels:
      severity: warning
    annotations:
      summary: "HMAS deadline miss rate is high"
      description: "Deadline misses: {{ $value }}/sec"
```

**Deploy Alerts**:

```bash
# Add rules to Prometheus ConfigMap
kubectl create configmap prometheus-rules \
  --from-file=alerts.yml \
  --namespace=keystone

# Update Prometheus to load rules
# (Requires restart or reload)
```

---

## Grafana Dashboards

Keystone includes three pre-built Grafana dashboards for comprehensive HMAS monitoring:

1. **HMAS Overview** - System-wide metrics and KPIs
2. **HMAS Agent Details** - Detailed agent performance analysis
3. **HMAS System Health** - Infrastructure and reliability monitoring

### Deployment Options

#### Option 1: Kubernetes Deployment (Recommended)

Deploy Grafana with pre-configured dashboards:

```bash
# Deploy Grafana with datasource and dashboard provisioning
kubectl apply -f k8s/grafana.yaml

# Deploy pre-built dashboards
kubectl apply -f k8s/grafana-dashboards-configmap.yaml

# Wait for Grafana to be ready
kubectl wait --for=condition=ready pod -l app=grafana -n keystone --timeout=120s

# Port-forward to access Grafana UI
kubectl port-forward -n keystone svc/grafana 3000:3000

# Open in browser
open http://localhost:3000
# Login: admin / admin (change in production!)
```

**What This Creates**:

- Grafana deployment (1 replica)
- PersistentVolumeClaim (10Gi for dashboards and data)
- ConfigMap for Prometheus datasource (auto-configured)
- ConfigMap for dashboard provisioning
- ConfigMap with 3 pre-built dashboards
- Service for Grafana UI (port 3000)

**Dashboards Auto-Loaded**:
The following dashboards are automatically available after deployment:

- **HMAS Overview** (`/d/hmas-overview`)
- **HMAS Agent Details** (`/d/hmas-agent-details`)
- **HMAS System Health** (`/d/hmas-system-health`)

#### Option 2: Helm Chart (Alternative)

```bash
# Add Grafana Helm repo
helm repo add grafana https://grafana.github.io/helm-charts
helm repo update

# Install Grafana via Helm
helm install grafana grafana/grafana \
  --namespace keystone \
  --set adminPassword=admin \
  --set persistence.enabled=true \
  --set persistence.size=10Gi

# Port-forward to access
kubectl port-forward -n keystone svc/grafana 3000:80

# Open in browser
open http://localhost:3000
# Login: admin / admin
```

### Dashboard Descriptions

#### 1. HMAS Overview Dashboard

**Purpose**: High-level system monitoring and KPIs

**Panels** (10 panels):

- **Message Throughput** (Time Series) - Messages/sec rate over time
- **Average Message Latency** (Gauge) - Current latency with thresholds (green <1ms, yellow <5ms, red >5ms)
- **Messages by Priority** (Stacked Time Series) - High/Normal/Low priority breakdown
- **Worker Utilization** (Gauge) - Worker thread utilization % (warning >70%, critical >90%)
- **Queue Depth** (Time Series) - Max queue depth with 1000-message threshold
- **Deadline Misses** (Time Series) - Deadline violation rate and miss time
- **HMAS Status** (Stat) - Health check status (UP/DOWN)
- **Uptime** (Stat) - System uptime in seconds
- **Total Messages Processed** (Stat) - Cumulative message count
- **Total Deadline Misses** (Stat) - Cumulative deadline violations

**Refresh**: 10 seconds
**Time Range**: Last 1 hour (default)

**Use Cases**:

- Ops dashboard for monitoring system health
- Performance overview for stakeholders
- First-line troubleshooting

#### 2. HMAS Agent Details Dashboard

**Purpose**: Detailed agent performance analysis and debugging

**Panels** (7 panels):

- **Message Processing Latency** (Time Series) - Current and 5-min average latency trends
- **Message Priority Distribution** (Pie Chart) - Visual breakdown of priority distribution
- **Agent Queue Depth** (Bar Chart) - Queue backlog visualization with thresholds
- **Worker Thread Utilization** (Time Series) - Utilization % with 5-min average
- **Latency Heatmap** (State Timeline) - Latency distribution (ready for histogram metrics)
- **Message Rate by Priority** (Stacked Bars) - 1-minute message increments by priority
- **Deadline Performance** (Time Series) - Miss time and rate per minute

**Refresh**: 10 seconds
**Time Range**: Last 1 hour (default)

**Use Cases**:

- Performance optimization and tuning
- Identifying bottlenecks in message processing
- Analyzing priority queue behavior
- Debugging latency issues

#### 3. HMAS System Health Dashboard

**Purpose**: Infrastructure monitoring and reliability metrics

**Panels** (9 panels):

- **HMAS Health Status** (Large Stat) - Visual health indicator (GREEN=UP, RED=DOWN)
- **System Uptime** (Stat) - Uptime with trend graph
- **Total Messages Processed** (Stat) - Cumulative count with trend
- **System Throughput** (Time Series) - 5-min and 1-min message rates
- **Resource Utilization** (Time Series) - Worker utilization with thresholds
- **Deadline Reliability** (Time Series) - Miss rate and hourly totals
- **Queue Health** (Time Series) - Queue depth with warning thresholds
- **Message Counts by Priority** (Multi-Stat) - Current counts color-coded by priority
- **Current Message Latency** (Large Stat) - Latest latency with color thresholds

**Refresh**: 10 seconds
**Time Range**: Last 6 hours (default)

**Use Cases**:

- SRE monitoring and alerting
- Capacity planning
- Reliability tracking (SLO/SLA monitoring)
- Incident response

### Manual Datasource Configuration

If not using auto-provisioned datasource:

1. Navigate to **Configuration** → **Data Sources**
2. Click **Add data source**
3. Select **Prometheus**
4. Configure:
   - **Name**: `Prometheus`
   - **URL**: `http://prometheus:9090` (Kubernetes service)
   - **Scrape interval**: `30s`
   - **HTTP Method**: `POST`
5. Click **Save & Test** (should show "Data source is working")

### Importing Dashboards Manually

If dashboards are not auto-provisioned:

```bash
# Get dashboard JSON files
kubectl cp keystone/<hmas-pod>:/var/lib/grafana/dashboards/hmas-overview.json ./hmas-overview.json

# Or use local files
ls grafana/dashboards/
# hmas-overview.json
# agent-details.json
# system-health.json
```

**Import via UI**:

1. Navigate to **Dashboards** → **Import**
2. Click **Upload JSON file**
3. Select dashboard JSON (e.g., `hmas-overview.json`)
4. Select datasource: `Prometheus`
5. Click **Import**

Repeat for all 3 dashboards.

### Dashboard Customization

All dashboards are editable and customizable:

**Common Customizations**:

- Adjust time ranges (default: 1h or 6h)
- Modify refresh intervals (default: 10s)
- Add alert thresholds
- Clone panels for custom views
- Export dashboards as JSON for version control

**Adding Variables** (future enhancement):

```
$namespace - Kubernetes namespace filter
$pod - Pod name filter
$agent_id - Agent ID filter (when per-agent metrics available)
```

### Example Dashboard Queries

**Messages Per Second**:

```promql
rate(hmas_messages_processed_total[5m])
```

**Latency Over Time (milliseconds)**:

```promql
hmas_message_latency_microseconds / 1000
```

**Queue Depth with Threshold**:

```promql
hmas_queue_depth_max
# Alert expression: hmas_queue_depth_max > 1000
```

**Worker Utilization**:

```promql
hmas_worker_utilization_percent
# Alert expression: hmas_worker_utilization_percent > 90
```

**Deadline Miss Rate**:

```promql
rate(hmas_deadline_misses_total[5m])
# Alert expression: rate(hmas_deadline_misses_total[5m]) > 10
```

---

## Centralized Logging with Loki

Keystone uses **Loki** for centralized log aggregation, providing a unified view of logs
from all HMAS pods alongside metrics in Grafana.

### Architecture

**Components**:

1. **Loki** - Log aggregation system (similar to Prometheus, but for logs)
2. **Promtail** - Log collection agent (runs as DaemonSet on every node)
3. **Grafana** - Unified metrics + logs visualization

**Log Flow**:

```
HMAS Pods → Promtail (collects) → Loki (aggregates) → Grafana (visualizes)
```

### Deployment

#### Deploy Loki

```bash
# Deploy Loki log aggregation server
kubectl apply -f k8s/loki.yaml

# Wait for Loki to be ready
kubectl wait --for=condition=ready pod -l app=loki -n keystone --timeout=120s

# Verify Loki is running
kubectl logs -n keystone -l app=loki --tail=20
```

**What This Creates**:

- Loki deployment (1 replica, Loki 2.9.3)
- PersistentVolumeClaim (50Gi for log storage)
- ConfigMap with Loki configuration
- Service for Loki API (port 3100)
- 31-day log retention policy

#### Deploy Promtail

```bash
# Deploy Promtail DaemonSet for log collection
kubectl apply -f k8s/promtail.yaml

# Verify Promtail is running on all nodes
kubectl get daemonset -n keystone promtail
kubectl get pods -n keystone -l app=promtail

# Check Promtail logs
kubectl logs -n keystone -l app=promtail --tail=20
```

**What This Creates**:

- Promtail DaemonSet (runs on every node)
- ConfigMap with Promtail configuration
- ServiceAccount and RBAC permissions
- ClusterRole for pod discovery

**Promtail Features**:

- Auto-discovers all pods in `keystone` namespace
- Parses log levels (ERROR, WARN, INFO)
- Adds labels: namespace, pod, container, app, component
- Ships logs to Loki in real-time

#### Update Grafana

The Loki datasource is auto-configured in Grafana (k8s/grafana.yaml):

```bash
# If Grafana is already running, restart to pick up new datasource
kubectl rollout restart deployment/grafana -n keystone

# Verify Grafana has Loki datasource
kubectl port-forward -n keystone svc/grafana 3000:3000
# Open http://localhost:3000
# Navigate to Configuration → Data Sources
# Loki should be listed
```

### Logs Dashboard

A pre-built **HMAS Logs** dashboard is included (`/d/hmas-logs`):

**Panels** (8 panels):

1. **HMAS Log Stream** (Live Logs) - Real-time log stream from all pods
2. **Log Volume by Pod** (Time Series) - Log rate per pod
3. **Logs by Level** (Pie Chart) - Distribution of ERROR/WARN/INFO logs
4. **Error Logs** (Log Panel) - Filtered error-only logs
5. **Warning Logs** (Log Panel) - Filtered warning-only logs
6. **Error and Warning Rate** (Time Series) - Trend of errors/warnings over time
7. **Log Volume by Container** (Stacked Bars) - Logs per container
8. **Total Log Volume** (Stat) - Total log count (last hour)

**Features**:

- Live streaming logs (10-second refresh)
- LogQL filtering and search
- Color-coded by log level
- Correlation with metrics (via derived fields)

### LogQL Queries

LogQL is Loki's query language (similar to PromQL).

#### Basic Queries

**All logs from HMAS**:

```logql
{namespace="keystone"}
```

**Logs from specific pod**:

```logql
{namespace="keystone", pod="hmas-deployment-abc123"}
```

**Logs from specific container**:

```logql
{namespace="keystone", container="hmas"}
```

#### Filtering

**Error logs only**:

```logql
{namespace="keystone"} |~ "ERROR"
```

**Warnings and errors**:

```logql
{namespace="keystone"} |~ "ERROR|WARN"
```

**Exclude INFO logs**:

```logql
{namespace="keystone"} != "INFO"
```

**Search for specific text**:

```logql
{namespace="keystone"} |~ "deadline miss"
```

**Case-insensitive search**:

```logql
{namespace="keystone"} |~ "(?i)error"
```

#### Aggregations

**Log rate (logs per second)**:

```logql
rate({namespace="keystone"}[5m])
```

**Log count over time**:

```logql
count_over_time({namespace="keystone"}[5m])
```

**Error rate by pod**:

```logql
sum by (pod) (rate({namespace="keystone"} |~ "ERROR" [5m]))
```

**Top 10 pods by log volume**:

```logql
topk(10, sum by (pod) (count_over_time({namespace="keystone"}[1h])))
```

#### Advanced Queries

**Parse JSON logs**:

```logql
{namespace="keystone"} | json | level="ERROR"
```

**Extract specific field**:

```logql
{namespace="keystone"} | json | latency_us > 1000
```

**Line format (custom output)**:

```logql
{namespace="keystone"} | line_format "{{.pod}} - {{.level}} - {{.msg}}"
```

### Log Retention

**Default Retention**: 31 days (744 hours)

Configure in `k8s/loki.yaml`:

```yaml
limits_config:
  retention_period: 744h  # 31 days

compactor:
  retention_enabled: true
  retention_delete_delay: 2h
```

**Storage Requirements**:

- Average: ~100 MB/day per pod (depends on log verbosity)
- 2 pods × 100 MB/day × 31 days = ~6.2 GB
- 50 GB PVC provides headroom for growth

### Logs + Metrics Correlation

Correlate logs with metrics using Grafana's **Explore** view:

1. Open **Explore** in Grafana
2. Select **Loki** datasource
3. Run log query: `{namespace="keystone"} |~ "ERROR"`
4. Click **Split** and select **Prometheus** datasource
5. Run metric query: `rate(hmas_deadline_misses_total[5m])`
6. Synchronized time ranges show logs and metrics side-by-side

**Derived Fields** (configured):

- Automatically link trace IDs in logs to Prometheus metrics
- Click trace ID in logs → jump to correlated metrics

---

## Troubleshooting

### Metrics Endpoint Not Responding

```bash
# Check if HMAS pods are running
kubectl get pods -n keystone -l app=hmas

# Check pod logs
kubectl logs -n keystone <pod-name>

# Test metrics endpoint directly
kubectl exec -n keystone <pod-name> -- curl localhost:9090/metrics
```

### Prometheus Not Scraping HMAS

```bash
# Check Prometheus targets
kubectl port-forward -n keystone svc/prometheus 9091:9090
open http://localhost:9091/targets

# Check if HMAS service is accessible from Prometheus pod
kubectl exec -n keystone <prometheus-pod> -- wget -O- http://hmas:9090/metrics
```

**Common Issues**:

- Service selector doesn't match pods
- Prometheus annotations missing on pods
- Network policy blocking traffic
- RBAC permissions missing

### No Metrics Data

```bash
# Verify PrometheusExporter is started
# Check HMAS logs for:
# "Prometheus exporter started on port 9090"

# Test metrics generation
kubectl exec -n keystone <pod-name> -- curl localhost:9090/metrics
```

### High Cardinality Warnings

If you see warnings about high cardinality:

- Avoid adding labels with many unique values (e.g., msg_id, timestamps)
- Use aggregations instead of per-message metrics
- Consider using histograms for distributions

---

## Monitoring Stack Integration

**Phase 6.3**: Prometheus Monitoring ✅

- Metrics export from HMAS (port 9090)
- Prometheus deployment with 30s scrape interval
- 20+ metrics: messages, latency, queue depth, worker utilization, deadlines, system health

**Phase 6.4**: Grafana Dashboards ✅

- 3 pre-built dashboards (HMAS Overview, Agent Details, System Health)
- Auto-provisioned Prometheus datasource
- 26 total panels across all dashboards

**Phase 6.5**: Centralized Logging ✅

- Loki log aggregation (31-day retention)
- Promtail DaemonSet for log collection
- HMAS Logs dashboard (8 panels)
- Auto-provisioned Loki datasource
- Logs + metrics correlation

**Complete Monitoring Stack Quick Start**:

```bash
# Deploy Prometheus (metrics)
kubectl apply -f k8s/prometheus.yaml

# Deploy Loki (logs)
kubectl apply -f k8s/loki.yaml
kubectl apply -f k8s/promtail.yaml

# Deploy Grafana (visualization)
kubectl apply -f k8s/grafana.yaml
kubectl apply -f k8s/grafana-dashboards-configmap.yaml

# Access Grafana
kubectl port-forward -n keystone svc/grafana 3000:3000
open http://localhost:3000  # Login: admin/admin
```

**Available Dashboards**:

- `/d/hmas-overview` - System KPIs and health
- `/d/hmas-agent-details` - Performance analysis
- `/d/hmas-system-health` - Infrastructure monitoring
- `/d/hmas-logs` - Centralized logs with filtering

---

## Configuration

### Adjust Scrape Interval

Edit `monitoring/prometheus.yml`:

```yaml
global:
  scrape_interval: 15s  # Increase frequency (default: 30s)
```

### Data Retention

Edit Prometheus deployment args:

```yaml
args:
  - '--storage.tsdb.retention.time=30d'  # Increase from 15d
```

### Resource Limits

Adjust Prometheus resources in `k8s/prometheus.yaml`:

```yaml
resources:
  limits:
    cpu: 2000m      # Increase if needed
    memory: 2Gi     # Increase if needed
```

---

## Performance Considerations

### Metrics Export Overhead

- **HTTP server**: Minimal overhead (~1ms per request)
- **Metric collection**: Lock-free atomics (negligible)
- **Scrape frequency**: 30s default (balance freshness vs load)

### Storage Requirements

**Per metric**:

- ~1-2 bytes per data point
- 30s scrape interval = 2 data points/minute
- 15 days retention = ~43k data points/metric

**Total** (20 metrics):

- ~20 metrics × 43k points × 2 bytes = ~1.7 MB
- Plus overhead: ~5-10 MB total

### Scaling Considerations

- Prometheus can handle 10M+ samples/sec
- HMAS metrics: ~20 samples every 30s = negligible load
- No special tuning needed for current scale

---

## Next Steps

### Phase 6.6: Production Readiness (Coming Next)

The final phase of production deployment:

- Production readiness checklist
- Deployment procedures and runbooks
- Rollback procedures and disaster recovery
- Security hardening (secrets management, RBAC, network policies)
- Performance testing in Kubernetes
- Incident response playbooks
- SLO/SLA definitions

---

**Phase**: 6.5 - Centralized Logging
**Last Updated**: 2025-11-19
**Status**: Complete ✅

**Completed Phases**:

- ✅ Phase 6.1 - Kubernetes Deployment Manifests
- ✅ Phase 6.2 - Helm Chart
- ✅ Phase 6.3 - Prometheus Monitoring
- ✅ Phase 6.4 - Grafana Dashboards
- ✅ Phase 6.5 - Centralized Logging (Loki + Promtail)
