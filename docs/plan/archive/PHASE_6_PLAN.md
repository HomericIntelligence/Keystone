# Phase 6: Production Deployment Plan

**Status**: 📝 Planning
**Target Start**: 2025-11-20
**Estimated Duration**: 3-4 weeks
**Branch**: TBD (claude/phase-6-production-*)

## Overview

Phase 6 transforms ProjectKeystone from a development prototype to a **production-ready deployment** with containerization, orchestration, monitoring, and operational tooling. This phase builds on the robust foundation of Phases 1-5 and Phase D performance optimizations.

### Current Status (Post-Phase 5 & 9.2-9.5)

**What We Have**:

- ✅ Complete 4-layer HMAS hierarchy (329 tests, 86.2% coverage)
- ✅ Chaos engineering infrastructure (Phase 5)
- ✅ Performance optimizations with simulation (Phase D)
- ✅ Quality gates: fuzz testing, static analysis, benchmarks, CI/CD (Phase 9.2-9.5)
- ✅ Basic Docker containerization (Dockerfile + docker-compose.yml)
- ✅ GitHub Actions CI/CD workflows

**What Phase 6 Adds**:

- Kubernetes deployment manifests (Deployments, Services, ConfigMaps)
- Production-grade monitoring (Prometheus + Grafana)
- Centralized logging (ELK stack or Loki)
- Helm charts for easy deployment
- Production readiness checklist
- Deployment automation and rollback procedures

---

## Phase 6 Architecture

```
┌─────────────────────────────────────────────────────┐
│               Ingress / Load Balancer               │
│         (Route traffic to HMAS instances)           │
└───────────────────┬─────────────────────────────────┘
                    │
    ┌───────────────┴───────────────┐
    │                               │
    ▼                               ▼
┌─────────────────┐           ┌─────────────────┐
│  HMAS Pod #1    │           │  HMAS Pod #2    │
│                 │           │                 │
│  - ChiefArch    │           │  - ChiefArch    │
│  - 4 Components │           │  - 4 Components │
│  - Work Stealing│           │  - Work Stealing│
│  - Metrics      │           │  - Metrics      │
└────────┬────────┘           └────────┬────────┘
         │                             │
         └──────────┬──────────────────┘
                    │
    ┌───────────────┼───────────────┐
    │               │               │
    ▼               ▼               ▼
┌─────────┐   ┌──────────┐   ┌─────────────┐
│Prometheus│  │ Grafana  │   │    Loki     │
│ (Metrics)│  │(Dashboard)│   │  (Logs)     │
└─────────┘   └──────────┘   └─────────────┘
```

---

## Phase 6 Sub-Phases

### Phase 6.1: Kubernetes Manifests (Week 1)

**Goal**: Deploy HMAS to Kubernetes with basic manifests

**Tasks**:

1. **Create Deployment manifest** (`k8s/deployment.yaml`) - 4 hours
   - Define HMAS container spec
   - Resource limits: CPU (1-2 cores), Memory (1-2 GB)
   - Liveness/readiness probes
   - Environment variables for configuration
   - Replica count: 2-3 for HA

2. **Create Service manifest** (`k8s/service.yaml`) - 2 hours
   - ClusterIP service for internal communication
   - Expose metrics endpoint (port 9090)
   - Health check endpoint (port 8080)

3. **Create ConfigMap** (`k8s/configmap.yaml`) - 2 hours
   - HMAS configuration (worker count, timeouts, etc.)
   - Logging levels
   - Feature flags

4. **Create Namespace** (`k8s/namespace.yaml`) - 1 hour
   - `projectkeystone` namespace for isolation
   - Resource quotas

5. **Test local Kubernetes deployment** (kind or minikube) - 3 hours
   - Deploy to local cluster
   - Verify pod startup
   - Test service connectivity
   - Verify logs and metrics

**Deliverables**:

