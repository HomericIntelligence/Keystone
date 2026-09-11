#include <nats.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using Json = nlohmann::json;
constexpr std::string_view kSchema = "hi/keystone/fleet-attach/v1";
constexpr std::size_t kMaxFrame = 1024 * 1024;
constexpr std::size_t kMaxPayload = 128 * 1024;

template <class T, auto Destroy>
using Handle = std::unique_ptr<T, decltype(Destroy)>;

void check(natsStatus status) {
  if (status != NATS_OK) {
    // Never include NATS error-stack text: it can contain a URL/credential.
    throw std::runtime_error("broker_status_" + std::to_string(status));
  }
}

bool identifier(std::string_view value) {
  return !value.empty() && value.size() <= 128 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':' ||
                  c == '.';
         });
}

bool subject(std::string_view value, bool filter = false) {
  if (value.size() > 256 || !value.starts_with("hi.")) {
    return false;
  }
  while (!value.empty()) {
    const auto end = value.find('.');
    const auto token = value.substr(0, end);
    if (!(filter && token == "*") &&
        (!identifier(token) || token.find(':') != std::string_view::npos)) {
      return false;
    }
    if (end == std::string_view::npos) {
      return true;
    }
    value.remove_prefix(end + 1);
  }
  return false;
}

std::string env(const char* key) {
  const char* value = std::getenv(key);
  return value == nullptr ? "" : value;
}

std::string text(const Json& object, const char* key) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    throw std::runtime_error("invalid_request");
  }
  return object.at(key).get<std::string>();
}

struct Config {
  std::string url;
  std::string stream;
  std::string consumer;
  std::string filter;
  std::string publish_prefix;
  std::string worker;
  std::uint64_t generation{};
  bool loopback_test{};
};

Config config(int argc, char** argv) {
  Config result;
  result.url = env("KEYSTONE_NATS_URL");
  for (int i = 1; i < argc; ++i) {
    std::string_view key = argv[i];
    if (key == "--allow-loopback-test") {
      result.loopback_test = true;
      continue;
    }
    if (++i == argc) {
      throw std::runtime_error("missing_option_value");
    }
    const std::string value = argv[i];
    if (key == "--nats-url") {
      result.url = value;
    } else if (key == "--stream") {
      result.stream = value;
    } else if (key == "--consumer") {
      result.consumer = value;
    } else if (key == "--subject") {
      result.filter = value;
    } else if (key == "--publish-prefix") {
      result.publish_prefix = value;
    } else if (key == "--worker-id") {
      result.worker = value;
    } else if (key == "--generation") {
      auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                    result.generation);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error("invalid_generation");
      }
    } else {
      throw std::runtime_error("unknown_option");
    }
  }
  if (!identifier(result.stream) ||
      result.stream.find('.') != std::string::npos ||
      !identifier(result.consumer) ||
      result.consumer.find('.') != std::string::npos ||
      !identifier(result.worker) ||
      result.worker.find('.') != std::string::npos ||
      !subject(result.filter, true) || !subject(result.publish_prefix) ||
      result.generation == 0) {
    throw std::runtime_error("invalid_configuration");
  }
  if (!result.filter.starts_with("hi.myrmidon.") &&
      result.filter != "hi.fleet.control." + result.worker) {
    throw std::runtime_error("unsupported_consumer_subject");
  }
  if (result.publish_prefix != "hi.fleet.events." + result.worker) {
    throw std::runtime_error("invalid_publish_boundary");
  }
  if (result.loopback_test) {
    constexpr std::string_view prefix = "nats://127.0.0.1:";
    if (!result.url.starts_with(prefix)) {
      throw std::runtime_error("test_transport_requires_literal_loopback");
    }
    const auto port = std::string_view(result.url).substr(prefix.size());
    unsigned int number{};
    const auto parsed =
        std::from_chars(port.data(), port.data() + port.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() ||
        number == 0 || number > 65535) {
      throw std::runtime_error("invalid_loopback_port");
    }
  } else if (!result.url.starts_with("tls://") ||
             result.url.find('@') != std::string::npos) {
    throw std::runtime_error("authenticated_tls_required");
  }
  return result;
}

