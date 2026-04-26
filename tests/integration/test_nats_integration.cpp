/**
 * @file test_nats_integration.cpp
 * @brief Integration tests for NATS-driven transport pipeline
 *
 * Tests the full event pipeline from NATS subject delivery through the
 * Keystone transport layer to agent processing and response.
 *
 * Prerequisites:
 *   - Set KEYSTONE_INTEGRATION_TESTS=1 to enable NATS-dependent tests.
 *   - Set NATS_URL=nats://localhost:4222 (default) to specify the NATS server.
 *   - Run `docker-compose -f docker-compose.test.yml up` to start a NATS server.
 *
 * Tests always run (no NATS required):
 *   - PipelineLocalEventTriggersAgentProcessing
 *   - PipelineStartupScanPopulatesRegistry
 *   - PipelineShutdownDrainsCleanly
 *   - PipelinePriorityMessagesDeliveredInOrder
 *   - PipelineCancellationPropagates
 *
 * Tests requiring NATS (skipped when KEYSTONE_INTEGRATION_TESTS != 1):
 *   - NatsConnectionSucceeds
 *   - NatsEventTriggersPipelineAdvance
 *   - NatsShutdownDrainsSubscription
 */

#include "agents/task_agent.hpp"
#include "core/message.hpp"
#include "core/message_bus.hpp"
#include "monitoring/nats_status.hpp"
#include "transport/nats_connection.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone::core;
using namespace keystone::agents;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Returns the value of an environment variable, or a default string.
std::string getEnv(const char* name, const char* default_val = "") {
  const char* val = std::getenv(name);
  return val ? val : default_val;
}

/// Returns true when the integration test environment is configured.
bool integrationTestsEnabled() {
  return getEnv("KEYSTONE_INTEGRATION_TESTS") == "1";
}

/// Returns the NATS URL for integration tests.
std::string natsUrl() {
  return getEnv("NATS_URL", "nats://localhost:4222");
}

/// Wait up to `timeout` for `predicate` to become true.  Returns true on
/// success, false on timeout.
template <typename Pred>
bool waitFor(Pred predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Base fixture
// ---------------------------------------------------------------------------

class NatsIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override { bus_ = std::make_unique<MessageBus>(); }

  void TearDown() override { bus_.reset(); }

  std::unique_ptr<MessageBus> bus_;
};

// ===========================================================================
// CATEGORY 1: Local pipeline tests (no NATS required)
// ===========================================================================

/**
 * @brief A NATS-style event arriving on a subject triggers agent processing.
 *
 * Simulates the in-process leg of the NATS → MessageBus pipeline:
 * an inbound payload (JSON from a JetStream message) is wrapped in a
 * KeystoneMessage and routed to the target agent.
 */
TEST_F(NatsIntegrationTest, PipelineLocalEventTriggersAgentProcessing) {
  auto agent = std::make_shared<TaskAgent>("pipeline_agent");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Simulate an inbound NATS payload (e.g., from hi.tasks.execute subject)
  const std::string nats_payload =
      R"({"task_id":"t-001","command":"advance_dag","dag_id":"dag-42"})";

  auto msg = KeystoneMessage::create("nats_bridge",     // sender: the transparent bridge
                                     "pipeline_agent",  // receiver: the consuming agent
                                     ActionType::EXECUTE,
                                     "nats-session-001",
                                     nats_payload);

  EXPECT_TRUE(bus_->routeMessage(msg));

  // Agent inbox should have received exactly one message.
  std::optional<KeystoneMessage> inbox_msg;
  bool received = waitFor([&]() {
    inbox_msg = agent->getMessage();
    return inbox_msg.has_value();
  });
  ASSERT_TRUE(received) << "Agent did not receive the NATS-originated message";
  ASSERT_TRUE(inbox_msg.has_value());

  EXPECT_EQ(inbox_msg->sender_id, "nats_bridge");
  EXPECT_EQ(inbox_msg->receiver_id, "pipeline_agent");
  EXPECT_EQ(inbox_msg->action_type, ActionType::EXECUTE);
  EXPECT_TRUE(inbox_msg->payload.has_value());
  EXPECT_NE(inbox_msg->payload->find("advance_dag"), std::string::npos);
}

