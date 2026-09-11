#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <nats.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace {
using Json = nlohmann::json;
using namespace std::chrono_literals;

class Child {
 public:
  explicit Child(const std::vector<std::string>& arguments) {
    int input[2];
    int output[2];
    if (pipe(input) != 0 || pipe(output) != 0) {
      throw std::runtime_error("pipe failed");
    }
    for (int descriptor : {input[0], input[1], output[0], output[1]}) {
      if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0) {
        throw std::runtime_error("close-on-exec setup failed");
      }
    }
    std::vector<char*> argv;
    for (const auto& arg : arguments) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
    for (int descriptor : {input[0], input[1], output[0], output[1]}) {
      posix_spawn_file_actions_addclose(&actions, descriptor);
    }
    const int spawned =
        posix_spawnp(&pid_, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(input[0]);
    close(output[1]);
    input_ = input[1];
    output_ = output[0];
    if (spawned != 0) {
      close(input_);
      close(output_);
      throw std::runtime_error("process could not start");
    }
  }
  ~Child() {
    closeInput();
    close(output_);
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      waitpid(pid_, nullptr, 0);
    }
  }
  void closeInput() {
    if (input_ >= 0) {
      close(input_);
      input_ = -1;
    }
  }
  void send(const Json& value) {
    const auto bytes = value.dump() + '\n';
    if (!sendRaw(bytes)) {
      throw std::runtime_error("write failed");
    }
  }
  bool sendRaw(std::string_view bytes) {
    while (!bytes.empty()) {
      auto written = write(input_, bytes.data(), bytes.size());
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
      pollfd descriptor{output_, POLLIN, 0};
      if (poll(&descriptor, 1, 5000) <= 0) {
        throw std::runtime_error("gateway response timed out");
      }
      char character{};
      if (read(output_, &character, 1) != 1) {
        throw std::runtime_error("gateway closed output");
      }
      if (character == '\n') {
        return Json::parse(line);
      }
      line += character;
      if (line.size() > 1024 * 1024) {
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
    waitpid(pid_, &status, 0);
    pid_ = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  std::vector<Json> observations;

 private:
  pid_t pid_{};
  int input_{-1};
  int output_{-1};
};

class FleetGateway : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(std::filesystem::exists(FLEET_GATEWAY_BINARY))
        << "Fleet's allocation attachment executable has not been implemented";
    std::signal(SIGPIPE, SIG_IGN);
    char pattern[] = "/tmp/keystone-fleet-test-XXXXXX";
    auto* path = mkdtemp(pattern);
    ASSERT_NE(path, nullptr);
    directory_ = path;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(socket_fd, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(
        bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)),
        0);
    socklen_t size = sizeof(address);
    ASSERT_EQ(
        getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address), &size),
        0);
    const auto port = std::to_string(ntohs(address.sin_port));
    close(socket_fd);
    url_ = "nats://127.0.0.1:" + port;
    server_ = std::make_unique<Child>(
        std::vector<std::string>{"nats-server", "-js", "-a", "127.0.0.1", "-p",
                                 port, "-sd", directory_});
    for (int attempt = 0; attempt < 100; ++attempt) {
      if (natsConnection_ConnectTo(&connection_, url_.c_str()) == NATS_OK) {
        break;
      }
      std::this_thread::sleep_for(20ms);
    }
    ASSERT_NE(connection_, nullptr) << "isolated nats-server did not start";
    ASSERT_EQ(natsConnection_JetStream(&js_, connection_, nullptr), NATS_OK);
    jsStreamConfig stream;
    jsStreamConfig_Init(&stream);
    stream.Name = "homeric-myrmidon";
    const char* subjects[] = {"hi.myrmidon.>", "hi.fleet.events.>"};
    stream.Subjects = subjects;
    stream.SubjectsLen = 2;
    stream.Storage = js_MemoryStorage;
    ASSERT_EQ(js_AddStream(nullptr, js_, &stream, nullptr, nullptr), NATS_OK);
    jsConsumerConfig consumer;
    jsConsumerConfig_Init(&consumer);
    consumer.Durable = "agent-one";
    consumer.FilterSubject = "hi.myrmidon.cpp.contributor.task.*";
    consumer.AckPolicy = js_AckExplicit;
    consumer.AckWait = 500000000;
    consumer.MaxAckPending = 1;
    ASSERT_EQ(js_AddConsumer(nullptr, js_, "homeric-myrmidon", &consumer,
                             nullptr, nullptr),
              NATS_OK);
  }
  void TearDown() override {
    jsCtx_Destroy(js_);
    natsConnection_Destroy(connection_);
    server_.reset();
    if (!directory_.empty()) {
      std::filesystem::remove_all(directory_);
    }
  }
  std::unique_ptr<Child> gateway(const std::string& consumer = "agent-one") {
    return std::make_unique<Child>(std::vector<std::string>{
        FLEET_GATEWAY_BINARY, "--nats-url", url_, "--allow-loopback-test",
        "--stream", "homeric-myrmidon", "--consumer", consumer, "--subject",
        "hi.myrmidon.cpp.contributor.task.*", "--publish-prefix",
        "hi.fleet.events.worker-one", "--worker-id", "worker-one",
        "--generation", "7"});
  }
  void publishWork() {
    const auto bytes = envelope_.dump();
    ASSERT_EQ(
        js_Publish(nullptr, js_, "hi.myrmidon.cpp.contributor.task.task-one",
                   bytes.data(), static_cast<int>(bytes.size()), nullptr,
                   nullptr),
        NATS_OK);
  }
  Json pull(Child& child) { return child.request({{"operation", "pull"}}); }
  std::string directory_;
  std::string url_;
  natsConnection* connection_{};
  jsCtx* js_{};
  std::unique_ptr<Child> server_;
  Json envelope_{{"schema", "hi/fleet/v1"},
                 {"commandId", "command-one"},
                 {"correlationId", "correlation-one"},
                 {"taskId", "task-one"},
                 {"agentId", "agent-one"},
                 {"generation", 7},
                 {"operation", "start"},
                 {"payload", {{"prompt", "DO-NOT-LOG-SECRET"}}}};
};

