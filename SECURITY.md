# Security Policy

## Supported Versions

| Version | Supported          |
|---------|-------------------|
| 0.1.x   | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in ProjectKeystone, please report it responsibly.

**Do NOT open a public GitHub issue for security vulnerabilities.**

### Reporting via GitHub Security Advisories (Recommended)

Private vulnerability reporting via
[GitHub Security Advisories](https://github.com/HomericIntelligence/ProjectKeystone/security/advisories/new)
is **enabled** for this repository. This is the preferred method for reporting vulnerabilities as it provides
a private communication channel between you and the maintainers.

### What to include

When reporting a vulnerability, please provide:

- **Component**: Which component is affected (e.g., `MessageBus`, `NATS client`, `Docker container`)
- **Version**: The affected version of ProjectKeystone (e.g., 0.1.x)
- **Description**: A clear description of the vulnerability
- **Steps to reproduce**: Detailed steps to reproduce the issue
- **Potential impact**: The security impact and severity (e.g., privilege escalation, data leak, DoS)
- **Suggested fix**: Any proposed remediation or patch (if available)

### Response timeline

- **Acknowledgment**: Within 48 hours
- **Initial assessment**: Within 1 week
- **Fix timeline**: Depends on severity, typically within 30 days for critical issues

### Scope

The following are in scope:

- C++ source code in `src/` and `include/`
- Docker/container configurations
- CI/CD pipeline configurations
- Kubernetes deployment manifests

### Out of scope

The following items are explicitly **NOT** covered by this security policy:

- Denial of Service via resource exhaustion (e.g., flooding message queues beyond configured limits)
- Social engineering attacks against maintainers or users
- Vulnerabilities in third-party dependencies already tracked and monitored by Dependabot
- Physical security issues requiring direct access to the host machine or network infrastructure
- Issues in external services or platforms that ProjectKeystone depends on (NATS, BlazingMQ)
- Attacks involving compromised or untrusted dependencies at build time
- Security issues in user code or applications that use ProjectKeystone
- Vulnerabilities in the Tailscale mesh network itself

### Credit policy

Researchers who responsibly disclose security vulnerabilities will be credited in the
release notes and/or security advisory for the fix, unless they prefer to remain
anonymous. Please indicate your preference when reporting.

### Security measures in place

- Static analysis: Semgrep, CodeQL, cppcheck
- Secret scanning: Gitleaks
- Dependency scanning: dependency-review-action, Trivy
- Container scanning: Trivy
- Command injection prevention (whitelist approach in TaskAgent)
- Error message sanitization (no path/address leakage)
- Non-root container execution in production
- Network policies with default-deny in Kubernetes