- ✅ Kubernetes manifests for Deployment, Service, ConfigMap, Namespace
- ✅ Successful deployment to local Kubernetes (kind/minikube)
- ✅ Health checks working
- ✅ Documentation: `docs/KUBERNETES_DEPLOYMENT.md`

**Estimated Time**: 12 hours

---

### Phase 6.2: Helm Chart (Week 2)

**Goal**: Create Helm chart for templated, versioned deployments

**Tasks**:

1. **Initialize Helm chart structure** (`helm/projectkeystone/`) - 2 hours
   - `Chart.yaml` - Chart metadata
   - `values.yaml` - Default configuration
   - `templates/` - Templated manifests
   - `README.md` - Usage instructions

2. **Templatize Kubernetes manifests** - 4 hours
   - Convert static manifests to Helm templates
   - Add `values.yaml` parameters:
     - Replica count
     - Resource limits
     - Image tag
     - Environment variables
   - Use Helm helpers for labels and selectors

3. **Add Helm release management** - 3 hours
   - Version tagging (SemVer)
   - Chart versioning in `Chart.yaml`
   - Release notes template

4. **Test Helm deployment** - 3 hours
   - `helm install projectkeystone ./helm/projectkeystone`
   - Verify templating with different values
   - Test upgrade and rollback
   - `helm test` for validation

**Deliverables**:

- ✅ Helm chart in `helm/projectkeystone/`
- ✅ Templated deployments with configurable values
- ✅ Helm tests for validation
- ✅ Documentation: `helm/projectkeystone/README.md`

**Estimated Time**: 12 hours

---

### Phase 6.3: Prometheus Monitoring (Week 2-3)

**Goal**: Export metrics to Prometheus for monitoring

**Tasks**:

1. **Implement Prometheus metrics endpoint** (`src/monitoring/prometheus_exporter.cpp`) - 6 hours
   - HTTP server on port 9090 (using simple HTTP library)
   - Export existing metrics in Prometheus format:
     - `hmas_messages_total{agent_id, priority}` - Counter
     - `hmas_queue_depth{agent_id}` - Gauge
     - `hmas_message_latency_seconds{agent_id}` - Histogram
     - `hmas_steals_total{worker_id}` - Counter
   - Add new metrics:
     - `hmas_agent_state{agent_id, state}` - Gauge (0=idle, 1=active, 2=failed)
     - `hmas_circuit_breaker_state{target_id, state}` - Gauge
     - `hmas_retry_attempts_total{message_id}` - Counter

2. **Prometheus configuration** (`monitoring/prometheus.yml`) - 2 hours
   - Scrape config for HMAS metrics endpoint
   - Retention period: 15 days
   - Alerting rules (optional for Phase 6)

3. **Deploy Prometheus to Kubernetes** - 3 hours
   - Use Prometheus Operator or Helm chart
   - ServiceMonitor for auto-discovery
   - Persistent volume for metrics storage

4. **Create Prometheus queries** - 2 hours
   - Message throughput: `rate(hmas_messages_total[5m])`
   - Queue depth trends: `avg(hmas_queue_depth)`
   - P95 latency: `histogram_quantile(0.95, hmas_message_latency_seconds)`

**Deliverables**:

- ✅ Prometheus exporter integrated into HMAS
- ✅ Prometheus deployed to Kubernetes
- ✅ Metrics visible in Prometheus UI
- ✅ Example queries documented

**Estimated Time**: 13 hours

---

### Phase 6.4: Grafana Dashboards (Week 3)

**Goal**: Visualize HMAS metrics with Grafana dashboards

**Tasks**:

1. **Deploy Grafana to Kubernetes** - 2 hours
   - Helm chart for Grafana
   - Connect to Prometheus data source
   - Persistent volume for dashboard storage

2. **Create HMAS Overview Dashboard** (`monitoring/grafana/hmas-overview.json`) - 4 hours
   - Panels:
     - Messages/sec (time series)
     - Queue depth by agent (heatmap)
     - P50/P95/P99 latency (graph)
     - Active agents (gauge)
     - Circuit breaker states (status panel)
     - Work-stealing efficiency (pie chart)
   - Time range selector (1h, 6h, 24h, 7d)
   - Refresh interval: 30s