std::string newEpoch() {
  std::random_device random;
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (int i = 0; i < 4; ++i) {
    output << std::setw(8) << random();
  }
  return output.str();
}

std::string now() {
  const auto timestamp = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(timestamp);
  std::tm utc{};
  gmtime_r(&seconds, &utc);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
         << std::setw(3)
         << (std::chrono::duration_cast<std::chrono::milliseconds>(
                 timestamp.time_since_epoch())
                 .count() %
             1000)
         << 'Z';
  return output.str();
}

void emit(const Json& frame) {
  const auto bytes = frame.dump();
  if (bytes.size() > kMaxFrame) {
    throw std::runtime_error("output_frame_too_large");
  }
  std::cout << bytes << '\n' << std::flush;
  if (!std::cout) {
    throw std::runtime_error("attachment_closed");
  }
}

// A bounded read rejects an unterminated/truncated final frame. A partial
// command must never execute merely because the SSH attachment ended.
bool readFrame(std::string& line) {
  line.clear();
  char character{};
  while (std::cin.get(character)) {
    if (character == '\n') {
      return true;
    }
    if (line.size() == kMaxFrame) {
      throw std::runtime_error("input_frame_too_large");
    }
    line += character;
  }
  if (!line.empty()) {
    throw std::runtime_error("truncated_input_frame");
  }
  return false;
}

class Gateway {
 public:
  explicit Gateway(Config configuration) : cfg_(std::move(configuration)) {
    natsOptions* raw_options{};
    check(natsOptions_Create(&raw_options));
    Handle<natsOptions, natsOptions_Destroy> options(raw_options,
                                                     natsOptions_Destroy);
    check(natsOptions_SetURL(options.get(), cfg_.url.c_str()));
    check(natsOptions_SetTimeout(options.get(), 3000));
    check(natsOptions_SetMaxReconnect(options.get(), 0));
    check(natsOptions_SetName(options.get(), "keystone-fleet-gateway"));
    if (!cfg_.loopback_test) {
      check(natsOptions_SetSecure(options.get(), true));
      const auto ca = env("KEYSTONE_NATS_TLS_CA_PATH");
      const auto credentials = env("KEYSTONE_NATS_CREDS");
      const auto cert = env("KEYSTONE_NATS_TLS_CERT_PATH");
      const auto key = env("KEYSTONE_NATS_TLS_KEY_PATH");
      if (!ca.empty()) {
        check(natsOptions_LoadCATrustedCertificates(options.get(), ca.c_str()));
      }
      if (!credentials.empty()) {
        check(natsOptions_SetUserCredentialsFromFiles(
            options.get(), credentials.c_str(), nullptr));
      }
      if (!cert.empty() && !key.empty()) {
        check(natsOptions_LoadCertificatesChain(options.get(), cert.c_str(),
                                                key.c_str()));
      } else if (!cert.empty() || !key.empty()) {
        throw std::runtime_error("incomplete_client_certificate");
      } else if (credentials.empty()) {
        throw std::runtime_error("broker_credentials_required");
      }
    }
    natsConnection* connection{};
    check(natsConnection_Connect(&connection, options.get()));
    connection_.reset(connection);
    jsCtx* context{};
    jsOptions options_js;
    jsOptions_Init(&options_js);
    options_js.Wait = 3000;
    check(natsConnection_JetStream(&context, connection_.get(), &options_js));
    js_.reset(context);
    jsConsumerInfo* raw_info{};
    check(js_GetConsumerInfo(&raw_info, js_.get(), cfg_.stream.c_str(),
                             cfg_.consumer.c_str(), nullptr, nullptr));
    Handle<jsConsumerInfo, jsConsumerInfo_Destroy> info(raw_info,
                                                        jsConsumerInfo_Destroy);
    const auto* consumer = info->Config;
    if (consumer == nullptr || consumer->AckPolicy != js_AckExplicit ||
        consumer->MaxAckPending != 1 || consumer->FilterSubject == nullptr ||
        cfg_.filter != consumer->FilterSubject ||
        consumer->Durable == nullptr || cfg_.consumer != consumer->Durable ||
        (consumer->DeliverSubject != nullptr &&
         consumer->DeliverSubject[0] != '\0')) {
      throw std::runtime_error(
          "consumer_requires_matching_durable_pull_explicit_ack_max_pending_"
          "one");
    }
    jsSubOptions subscribe;
    jsSubOptions_Init(&subscribe);
    subscribe.Stream = cfg_.stream.c_str();
    subscribe.Consumer = cfg_.consumer.c_str();
    subscribe.ManualAck = true;
    natsSubscription* subscription{};
    check(js_PullSubscribe(&subscription, js_.get(), cfg_.filter.c_str(),
                           nullptr, nullptr, &subscribe, nullptr));
    subscription_.reset(subscription);
  }

