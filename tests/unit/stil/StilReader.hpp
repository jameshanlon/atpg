#pragma once

#include "atpg/Result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// A deliberately small scanner for the STIL that atpg::stil emits, for
/// test use only.
///
/// Like RandomCircuit.hpp's simulator this is a *second* implementation and
/// must never call into atpg::stil: a test that reuses the writer's own
/// formatting helpers cannot detect the writer disagreeing with itself. It
/// is a scanner rather than a parser - it locates the blocks it needs and
/// pulls their fields out - because a full STIL parser would be a second
/// thing to get wrong, and its bugs could mask the writer's.
namespace atpg::testing {

/// One entry of the Signals block.
struct StilSignal {
  std::string name;
  bool isInput = false;
};

/// One V statement's two group assignments.
struct StilVector {
  std::string inputs;
  std::string outputs;
};

/// The parts of a STIL program this scanner understands.
struct StilProgram {
  /// Signals, in declaration order.
  std::vector<StilSignal> signals;
  /// SignalGroups "PI" membership, in order.
  std::vector<std::string> piGroup;
  /// SignalGroups "PO" membership, in order.
  std::vector<std::string> poGroup;
  /// One entry per V statement, in order.
  std::vector<StilVector> vectors;
};

namespace detail {

/// The text between the first `{` at or after `opener` and its matching
/// `}`, or empty when the section is absent or unbalanced.
inline std::string_view braceSection(std::string_view text, std::string_view opener) {
  const std::size_t start = text.find(opener);
  if (start == std::string_view::npos) {
    return {};
  }
  const std::size_t brace = text.find('{', start);
  if (brace == std::string_view::npos) {
    return {};
  }
  int depth = 0;
  for (std::size_t i = brace; i < text.size(); ++i) {
    if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      --depth;
      if (depth == 0) {
        return text.substr(brace + 1, i - brace - 1);
      }
    }
  }
  return {};
}

/// Every double-quoted string in `text`, in order.
inline std::vector<std::string> quotedNames(std::string_view text) {
  std::vector<std::string> names;
  std::size_t i = 0;
  while (true) {
    const std::size_t open = text.find('"', i);
    if (open == std::string_view::npos) {
      break;
    }
    const std::size_t close = text.find('"', open + 1);
    if (close == std::string_view::npos) {
      break;
    }
    names.emplace_back(text.substr(open + 1, close - open - 1));
    i = close + 1;
  }
  return names;
}

/// The membership list of the `"<name>" = '...'` group assignment.
inline Result<std::vector<std::string>> groupMembers(std::string_view groups,
                                                     std::string_view name) {
  std::string needle = "\"";
  needle += name;
  needle += "\"";
  const std::size_t at = groups.find(needle);
  if (at == std::string_view::npos) {
    return Error("readStil: SignalGroups has no group named " + std::string(name));
  }
  const std::size_t open = groups.find('\'', at);
  if (open == std::string_view::npos) {
    return Error("readStil: group assignment is not quoted");
  }
  const std::size_t close = groups.find('\'', open + 1);
  if (close == std::string_view::npos) {
    return Error("readStil: group assignment is unterminated");
  }
  return quotedNames(groups.substr(open + 1, close - open - 1));
}

/// The value of a `"<group>"=<value>;` assignment inside a V statement.
inline Result<std::string> fieldValue(std::string_view body, std::string_view group) {
  std::string needle = "\"";
  needle += group;
  needle += "\"=";
  const std::size_t at = body.find(needle);
  if (at == std::string_view::npos) {
    return Error("readStil: a V statement does not assign " + std::string(group));
  }
  const std::size_t start = at + needle.size();
  const std::size_t semi = body.find(';', start);
  if (semi == std::string_view::npos) {
    return Error("readStil: a V statement assignment has no ';'");
  }
  return std::string(body.substr(start, semi - start));
}

} // namespace detail

/// Scans the parts of a STIL program the tests check. Returns an Error when
/// a block it needs is missing or malformed.
inline Result<StilProgram> readStil(std::string_view text) {
  StilProgram program;

  const std::string_view signals = detail::braceSection(text, "Signals");
  if (signals.empty()) {
    return Error("readStil: no Signals block");
  }
  std::size_t i = 0;
  while (true) {
    const std::size_t open = signals.find('"', i);
    if (open == std::string_view::npos) {
      break;
    }
    const std::size_t close = signals.find('"', open + 1);
    if (close == std::string_view::npos) {
      return Error("readStil: unterminated signal name");
    }
    const std::size_t semi = signals.find(';', close);
    if (semi == std::string_view::npos) {
      return Error("readStil: a signal declaration has no ';'");
    }
    const std::string_view direction = signals.substr(close + 1, semi - close - 1);
    StilSignal signal;
    signal.name = std::string(signals.substr(open + 1, close - open - 1));
    if (direction.find("Out") != std::string_view::npos) {
      signal.isInput = false;
    } else if (direction.find("In") != std::string_view::npos) {
      signal.isInput = true;
    } else {
      return Error("readStil: a signal declaration has no direction");
    }
    program.signals.push_back(std::move(signal));
    i = semi + 1;
  }

  const std::string_view groups = detail::braceSection(text, "SignalGroups");
  if (groups.empty()) {
    return Error("readStil: no SignalGroups block");
  }
  ATPG_ASSIGN_OR_RETURN(program.piGroup, detail::groupMembers(groups, "PI"));
  ATPG_ASSIGN_OR_RETURN(program.poGroup, detail::groupMembers(groups, "PO"));

  // "PatternBurst" and "PatternExec" also begin with "Pattern", so the
  // opener includes the space and quote that only the Pattern block has.
  const std::string_view pattern = detail::braceSection(text, "\nPattern \"");
  if (pattern.empty()) {
    return Error("readStil: no Pattern block");
  }
  std::size_t v = 0;
  while (true) {
    const std::size_t at = pattern.find("V {", v);
    if (at == std::string_view::npos) {
      break;
    }
    const std::size_t end = pattern.find('}', at);
    if (end == std::string_view::npos) {
      return Error("readStil: unterminated V statement");
    }
    const std::string_view body = pattern.substr(at + 3, end - at - 3);
    StilVector vector;
    ATPG_ASSIGN_OR_RETURN(vector.inputs, detail::fieldValue(body, "PI"));
    ATPG_ASSIGN_OR_RETURN(vector.outputs, detail::fieldValue(body, "PO"));
    program.vectors.push_back(std::move(vector));
    v = end + 1;
  }

  return program;
}

} // namespace atpg::testing