3. **Create Agent Detail Dashboard** (`monitoring/grafana/agent-details.json`) - 3 hours
   - Agent selector variable
   - Panels:
     - Message count by priority
     - State transitions (state timeline)
     - Retry attempts distribution
     - Error rate
     - Processing time histogram

4. **Create System Health Dashboard** (`monitoring/grafana/system-health.json`) - 3 hours
   - Panels:
     - CPU/Memory usage (from Kubernetes metrics)
     - Pod restarts
     - Network traffic
     - Disk I/O
     - Failed agents count

5. **Export dashboards as JSON** - 1 hour
   - Save to `monitoring/grafana/*.json`
   - Import via ConfigMap or Grafana provisioning

**Deliverables**:

- ✅ Grafana deployed to Kubernetes
- ✅ 3 dashboards: Overview, Agent Details, System Health
- ✅ Dashboards exported as JSON
- ✅ Documentation: `docs/MONITORING.md`

**Estimated Time**: 13 hours

---

### Phase 6.5: Centralized Logging (Week 3-4)

**Goal**: Aggregate logs from all HMAS pods

**Tasks**:

1. **Choose logging stack** - 1 hour
   - **Option A**: ELK stack (Elasticsearch + Logstash + Kibana)
   - **Option B**: Loki + Promtail + Grafana (lighter weight)
   - **Decision**: Use Loki for consistency with Prometheus/Grafana

2. **Deploy Loki to Kubernetes** - 3 hours
   - Helm chart for Loki
   - Persistent volume for log storage
   - Retention period: 7 days

3. **Deploy Promtail as DaemonSet** - 2 hours
   - Scrape logs from all pods in `projectkeystone` namespace
   - Parse JSON log format from spdlog
   - Add labels: `pod`, `container`, `namespace`

4. **Configure Loki data source in Grafana** - 1 hour
   - Add Loki as data source
   - Test LogQL queries

5. **Create Logs Dashboard** (`monitoring/grafana/logs-dashboard.json`) - 3 hours
   - Panels:
     - Log stream (live tail)
     - Error rate (count by level)
     - Top errors (aggregation)
     - Search box for filtering

6. **Enhance spdlog JSON output** - 3 hours
   - Ensure structured logging (JSON format)
   - Include context fields: `worker_id`, `agent_id`, `trace_id`
   - Timestamp in ISO 8601 format

**Deliverables**:

- ✅ Loki deployed to Kubernetes
- ✅ Promtail scraping HMAS logs
- ✅ Logs visible in Grafana
- ✅ Logs Dashboard created

**Estimated Time**: 13 hours

---

### Phase 6.6: Production Readiness (Week 4)

**Goal**: Finalize production deployment checklist

**Tasks**:

1. **Create production checklist** (`docs/PRODUCTION_READINESS.md`) - 3 hours
   - [ ] All tests passing (329/329)
   - [ ] Code coverage ≥ 95%
   - [ ] Static analysis clean (clang-tidy, cppcheck)
   - [ ] Fuzz testing (1 hour, no crashes)
   - [ ] Performance benchmarks within targets
   - [ ] Liveness/readiness probes configured
   - [ ] Resource limits set
   - [ ] Monitoring and alerting operational
   - [ ] Logging centralized and queryable
   - [ ] Rollback procedure documented
   - [ ] Incident response runbook created

2. **Document deployment procedures** (`docs/DEPLOYMENT.md`) - 4 hours
   - Prerequisites (kubectl, Helm, access)
   - Step-by-step deployment guide
   - Configuration options
   - Verification steps
   - Troubleshooting guide

3. **Document rollback procedures** (`docs/ROLLBACK.md`) - 2 hours
   - `helm rollback` command
   - Verification after rollback
   - Common rollback scenarios