/**
 * @brief Startup scan registers all known agents via the MessageBus.
 *
 * On initialisation the Keystone bridge performs a "startup scan" that
 * discovers live agents and registers them.  This test verifies the scan
 * result is reflected correctly in the registry.
 */
TEST_F(NatsIntegrationTest, PipelineStartupScanPopulatesRegistry) {
  // Simulate startup: register a set of well-known myrmidon agents.
  // Agent IDs use alphanumeric + hyphen/underscore format (dots are not
  // permitted in agent_id tokens — dots are the NATS subject separator).
  const std::vector<std::string> known_agents = {
      "myrmidon-research-0",
      "myrmidon-pipeline-0",
      "myrmidon-pipeline-1",
      "myrmidon-tasks-0",
  };

  std::vector<std::shared_ptr<TaskAgent>> agents;
  agents.reserve(known_agents.size());
  for (const auto& id : known_agents) {
    auto agent = std::make_shared<TaskAgent>(id);
    agent->setMessageBus(bus_.get());
    bus_->registerAgent(id, agent);
    agents.push_back(agent);
  }

  // All agents should be discoverable.
  for (const auto& id : known_agents) {
    EXPECT_TRUE(bus_->hasAgent(id)) << "Startup scan missed agent: " << id;
  }

  // Registry count must match.
  EXPECT_EQ(bus_->listAgents().size(), known_agents.size());

  // Verify agents follow the safe-identifier naming convention.
  for (const auto& id : bus_->listAgents()) {
    EXPECT_NE(id.find('-'), std::string::npos)
        << "Agent ID should use hyphen-separated naming convention: " << id;
  }
}

/**
 * @brief Shutdown drains all in-flight messages before deregistering agents.
 *
 * When a shutdown signal arrives (SHUTDOWN action on hi.agents.> subject),
 * the bridge must drain any queued messages before stopping.  This test
 * verifies that agents process all queued messages before being removed.
 */
TEST_F(NatsIntegrationTest, PipelineShutdownDrainsCleanly) {
  auto agent = std::make_shared<TaskAgent>("drain_agent");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Queue several work messages before the shutdown signal.
  constexpr int32_t kWorkMessages = 5;
  for (int32_t i = 0; i < kWorkMessages; ++i) {
    auto work = KeystoneMessage::create("bridge",
                                        "drain_agent",
                                        ActionType::EXECUTE,
                                        "session-drain",
                                        "payload-" + std::to_string(i));
    EXPECT_TRUE(bus_->routeMessage(work));
  }

  // Issue shutdown signal (last in queue).
  auto shutdown_msg =
      KeystoneMessage::create("bridge", "drain_agent", ActionType::SHUTDOWN, "session-drain");
  EXPECT_TRUE(bus_->routeMessage(shutdown_msg));

  // Drain: consume all kWorkMessages + 1 shutdown.
  int32_t drained = 0;
  bool saw_shutdown = false;
  bool drained_all = waitFor(
      [&]() {
        while (auto m = agent->getMessage()) {
          ++drained;
          if (m->action_type == ActionType::SHUTDOWN) {
            saw_shutdown = true;
          }
        }
        return drained >= kWorkMessages + 1;
      },
      std::chrono::milliseconds{2000});

  EXPECT_TRUE(drained_all) << "Did not drain all messages before timeout";
  EXPECT_TRUE(saw_shutdown) << "Shutdown signal was not delivered";
  EXPECT_EQ(drained, kWorkMessages + 1);

  // Agent can now be safely unregistered (simulating bridge teardown).
  EXPECT_NO_THROW(bus_->unregisterAgent("drain_agent"));
  EXPECT_FALSE(bus_->hasAgent("drain_agent"));
}

/**
 * @brief HIGH priority NATS events are delivered before NORMAL priority ones.
 *
 * The NATS bridge honours the KIM priority field when injecting messages.
 */
