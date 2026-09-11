#include <nats.h>

#if defined(__APPLE__)
#include <crt_externs.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using Json = nlohmann::json;
constexpr std::string_view K_SCHEMA = "hi/keystone/fleet-attach/v1";
constexpr std::size_t K_MAX_FRAME = 1024UL * 1024UL;
constexpr std::size_t K_MAX_PAYLOAD = 128UL * 1024UL;

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
    const auto END = value.find('.');
    const auto TOKEN = value.substr(0, END);
    if (!(filter && TOKEN == "*") &&
        (!identifier(TOKEN) || std::ranges::find(TOKEN, ':') != TOKEN.end())) {
      return false;
    }
    if (END == std::string_view::npos) {
      return true;
    }
    value.remove_prefix(END + 1);
  }
  return false;
}

// Copy only configured values from the POSIX startup environment, before the
// broker starts threads. No runtime environment lookup or mutation is needed.
std::string env(char** entries, std::string_view key) {
  if (entries != nullptr) {
    for (auto* entry = entries; *entry != nullptr; entry = std::next(entry)) {
      const std::string_view VALUE{*entry};
      if (VALUE.size() > key.size() && VALUE.starts_with(key) &&
          VALUE[key.size()] == '=') {
        return std::string{VALUE.substr(key.size() + 1)};
      }
    }
  }
  return {};
}

std::string text(const Json& object, const char* key) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    throw std::runtime_error("invalid_request");
  }
  return object.at(key).get<std::string>();
}

struct Config {
  std::string url;
  std::string ca;
  std::string credentials;
  std::string cert;
  std::string key;
  std::string stream;
  std::string consumer;
  std::string filter;
  std::string publish_prefix;
  std::string worker;
  std::uint64_t generation{};
  bool loopback_test{};
};

