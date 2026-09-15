#!/usr/bin/env bash
# Resolve native release assets and reject corrupt bytes before extraction.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/keystone-tools-test.XXXXXX")"
trap 'rm -rf "$TEST_ROOT"' EXIT
mkdir "$TEST_ROOT/bin"
if command -v gsha256sum > /dev/null 2>&1; then
    CI_REAL_SHA256SUM="$(command -v gsha256sum)"
else
    CI_REAL_SHA256SUM="$(command -v sha256sum)"
fi
export CI_REAL_SHA256SUM
export CI_TOOL_TRACE="$TEST_ROOT/trace"

cat > "$TEST_ROOT/bin/dpkg" <<'DPKG'
#!/usr/bin/env bash
set -euo pipefail
[ "$1" = --print-architecture ]
printf '%s\n' "$CI_TOOL_ARCH"
DPKG
cat > "$TEST_ROOT/bin/curl" <<'CURL'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" > "$CI_TOOL_TRACE.url"
while [ "$#" -gt 0 ]; do
    if [ "$1" = -o ]; then printf 'deliberately corrupt archive\n' > "$2"; exit 0; fi
    shift
done
exit 91
CURL
cat > "$TEST_ROOT/bin/sha256sum" <<'HASH'
#!/usr/bin/env bash
set -euo pipefail
cat > "$CI_TOOL_TRACE.digest"
exec "$CI_REAL_SHA256SUM" "$@" < "$CI_TOOL_TRACE.digest"
HASH
cat > "$TEST_ROOT/bin/tar" <<'TAR'
#!/usr/bin/env bash
set -euo pipefail
touch "$CI_TOOL_TRACE.extracted"
TAR
chmod +x "$TEST_ROOT/bin/"*

failures=0
index=0
while IFS=' ' read -r tool arch asset digest; do
    index=$((index + 1))
    export CI_TOOL_ARCH="$arch"
    rm -f "$CI_TOOL_TRACE.url" "$CI_TOOL_TRACE.digest" "$CI_TOOL_TRACE.extracted"
    if PATH="$TEST_ROOT/bin:$PATH" bash "$ROOT/ci/install-tool.sh" "$tool" "$TEST_ROOT/destination" \
        > "$TEST_ROOT/output" 2>&1; then
        result=0
    else
        result=$?
    fi
    if [ "$result" -ne 0 ] && [ -f "$CI_TOOL_TRACE.url" ] && [ -f "$CI_TOOL_TRACE.digest" ] \
        && grep -qF "/$asset" "$CI_TOOL_TRACE.url" \
        && grep -q "^$digest  " "$CI_TOOL_TRACE.digest" \
        && [ ! -e "$CI_TOOL_TRACE.extracted" ] \
        && grep -q 'FAILED' "$TEST_ROOT/output"; then
        printf 'ok %s - %s %s selects official asset and rejects corruption\n' "$index" "$tool" "$arch"
    else
        printf 'not ok %s - %s %s\n' "$index" "$tool" "$arch"
        cat "$TEST_ROOT/output"
        failures=$((failures + 1))
    fi
done <<'CASES'
uv amd64 uv-x86_64-unknown-linux-gnu.tar.gz 90b2f223fb69d19db49e117da601f64978593417988530aa733d456141b4bcbb
uv arm64 uv-aarch64-unknown-linux-gnu.tar.gz 769d373e146692c639b5fbaae33b331c297a32e03d30448772051902df52bbf4
gitleaks amd64 gitleaks_8.30.1_linux_x64.tar.gz 551f6fc83ea457d62a0d98237cbad105af8d557003051f41f3e7ca7b3f2470eb
gitleaks arm64 gitleaks_8.30.1_linux_arm64.tar.gz e4a487ee7ccd7d3a7f7ec08657610aa3606637dab924210b3aee62570fb4b080
nats-server amd64 nats-server-v2.10.24-linux-amd64.tar.gz ee6500f364e3a741b496ae0296c04f2a9d53bbaabac457104ac74596b4a59d85
nats-server arm64 nats-server-v2.10.24-linux-arm64.tar.gz a4ae6c46ef545a13a3214bc35696b2806e05b60742f7ed5b2082d3c2f5af854f
CASES
printf '1..%s\n' "$index"
exit "$failures"