4. **Create incident response runbook** (`docs/INCIDENT_RESPONSE.md`) - 3 hours
   - Pod crash loop: Check logs, restart, scale down
   - High latency: Check queue depths, scale up workers
   - Memory leak: Analyze metrics, restart pods, rollback
   - Circuit breaker open: Investigate target agent, reset if needed

5. **Security hardening** - 3 hours
   - Run as non-root user in container
   - Read-only root filesystem
   - Drop unnecessary capabilities
   - Network policies (optional)
   - Secrets management (if needed)

6. **Performance testing in Kubernetes** - 4 hours
   - Deploy to local cluster
   - Run benchmarks
   - Verify metrics collection
   - Load test with 100+ agents

**Deliverables**:

- ✅ Production readiness checklist
- ✅ Deployment, rollback, and incident response docs
- ✅ Security hardening applied
- ✅ Performance validated in Kubernetes

**Estimated Time**: 19 hours

---

## Success Criteria

### Must Have ✅

- [ ] Kubernetes manifests deploy HMAS successfully
- [ ] Helm chart enables templated deployments
- [ ] Prometheus exports HMAS metrics
- [ ] Grafana dashboards visualize key metrics
- [ ] Loki aggregates logs from all pods
- [ ] Health checks (liveness/readiness) working
- [ ] Resource limits prevent runaway pods
- [ ] Documentation complete (deployment, monitoring, incident response)

### Should Have 🎯

- [ ] Helm tests validate deployments
- [ ] Alerting rules for critical issues (pod restarts, high latency)
- [ ] Rolling updates with zero downtime
- [ ] Horizontal Pod Autoscaler (HPA) based on CPU/memory
- [ ] Production readiness checklist 100% complete

### Nice to Have 💡

- [ ] Multi-cluster deployment (dev, staging, prod)
- [ ] GitOps with ArgoCD or Flux
- [ ] Service mesh (Istio/Linkerd) for advanced networking
- [ ] Distributed tracing (Jaeger/Zipkin)
- [ ] Cost optimization (spot instances, resource right-sizing)

---

## Implementation Checklist

### Phase 6.1: Kubernetes Manifests ✅

- [ ] Create `k8s/namespace.yaml`
- [ ] Create `k8s/deployment.yaml`
- [ ] Create `k8s/service.yaml`
- [ ] Create `k8s/configmap.yaml`
- [ ] Test local deployment (kind/minikube)
- [ ] Document: `docs/KUBERNETES_DEPLOYMENT.md`

### Phase 6.2: Helm Chart ✅

- [ ] Initialize Helm chart structure
- [ ] Create `Chart.yaml`
- [ ] Create `values.yaml`
- [ ] Templatize manifests in `templates/`
- [ ] Add Helm tests
- [ ] Document: `helm/projectkeystone/README.md`

### Phase 6.3: Prometheus Monitoring ✅

- [ ] Implement Prometheus exporter
- [ ] Add metrics endpoint `:9090/metrics`
- [ ] Deploy Prometheus to Kubernetes
- [ ] Create ServiceMonitor
- [ ] Verify metrics scraping
- [ ] Document example queries

### Phase 6.4: Grafana Dashboards ✅

- [ ] Deploy Grafana to Kubernetes
- [ ] Create HMAS Overview dashboard
- [ ] Create Agent Details dashboard
- [ ] Create System Health dashboard
- [ ] Export dashboards as JSON
- [ ] Document: `docs/MONITORING.md`

### Phase 6.5: Centralized Logging ✅

- [ ] Deploy Loki to Kubernetes
- [ ] Deploy Promtail DaemonSet
- [ ] Configure Loki data source in Grafana
- [ ] Create Logs Dashboard
- [ ] Enhance spdlog JSON output
- [ ] Verify log aggregation

### Phase 6.6: Production Readiness ✅