  Json execute(const Json& request) {
    const auto operation = text(request, "operation");
    if (operation == "pull") {
      return pull();
    }
    if (operation == "publish") {
      return publish(request);
    }
    if (operation == "ack" || operation == "nak" || operation == "inProgress") {
      return acknowledge(operation, text(request, "deliveryId"));
    }
    throw std::runtime_error("unsupported_operation");
  }

 private:
  Json metadata(std::string_view payload) const {
    Json result = Json::object();
    // Parse only to select bounded public identifiers. Never copy payload or
    // arbitrary additional fields into telemetry, including error messages.
    auto envelope = Json::parse(payload, nullptr, false);
    if (envelope.is_discarded() || !envelope.is_object() ||
        envelope.value("schema", Json()) != "hi/fleet/v1") {
      return result;
    }
    for (const auto* field :
         {"correlationId", "taskId", "executionId", "agentId"}) {
      if (envelope.contains(field) && envelope[field].is_string() &&
          identifier(envelope[field].get<std::string>())) {
        result[field] = envelope[field];
      }
    }
    for (const auto* field : {"eventId", "commandId"}) {
      if (envelope.contains(field) && envelope[field].is_string() &&
          identifier(envelope[field].get<std::string>())) {
        result["messageId"] = envelope[field];
        break;
      }
    }
    if (envelope.contains("operation") && envelope["operation"].is_string() &&
        identifier(envelope["operation"].get<std::string>())) {
      result["messageKind"] = envelope["operation"];
    }
    return result;
  }

  void observe(const Json& identifiers, std::string_view operation,
               std::string_view message_subject, std::size_t bytes,
               std::string_view result) {
    Json observation = identifiers;
    observation["schema"] = "hi/fleet/observation/v1";
    observation["eventId"] =
        epoch_ + ":" + std::to_string(++observation_sequence_);
    observation["sourceId"] =
        "keystone:" + cfg_.worker + ":" + cfg_.consumer + ":" + epoch_;
    observation["sourceSequence"] = observation_sequence_;
    observation["observedAt"] = now();
    observation["source"] = operation == "deliver" || operation == "redeliver"
                                ? "Agamemnon"
                                : "Hephaestus";
    observation["target"] = operation == "deliver" || operation == "redeliver"
                                ? "Hephaestus"
                            : operation == "publish" ? "Agamemnon"
                                                     : "Keystone";
    observation["workerId"] = cfg_.worker;
    observation["generation"] = cfg_.generation;
    observation["transport"] = "nats-jetstream";
    observation["subject"] = message_subject;
    observation["consumerId"] = cfg_.consumer;
    observation["stream"] = cfg_.stream;
    if (operation == "publish" || operation == "deliver" ||
        operation == "redeliver") {
      observation["bytes"] = bytes;
    }
    observation["operation"] = operation;
    observation["result"] = result;
    emit({{"schema", kSchema},
          {"type", "observation"},
          {"observation", observation}});
  }

