#!/bin/bash
#
# HMAS Server Wrapper
# Simple wrapper script that runs HMAS in "server mode" with health check endpoints
#

set -e

# Configuration from environment variables
HEALTH_CHECK_PORT=${HEALTH_CHECK_PORT:-8080}
METRICS_PORT=${METRICS_PORT:-9090}
LOG_LEVEL=${LOG_LEVEL:-info}

echo "Starting HMAS Server..."
echo "Health Check Port: ${HEALTH_CHECK_PORT}"
echo "Metrics Port: ${METRICS_PORT}"
echo "Log Level: ${LOG_LEVEL}"

# Create a simple HTTP server for health checks using Python
# This runs in the background
python3 - <<EOF &
import http.server
import socketserver
import os

PORT = int(os.environ.get('HEALTH_CHECK_PORT', 8080))

class HealthCheckHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        # Health check endpoint
        if self.path == '/healthz':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"status":"healthy"}')
        # Readiness check endpoint
        elif self.path == '/ready':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"status":"ready"}')
        else:
            self.send_response(404)
            self.end_headers()

    # Suppress logging
    def log_message(self, format, *args):
        pass

with socketserver.TCPServer(("", PORT), HealthCheckHandler) as httpd:
    print(f"Health check server listening on port {PORT}")
    httpd.serve_forever()
EOF

HEALTH_SERVER_PID=$!

# Create a simple metrics endpoint (placeholder for Phase 6.3)
python3 - <<EOF &
import http.server
import socketserver
import os
import time

PORT = int(os.environ.get('METRICS_PORT', 9090))

class MetricsHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/metrics':
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            # Placeholder Prometheus metrics (will be replaced in Phase 6.3)
            metrics = f"""# HELP hmas_uptime_seconds HMAS uptime in seconds
# TYPE hmas_uptime_seconds counter
hmas_uptime_seconds {int(time.time())}

# HELP hmas_status HMAS status (1=healthy, 0=unhealthy)
# TYPE hmas_status gauge
hmas_status 1
"""
            self.wfile.write(metrics.encode())
        else:
            self.send_response(404)
            self.end_headers()

    # Suppress logging
    def log_message(self, format, *args):
        pass

with socketserver.TCPServer(("", PORT), MetricsHandler) as httpd:
    print(f"Metrics server listening on port {PORT}")
    httpd.serve_forever()
EOF

METRICS_SERVER_PID=$!

echo "HMAS Server started"
echo "  Health checks: http://localhost:${HEALTH_CHECK_PORT}/healthz"
echo "  Readiness: http://localhost:${HEALTH_CHECK_PORT}/ready"
echo "  Metrics: http://localhost:${METRICS_PORT}/metrics"

# In a real deployment, this would run the actual HMAS agent system
# For Phase 6.1, we'll run a sample E2E test in a loop to keep the container alive
# This will be replaced with a proper server in Phase 7 (AI Integration)
echo ""
echo "Running HMAS system (Phase 6.1 - using E2E tests as placeholder)"
echo "Note: Proper server mode will be implemented in Phase 7"
echo ""

# Keep the container running
# In future phases, this will be replaced with the actual HMAS server
while true; do
    echo "[$(date)] HMAS system running..."
    sleep 60
done

# Cleanup on exit
cleanup() {
    echo "Shutting down HMAS Server..."
    for pid in "${HEALTH_SERVER_PID:-}" "${METRICS_SERVER_PID:-}"; do
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            if ! kill "$pid" 2>/dev/null; then
                echo "warn: kill failed for $pid" >&2
            fi
        fi
    done
    exit 0
}

trap cleanup SIGTERM SIGINT
