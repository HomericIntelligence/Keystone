# Metrics Security Guide

**ProjectKeystone HMAS - Secure Metrics Collection and Access Control**

## Overview

This guide covers security best practices for metrics endpoints in ProjectKeystone HMAS, including authentication,
authorization, encryption, and access control.

**Phase 6.8 M4**: Comprehensive metrics security implementation

---

## Security Layers

### 1. Network Isolation (NetworkPolicy)

**Purpose**: Restrict network access to metrics endpoints at the Kubernetes network layer

**Implementation**: Only Prometheus pods can access HMAS metrics port (9090/9443)

```yaml
# Applied from k8s/metrics-security.yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: hmas-metrics-access
spec:
  podSelector:
    matchLabels:
      app: hmas
  ingress:
    - from:
        - podSelector:
            matchLabels:
              app: prometheus
      ports:
        - protocol: TCP
          port: 9090  # Insecure metrics (internal only)
        - protocol: TCP
          port: 9443  # Secure metrics (TLS + auth)
```

**Benefits**:

- Zero-trust networking: explicit allow-list
- Prevents unauthorized scraping from other namespaces
- Defense-in-depth: additional layer beyond authentication

---

### 2. Authentication (Basic Auth)

**Purpose**: Verify identity of clients accessing metrics

**Implementation**: HTTP Basic Authentication with bcrypt-hashed passwords

**Setup**:

```bash
# Generate htpasswd file with bcrypt
htpasswd -nBc auth prometheus
# Enter strong password when prompted

# Create secret
kubectl create secret generic prometheus-scrape-credentials \
  --from-file=htpasswd=auth \
  -n projectkeystone

# Verify secret
kubectl get secret prometheus-scrape-credentials -n projectkeystone
```

**Prometheus Configuration**:

```yaml
scrape_configs:
  - job_name: 'projectkeystone-hmas-secure'
    scheme: https
    basic_auth:
      username: prometheus
      password_file: /etc/prometheus/secrets/password
    tls_config:
      ca_file: /etc/prometheus/certs/ca.crt
      insecure_skip_verify: false
    static_configs:
      - targets: ['hmas:9443']
```

**Security Considerations**:

- Use strong passwords (20+ characters, random)
- Rotate passwords quarterly
- Store passwords in Kubernetes secrets (not ConfigMaps)
- Use bcrypt hashing (NOT plaintext or MD5)

---

### 3. Encryption (TLS)

**Purpose**: Encrypt metrics data in transit

**Implementation**: TLS 1.2+ with strong cipher suites

**Generate Self-Signed Certificate** (Development):

```bash
# Generate private key and certificate
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout tls.key -out tls.crt \
  -subj "/CN=hmas-metrics.projectkeystone.svc.cluster.local/O=projectkeystone"

# Create TLS secret
kubectl create secret tls metrics-tls \
  --cert=tls.crt \
  --key=tls.key \
  -n projectkeystone

# Verify secret
kubectl describe secret metrics-tls -n projectkeystone
```

**Production Certificate** (cert-manager):

```yaml
# Install cert-manager first
# https://cert-manager.io/docs/installation/

apiVersion: cert-manager.io/v1
kind: Certificate
metadata:
  name: metrics-tls
  namespace: projectkeystone
spec:
  secretName: metrics-tls
  issuerRef:
    name: letsencrypt-prod
    kind: ClusterIssuer
  dnsNames:
    - hmas-metrics.projectkeystone.svc.cluster.local
  duration: 2160h  # 90 days
  renewBefore: 360h  # 15 days
```

**TLS Configuration** (nginx sidecar):

```nginx
ssl_protocols TLSv1.2 TLSv1.3;
ssl_ciphers HIGH:!aNULL:!MD5;
ssl_prefer_server_ciphers on;
ssl_session_cache shared:SSL:10m;
ssl_session_timeout 10m;
```

**Security Considerations**:

- Use TLS 1.2 or higher (disable TLS 1.0/1.1)
- Disable weak cipher suites (MD5, RC4, DES)
- Enable HSTS header
- Rotate certificates before expiration

---

### 4. Authorization (RBAC)

**Purpose**: Limit Prometheus ServiceAccount permissions

**Implementation**: Principle of least privilege with ClusterRole

```yaml
# Read-only access to scrape targets
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: prometheus-metrics-scraper
rules:
  - apiGroups: [""]
    resources:
      - nodes
      - services
      - endpoints
      - pods
    verbs: ["get", "list", "watch"]  # Read-only
  - nonResourceURLs: ["/metrics"]
    verbs: ["get"]
```

**Apply RBAC**:

```bash
# Create ServiceAccount
kubectl apply -f k8s/metrics-security.yaml

# Verify permissions
kubectl auth can-i list pods \
  --as=system:serviceaccount:projectkeystone:prometheus-metrics \
  -n projectkeystone
# Output: yes

kubectl auth can-i delete pods \
  --as=system:serviceaccount:projectkeystone:prometheus-metrics \
  -n projectkeystone
# Output: no (good!)
```

**Security Considerations**:

- Never grant `*` permissions
- Namespace-scope when possible (use Role, not ClusterRole)
- Audit RBAC periodically
- Use separate ServiceAccounts for different components

---

### 5. Secrets Management

**Purpose**: Securely store credentials and certificates

**Best Practices**:

1. **Never commit secrets to Git**:

   ```bash
   # Add to .gitignore
   echo "*.key" >> .gitignore
   echo "*.crt" >> .gitignore
   echo "auth" >> .gitignore
   ```

2. **Use External Secrets Operator** (Production):

   ```yaml
   apiVersion: external-secrets.io/v1beta1
   kind: ExternalSecret
   metadata:
     name: prometheus-credentials
   spec:
     secretStoreRef:
       name: vault-backend
       kind: SecretStore
     target:
       name: prometheus-scrape-credentials
     data:
       - secretKey: password
         remoteRef:
           key: secret/prometheus/scrape-password
   ```

3. **Enable encryption at rest**:

   ```yaml
   # kube-apiserver configuration
   --encryption-provider-config=/etc/kubernetes/encryption-config.yaml
   ```

4. **Rotate secrets regularly**:

   ```bash
   # Quarterly password rotation
   htpasswd -nB prometheus > auth.new
   kubectl create secret generic prometheus-scrape-credentials \
     --from-file=htpasswd=auth.new \
     --dry-run=client -o yaml | kubectl apply -f -

   # Restart Prometheus to pick up new secret
   kubectl rollout restart deployment/prometheus -n projectkeystone
   ```

**Security Considerations**:

- Use Vault, AWS Secrets Manager, or GCP Secret Manager
- Enable Kubernetes encryption at rest
- Audit secret access logs
- Implement secret rotation policies

---

## Deployment Scenarios

### Scenario 1: Internal Metrics (No Encryption)

**Use Case**: Metrics accessed only within Kubernetes cluster

**Configuration**:

- NetworkPolicy: Restrict to Prometheus namespace
- Authentication: None (rely on NetworkPolicy)
- Encryption: None
- Port: 9090 (HTTP)

**Risk Level**: Low (if cluster is trusted)

```yaml
# Prometheus scrape config
scrape_configs:
  - job_name: 'hmas-internal'
    static_configs:
      - targets: ['hmas:9090']
```

---

### Scenario 2: Authenticated Metrics (Basic Auth)

**Use Case**: Metrics accessed by multiple Prometheus instances

**Configuration**:

- NetworkPolicy: Allow from Prometheus namespaces
- Authentication: Basic Auth
- Encryption: None (internal network)
- Port: 9090 (HTTP with auth)

**Risk Level**: Medium

