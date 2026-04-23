#include "monitoring/health_check_server.hpp"
#include "monitoring/nats_status.hpp"

#include <atomic>
#include <csignal>
#include <iostream>

namespace {
std::atomic<bool> g_stop{false};

void signalHandler(int /*sig*/) {
  g_stop.store(true, std::memory_order_release);
}
}  // namespace

int main() {
  std::signal(SIGTERM, signalHandler);
  std::signal(SIGINT, signalHandler);

  keystone::monitoring::NatsStatusTracker nats_status;
  keystone::monitoring::HealthCheckServer health_server(8080, nullptr, &nats_status);

  if (!health_server.start()) {
    std::cerr << "keystone-daemon: failed to start health check server\n";
    return 1;
  }

  std::cout << "Keystone daemon started. Health endpoint: "
               "http://0.0.0.0:"
            << health_server.getPort() << "/v1/health\n";

  while (!g_stop.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  health_server.stop();
  std::cout << "Keystone daemon stopped.\n";
  return 0;
}
