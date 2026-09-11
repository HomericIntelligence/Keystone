#include <gtest/gtest.h>

// POSIX process types and macros are part of the C interface.
extern "C" {
#include <fcntl.h>
#include <nats.h>
#include <netinet/in.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <status.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
}

#if defined(__APPLE__)
#include <crt_externs.h>
#endif

#include <array>
#include <bit>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using Json = nlohmann::json;

std::array<int, 2> makePipe() {
  std::array<int, 2> descriptors{};
#if defined(__linux__)
  if (pipe2(descriptors.data(), O_CLOEXEC) != 0) {
    throw std::runtime_error("pipe failed");
  }
#else
  if (pipe(descriptors.data()) != 0) {
    throw std::runtime_error("pipe failed");
  }
  for (const int DESCRIPTOR : descriptors) {
    if (fcntl(DESCRIPTOR, F_SETFD, FD_CLOEXEC) != 0) {
      close(descriptors[0]);
      close(descriptors[1]);
      throw std::runtime_error("close-on-exec setup failed");
    }
  }
#endif
  return descriptors;
}

class Child {
 public:
  explicit Child(std::vector<std::string> arguments) {
    const auto INPUT_PIPE = makePipe();
    std::array<int, 2> output_pipe{};
    try {
      output_pipe = makePipe();
    } catch (...) {
      close(INPUT_PIPE[0]);
      close(INPUT_PIPE[1]);
      throw;
    }
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& arg : arguments) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, INPUT_PIPE[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    for (const int DESCRIPTOR :
         {INPUT_PIPE[0], INPUT_PIPE[1], output_pipe[0], output_pipe[1]}) {
      posix_spawn_file_actions_addclose(&actions, DESCRIPTOR);
    }
#if defined(__APPLE__)
    auto** process_environment = *_NSGetEnviron();
#else
    auto** process_environment = environ;
#endif
    const int SPAWNED = posix_spawnp(&pid, argv[0], &actions, nullptr,
                                     argv.data(), process_environment);
    posix_spawn_file_actions_destroy(&actions);
    close(INPUT_PIPE[0]);
    close(output_pipe[1]);
    input = INPUT_PIPE[1];
    output = output_pipe[0];
    if (SPAWNED != 0) {
      close(input);
      close(output);
      throw std::runtime_error("process could not start");
    }
  }
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;
  Child(Child&&) = delete;
  Child& operator=(Child&&) = delete;
  ~Child() {
    closeInput();
    close(output);
    if (pid > 0) {
      kill(pid, SIGTERM);
      waitpid(pid, nullptr, 0);
    }
  }
  void closeInput() {
    if (input >= 0) {
      close(input);
      input = -1;
    }
  }
  void send(const Json& value) const {
    const auto BYTES = value.dump() + '\n';
    if (!sendRaw(BYTES)) {
      throw std::runtime_error("write failed");
    }
  }
  [[nodiscard]] bool sendRaw(std::string_view bytes) const {
    while (!bytes.empty()) {
      auto written = write(input, bytes.data(), bytes.size());
      if (written <= 0) {
        return false;
      }
      bytes.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
  }
  Json receive() {
    std::string line;
    for (;;) {
      pollfd descriptor{.fd = output, .events = POLLIN, .revents = 0};
      if (poll(&descriptor, 1, 5000) <= 0) {
        throw std::runtime_error("gateway response timed out");
      }
      char character{};
      if (read(output, &character, 1) != 1) {
        throw std::runtime_error("gateway closed output");
      }
      if (character == '\n') {
        return Json::parse(line);
      }
      line += character;
      if (line.size() > 1024UL * 1024UL) {
        throw std::runtime_error("response exceeded frame limit");
      }
    }
  }
  Json request(Json request) {
    request["schema"] = "hi/keystone/fleet-attach/v1";
    request["requestId"] = "request-1";
    send(request);
    for (;;) {
      auto frame = receive();
      if (frame.at("type") == "observation") {
        observations.push_back(frame.at("observation"));
      } else {
        return frame;
      }
    }
  }
  int finish() {
    closeInput();
    int status{};
    waitpid(pid, &status, 0);
    pid = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  std::vector<Json> observations;

 private:
  pid_t pid{};
  int input{-1};
  int output{-1};
};

class FleetGateway : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(std::filesystem::exists(FLEET_GATEWAY_BINARY))
        << "Fleet's allocation attachment executable has not been implemented";
    ASSERT_NE(std::signal(SIGPIPE, SIG_IGN), SIG_ERR);
    auto pattern = std::to_array("/tmp/keystone-fleet-test-XXXXXX");
    auto* path = mkdtemp(pattern.data());
    ASSERT_NE(path, nullptr);
    directory = path;
    const int SOCKET_FD = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(SOCKET_FD, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(
        bind(SOCKET_FD, std::bit_cast<sockaddr*>(&address), sizeof(address)),
        0);
    socklen_t size = sizeof(address);
    ASSERT_EQ(getsockname(SOCKET_FD, std::bit_cast<sockaddr*>(&address), &size),
              0);
    const auto PORT = std::to_string(ntohs(address.sin_port));
    close(SOCKET_FD);
    url = "nats://127.0.0.1:" + PORT;
    server = std::make_unique<Child>(std::vector<std::string>{
        "nats-server", "-js", "-a", "127.0.0.1", "-p", PORT, "-sd", directory});
    for (int attempt = 0; attempt < 100; ++attempt) {
      if (natsConnection_ConnectTo(&connection, url.c_str()) == NATS_OK) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    ASSERT_NE(connection, nullptr) << "isolated nats-server did not start";
    ASSERT_EQ(natsConnection_JetStream(&js, connection, nullptr), NATS_OK);
    jsStreamConfig stream;
    jsStreamConfig_Init(&stream);
    stream.Name = "homeric-myrmidon";
    std::array<const char*, 2> subjects{"hi.myrmidon.>", "hi.fleet.events.>"};
    stream.Subjects = subjects.data();
    stream.SubjectsLen = 2;
    stream.Storage = js_MemoryStorage;
    ASSERT_EQ(js_AddStream(nullptr, js, &stream, nullptr, nullptr), NATS_OK);
    jsConsumerConfig consumer;
    jsConsumerConfig_Init(&consumer);
    consumer.Durable = "agent-one";
    consumer.FilterSubject = "hi.myrmidon.cpp.contributor.task.*";
    consumer.AckPolicy = js_AckExplicit;
    consumer.AckWait = 500000000;
    consumer.MaxAckPending = 1;
    ASSERT_EQ(js_AddConsumer(nullptr, js, "homeric-myrmidon", &consumer,
                             nullptr, nullptr),
              NATS_OK);
  }
  void TearDown() override {
    jsCtx_Destroy(js);
    natsConnection_Destroy(connection);
    server.reset();
    if (!directory.empty()) {
      std::filesystem::remove_all(directory);
    }
  }
  std::unique_ptr<Child> gateway(const std::string& consumer = "agent-one") {
    return std::make_unique<Child>(std::vector<std::string>{
        FLEET_GATEWAY_BINARY, "--nats-url", url, "--allow-loopback-test",
        "--stream", "homeric-myrmidon", "--consumer", consumer, "--subject",
        "hi.myrmidon.cpp.contributor.task.*", "--publish-prefix",
        "hi.fleet.events.worker-one", "--worker-id", "worker-one",
        "--generation", "7"});
  }
  void publishWork() {
    const auto BYTES = envelope.dump();
    ASSERT_EQ(
        js_Publish(nullptr, js, "hi.myrmidon.cpp.contributor.task.task-one",
                   BYTES.data(), static_cast<int>(BYTES.size()), nullptr,
                   nullptr),
        NATS_OK);
  }
  static Json pull(Child& child) {
    return child.request({{"operation", "pull"}});
  }
  std::string directory;
  std::string url;
  natsConnection* connection{};
  jsCtx* js{};
  std::unique_ptr<Child> server;
  Json envelope{{"schema", "hi/fleet/v1"},
                {"commandId", "command-one"},
                {"correlationId", "correlation-one"},
                {"taskId", "task-one"},
                {"agentId", "agent-one"},
                {"generation", 7},
                {"operation", "start"},
                {"payload", {{"prompt", "DO-NOT-LOG-SECRET"}}}};
};

TEST_F(FleetGateway, AcceptsBrokerEndpointFromStartupEnvironment) {
  publishWork();
  Child child({"/usr/bin/env", "KEYSTONE_NATS_URL=" + url, FLEET_GATEWAY_BINARY,
               "--allow-loopback-test", "--stream", "homeric-myrmidon",
               "--consumer", "agent-one", "--subject",
               "hi.myrmidon.cpp.contributor.task.*", "--publish-prefix",
               "hi.fleet.events.worker-one", "--worker-id", "worker-one",
               "--generation", "7"});
  const auto RESPONSE = pull(child);
  ASSERT_EQ(RESPONSE.at("ok"), true);
  EXPECT_EQ(Json::parse(RESPONSE.at("payload").get<std::string>()), envelope);
  EXPECT_EQ(child
                .request({{"operation", "ack"},
                          {"deliveryId", RESPONSE.at("deliveryId")}})
                .at("confirmation"),
            "broker");
}

TEST_F(FleetGateway, DeliversOpaqueEnvelopeAndAcknowledgesOnlyOnRequest) {
  publishWork();
  auto child = gateway();
  const auto RESPONSE = pull(*child);
  ASSERT_EQ(RESPONSE.at("ok"), true);
  EXPECT_EQ(Json::parse(RESPONSE.at("payload").get<std::string>()), envelope);
  EXPECT_EQ(RESPONSE.at("subject"),
            "hi.myrmidon.cpp.contributor.task.task-one");
  EXPECT_EQ(RESPONSE.at("numDelivered"), 1);
  const auto& delivery = RESPONSE.at("deliveryId");
  EXPECT_EQ(pull(*child).at("ok"), false);
  EXPECT_EQ(child->request({{"operation", "ack"}, {"deliveryId", "foreign"}})
                .at("ok"),
            false);
  EXPECT_EQ(
      child->request({{"operation", "inProgress"}, {"deliveryId", delivery}})
          .at("ok"),
      true);
  EXPECT_EQ(
      child->request({{"operation", "ack"}, {"deliveryId", delivery}}).at("ok"),
      true);
  jsConsumerInfo* info{};
  ASSERT_EQ(js_GetConsumerInfo(&info, js, "homeric-myrmidon", "agent-one",
                               nullptr, nullptr),
            NATS_OK);
  EXPECT_EQ(info->NumAckPending, 0);
  jsConsumerInfo_Destroy(info);
  ASSERT_GE(child->observations.size(), 3);
  const auto& observation = child->observations.front();
  EXPECT_EQ(observation.at("operation"), "deliver");
  EXPECT_EQ(observation.at("messageId"), "command-one");
  EXPECT_EQ(observation.at("taskId"), "task-one");
  EXPECT_EQ(observation.at("workerId"), "worker-one");
  EXPECT_EQ(observation.at("source"), "Agamemnon");
  EXPECT_EQ(observation.at("target"), "Hephaestus");
  EXPECT_FALSE(observation.at("observedAt").get<std::string>().empty());
  EXPECT_EQ(Json(child->observations).dump().find("DO-NOT-LOG-SECRET"),
            std::string::npos);
  EXPECT_EQ(observation.at("bytes"), envelope.dump().size());
  EXPECT_FALSE(child->observations.back().contains("bytes"))
      << "ACK wire size was not measured and must not reuse payload length";
  EXPECT_EQ(child->finish(), 0);
}

TEST_F(FleetGateway, DetachDoesNotAckAndReconnectUsesTheSameDurable) {
  publishWork();
  auto first = gateway();
  auto delivered = pull(*first);
  ASSERT_EQ(delivered.at("ok"), true);
  EXPECT_EQ(first->finish(), 0);
  first.reset();
  std::this_thread::sleep_for(std::chrono::milliseconds{600});
  auto second = gateway();
  auto redelivered = pull(*second);
  ASSERT_EQ(redelivered.at("ok"), true);
  EXPECT_GE(redelivered.at("numDelivered").get<int>(), 2);
  EXPECT_NE(redelivered.at("deliveryId"), delivered.at("deliveryId"));
  EXPECT_EQ(second->observations.front().at("operation"), "redeliver");
  EXPECT_EQ(second
                ->request({{"operation", "ack"},
                           {"deliveryId", delivered.at("deliveryId")}})
                .at("ok"),
            false);
  EXPECT_EQ(second
                ->request({{"operation", "ack"},
                           {"deliveryId", redelivered.at("deliveryId")}})
                .at("ok"),
            true);
}

TEST_F(FleetGateway, RejectsPublishOutsideConfiguredBoundaryAndDeduplicates) {
  auto child = gateway();
  const Json PUBLISH{{"operation", "publish"},
                     {"subject", "hi.fleet.events.worker-one"},
                     {"payload", envelope.dump()},
                     {"messageId", "event-one"}};
  auto rejected = PUBLISH;
  rejected["subject"] = "hi.fleet.events.worker-one-foreign";
  EXPECT_EQ(child->request(rejected).at("ok"), false);
  rejected["subject"] = "hi.fleet.events.worker-one.>";
  EXPECT_EQ(child->request(rejected).at("ok"), false);
  EXPECT_EQ(child->request(PUBLISH).at("ok"), true);
  auto duplicate = child->request(PUBLISH);
  EXPECT_EQ(duplicate.at("ok"), true);
  EXPECT_EQ(duplicate.at("duplicate"), true);
  EXPECT_EQ(child->observations.back().at("operation"), "publish");
  EXPECT_EQ(child->observations.back().at("messageId"), "event-one");
}

TEST_F(FleetGateway, RejectsMissingConsumerWithoutCreatingOne) {
  auto child = gateway("missing-agent");
  EXPECT_NE(child->finish(), 0);
  jsConsumerInfo* info{};
  EXPECT_EQ(js_GetConsumerInfo(&info, js, "homeric-myrmidon", "missing-agent",
                               nullptr, nullptr),
            NATS_NOT_FOUND);
  jsConsumerInfo_Destroy(info);
}

TEST_F(FleetGateway, NakPermitsBrokerRedelivery) {
  publishWork();
  auto child = gateway();
  const auto FIRST = pull(*child);
  ASSERT_EQ(FIRST.at("ok"), true);
  EXPECT_EQ(child
                ->request({{"operation", "nak"},
                           {"deliveryId", FIRST.at("deliveryId")}})
                .at("ok"),
            true);
  const auto SECOND = pull(*child);
  ASSERT_EQ(SECOND.at("ok"), true);
  EXPECT_GE(SECOND.at("numDelivered").get<int>(), 2);
}

TEST_F(FleetGateway, InProgressDoesNotCompleteTheMessage) {
  publishWork();
  auto child = gateway();
  const auto FIRST = pull(*child);
  ASSERT_EQ(FIRST.at("ok"), true);
  for (int count = 0; count < 3; ++count) {
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    EXPECT_EQ(child
                  ->request({{"operation", "inProgress"},
                             {"deliveryId", FIRST.at("deliveryId")}})
                  .at("ok"),
              true);
  }
  jsConsumerInfo* info{};
  ASSERT_EQ(js_GetConsumerInfo(&info, js, "homeric-myrmidon", "agent-one",
                               nullptr, nullptr),
            NATS_OK);
  EXPECT_EQ(info->NumAckPending, 1);
  EXPECT_EQ(info->NumRedelivered, 0);
  jsConsumerInfo_Destroy(info);
  EXPECT_EQ(child
                ->request({{"operation", "ack"},
                           {"deliveryId", FIRST.at("deliveryId")}})
                .at("ok"),
            true);
}

TEST_F(FleetGateway, SeparateLogicalConsumersCanHoldWorkConcurrently) {
  jsConsumerConfig consumer;
  jsConsumerConfig_Init(&consumer);
  consumer.Durable = "agent-two";
  consumer.FilterSubject = "hi.myrmidon.cpp.contributor.task.*";
  consumer.AckPolicy = js_AckExplicit;
  consumer.MaxAckPending = 1;
  ASSERT_EQ(js_AddConsumer(nullptr, js, "homeric-myrmidon", &consumer, nullptr,
                           nullptr),
            NATS_OK);
  publishWork();
  auto first = gateway();
  auto second = gateway("agent-two");
  EXPECT_EQ(pull(*first).at("ok"), true);
  EXPECT_EQ(pull(*second).at("ok"), true);
  // These are separate durable transports. Claim authority must fence duplicate
  // issue execution; consumer identity alone is not a work-claim mechanism.
  EXPECT_EQ(pull(*first).at("ok"), false);
  EXPECT_EQ(pull(*second).at("ok"), false);
}

TEST_F(FleetGateway, IncompleteFrameCannotPublishOnEof) {
  auto child = gateway();
  const Json COMMAND{{"schema", "hi/keystone/fleet-attach/v1"},
                     {"requestId", "request-one"},
                     {"operation", "publish"},
                     {"subject", "hi.fleet.events.worker-one"},
                     {"payload", envelope.dump()},
                     {"messageId", "event-one"}};
  ASSERT_TRUE(child->sendRaw(COMMAND.dump()));
  EXPECT_EQ(child->finish(), 2);
  jsStreamInfo* info{};
  ASSERT_EQ(js_GetStreamInfo(&info, js, "homeric-myrmidon", nullptr, nullptr),
            NATS_OK);
  EXPECT_EQ(info->State.Msgs, 0);
  jsStreamInfo_Destroy(info);
}

TEST_F(FleetGateway, OversizedFrameClosesAttachmentWithoutAcceptingACommand) {
  auto child = gateway();
  static_cast<void>(child->sendRaw(std::string((1024 * 1024) + 1, 'x')));
  EXPECT_EQ(child->finish(), 2);
}

TEST_F(FleetGateway, InvalidJsonDoesNotLeakContentAndSubsequentRequestsWork) {
  publishWork();
  auto child = gateway();
  ASSERT_TRUE(child->sendRaw("{DO-NOT-LOG-SECRET}\n"));
  const auto ERROR = child->receive();
  EXPECT_EQ(ERROR.at("ok"), false);
  EXPECT_EQ(ERROR.dump().find("DO-NOT-LOG-SECRET"), std::string::npos);
  EXPECT_EQ(pull(*child).at("ok"), true);
}

TEST_F(FleetGateway, RejectsConsumerWithoutExplicitAcknowledgments) {
  jsConsumerConfig consumer;
  jsConsumerConfig_Init(&consumer);
  consumer.Durable = "unsafe-agent";
  consumer.FilterSubject = "hi.myrmidon.cpp.contributor.task.*";
  consumer.AckPolicy = js_AckNone;
  ASSERT_EQ(js_AddConsumer(nullptr, js, "homeric-myrmidon", &consumer, nullptr,
                           nullptr),
            NATS_OK);
  auto child = gateway("unsafe-agent");
  EXPECT_EQ(child->finish(), 2);
}
}  // namespace
