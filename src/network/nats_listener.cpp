#include "network/nats_listener.hpp"

#include <cctype>
#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace keystone {
namespace network {

// ---------------------------------------------------------------------------
// Internal parsing helpers (exposed via anonymous namespace for unit tests
// via friend linkage; tested through NATSListener::classify_subject).
// ---------------------------------------------------------------------------

namespace {

bool is_safe_token(std::string_view token) {
  if (token.empty()) {
    return false;
  }
  for (char c : token) {
    if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

std::vector<std::string_view> split_subject(std::string_view subject) {
  std::vector<std::string_view> parts;
  while (true) {
    auto pos = subject.find('.');
    if (pos == std::string_view::npos) {
      parts.push_back(subject);
      break;
    }
    parts.push_back(subject.substr(0, pos));
    subject.remove_prefix(pos + 1);
  }
  return parts;
}

bool is_terminal_verb(std::string_view verb) {
  return verb == "completed" || verb == "failed";
}

bool is_known_verb(std::string_view verb) {
  return verb == "completed" || verb == "failed" || verb == "updated" || verb == "created" ||
         verb == "assigned" || verb == "started";
}

}  // namespace

// ---------------------------------------------------------------------------
// SubjectClassification — pure parsing, no NATS dependency, unit-testable.
// ---------------------------------------------------------------------------

SubjectClassification NATSListener::classify_subject(std::string_view subject) noexcept {
  SubjectClassification result;
  auto parts = split_subject(subject);

  if (parts.size() < 5) {
    result.verdict = SubjectVerdict::kMalformed;
    return result;
  }

  result.team_id = parts[2];
  result.task_id = parts[3];
  result.verb = parts[4];

  if (!is_safe_token(result.team_id) || !is_safe_token(result.task_id)) {
    result.verdict = SubjectVerdict::kUnsafeToken;
    return result;
  }

  if (!is_known_verb(result.verb)) {
    result.verdict = SubjectVerdict::kUnknownVerb;
    return result;
  }

  if (!is_terminal_verb(result.verb)) {
    result.verdict = SubjectVerdict::kNonTerminalVerb;
    return result;
  }

  result.verdict = SubjectVerdict::kTerminal;
  return result;
}

// ---------------------------------------------------------------------------
// NATSListener
// ---------------------------------------------------------------------------

NATSListener::NATSListener(NATSListenerConfig cfg, AdvanceDagCallback cb)
    : cfg_(std::move(cfg)), callback_(std::move(cb)) {
  if (!callback_) {
    throw std::invalid_argument("NATSListener: callback must not be null");
  }
}

NATSListener::~NATSListener() {
  stop();
}

natsStatus NATSListener::start(jsCtx* js) {
  if (!js) {
    return NATS_INVALID_ARG;
  }

  // Store the JetStream context for use in the pull_loop thread
  js_ = js;

  // Create a durable pull consumer with MaxAckPending = 1 for rate-limiting
  jsSubOptions sub_opts;
  jsSubOptions_Init(&sub_opts);
  sub_opts.Config.MaxAckPending = cfg_.max_ack_pending;

  const int attempts = cfg_.max_attempts > 0 ? cfg_.max_attempts : 1;
  natsStatus s = NATS_ERR;

  // Create a pull consumer (no callback, subscription will be used for fetch)
  for (int attempt = 1; attempt <= attempts; ++attempt) {
    jsErrCode jerr = static_cast<jsErrCode>(0);
    // Pass NULL for the message handler callback since we'll use pull-based fetch
    s = js_Subscribe(&sub_, js, cfg_.subject.c_str(), nullptr, nullptr, nullptr, &sub_opts, &jerr);
    if (s == NATS_OK) {
      break;
    }
    spdlog::warn("NATSListener: subscribe attempt {}/{} failed status={} jerr={}",
                 attempt,
                 attempts,
                 static_cast<int>(s),
                 static_cast<int>(jerr));
  }

  if (s != NATS_OK) {
    spdlog::error("NATSListener: all {} subscribe attempt(s) failed status={}",
                  attempts,
                  static_cast<int>(s));
    return s;
  }

  // Start the pull-based fetch loop on a dedicated thread
  try {
    listener_thread_ = std::thread(&NATSListener::pull_loop, this);
  } catch (const std::exception& ex) {
    spdlog::error("NATSListener: failed to start listener thread: {}", ex.what());
    natsSubscription_Unsubscribe(sub_);
    natsSubscription_Destroy(sub_);
    sub_ = nullptr;
    return NATS_ERR;
  }

  return NATS_OK;
}

void NATSListener::stop() {
  if (stopped_.exchange(true)) {
    return;
  }

  // Signal the pull_loop thread to exit
  // The thread will see stopped_ == true and exit cleanly

  // Wait for the listener thread to finish if it was started
  if (listener_thread_.joinable()) {
    listener_thread_.join();
  }

  // Clean up the subscription after the thread has exited
  if (sub_ != nullptr) {
    natsSubscription_Unsubscribe(sub_);
    natsSubscription_Destroy(sub_);
    sub_ = nullptr;
  }

  js_ = nullptr;
}

void NATSListener::pull_loop() noexcept {
  // Pull-based fetch loop running on listener_thread_
  // Repeatedly fetches one message at a time (MaxAckPending = 1 rate-limit)
  // and calls handle_message() for processing.
  // Exits cleanly when stopped_ becomes true.

  constexpr int timeout_ms = 1000;  // 1-second timeout per fetch

  while (!stopped_.load(std::memory_order_acquire)) {
    natsMsgList list{};
    jsErrCode js_err = static_cast<jsErrCode>(0);
    natsStatus s = natsSubscription_Fetch(&list, sub_, 1, timeout_ms, &js_err);

    if (s == NATS_TIMEOUT) {
      // No message available within timeout; loop back and check stopped_ flag
      continue;
    }

    if (s != NATS_OK) {
      // Error in fetch (connection issue, etc.)
      spdlog::error("NATSListener: natsSubscription_Fetch failed status={}", static_cast<int>(s));
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    if (list.Count == 0 || list.Msgs == nullptr) {
      natsMsgList_Destroy(&list);
      continue;
    }

    // Take ownership of the first (and only) message
    natsMsg* msg = list.Msgs[0];
    list.Msgs[0] = nullptr;
    list.Count = 0;
    natsMsgList_Destroy(&list);

    // Handle the message (includes ack/nak)
    handle_message(msg);
  }

  spdlog::debug("NATSListener: pull_loop exiting");
}

void NATSListener::handle_message(natsMsg* msg) noexcept {
  // Ensure we always ack or nak before exiting, even on exceptions
  bool should_ack = false;

  auto finish = [&]() {
    // Only ack/nak if not already done (for safety)
    if (msg != nullptr) {
      natsStatus ack_s = should_ack ? natsMsg_Ack(msg, nullptr) : natsMsg_Nak(msg, nullptr);
      if (ack_s != NATS_OK) {
        spdlog::warn("NATSListener: ack/nak failed status={}", static_cast<int>(ack_s));
      }
      natsMsg_Destroy(msg);
    }
  };

  [&]() {
    const char* raw = natsMsg_GetSubject(msg);
    std::string_view subject(raw != nullptr ? raw : "");

    auto cls = classify_subject(subject);

    switch (cls.verdict) {
      case SubjectVerdict::kMalformed:
        spdlog::warn("NATSListener: malformed subject subject={}", subject);
        return;  // nak

      case SubjectVerdict::kUnsafeToken:
        spdlog::warn("NATSListener: unsafe token team_id={} task_id={} subject={}",
                     cls.team_id,
                     cls.task_id,
                     subject);
        return;  // nak

      case SubjectVerdict::kUnknownVerb:
        spdlog::debug("NATSListener: unknown verb={} subject={}", cls.verb, subject);
        should_ack = true;
        return;

      case SubjectVerdict::kNonTerminalVerb:
        spdlog::debug("NATSListener: non-terminal verb={} subject={}", cls.verb, subject);
        should_ack = true;
        return;

      case SubjectVerdict::kTerminal:
        try {
          callback_(cls.team_id, cls.task_id);
          spdlog::info("NATSListener: advancing_dag team_id={} task_id={}",
                       cls.team_id,
                       cls.task_id);
          should_ack = true;
        } catch (const std::exception& ex) {
          spdlog::error("NATSListener: callback threw team_id={} task_id={} error={}",
                        cls.team_id,
                        cls.task_id,
                        ex.what());
          // nak: allow redelivery
        } catch (...) {
          spdlog::error(
              "NATSListener: callback threw unknown exception "
              "team_id={} task_id={}",
              cls.team_id,
              cls.task_id);
          // nak
        }
        return;
    }
  }();

  finish();
}

}  // namespace network
}  // namespace keystone