```yaml
# Prometheus scrape config
scrape_configs:
  - job_name: 'hmas-authenticated'
    basic_auth:
      username: prometheus
      password_file: /etc/prometheus/secrets/password
    static_configs:
      - targets: ['hmas:9090']
```

---

### Scenario 3: Fully Secured Metrics (TLS + Basic Auth)

**Use Case**: Production deployment with external Prometheus or Grafana Cloud

**Configuration**:

- NetworkPolicy: Strict allow-list
- Authentication: Basic Auth with bcrypt
- Encryption: TLS 1.2+ with strong ciphers
- Port: 9443 (HTTPS)

**Risk Level**: Low (recommended for production)

```yaml
# Prometheus scrape config
scrape_configs:
  - job_name: 'hmas-secure'
    scheme: https
    basic_auth:
      username: prometheus
      password_file: /etc/prometheus/secrets/password
    tls_config:
      ca_file: /etc/prometheus/certs/ca.crt
      insecure_skip_verify: false
      server_name: hmas-metrics.projectkeystone.svc.cluster.local
    static_configs:
      - targets: ['hmas:9443']
```

---

## Nginx Sidecar Pattern

**Purpose**: Add TLS and authentication to metrics without modifying application

**Deployment**:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: hmas
spec:
  template:
    spec:
      containers:
        # Main HMAS container
        - name: hmas
          image: projectkeystone:latest
          ports:
            - containerPort: 9090  # Insecure metrics (localhost only)

        # Nginx sidecar for security
        - name: metrics-proxy
          image: nginx:1.25-alpine
          ports:
            - containerPort: 9443  # Secure metrics (external)
          volumeMounts:
            - name: nginx-config
              mountPath: /etc/nginx/nginx.conf
              subPath: nginx.conf
            - name: auth-config
              mountPath: /etc/nginx/auth
            - name: tls-certs
              mountPath: /etc/nginx/certs
      volumes:
        - name: nginx-config
          configMap:
            name: metrics-auth-config
        - name: auth-config
          secret:
            secretName: prometheus-scrape-credentials
        - name: tls-certs
          secret:
            secretName: metrics-tls
```

**Benefits**:

- No application code changes required
- Centralized security configuration
- Easy to update/rotate credentials
- Standard nginx/envoy patterns

---

## Security Headers

**Recommended HTTP headers** for metrics endpoints:

```nginx
# Prevent MIME sniffing
add_header X-Content-Type-Options nosniff;

# Prevent clickjacking
add_header X-Frame-Options DENY;

# Enable XSS protection
add_header X-XSS-Protection "1; mode=block";

# Enforce HTTPS
add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;

# Content Security Policy
add_header Content-Security-Policy "default-src 'none'; script-src 'none'";

# Referrer policy
add_header Referrer-Policy "no-referrer";
```

---

## Monitoring and Auditing

### 1. Enable Audit Logging

```yaml
# kube-apiserver audit policy
apiVersion: audit.k8s.io/v1
kind: Policy
rules:
  # Log secret access
  - level: RequestResponse
    resources:
      - group: ""
        resources: ["secrets"]
    namespaces: ["projectkeystone"]

  # Log metrics endpoint access
  - level: Metadata
    nonResourceURLs: ["/metrics*"]
```

### 2. Monitor Failed Authentication

```promql
# Alert on failed basic auth attempts
rate(nginx_http_requests_total{status="401"}[5m]) > 10
```

### 3. Track Certificate Expiration

```promql
# Alert 30 days before expiration
(ssl_certificate_expiry_seconds - time()) / 86400 < 30
```

---

## Quick Reference

### Deploy Metrics Security

```bash
# 1. Generate credentials
htpasswd -nBc auth prometheus

# 2. Generate TLS certificate
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout tls.key -out tls.crt \
  -subj "/CN=hmas-metrics.projectkeystone.svc.cluster.local"

# 3. Create secrets
kubectl create secret generic prometheus-scrape-credentials \
  --from-file=htpasswd=auth -n projectkeystone

