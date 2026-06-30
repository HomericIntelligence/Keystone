#pragma once

#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace keystone {
namespace core {

// Matches UUIDs, alphanumeric slugs, hyphens, and underscores.
// Rejects path traversal characters (/, .., spaces, newlines, semicolons,
// etc.). Applied at all external input boundaries where IDs are extracted from
// NATS subject tokens or network messages before use in routing/API calls.
inline const std::regex& safeIdPattern() {
  static const std::regex kPattern{R"(^[a-zA-Z0-9_-]+$)"};
  return kPattern;
}

// Matches a single NATS subject token: either a safe identifier token
// ([a-zA-Z0-9_-]+) or one of the two NATS wildcard characters (* or >).
// Dots are NOT valid within an individual token -- they are the subject
// separator and belong to validateNatsSubject().
inline const std::regex& natsTokenPattern() {
  static const std::regex kPattern{R"(^([a-zA-Z0-9_-]+|[*>])$)"};
  return kPattern;
}

/**
 * @brief Validate that a subject token is safe to use as an identifier.
 *
 * Rejects empty strings and any value containing path traversal characters,
 * slashes, spaces, or other characters outside [a-zA-Z0-9_-].
 *
 * This function does NOT accept NATS wildcard tokens (* or >). Use
 * validateNatsSubjectToken() when building NATS subject filters where
 * wildcards are intentional.
 *
 * @param value The token value to validate (e.g. agent_id, task_id).
 * @param label Human-readable label used in the exception message.
 * @return The validated value (unchanged).
 * @throws std::invalid_argument if value is empty or contains unsafe
 * characters.
 */
inline const std::string& validateSubjectToken(const std::string& value, const std::string& label) {
  if (value.empty() || !std::regex_match(value, safeIdPattern())) {
    throw std::invalid_argument("Invalid " + label + ": unsafe characters in '" + value + "'");
  }
  return value;
}

/**
 * @brief Validate a single NATS subject token, including wildcard tokens.
 *
 * Unlike validateSubjectToken(), this function accepts the NATS wildcard
 * characters:
 *   - `*`  matches any single token (e.g. `hi.agents.*.status`)
 *   - `>`  matches one or more tokens and must appear as the final token
 *          (e.g. `hi.agents.>`)
 *
 * Valid inputs: alphanumeric slugs, hyphens, underscores, `*`, `>`.
 * Invalid inputs: empty string, dot-separated paths, spaces, slashes, and
 * any other character not in [a-zA-Z0-9_-*>].
 *
 * Distinction from validateSubjectToken():
 *   - Use validateSubjectToken()    when a token must be a concrete identifier
 *     (agent ID, task ID, stream name) -- wildcards are rejected.
 *   - Use validateNatsSubjectToken() when constructing NATS subject filters
 *     where `*` and `>` are semantically meaningful wildcards.
 *
 * @param value The single token to validate (must not contain dots).
 * @param label Human-readable label used in the exception message.
 * @return The validated value (unchanged).
 * @throws std::invalid_argument if value is empty, contains a dot, or
 *         contains characters outside the NATS token grammar.
 */
inline const std::string& validateNatsSubjectToken(const std::string& value,
                                                   const std::string& label) {
  if (value.empty() || !std::regex_match(value, natsTokenPattern())) {
    throw std::invalid_argument("Invalid NATS token " + label +
                                ": must be [a-zA-Z0-9_-], '*', or '>' -- got '" + value + "'");
  }
  return value;
}

/**
 * @brief Validate a full dot-separated NATS subject string.
 *
 * A valid NATS subject is a sequence of one or more tokens separated by dots.
 * Each token must satisfy the NATS token grammar (see
 * validateNatsSubjectToken). The `>` wildcard is only permitted as the final
 * token.
 *
 * Examples of valid subjects:
 *   - `hi.agents.task-1`
 *   - `hi.myrmidon.*.status`
 *   - `hi.research.>`
 *
 * Examples of invalid subjects:
 *   - `` (empty)
 *   - `hi..agents` (empty token between dots)
 *   - `hi.agents.>.extra` (`>` is not the last token)
 *   - `hi.agents.bad token` (space in token)
 *
 * @param subject The full NATS subject to validate (e.g. "hi.agents.>").
 * @param label   Human-readable label used in exception messages.
 * @return The validated subject (unchanged).
 * @throws std::invalid_argument if the subject is empty, contains an empty
 *         token, contains a `>` that is not the final token, or contains a
 *         token with invalid characters.
 */
inline const std::string& validateNatsSubject(const std::string& subject,
                                              const std::string& label) {
  if (subject.empty()) {
    throw std::invalid_argument("Invalid NATS subject " + label + ": subject must not be empty");
  }

  std::string_view remaining{subject};
  bool saw_gt = false;

  while (!remaining.empty()) {
    if (saw_gt) {
      // A '>' token was already seen but there are more tokens after it.
      throw std::invalid_argument("Invalid NATS subject " + label +
                                  ": '>' wildcard must be the last token in '" + subject + "'");
    }

    const auto dot_pos = remaining.find('.');
    const std::string_view token_sv = (dot_pos == std::string_view::npos)
                                          ? remaining
                                          : remaining.substr(0, dot_pos);

    const std::string token{token_sv};
    // Validate the individual token (reuse natsTokenPattern).
    if (token.empty() || !std::regex_match(token, natsTokenPattern())) {
      throw std::invalid_argument("Invalid NATS subject " + label + ": token '" + token +
                                  "' in subject '" + subject + "' contains invalid characters");
    }

    if (token == ">") {
      saw_gt = true;
    }

    if (dot_pos == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(dot_pos + 1);
  }

  return subject;
}

}  // namespace core
}  // namespace keystone
