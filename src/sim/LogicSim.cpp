#include "atpg/sim/LogicSim.hpp"

#include <functional>

namespace atpg::sim {

namespace {

template <typename BinaryOp>
bool fold(const std::vector<ir::GateId>& fanin, const std::vector<bool>& values, bool identity,
          bool negate, BinaryOp op) {
  bool result = identity;
  for (const ir::GateId in : fanin) {
    result = op(result, values[in]);
  }
  return negate ? !result : result;
}

Result<bool> evaluate(const ir::Gate& gate, const std::vector<bool>& values) {
  switch (gate.type) {
    case ir::GateType::And:
      return fold(gate.fanin, values, /* identity */ true, /* negate */ false,
                  std::logical_and<>{});
    case ir::GateType::Nand:
      return fold(gate.fanin, values, /* identity */ true, /* negate */ true, std::logical_and<>{});
    case ir::GateType::Or:
      return fold(gate.fanin, values, /* identity */ false, /* negate */ false,
                  std::logical_or<>{});
    case ir::GateType::Nor:
      return fold(gate.fanin, values, /* identity */ false, /* negate */ true, std::logical_or<>{});
    case ir::GateType::Xor:
      return fold(gate.fanin, values, /* identity */ false, /* negate */ false,
                  std::not_equal_to<>{});
    case ir::GateType::Xnor:
      return fold(gate.fanin, values, /* identity */ false, /* negate */ true,
                  std::not_equal_to<>{});
    case ir::GateType::Buf:
      return static_cast<bool>(values[gate.fanin[0]]);
    case ir::GateType::Not:
      return !values[gate.fanin[0]];
    case ir::GateType::Po:
      return static_cast<bool>(values[gate.fanin[0]]);
    case ir::GateType::Pi:
      return Error("simulate: primary input has no assigned value");
  }
  return Error("simulate: unhandled gate type");
}

} // namespace

Result<std::vector<bool>> simulate(const ir::Graph& graph, const std::vector<bool>& piValues) {
  const auto& pis = graph.primaryInputs();
  if (piValues.size() != pis.size()) {
    return Error("simulate: stimulus width does not match primary input count");
  }

  std::vector<bool> values(graph.size(), false);
  for (std::size_t i = 0; i < pis.size(); ++i) {
    values[pis[i]] = piValues[i];
  }

  // Every non-PI gate appears in levelOrder() exactly once, after all of its
  // fanin; PIs are already assigned above, so they're simply skipped here.
  for (const ir::GateId id : graph.levelOrder()) {
    if (graph.gate(id).type != ir::GateType::Pi) {
      Result<bool> result = evaluate(graph.gate(id), values);
      if (!result) {
        return Error(result.error());
      }
      values[id] = result.value();
    }
  }

  std::vector<bool> outputs;
  outputs.reserve(graph.primaryOutputs().size());
  for (const ir::GateId id : graph.primaryOutputs()) {
    outputs.push_back(values[id]);
  }
  return outputs;
}

} // namespace atpg::sim