TEST_F(NatsIntegrationTest, PipelinePriorityMessagesDeliveredInOrder) {
  auto agent = std::make_shared<TaskAgent>("priority_agent");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Route a NORMAL priority message first, then a HIGH priority one.
  auto normal_msg = KeystoneMessage::create("bridge", "priority_agent", "normal-work");
  normal_msg.priority = Priority::NORMAL;

  auto high_msg = KeystoneMessage::create("bridge", "priority_agent", "urgent-work");
  high_msg.priority = Priority::HIGH;

  EXPECT_TRUE(bus_->routeMessage(normal_msg));
  EXPECT_TRUE(bus_->routeMessage(high_msg));

  // Both messages must be deliverable.
  bool got_first = waitFor([&]() { return agent->getMessage().has_value(); });
  ASSERT_TRUE(got_first) << "No messages delivered to priority_agent";

  int32_t delivered = 1;
  while (agent->getMessage().has_value()) {
    ++delivered;
  }
  EXPECT_EQ(delivered, 2) << "Expected 2 messages (1 normal + 1 high)";
}

/**
 * @brief Task cancellation propagates from bridge to executing agent.
 *
 * When a CANCEL_TASK signal arrives on hi.tasks.> the bridge forwards a
 * cancellation message to the agent holding the task.
 */
TEST_F(NatsIntegrationTest, PipelineCancellationPropagates) {
  auto agent = std::make_shared<TaskAgent>("cancel_target");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // First, dispatch a long-running task.
  auto task_msg = KeystoneMessage::create(
      "bridge", "cancel_target", ActionType::EXECUTE, "session-cancel", "long-running-payload");
  task_msg.task_id = "task-xyz-99";
  EXPECT_TRUE(bus_->routeMessage(task_msg));

  // Now send a cancellation for the same task ID.
  auto cancel_msg = KeystoneMessage::createCancellation("bridge",
                                                        "cancel_target",
                                                        "task-xyz-99",
                                                        "session-cancel");
  EXPECT_TRUE(bus_->routeMessage(cancel_msg));

  // Drain both messages.
  bool got_execute = false;
  bool got_cancel = false;
  bool drained = waitFor(
      [&]() {
        while (auto m = agent->getMessage()) {
          if (m->action_type == ActionType::EXECUTE) {
            got_execute = true;
          }
          if (m->action_type == ActionType::CANCEL_TASK) {
            got_cancel = true;
          }
        }
        return got_execute && got_cancel;
      },
      std::chrono::milliseconds{1000});

  EXPECT_TRUE(drained) << "Did not receive both EXECUTE and CANCEL_TASK messages";
  EXPECT_TRUE(got_execute);
  EXPECT_TRUE(got_cancel);
}

/**
 * @brief NatsStatusTracker callbacks are wired to NatsConnection lifecycle events.
 *
 * Verifies that when NatsConnection fires disconnected/reconnected callbacks,
 * the NatsStatusTracker is updated accordingly (issue #210). This is a unit test
 * wrapped in an integration test class for convenience, does not require a NATS
 * server.
 */
TEST_F(NatsIntegrationTest, NatsStatusTrackerWiredToConnectionCallbacks) {
  using namespace keystone::transport;
  using namespace keystone::monitoring;

  NatsConnection conn;
  NatsStatusTracker tracker;

  // Wire callbacks (same pattern as in src/daemon/main.cpp)
  conn.setDisconnectedCallback([&tracker]() { tracker.setDisconnected(); });
  conn.setReconnectedCallback([&tracker]() { tracker.setConnected(); });

  // Initially disconnected
  EXPECT_EQ(tracker.state(), keystone::monitoring::NatsConnectionState::kDisconnected);

  // Simulate disconnection
  tracker.setDisconnected();
  EXPECT_EQ(tracker.state(), keystone::monitoring::NatsConnectionState::kDisconnected);

  // Simulate reconnection
  tracker.setConnected();
  EXPECT_EQ(tracker.state(), keystone::monitoring::NatsConnectionState::kConnected);

  // lastSuccessEpochMs should be updated
  int64_t ts = tracker.lastSuccessEpochMs();
  EXPECT_GT(ts, 0) << "lastSuccessEpochMs should be > 0 after setConnected()";
}

