#!/usr/bin/env bash
# Install pinned native CI tools; verify official archive hashes before extraction.
set -euo pipefail
tool="${1:?tool is required}"
destination="${2:?destination is required}"
architecture="$(dpkg --print-architecture)"
case "$tool:$architecture" in
    uv:amd64)
        repository=astral-sh/uv; version=0.12.1
        asset=uv-x86_64-unknown-linux-gnu.tar.gz
        checksum=90b2f223fb69d19db49e117da601f64978593417988530aa733d456141b4bcbb ;;
    uv:arm64)
        repository=astral-sh/uv; version=0.12.1
        asset=uv-aarch64-unknown-linux-gnu.tar.gz
        checksum=769d373e146692c639b5fbaae33b331c297a32e03d30448772051902df52bbf4 ;;
    gitleaks:amd64)
        repository=gitleaks/gitleaks; version=v8.30.1
        asset=gitleaks_8.30.1_linux_x64.tar.gz
        checksum=551f6fc83ea457d62a0d98237cbad105af8d557003051f41f3e7ca7b3f2470eb ;;
    gitleaks:arm64)
        repository=gitleaks/gitleaks; version=v8.30.1
        asset=gitleaks_8.30.1_linux_arm64.tar.gz
        checksum=e4a487ee7ccd7d3a7f7ec08657610aa3606637dab924210b3aee62570fb4b080 ;;
    nats-server:amd64)
        repository=nats-io/nats-server; version=v2.10.24
        asset=nats-server-v2.10.24-linux-amd64.tar.gz
        checksum=ee6500f364e3a741b496ae0296c04f2a9d53bbaabac457104ac74596b4a59d85 ;;
    nats-server:arm64)
        repository=nats-io/nats-server; version=v2.10.24
        asset=nats-server-v2.10.24-linux-arm64.tar.gz
        checksum=a4ae6c46ef545a13a3214bc35696b2806e05b60742f7ed5b2082d3c2f5af854f ;;
    *) echo "Unsupported CI tool/architecture: $tool:$architecture" >&2; exit 1 ;;
esac

temporary="$(mktemp -d)"
trap 'rm -rf "$temporary"' EXIT
archive="$temporary/$asset"
curl --fail --silent --show-error --location --retry 5 --retry-all-errors \
    --connect-timeout 15 --max-time 180 \
    "https://github.com/$repository/releases/download/$version/$asset" -o "$archive"
printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check
mkdir -p "$destination"
case "$tool" in
    uv)
        member_root="${asset%.tar.gz}"
        tar xzf "$archive" -C "$destination" --strip-components=1 "$member_root/uv" "$member_root/uvx"
        ;;
    nats-server)
        tar xzf "$archive" -C "$destination" --strip-components=1 "${asset%.tar.gz}/nats-server"
        ;;
    gitleaks) tar xzf "$archive" -C "$destination" gitleaks ;;
esac