  Json pull() {
    if (pending_) {
      throw std::runtime_error("delivery_pending");
    }
    natsMsgList messages{};
    const auto status = natsSubscription_Fetch(&messages, subscription_.get(),
                                               1, 1000, nullptr);
    if (status == NATS_TIMEOUT) {
      natsMsgList_Destroy(&messages);
      return {{"ok", true}, {"empty", true}};
    }
    if (status != NATS_OK) {
      natsMsgList_Destroy(&messages);
      check(status);
    }
    if (messages.Count != 1 || messages.Msgs == nullptr) {
      natsMsgList_Destroy(&messages);
      throw std::runtime_error("unexpected_fetch_count");
    }
    pending_.reset(messages.Msgs[0]);
    messages.Msgs[0] = nullptr;
    natsMsgList_Destroy(&messages);
    delivery_ = epoch_ + ":d:" + std::to_string(++delivery_sequence_);
    const char* raw_subject = natsMsg_GetSubject(pending_.get());
    pending_subject_ = raw_subject == nullptr ? "" : raw_subject;
    const int length = natsMsg_GetDataLength(pending_.get());
    if (length < 0 || static_cast<std::size_t>(length) > kMaxPayload ||
        !subject(pending_subject_)) {
      throw std::runtime_error("invalid_delivered_message");
    }
    pending_bytes_ = static_cast<std::size_t>(length);
    const std::string payload(natsMsg_GetData(pending_.get()), pending_bytes_);
    // Validate UTF-8 before emitting metadata; payload remains otherwise
    // opaque.
    static_cast<void>(Json(payload).dump());
    pending_metadata_ = metadata(payload);
    const char* message_id{};
    if (natsMsgHeader_Get(pending_.get(), "Nats-Msg-Id", &message_id) ==
            NATS_OK &&
        message_id != nullptr && identifier(message_id)) {
      pending_metadata_["messageId"] = message_id;
    }
    jsMsgMetaData* raw_metadata{};
    check(natsMsg_GetMetaData(&raw_metadata, pending_.get()));
    Handle<jsMsgMetaData, jsMsgMetaData_Destroy> meta(raw_metadata,
                                                      jsMsgMetaData_Destroy);
    if (!pending_metadata_.contains("messageId")) {
      pending_metadata_["messageId"] =
          cfg_.stream + ":" + std::to_string(meta->Sequence.Stream);
    }
    observe(pending_metadata_, meta->NumDelivered > 1 ? "redeliver" : "deliver",
            pending_subject_, pending_bytes_, "received");
    return {{"ok", true},
            {"empty", false},
            {"deliveryId", delivery_},
            {"subject", pending_subject_},
            {"payload", payload},
            {"streamSequence", meta->Sequence.Stream},
            {"numDelivered", meta->NumDelivered}};
  }

  Json publish(const Json& request) {
    const auto destination = text(request, "subject");
    const auto payload = text(request, "payload");
    const auto message_id = text(request, "messageId");
    if (!subject(destination) ||
        (destination != cfg_.publish_prefix &&
         !destination.starts_with(cfg_.publish_prefix + '.')) ||
        payload.size() > kMaxPayload || !identifier(message_id)) {
      throw std::runtime_error("invalid_publish");
    }
    jsPubOptions options;
    jsPubOptions_Init(&options);
    options.MsgId = message_id.c_str();
    options.MaxWait = 3000;
    jsPubAck* raw_ack{};
    const auto status =
        js_Publish(&raw_ack, js_.get(), destination.c_str(), payload.data(),
                   static_cast<int>(payload.size()), &options, nullptr);
    Handle<jsPubAck, jsPubAck_Destroy> ack(raw_ack, jsPubAck_Destroy);
    auto identifiers = metadata(payload);
    identifiers["messageId"] = message_id;
    observe(identifiers, "publish", destination, payload.size(),
            status == NATS_OK ? "confirmed" : "unknown");
    check(status);
    return {{"ok", true},
            {"streamSequence", ack->Sequence},
            {"duplicate", ack->Duplicate}};
  }