Config config(std::span<char*> arguments, char** environment) {
  Config result;
  result.url = env(environment, "KEYSTONE_NATS_URL");
  result.ca = env(environment, "KEYSTONE_NATS_TLS_CA_PATH");
  result.credentials = env(environment, "KEYSTONE_NATS_CREDS");
  result.cert = env(environment, "KEYSTONE_NATS_TLS_CERT_PATH");
  result.key = env(environment, "KEYSTONE_NATS_TLS_KEY_PATH");
  if (!arguments.empty()) {
    arguments = arguments.subspan(1);
  }
  while (!arguments.empty()) {
    const std::string_view KEY{arguments.front()};
    arguments = arguments.subspan(1);
    if (KEY == "--allow-loopback-test") {
      result.loopback_test = true;
      continue;
    }
    if (arguments.empty()) {
      throw std::runtime_error("missing_option_value");
    }
    const std::string_view VALUE{arguments.front()};
    arguments = arguments.subspan(1);
    if (KEY == "--nats-url") {
      result.url = VALUE;
    } else if (KEY == "--stream") {
      result.stream = VALUE;
    } else if (KEY == "--consumer") {
      result.consumer = VALUE;
    } else if (KEY == "--subject") {
      result.filter = VALUE;
    } else if (KEY == "--publish-prefix") {
      result.publish_prefix = VALUE;
    } else if (KEY == "--worker-id") {
      result.worker = VALUE;
    } else if (KEY == "--generation") {
      const auto PARSED =
          std::from_chars(VALUE.begin(), VALUE.end(), result.generation);
      if (PARSED.ec != std::errc{} || PARSED.ptr != VALUE.end()) {
        throw std::runtime_error("invalid_generation");
      }
    } else {
      throw std::runtime_error("unknown_option");
    }
  }
  if (!identifier(result.stream) ||
      std::ranges::find(result.stream, '.') != result.stream.end() ||
      !identifier(result.consumer) ||
      std::ranges::find(result.consumer, '.') != result.consumer.end() ||
      !identifier(result.worker) ||
      std::ranges::find(result.worker, '.') != result.worker.end() ||
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
    constexpr std::string_view PREFIX = "nats://127.0.0.1:";
    if (!result.url.starts_with(PREFIX)) {
      throw std::runtime_error("test_transport_requires_literal_loopback");
    }
    const auto PORT = std::string_view{result.url}.substr(PREFIX.size());
    unsigned int number{};
    const auto PARSED = std::from_chars(PORT.begin(), PORT.end(), number);
    if (PARSED.ec != std::errc{} || PARSED.ptr != PORT.end() || number == 0 ||
        number > 65535) {
      throw std::runtime_error("invalid_loopback_port");
    }
  } else if (!result.url.starts_with("tls://") ||
             std::ranges::find(result.url, '@') != result.url.end()) {
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
  const auto TIMESTAMP = std::chrono::system_clock::now();
  const auto SECONDS = std::chrono::system_clock::to_time_t(TIMESTAMP);
  std::tm utc{};
  gmtime_r(&SECONDS, &utc);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
         << std::setw(3)
         << (std::chrono::duration_cast<std::chrono::milliseconds>(
                 TIMESTAMP.time_since_epoch())
                 .count() %
             1000)
         << 'Z';
  return output.str();
}

void emit(const Json& frame) {
  const auto BYTES = frame.dump();
  if (BYTES.size() > K_MAX_FRAME) {
    throw std::runtime_error("output_frame_too_large");
  }
  std::cout << BYTES << '\n' << std::flush;
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
    if (line.size() == K_MAX_FRAME) {
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
  explicit Gateway(Config configuration) : cfg(std::move(configuration)) {
    natsOptions* raw_options{};
    check(natsOptions_Create(&raw_options));
    Handle<natsOptions, natsOptions_Destroy> options(raw_options,
                                                     natsOptions_Destroy);
    check(natsOptions_SetURL(options.get(), cfg.url.c_str()));
    check(natsOptions_SetTimeout(options.get(), 3000));
    check(natsOptions_SetMaxReconnect(options.get(), 0));
    check(natsOptions_SetName(options.get(), "keystone-fleet-gateway"));
    if (!cfg.loopback_test) {
      check(natsOptions_SetSecure(options.get(), true));
      if (!cfg.ca.empty()) {
        check(natsOptions_LoadCATrustedCertificates(options.get(),
                                                    cfg.ca.c_str()));
      }
      if (!cfg.credentials.empty()) {
        check(natsOptions_SetUserCredentialsFromFiles(
            options.get(), cfg.credentials.c_str(), nullptr));
      }
      if (!cfg.cert.empty() && !cfg.key.empty()) {
        check(natsOptions_LoadCertificatesChain(options.get(), cfg.cert.c_str(),
                                                cfg.key.c_str()));
      } else if (!cfg.cert.empty() || !cfg.key.empty()) {
        throw std::runtime_error("incomplete_client_certificate");
      } else if (cfg.credentials.empty()) {
        throw std::runtime_error("broker_credentials_required");
      }
    }
    natsConnection* raw_connection{};
    check(natsConnection_Connect(&raw_connection, options.get()));
    connection.reset(raw_connection);
    jsCtx* context{};
    jsOptions options_js;
    jsOptions_Init(&options_js);
    options_js.Wait = 3000;
    check(natsConnection_JetStream(&context, connection.get(), &options_js));
    js.reset(context);
    jsConsumerInfo* raw_info{};
    check(js_GetConsumerInfo(&raw_info, js.get(), cfg.stream.c_str(),
                             cfg.consumer.c_str(), nullptr, nullptr));
    Handle<jsConsumerInfo, jsConsumerInfo_Destroy> info(raw_info,
                                                        jsConsumerInfo_Destroy);
    const auto* consumer = info->Config;
    if (consumer == nullptr || consumer->AckPolicy != js_AckExplicit ||
        consumer->MaxAckPending != 1 || consumer->FilterSubject == nullptr ||
        cfg.filter != consumer->FilterSubject || consumer->Durable == nullptr ||
        cfg.consumer != consumer->Durable ||
        (consumer->DeliverSubject != nullptr &&
         *consumer->DeliverSubject != '\0')) {
      throw std::runtime_error(
          "consumer_requires_matching_durable_pull_explicit_ack_max_pending_"
          "one");
    }
    jsSubOptions subscribe;
    jsSubOptions_Init(&subscribe);
    subscribe.Stream = cfg.stream.c_str();
    subscribe.Consumer = cfg.consumer.c_str();
    subscribe.ManualAck = true;
    natsSubscription* raw_subscription{};
    check(js_PullSubscribe(&raw_subscription, js.get(), cfg.filter.c_str(),
                           nullptr, nullptr, &subscribe, nullptr));
    subscription.reset(raw_subscription);
  }

  Json execute(const Json& request) {
    const auto OPERATION = text(request, "operation");
    if (OPERATION == "pull") {
      return pull();
    }
    if (OPERATION == "publish") {
      return publish(request);
    }
    if (OPERATION == "ack" || OPERATION == "nak" || OPERATION == "inProgress") {
      return acknowledge(OPERATION, request);
    }
    throw std::runtime_error("unsupported_operation");
  }

 private:
  static Json metadata(std::string_view payload) {
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

  struct Observation {
    std::string_view operation;
    std::string_view subject;
    std::size_t bytes;
    std::string_view result;
  };

  void observe(const Json& identifiers, const Observation& fact) {
    Json observation = identifiers;
    observation["schema"] = "hi/fleet/observation/v1";
    observation["eventId"] =
        EPOCH + ":" + std::to_string(++observation_sequence);
    observation["sourceId"] =
        "keystone:" + cfg.worker + ":" + cfg.consumer + ":" + EPOCH;
    observation["sourceSequence"] = observation_sequence;
    observation["observedAt"] = now();
    observation["source"] =
        fact.operation == "deliver" || fact.operation == "redeliver"
            ? "Agamemnon"
            : "Hephaestus";
    observation["target"] =
        fact.operation == "deliver" || fact.operation == "redeliver"
            ? "Hephaestus"
        : fact.operation == "publish" ? "Agamemnon"
                                      : "Keystone";
    observation["workerId"] = cfg.worker;
    observation["generation"] = cfg.generation;
    observation["transport"] = "nats-jetstream";
    observation["subject"] = fact.subject;
    observation["consumerId"] = cfg.consumer;
    observation["stream"] = cfg.stream;
    if (fact.operation == "publish" || fact.operation == "deliver" ||
        fact.operation == "redeliver") {
      observation["bytes"] = fact.bytes;
    }
    observation["operation"] = fact.operation;
    observation["result"] = fact.result;
    emit({{"schema", K_SCHEMA},
          {"type", "observation"},
          {"observation", observation}});
  }

  Json pull() {
    if (pending) {
      throw std::runtime_error("delivery_pending");
    }
    natsMsgList messages{};
    const auto STATUS =
        natsSubscription_Fetch(&messages, subscription.get(), 1, 1000, nullptr);
    if (STATUS == NATS_TIMEOUT) {
      natsMsgList_Destroy(&messages);
      return {{"ok", true}, {"empty", true}};
    }
    if (STATUS != NATS_OK) {
      natsMsgList_Destroy(&messages);
      check(STATUS);
    }
    if (messages.Count != 1 || messages.Msgs == nullptr) {
      natsMsgList_Destroy(&messages);
      throw std::runtime_error("unexpected_fetch_count");
    }
    pending.reset(*messages.Msgs);
    *messages.Msgs = nullptr;
    natsMsgList_Destroy(&messages);
    delivery = EPOCH + ":d:" + std::to_string(++delivery_sequence);
    const char* raw_subject = natsMsg_GetSubject(pending.get());
    pending_subject = raw_subject == nullptr ? "" : raw_subject;
    const int LENGTH = natsMsg_GetDataLength(pending.get());
    if (LENGTH < 0 || static_cast<std::size_t>(LENGTH) > K_MAX_PAYLOAD ||
        !subject(pending_subject)) {
      throw std::runtime_error("invalid_delivered_message");
    }
    pending_bytes = static_cast<std::size_t>(LENGTH);
    const std::string PAYLOAD(natsMsg_GetData(pending.get()), pending_bytes);
    // Validate UTF-8 before emitting metadata; payload remains otherwise
    // opaque.
    static_cast<void>(Json(PAYLOAD).dump());
    pending_metadata = metadata(PAYLOAD);
    const char* message_id{};
    if (natsMsgHeader_Get(pending.get(), "Nats-Msg-Id", &message_id) ==
            NATS_OK &&
        message_id != nullptr && identifier(message_id)) {
      pending_metadata["messageId"] = message_id;
    }
    jsMsgMetaData* raw_metadata{};
    check(natsMsg_GetMetaData(&raw_metadata, pending.get()));
    Handle<jsMsgMetaData, jsMsgMetaData_Destroy> meta(raw_metadata,
                                                      jsMsgMetaData_Destroy);
    if (!pending_metadata.contains("messageId")) {
      pending_metadata["messageId"] =
          cfg.stream + ":" + std::to_string(meta->Sequence.Stream);
    }
    observe(pending_metadata,
            {.operation = meta->NumDelivered > 1 ? "redeliver" : "deliver",
             .subject = pending_subject,
             .bytes = pending_bytes,
             .result = "received"});
    return {{"ok", true},
            {"empty", false},
            {"deliveryId", delivery},
            {"subject", pending_subject},
            {"payload", PAYLOAD},
            {"streamSequence", meta->Sequence.Stream},
            {"numDelivered", meta->NumDelivered}};
  }

  Json publish(const Json& request) {
    const auto DESTINATION = text(request, "subject");
    const auto PAYLOAD = text(request, "payload");
    const auto MESSAGE_ID = text(request, "messageId");
    if (!subject(DESTINATION) ||
        (DESTINATION != cfg.publish_prefix &&
         !DESTINATION.starts_with(cfg.publish_prefix + '.')) ||
        PAYLOAD.size() > K_MAX_PAYLOAD || !identifier(MESSAGE_ID)) {
      throw std::runtime_error("invalid_publish");
    }
    jsPubOptions options;
    jsPubOptions_Init(&options);
    options.MsgId = MESSAGE_ID.c_str();
    options.MaxWait = 3000;
    jsPubAck* raw_ack{};
    const auto STATUS =
        js_Publish(&raw_ack, js.get(), DESTINATION.c_str(), PAYLOAD.data(),
                   static_cast<int>(PAYLOAD.size()), &options, nullptr);
    Handle<jsPubAck, jsPubAck_Destroy> ack(raw_ack, jsPubAck_Destroy);
    auto identifiers = metadata(PAYLOAD);
    identifiers["messageId"] = MESSAGE_ID;
    observe(identifiers,
            {.operation = "publish",
             .subject = DESTINATION,
             .bytes = PAYLOAD.size(),
             .result = STATUS == NATS_OK ? "confirmed" : "unknown"});
    check(STATUS);
    return {{"ok", true},
            {"streamSequence", ack->Sequence},
            {"duplicate", ack->Duplicate}};
  }

  Json acknowledge(std::string_view operation, const Json& request) {
    const auto REQUESTED_DELIVERY = text(request, "deliveryId");
    if (!pending || REQUESTED_DELIVERY != delivery) {
      throw std::runtime_error("unknown_delivery");
    }
    natsStatus status{NATS_OK};
    if (operation == "ack") {
      status = natsMsg_AckSync(pending.get(), nullptr, nullptr);
    } else {
      status = operation == "nak" ? natsMsg_Nak(pending.get(), nullptr)
                                  : natsMsg_InProgress(pending.get(), nullptr);
      if (status == NATS_OK) {
        status = natsConnection_FlushTimeout(connection.get(), 3000);
      }
    }
    // For NAK/in-progress a flush only confirms transport round-trip, not an
    // explicit JetStream acknowledgement; report the distinction in telemetry.
    observe(pending_metadata, {.operation = operation,
                               .subject = pending_subject,
                               .bytes = pending_bytes,
                               .result = status != NATS_OK    ? "unknown"
                                         : operation == "ack" ? "confirmed"
                                                              : "sent"});
    check(status);
    if (operation != "inProgress") {
      pending.reset();
      delivery.clear();
    }
    return {{"ok", true},
            {"confirmation", operation == "ack" ? "broker" : "transport"}};
  }

  Config cfg;
  const std::string EPOCH{newEpoch()};
  std::uint64_t observation_sequence{};
  std::uint64_t delivery_sequence{};
  std::string delivery;
  std::string pending_subject;
  std::size_t pending_bytes{};
  Json pending_metadata;
  // Reverse destruction order keeps the connection alive until delivery and
  // subscription handles are destroyed. None of these destructors ACKs work.
  Handle<natsConnection, natsConnection_Destroy> connection{
      nullptr, natsConnection_Destroy};
  Handle<jsCtx, jsCtx_Destroy> js{nullptr, jsCtx_Destroy};
  Handle<natsSubscription, natsSubscription_Destroy> subscription{
      nullptr, natsSubscription_Destroy};
  Handle<natsMsg, natsMsg_Destroy> pending{nullptr, natsMsg_Destroy};
};
}  // namespace

int main(int argc, char** argv) {
  const std::span ARGUMENTS{argv, static_cast<std::size_t>(argc)};
  if (ARGUMENTS.size() == 2 && std::string_view{ARGUMENTS.back()} == "--help") {
    std::cout << "keystone-fleet-gateway --stream NAME --consumer DURABLE "
                 "--subject FILTER\n"
                 "  --publish-prefix hi.fleet.events.WORKER --worker-id WORKER "
                 "--generation N\n"
                 "  [--nats-url tls://HOST:PORT] [--allow-loopback-test]\n"
                 "Authenticated allocation attachment over bounded JSONL "
                 "stdin/stdout.\n";
    return 0;
  }
  try {
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
      throw std::runtime_error("signal_configuration_failed");
    }
#if defined(__APPLE__)
    auto** environment = *_NSGetEnviron();
#else
    auto** environment = environ;
#endif
    Gateway gateway(config(ARGUMENTS, environment));
    std::string line;
    while (readFrame(line)) {
      Json response{
          {"schema", K_SCHEMA}, {"type", "response"}, {"requestId", nullptr}};
      try {
        const auto REQUEST = Json::parse(line);
        if (!REQUEST.is_object() || text(REQUEST, "schema") != K_SCHEMA ||
            !identifier(text(REQUEST, "requestId"))) {
          throw std::runtime_error("invalid_request");
        }
        response["requestId"] = REQUEST["requestId"];
        response.update(gateway.execute(REQUEST));
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
