#include "transport/transparent_bridge.hpp"

#include "core/message_bus.hpp"
#include "core/message_serializer.hpp"
#include "transport/nats_connection.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace keystone {
namespace transport {

// ---------------------------------------------------------------------------
// Subject derivation
// ---------------------------------------------------------------------------

std::string deriveNatsSubject(std::string_view receiver_id) {
  return "hi.agents." + std::string(receiver_id);
}

// ---------------------------------------------------------------------------
// TransparentBridge
// ---------------------------------------------------------------------------

TransparentBridge::TransparentBridge(core::MessageBus& bus, NatsConnection& conn, BridgeConfig cfg)
    : bus_(bus), conn_(conn), cfg_(std::move(cfg)) {}

TransparentBridge::~TransparentBridge() {
  stop();
}

natsStatus TransparentBridge::attach() {
  // -------------------------------------------------------------------------
  // Outbound path: register the NATS publisher callback with MessageBus.
  // MessageBus::routeMessage() serialises the KeystoneMessage and calls this
  // lambda with (subject, serialized_bytes) when local lookup fails (#512).
  // -------------------------------------------------------------------------
  bus_.setNatsPublisher([this](std::string_view subject, std::span<const std::byte> payload) {
    natsConnection* nc = conn_.handle();
    if (nc == nullptr || payload.empty()) {
      return;
    }
    natsStatus s = natsConnection_Publish(nc,
                                          subject.data(),
                                          reinterpret_cast<const char*>(payload.data()),
                                          static_cast<int>(payload.size()));
    if (s != NATS_OK) {
      spdlog::error(
          "TransparentBridge: natsConnection_Publish failed subject={} "
          "status={}",
          subject,
          static_cast<int>(s));
    }
  });

  // -------------------------------------------------------------------------
  // Inbound path: subscribe to cfg_.inbound_subject and start pull loop.
  // -------------------------------------------------------------------------
  jsCtx* js = conn_.jsContext();
  if (js == nullptr) {
    spdlog::error(
        "TransparentBridge::attach: NatsConnection has no JetStream context "
        "(not connected?)");
    return NATS_ERR;
  }

  jsSubOptions sub_opts;
  jsSubOptions_Init(&sub_opts);
  sub_opts.Config.MaxAckPending = cfg_.max_ack_pending;

  const int attempts = cfg_.max_attempts > 0 ? cfg_.max_attempts : 1;
  natsStatus s = NATS_ERR;

  for (int attempt = 1; attempt <= attempts; ++attempt) {
    jsErrCode jerr = static_cast<jsErrCode>(0);
    s = js_Subscribe(
        &sub_, js, cfg_.inbound_subject.c_str(), nullptr, nullptr, nullptr, &sub_opts, &jerr);
    if (s == NATS_OK) {
      break;
    }
    spdlog::warn("TransparentBridge: subscribe attempt {}/{} failed status={} jerr={}",
                 attempt,
                 attempts,
                 static_cast<int>(s),
                 static_cast<int>(jerr));
  }

  if (s != NATS_OK) {
    spdlog::error(
        "TransparentBridge: all {} subscribe attempt(s) failed; inbound path "
        "inactive",
        attempts);
    return s;
  }

  // Start pull-loop thread.
  try {
    inbound_thread_ = std::thread(&TransparentBridge::inbound_loop, this);
  } catch (const std::exception& ex) {
    spdlog::error("TransparentBridge: failed to start inbound thread: {}", ex.what());
    natsSubscription_Unsubscribe(sub_);
    natsSubscription_Destroy(sub_);
    sub_ = nullptr;
    return NATS_ERR;
  }

  spdlog::info("TransparentBridge: attached subject={}", cfg_.inbound_subject);
  return NATS_OK;
}

void TransparentBridge::stop() {
  if (stopped_.exchange(true)) {
    return;  // Already stopped — idempotent.
  }

  // Unregister the outbound publisher so MessageBus stops calling it.
  bus_.setNatsPublisher(nullptr);

  // Join the inbound thread before destroying the subscription.
  if (inbound_thread_.joinable()) {
    inbound_thread_.join();
  }

  if (sub_ != nullptr) {
    natsSubscription_Unsubscribe(sub_);
    natsSubscription_Destroy(sub_);
    sub_ = nullptr;
  }

  spdlog::info("TransparentBridge: stopped");
}

void TransparentBridge::inbound_loop() noexcept {
  constexpr int kTimeoutMs = 1000;  // 1-second fetch timeout

  while (!stopped_.load(std::memory_order_acquire)) {
    natsMsgList list{};
    jsErrCode js_err = static_cast<jsErrCode>(0);
    natsStatus s = natsSubscription_Fetch(&list, sub_, 1, kTimeoutMs, &js_err);

    if (s == NATS_TIMEOUT) {
      continue;  // Normal — no message within timeout window.
    }

    if (s != NATS_OK) {
      spdlog::error("TransparentBridge: natsSubscription_Fetch failed status={}",
                    static_cast<int>(s));
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    if (list.Count == 0 || list.Msgs == nullptr) {
      natsMsgList_Destroy(&list);
      continue;
    }

    // Take ownership of the single message.
    natsMsg* msg = list.Msgs[0];
    list.Msgs[0] = nullptr;
    list.Count = 0;
    natsMsgList_Destroy(&list);

    bool should_ack = false;

    [&]() {
      const void* data = natsMsg_GetData(msg);
      int data_len = natsMsg_GetDataLength(msg);

      if (data == nullptr || data_len <= 0) {
        spdlog::warn("TransparentBridge: received empty inbound message");
        return;  // will nak
      }

      try {
        const auto* bytes = static_cast<const uint8_t*>(data);
        core::KeystoneMessage km =
            core::MessageSerializer::deserialize(bytes, static_cast<size_t>(data_len));

        // Route to local MessageBus.  If no local agent is registered for this
        // receiver_id the message is dropped (avoid re-publishing to NATS and
        // creating a loop — the outbound publisher would re-trigger inbound).
        bool routed = bus_.routeMessage(km);
        if (!routed) {
          spdlog::warn(
              "TransparentBridge: inbound message receiver_id={} not found "
              "locally (dropped)",
              km.receiver_id);
        }
        should_ack = true;
      } catch (const std::exception& ex) {
        spdlog::error("TransparentBridge: deserialization failed: {}", ex.what());
        // nak — allow redelivery
      } catch (...) {
        spdlog::error("TransparentBridge: deserialization threw unknown exception");
        // nak
      }
    }();

    natsStatus ack_s = should_ack ? natsMsg_Ack(msg, nullptr) : natsMsg_Nak(msg, nullptr);
    if (ack_s != NATS_OK) {
      spdlog::warn("TransparentBridge: ack/nak failed status={}", static_cast<int>(ack_s));
    }
    natsMsg_Destroy(msg);
  }

  spdlog::debug("TransparentBridge: inbound_loop exiting");
}

}  // namespace transport
}  // namespace keystone