- [ ] Create production readiness checklist
- [ ] Document deployment procedures
- [ ] Document rollback procedures
- [ ] Create incident response runbook
- [ ] Apply security hardening
- [ ] Performance test in Kubernetes

---

## Risk Mitigation

### Risk 1: Kubernetes Complexity

**Impact**: Medium
**Likelihood**: Medium
**Mitigation**: Start with simple manifests, use Helm for templating, test locally with kind/minikube before production.

### Risk 2: Monitoring Overhead

**Impact**: Low
**Likelihood**: Low
**Mitigation**: Prometheus metrics are pull-based (low overhead). Sample metrics at 30s interval.

### Risk 3: Log Volume

**Impact**: Medium
**Likelihood**: Medium
**Mitigation**: Use Loki with retention policy (7 days). Tune log levels (INFO in prod, not DEBUG).

### Risk 4: Resource Limits Too Restrictive

**Impact**: High
**Likelihood**: Medium
**Mitigation**: Start with generous limits (2 CPU, 2 GB RAM), tune based on observed usage.

### Risk 5: Deployment Failures

**Impact**: High
**Likelihood**: Low
**Mitigation**: Test in local Kubernetes first. Use Helm for easy rollback. Document troubleshooting steps.

---

## Performance Expectations

**Resource Usage** (per HMAS pod):

- **CPU**: 0.5-2 cores (depends on worker count and load)
- **Memory**: 512 MB - 2 GB (depends on agent count)
- **Network**: Low (<10 Mbps for metrics/logs)

**Scalability**:

- **Horizontal**: 2-5 replicas with load balancer
- **Vertical**: Tune worker count via ConfigMap (4-16 workers)

**Monitoring Performance**:

- **Metrics scrape interval**: 30s
- **Dashboard refresh**: 30s
- **Log retention**: 7 days
- **Metrics retention**: 15 days

---

## Testing Strategy

### Local Kubernetes Testing (kind/minikube)

1. Deploy HMAS with Helm
2. Verify pod startup and health checks
3. Send test messages, verify processing
4. Check Prometheus metrics endpoint
5. View dashboards in Grafana
6. Query logs in Loki

### Integration Testing

1. Deploy full stack (HMAS + Prometheus + Grafana + Loki)
2. Run E2E tests inside Kubernetes
3. Verify metrics collection
4. Verify log aggregation
5. Simulate pod failure (kill pod, verify restart)
6. Test rolling update (change image tag)

### Load Testing

1. Deploy with multiple replicas
2. Send high message volume
3. Monitor queue depths and latency
4. Verify work-stealing across workers
5. Check for resource saturation

---

## Documentation Plan

### Required Documentation

1. **KUBERNETES_DEPLOYMENT.md** - Kubernetes manifest guide
2. **HELM_CHART.md** - Helm chart usage
3. **MONITORING.md** - Prometheus + Grafana setup
4. **LOGGING.md** - Loki + Promtail setup
5. **DEPLOYMENT.md** - Step-by-step deployment guide
6. **ROLLBACK.md** - Rollback procedures
7. **INCIDENT_RESPONSE.md** - Runbook for common issues
8. **PRODUCTION_READINESS.md** - Checklist before go-live

### README Updates

- Add "Production Deployment" section
- Link to Helm chart
- Link to monitoring dashboards
- Link to operational docs

---

## Next Steps

**Week 1**: Kubernetes manifests and local testing
**Week 2**: Helm chart and Prometheus integration
**Week 3**: Grafana dashboards and Loki logging
**Week 4**: Production readiness and documentation

**After Phase 6**: Move to **Phase 7: AI Integration**

- LLM-based task agents
- Natural language goal processing
- Code generation integration
- AI-powered debugging

---

**Status**: 📝 Planning Complete - Ready for Implementation
**Total Estimated Time**: 82 hours (~3-4 weeks with buffer)
**Dependencies**: Phases 1-5, D, 9.2-9.5 complete
**Last Updated**: 2025-11-19
