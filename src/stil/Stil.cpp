#include "atpg/stil/Stil.hpp"

#include "atpg/sim/LogicSim.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace atpg::stil {

namespace {

/// The `"a" + "b" + "c"` membership expression of a SignalGroups entry.
std::string groupExpression(const ir::Graph& graph, const std::vector<ir::GateId>& ids) {
  std::string expression;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    fmt::format_to(std::back_inserter(expression), "{}\"{}\"", i == 0 ? "" : " + ",
                   graph.gate(ids[i]).name);
  }
  return expression;
}

/// `bits` rendered with `zero`/`one` as its two characters - `0`/`1` for
/// stimulus, `L`/`H` for an expected response.
std::string toChars(const std::vector<bool>& bits, char zero, char one) {
  std::string text;
  text.reserve(bits.size());
  for (const bool bit : bits) {
    text.push_back(bit ? one : zero);
  }
  return text;
}

} // namespace

Result<std::string> writeStil(const ir::Graph& graph,
                              const std::vector<std::vector<bool>>& patterns,
                              std::string_view designName) {
  const std::vector<ir::GateId>& pis = graph.primaryInputs();
  const std::vector<ir::GateId>& pos = graph.primaryOutputs();

  if (pis.empty()) {
    return Error("writeStil: the design has no primary inputs, so there is no stimulus to apply");
  }
  if (pos.empty()) {
    return Error("writeStil: the design has no primary outputs, so there is no response to check");
  }

  // STIL requires signal names to be unique across the whole Signals block,
  // so inputs and outputs are checked together rather than separately.
  std::set<std::string> seen;
  for (const std::vector<ir::GateId>* group : {&pis, &pos}) {
    for (const ir::GateId id : *group) {
      if (!seen.insert(graph.gate(id).name).second) {
        return Error(fmt::format("writeStil: duplicate signal name \"{}\"", graph.gate(id).name));
      }
    }
  }

  for (const std::vector<bool>& pattern : patterns) {
    if (pattern.size() != pis.size()) {
      return Error("writeStil: pattern width does not match primary input count");
    }
  }

  std::string out;
  auto emit = std::back_inserter(out);

  out += "STIL 1.0;\n\n";
  fmt::format_to(emit, "Header {{\n  Title \"atpg-generated test patterns for {}\";\n}}\n\n",
                 designName);

  out += "Signals {\n";
  for (const ir::GateId id : pis) {
    fmt::format_to(emit, "  \"{}\" In;\n", graph.gate(id).name);
  }
  for (const ir::GateId id : pos) {
    fmt::format_to(emit, "  \"{}\" Out;\n", graph.gate(id).name);
  }
  out += "}\n\n";

  fmt::format_to(emit, "SignalGroups {{\n  \"PI\" = '{}';\n  \"PO\" = '{}';\n}}\n\n",
                 groupExpression(graph, pis), groupExpression(graph, pos));

  out += R"(Timing "timing" {
  WaveformTable "wft" {
    Period '100ns';
    Waveforms {
      "PI" { 01 { '0ns' D/U; } }
      "PO" { LH { '50ns' L/H; } }
    }
  }
}

PatternBurst "burst" {
  PatList { "patterns" { } }
}

PatternExec {
  Timing "timing";
  PatternBurst "burst";
}

Pattern "patterns" {
  W "wft";
)";
  for (const std::vector<bool>& pattern : patterns) {
    ATPG_ASSIGN_OR_RETURN(const std::vector<bool> outputs, sim::simulate(graph, pattern));

    fmt::format_to(emit, "  V {{ \"PI\"={}; \"PO\"={}; }}\n", toChars(pattern, '0', '1'),
                   toChars(outputs, 'L', 'H'));
  }
  out += "}\n";

  return out;
}

} // namespace atpg::stil