kubectl create secret tls metrics-tls \
  --cert=tls.crt --key=tls.key -n projectkeystone

# 4. Deploy security configuration
kubectl apply -f k8s/metrics-security.yaml

# 5. Update Prometheus configuration
kubectl apply -f k8s/prometheus.yaml

# 6. Restart Prometheus
kubectl rollout restart deployment/prometheus -n projectkeystone
```

### Test Secure Metrics

```bash
# Without credentials (should fail)
curl -k https://hmas:9443/metrics
# Expected: 401 Unauthorized

# With credentials (should succeed)
curl -k -u prometheus:PASSWORD https://hmas:9443/metrics
# Expected: Prometheus metrics output

# Verify TLS
openssl s_client -connect hmas:9443 -servername hmas-metrics.projectkeystone.svc.cluster.local
```

---

## Compliance Checklist

### NIST Cybersecurity Framework

- ✅ **Identify**: Metrics endpoints documented and inventoried
- ✅ **Protect**: Authentication, encryption, and access control implemented
- ✅ **Detect**: Audit logging and failed auth monitoring enabled
- ✅ **Respond**: Incident response procedures for credential compromise
- ✅ **Recover**: Secret rotation and certificate renewal procedures

### CIS Kubernetes Benchmark

- ✅ **5.1.5**: Ensure that default service accounts are not used
- ✅ **5.2.1**: Minimize the admission of privileged containers
- ✅ **5.2.2**: Minimize the admission of containers with added capabilities
- ✅ **5.7.1**: Do not admit containers with NET_RAW capability
- ✅ **5.7.3**: Apply Security Context to pods and containers

### OWASP Top 10

- ✅ **A01 Broken Access Control**: RBAC + NetworkPolicy
- ✅ **A02 Cryptographic Failures**: TLS 1.2+ encryption
- ✅ **A07 Identification and Authentication Failures**: Basic Auth with bcrypt
- ✅ **A09 Security Logging and Monitoring Failures**: Audit logs enabled

---

## Troubleshooting

### 401 Unauthorized

**Problem**: Prometheus cannot scrape metrics

**Solutions**:

1. Verify credentials: `kubectl get secret prometheus-scrape-credentials -o yaml`
2. Check password hash: `htpasswd -v auth prometheus`
3. Ensure Prometheus has secret mounted
4. Check nginx logs: `kubectl logs -n projectkeystone <pod> -c metrics-proxy`

### Certificate Errors

**Problem**: TLS handshake failures

**Solutions**:

1. Verify certificate: `openssl x509 -in tls.crt -text -noout`
2. Check expiration: `openssl x509 -in tls.crt -noout -dates`
3. Verify CN matches: `openssl x509 -in tls.crt -noout -subject`
4. Regenerate if needed

### NetworkPolicy Blocking

**Problem**: Prometheus cannot reach HMAS metrics

**Solutions**:

1. Check NetworkPolicy: `kubectl describe networkpolicy hmas-metrics-access`
2. Verify pod labels: `kubectl get pods -n projectkeystone --show-labels`
3. Test connectivity: `kubectl exec -it prometheus-xxx -- wget -O- http://hmas:9090/metrics`
4. Temporarily remove NetworkPolicy for testing

---

## Production Recommendations

1. **Use cert-manager** for automatic certificate renewal
2. **Implement Vault** or cloud-native secrets management
3. **Enable mutual TLS** (mTLS) for zero-trust
4. **Deploy metrics proxy** as sidecar (nginx/envoy)
5. **Audit regularly** with tools like kubescape or kube-bench
6. **Rotate secrets** quarterly or after personnel changes
7. **Monitor failed auth attempts** and alert on anomalies
8. **Document runbooks** for security incidents

---

**Last Updated**: 2025-11-21
**Phase**: 6.8 M4 - Metrics Security
**Status**: Production-ready security configuration