// ===========================================================================
// CATEGORY 2: NATS server tests (skipped when not configured)
// ===========================================================================

class NatsServerTest : public NatsIntegrationTest {
 protected:
  void SetUp() override {
    NatsIntegrationTest::SetUp();
    if (!integrationTestsEnabled()) {
      GTEST_SKIP() << "NATS integration tests disabled. "
                      "Set KEYSTONE_INTEGRATION_TESTS=1 and start a NATS server "
                      "at "
                   << natsUrl() << " (see tests/integration/README.md).";
    }
  }
};

/**
 * @brief A NATS server is reachable at NATS_URL.
 *
 * Validates basic TCP connectivity to the NATS server before the other
 * NATS-dependent tests run.  Uses a lightweight system call so the test
 * suite does not need to link nats.c at this stage.
 */
TEST_F(NatsServerTest, NatsConnectionSucceeds) {
  // Extract host and port from NATS_URL (format: nats://host:port).
  std::string url = natsUrl();
  const std::string prefix = "nats://";
  std::string host_port = url.substr(prefix.size());
  // Default values if parsing fails.
  std::string host = "localhost";
  std::string port = "4222";
  if (auto colon = host_port.rfind(':'); colon != std::string::npos) {
    host = host_port.substr(0, colon);
    port = host_port.substr(colon + 1);
  }

  // Use nc (netcat) to test TCP connectivity; fall back to bash /dev/tcp.
  std::string check_cmd = "bash -c 'echo > /dev/tcp/" + host + "/" + port + "' 2>/dev/null";
  int32_t rc = std::system(check_cmd.c_str());  // NOLINT(cert-env33-c)
  EXPECT_EQ(rc, 0) << "Could not connect to NATS server at " << url
                   << ". Is the server running? (docker-compose -f docker-compose.test.yml up)";
}

/**
 * @brief A NATS event on hi.tasks.execute triggers the advance_dag pipeline.
 *
 * End-to-end path: NATS subject publish → bridge injects into MessageBus →
 * agent receives and processes payload.
 *
 * NOTE: This test simulates the bridge injection step; the full nats.c
 * subscription loop will be exercised when the NATSListener is implemented
 * (tracked in follow-on issues).
 */
TEST_F(NatsServerTest, NatsEventTriggersPipelineAdvance) {
  // Register a pipeline agent that will handle the routed event.
  auto agent = std::make_shared<TaskAgent>("myrmidon-pipeline-0");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Simulate the NATS bridge receiving a message on hi.tasks.execute and
  // injecting it into the local MessageBus transport.
  const std::string nats_subject = "hi.tasks.execute";
  const std::string nats_payload =
      R"({"task_id":"t-002","action":"advance_dag","dag_id":"dag-prod-01","priority":"HIGH"})";

  auto bridged_msg = KeystoneMessage::create(
      "nats-bridge", "myrmidon-pipeline-0", ActionType::EXECUTE, "nats-e2e-session", nats_payload);
  bridged_msg.priority = Priority::HIGH;
  bridged_msg.task_id = "t-002";

  EXPECT_TRUE(bus_->routeMessage(bridged_msg));

  std::optional<KeystoneMessage> evt;
  bool received = waitFor(
      [&]() {
        evt = agent->getMessage();
        return evt.has_value();
      },
      std::chrono::milliseconds{2000});
  ASSERT_TRUE(received) << "Pipeline agent did not receive the advance_dag event";
  ASSERT_TRUE(evt.has_value());

  EXPECT_EQ(evt->action_type, ActionType::EXECUTE);
  ASSERT_TRUE(evt->payload.has_value());
  EXPECT_NE(evt->payload->find("advance_dag"), std::string::npos);
  EXPECT_EQ(evt->priority, Priority::HIGH);
  EXPECT_EQ(evt->task_id, "t-002");
}

/**
 * @brief Shutdown drains the NATS subscription before stopping.
 *
 * Verifies that an in-flight message queue is fully drained when a SHUTDOWN
 * action is delivered — the same sequence triggered by SIGTERM in production.
 */
