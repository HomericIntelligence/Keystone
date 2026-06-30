#include "core/message_bus.hpp"
#include "monitoring/health_check_server.hpp"
#include "monitoring/nats_status.hpp"
#include "network/nats_listener.hpp"
#include "transport/nats_connection.hpp"
#include "transport/transparent_bridge.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_stop{false};

void signalHandler(int /*sig*/) {
  g_stop.store(true, std::memory_order_release);
}

std::string envOr(const char* name, std::string def) {
  const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
  return (v != nullptr && v[0] != '\0') ? std::string(v) : std::move(def);
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

  // Create the local MessageBus for agent routing.
  keystone::core::MessageBus message_bus;

  // -------------------------------------------------------------------------
  // Wire NATSListener into the daemon startup path (Issue #180).
  // Wire NatsConnection into MessageBus for transparent bridge routing
  // (Issues #206, #333).
  //
  // Configuration is drawn from environment variables so the binary stays
  // zero-config by default:
  //   KEYSTONE_NATS_URL      — NATS server URL   (default:
  //   nats://localhost:4222) KEYSTONE_NATS_SUBJECT  — subject pattern (default:
  //   hi.tasks.>) KEYSTONE_NATS_DURABLE  — durable consumer  (default:
  //   keystone-daemon)
  // -------------------------------------------------------------------------

  keystone::transport::NatsConfig nats_cfg;
  nats_cfg.url = envOr("KEYSTONE_NATS_URL", "nats://localhost:4222");

  keystone::network::NATSListenerConfig listener_cfg;
  listener_cfg.subject = envOr("KEYSTONE_NATS_SUBJECT", "hi.tasks.>");
  listener_cfg.durable_name = envOr("KEYSTONE_NATS_DURABLE", "keystone-daemon");
  listener_cfg.max_ack_pending = 1;

  // DAG-advance callback: log the event (production code would call the real
  // DAG advancer once it is wired in from ProjectAgamemnon).
  auto dag_advance = [](std::string_view team_id, std::string_view task_id) {
    std::cout << "keystone-daemon: dag_advance team=" << team_id << " task=" << task_id << '\n';
  };

  keystone::transport::NatsConnection nats_conn(nats_cfg);
  keystone::network::NATSListener listener(listener_cfg, dag_advance);

  // TransparentBridge wires the outbound (MessageBus → NATS) and inbound
  // (NATS → MessageBus) paths automatically (Issue #512).  The bridge must be
  // declared before connect() so its outbound publisher is registered before
  // any routing attempts, and stopped before nats_conn.disconnect().
  keystone::transport::TransparentBridge bridge(message_bus, nats_conn);

  // Wire NatsStatusTracker callbacks into NATS connection lifecycle (Issue
  // #210).
  nats_conn.setDisconnectedCallback([&nats_status]() { nats_status.setDisconnected(); });
  nats_conn.setReconnectedCallback([&nats_status]() { nats_status.setConnected(); });

  // Attempt to connect to NATS; log a warning but continue if unavailable so
  // the health endpoint remains reachable.
  if (nats_conn.connect()) {
    // Connection succeeded — update tracker.
    nats_status.setConnected();

    // attach() wires the outbound publisher and starts the inbound pull loop.
    // jsContext() is called internally by the bridge via conn_.jsContext().
    natsStatus bridge_s = bridge.attach();
    if (bridge_s != NATS_OK) {
      std::cerr << "keystone-daemon: TransparentBridge::attach failed status="
                << static_cast<int>(bridge_s) << " (continuing without bridge)\n";
    } else {
      std::cout << "keystone-daemon: TransparentBridge attached "
                   "subject=hi.agents.>\n";
    }

    jsCtx* js = nats_conn.jsContext();
    if (js != nullptr) {
      natsStatus s = listener.start(js);
      if (s != NATS_OK) {
        std::cerr << "keystone-daemon: NATSListener::start failed status=" << static_cast<int>(s)
                  << " (continuing without NATS)\n";
      } else {
        std::cout << "keystone-daemon: NATSListener active subject=" << listener_cfg.subject
                  << '\n';
      }
    } else {
      std::cerr << "keystone-daemon: failed to obtain JetStream context "
                   "(continuing without NATS)\n";
    }
  } else {
    std::cerr << "keystone-daemon: NATS unavailable at " << nats_cfg.url
              << " (continuing without NATS)\n";
  }

  while (!g_stop.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Graceful shutdown: stop bridge and NATSListener before closing the
  // connection (bridge must stop before nats_conn.disconnect()).
  bridge.stop();
  listener.stop();
  nats_conn.disconnect();

  health_server.stop();
  std::cout << "Keystone daemon stopped.\n";
  return 0;
}