  Json acknowledge(const std::string& operation, const std::string& delivery) {
    if (!pending_ || delivery != delivery_) {
      throw std::runtime_error("unknown_delivery");
    }
    natsStatus status;
    if (operation == "ack") {
      status = natsMsg_AckSync(pending_.get(), nullptr, nullptr);
    } else {
      status = operation == "nak" ? natsMsg_Nak(pending_.get(), nullptr)
                                  : natsMsg_InProgress(pending_.get(), nullptr);
      if (status == NATS_OK) {
        status = natsConnection_FlushTimeout(connection_.get(), 3000);
      }
    }
    // For NAK/in-progress a flush only confirms transport round-trip, not an
    // explicit JetStream acknowledgement; report the distinction in telemetry.
    observe(pending_metadata_, operation, pending_subject_, pending_bytes_,
            status != NATS_OK    ? "unknown"
            : operation == "ack" ? "confirmed"
                                 : "sent");
    check(status);
    if (operation != "inProgress") {
      pending_.reset();
      delivery_.clear();
    }
    return {{"ok", true},
            {"confirmation", operation == "ack" ? "broker" : "transport"}};
  }

  Config cfg_;
  const std::string epoch_{newEpoch()};
  std::uint64_t observation_sequence_{};
  std::uint64_t delivery_sequence_{};
  std::string delivery_;
  std::string pending_subject_;
  std::size_t pending_bytes_{};
  Json pending_metadata_;
  // Reverse destruction order keeps the connection alive until delivery and
  // subscription handles are destroyed. None of these destructors ACKs work.
  Handle<natsConnection, natsConnection_Destroy> connection_{
      nullptr, natsConnection_Destroy};
  Handle<jsCtx, jsCtx_Destroy> js_{nullptr, jsCtx_Destroy};
  Handle<natsSubscription, natsSubscription_Destroy> subscription_{
      nullptr, natsSubscription_Destroy};
  Handle<natsMsg, natsMsg_Destroy> pending_{nullptr, natsMsg_Destroy};
};
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << "keystone-fleet-gateway --stream NAME --consumer DURABLE "
                 "--subject FILTER\n"
                 "  --publish-prefix hi.fleet.events.WORKER --worker-id WORKER "
                 "--generation N\n"
                 "  [--nats-url tls://HOST:PORT] [--allow-loopback-test]\n"
                 "Authenticated allocation attachment over bounded JSONL "
                 "stdin/stdout.\n";
    return 0;
  }
  std::signal(SIGPIPE, SIG_IGN);
  try {
    Gateway gateway(config(argc, argv));
    std::string line;
    while (readFrame(line)) {
      Json response{
          {"schema", kSchema}, {"type", "response"}, {"requestId", nullptr}};
      try {
        const auto request = Json::parse(line);
        if (!request.is_object() || text(request, "schema") != kSchema ||
            !identifier(text(request, "requestId"))) {
          throw std::runtime_error("invalid_request");
        }
        response["requestId"] = request["requestId"];
        response.update(gateway.execute(request));
      } catch (const Json::exception&) {
        response.update({{"ok", false}, {"error", "invalid_json_or_utf8"}});
      } catch (const std::runtime_error& error) {
        response.update({{"ok", false}, {"error", error.what()}});
      }
      emit(response);
    }
    return 0;
  } catch (const std::exception&) {
    // Configuration and framing errors terminate the attachment without ACK.
    std::cerr
        << "keystone-fleet-gateway: startup, framing, or attachment failure\n";
    return 2;
  }
}