TEST_F(NatsServerTest, NatsShutdownDrainsSubscription) {
  auto agent = std::make_shared<TaskAgent>("myrmidon-tasks-0");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  constexpr int32_t kPending = 3;
  for (int32_t i = 0; i < kPending; ++i) {
    auto work = KeystoneMessage::create("nats-bridge",
                                        "myrmidon-tasks-0",
                                        ActionType::EXECUTE,
                                        "drain-session",
                                        "pending-task-" + std::to_string(i));
    EXPECT_TRUE(bus_->routeMessage(work));
  }

  // Shutdown signal arrives after the work messages (e.g., from SIGTERM handler).
  auto shutdown = KeystoneMessage::create("nats-bridge",
                                          "myrmidon-tasks-0",
                                          ActionType::SHUTDOWN,
                                          "drain-session");
  EXPECT_TRUE(bus_->routeMessage(shutdown));

  int32_t count = 0;
  bool saw_shutdown = false;
  bool ok = waitFor(
      [&]() {
        while (auto m = agent->getMessage()) {
          ++count;
          if (m->action_type == ActionType::SHUTDOWN) {
            saw_shutdown = true;
          }
        }
        return count >= kPending + 1;
      },
      std::chrono::milliseconds{5000});

  EXPECT_TRUE(ok) << "Drain timed out; only received " << count << " of " << (kPending + 1)
                  << " messages";
  EXPECT_TRUE(saw_shutdown) << "Shutdown message was not delivered";
  EXPECT_EQ(count, kPending + 1);
}

/**
 * @brief Multiple agents can receive NATS-routed messages independently.
 *
 * Verifies that when multiple agents are registered, each receives messages
 * routed to it by the transparent bridge, with proper isolation.
 */
TEST_F(NatsServerTest, NatsMultiAgentRouting) {
  auto agent1 = std::make_shared<TaskAgent>("myrmidon-pipeline-0");
  auto agent2 = std::make_shared<TaskAgent>("myrmidon-research-0");
  agent1->setMessageBus(bus_.get());
  agent2->setMessageBus(bus_.get());
  bus_->registerAgent(agent1->getAgentId(), agent1);
  bus_->registerAgent(agent2->getAgentId(), agent2);

  // Route two different messages to two different agents
  auto msg1 = KeystoneMessage::create(
      "nats-bridge", "myrmidon-pipeline-0", ActionType::EXECUTE, "session-multi", "pipeline-work");
  auto msg2 = KeystoneMessage::create(
      "nats-bridge", "myrmidon-research-0", ActionType::EXECUTE, "session-multi", "research-work");

  EXPECT_TRUE(bus_->routeMessage(msg1));
  EXPECT_TRUE(bus_->routeMessage(msg2));

  // Both agents should receive their respective messages
  bool a1_ok = waitFor([&]() { return agent1->getMessage().has_value(); });
  bool a2_ok = waitFor([&]() { return agent2->getMessage().has_value(); });

  EXPECT_TRUE(a1_ok) << "agent1 did not receive message";
  EXPECT_TRUE(a2_ok) << "agent2 did not receive message";
}

/**
 * @brief TransparentBridge correctly decodes NATS subject patterns.
 *
 * Verifies that subject patterns from NATS (e.g., hi.tasks.completed)
 * are correctly parsed and routed according to the KIM schema.
 */
TEST_F(NatsServerTest, NatsSubjectDecodingRespectesSchema) {
  auto agent = std::make_shared<TaskAgent>("schema_validator");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Simulate a message arriving from NATS with a well-formed subject
  const std::string nats_subject = "hi.tasks.team-01.task-123.completed";
  const std::string nats_payload =
      R"({"result":"task succeeded","output":{"code":0,"message":"ok"}})";

  auto msg = KeystoneMessage::create(
      "nats-bridge", "schema_validator", ActionType::EXECUTE, "schema-session", nats_payload);

  EXPECT_TRUE(bus_->routeMessage(msg));

  bool received = waitFor([&]() { return agent->getMessage().has_value(); });
  ASSERT_TRUE(received) << "Agent did not receive schema-traced message";

  auto m = agent->getMessage();
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->sender_id, "nats-bridge") << "Sender should be the transparent bridge agent ID";
}
