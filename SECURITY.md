# Security Policy

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| 0.1.x   | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in ProjectKeystone, please report it responsibly.

**Do NOT open a public GitHub issue for security vulnerabilities.**

Instead, please report vulnerabilities by emailing the maintainers directly or by using
[GitHub's private vulnerability reporting](https://github.com/HomericIntelligence/ProjectKeystone/security/advisories/new).

### What to include

- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

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