TEST_F(FleetGateway, DeliversOpaqueEnvelopeAndAcknowledgesOnlyOnRequest) {
  publishWork();
  auto child = gateway();
  const auto response = pull(*child);
  ASSERT_EQ(response.at("ok"), true);
  EXPECT_EQ(Json::parse(response.at("payload").get<std::string>()), envelope_);
  EXPECT_EQ(response.at("subject"),
            "hi.myrmidon.cpp.contributor.task.task-one");
  EXPECT_EQ(response.at("numDelivered"), 1);
  const auto delivery = response.at("deliveryId");
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
  ASSERT_EQ(js_GetConsumerInfo(&info, js_, "homeric-myrmidon", "agent-one",
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
  EXPECT_EQ(observation.at("bytes"), envelope_.dump().size());
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
  std::this_thread::sleep_for(600ms);
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
  Json publish{{"operation", "publish"},
               {"subject", "hi.fleet.events.worker-one"},
               {"payload", envelope_.dump()},
               {"messageId", "event-one"}};
  auto rejected = publish;
  rejected["subject"] = "hi.fleet.events.worker-one-foreign";
  EXPECT_EQ(child->request(rejected).at("ok"), false);
  rejected["subject"] = "hi.fleet.events.worker-one.>";
  EXPECT_EQ(child->request(rejected).at("ok"), false);
  EXPECT_EQ(child->request(publish).at("ok"), true);
  auto duplicate = child->request(publish);
  EXPECT_EQ(duplicate.at("ok"), true);
  EXPECT_EQ(duplicate.at("duplicate"), true);
  EXPECT_EQ(child->observations.back().at("operation"), "publish");
  EXPECT_EQ(child->observations.back().at("messageId"), "event-one");
}

TEST_F(FleetGateway, RejectsMissingConsumerWithoutCreatingOne) {
  auto child = gateway("missing-agent");
  EXPECT_NE(child->finish(), 0);
  jsConsumerInfo* info{};
  EXPECT_EQ(js_GetConsumerInfo(&info, js_, "homeric-myrmidon", "missing-agent",
                               nullptr, nullptr),
            NATS_NOT_FOUND);
  jsConsumerInfo_Destroy(info);
}

TEST_F(FleetGateway, NakPermitsBrokerRedelivery) {
  publishWork();
  auto child = gateway();
  const auto first = pull(*child);
  ASSERT_EQ(first.at("ok"), true);
  EXPECT_EQ(child
                ->request({{"operation", "nak"},
                           {"deliveryId", first.at("deliveryId")}})
                .at("ok"),
            true);
  const auto second = pull(*child);
  ASSERT_EQ(second.at("ok"), true);
  EXPECT_GE(second.at("numDelivered").get<int>(), 2);
}

TEST_F(FleetGateway, InProgressDoesNotCompleteTheMessage) {
  publishWork();
  auto child = gateway();
  const auto first = pull(*child);
  ASSERT_EQ(first.at("ok"), true);
  for (int count = 0; count < 3; ++count) {
    std::this_thread::sleep_for(250ms);
    EXPECT_EQ(child
                  ->request({{"operation", "inProgress"},
                             {"deliveryId", first.at("deliveryId")}})
                  .at("ok"),
              true);
  }
  jsConsumerInfo* info{};
  ASSERT_EQ(js_GetConsumerInfo(&info, js_, "homeric-myrmidon", "agent-one",
                               nullptr, nullptr),
            NATS_OK);
  EXPECT_EQ(info->NumAckPending, 1);
  EXPECT_EQ(info->NumRedelivered, 0);
  jsConsumerInfo_Destroy(info);
  EXPECT_EQ(child
                ->request({{"operation", "ack"},
                           {"deliveryId", first.at("deliveryId")}})
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
  ASSERT_EQ(js_AddConsumer(nullptr, js_, "homeric-myrmidon", &consumer, nullptr,
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
  Json command{{"schema", "hi/keystone/fleet-attach/v1"},
               {"requestId", "request-one"},
               {"operation", "publish"},
               {"subject", "hi.fleet.events.worker-one"},
               {"payload", envelope_.dump()},
               {"messageId", "event-one"}};
  ASSERT_TRUE(child->sendRaw(command.dump()));
  EXPECT_EQ(child->finish(), 2);
  jsStreamInfo* info{};
  ASSERT_EQ(js_GetStreamInfo(&info, js_, "homeric-myrmidon", nullptr, nullptr),
            NATS_OK);
  EXPECT_EQ(info->State.Msgs, 0);
  jsStreamInfo_Destroy(info);
}

TEST_F(FleetGateway, OversizedFrameClosesAttachmentWithoutAcceptingACommand) {
  auto child = gateway();
  static_cast<void>(child->sendRaw(std::string(1024 * 1024 + 1, 'x')));
  EXPECT_EQ(child->finish(), 2);
}

TEST_F(FleetGateway, InvalidJsonDoesNotLeakContentAndSubsequentRequestsWork) {
  publishWork();
  auto child = gateway();
  ASSERT_TRUE(child->sendRaw("{DO-NOT-LOG-SECRET}\n"));
  const auto error = child->receive();
  EXPECT_EQ(error.at("ok"), false);
  EXPECT_EQ(error.dump().find("DO-NOT-LOG-SECRET"), std::string::npos);
  EXPECT_EQ(pull(*child).at("ok"), true);
}

TEST_F(FleetGateway, RejectsConsumerWithoutExplicitAcknowledgments) {
  jsConsumerConfig consumer;
  jsConsumerConfig_Init(&consumer);
  consumer.Durable = "unsafe-agent";
  consumer.FilterSubject = "hi.myrmidon.cpp.contributor.task.*";
  consumer.AckPolicy = js_AckNone;
  ASSERT_EQ(js_AddConsumer(nullptr, js_, "homeric-myrmidon", &consumer, nullptr,
                           nullptr),
            NATS_OK);
  auto child = gateway("unsafe-agent");
  EXPECT_EQ(child->finish(), 2);
}
}  // namespace
