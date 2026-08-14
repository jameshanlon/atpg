#include "atpg/sim/LogicSim.hpp"

#include <functional>

namespace atpg::sim {

namespace {

template <typename BinaryOp, typename Read>
bool fold(std::size_t arity, bool identity, bool negate, BinaryOp op, Read&& read) {
  bool result = identity;
  for (std::size_t i = 0; i < arity; ++i) {
    result = op(result, read(i));
  }
  return negate ? !result : result;
}

template <typename Read> Result<bool> evaluateGate(const ir::Gate& gate, Read&& read) {
  switch (gate.type) {
    case ir::GateType::And:
      return fold(gate.fanin.size(), true, false, std::logical_and<>{}, read);
    case ir::GateType::Nand:
      return fold(gate.fanin.size(), true, true, std::logical_and<>{}, read);
    case ir::GateType::Or:
      return fold(gate.fanin.size(), false, false, std::logical_or<>{}, read);
    case ir::GateType::Nor:
      return fold(gate.fanin.size(), false, true, std::logical_or<>{}, read);
    case ir::GateType::Xor:
      return fold(gate.fanin.size(), false, false, std::not_equal_to<>{}, read);
    case ir::GateType::Xnor:
      return fold(gate.fanin.size(), false, true, std::not_equal_to<>{}, read);
    case ir::GateType::Buf:
      return static_cast<bool>(read(0));
    case ir::GateType::Not:
      return !read(0);
    case ir::GateType::Po:
      return static_cast<bool>(read(0));
    case ir::GateType::Pi:
      return Error("simulate: primary input has no assigned value");
  }
  return Error("simulate: unhandled gate type");
}

// `faultPin == nullptr` means "no fault" - the good-circuit case.
Result<std::vector<bool>> simulateImpl(const ir::Graph& graph, const std::vector<bool>& piValues,
                                       const fault::PinRef* faultPin,
                                       fault::StuckValue faultValue) {
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
    const ir::Gate& gate = graph.gate(id);
    if (gate.type == ir::GateType::Pi) {
      if (faultPin != nullptr && faultPin->kind == fault::PinKind::Output && faultPin->gate == id) {
        values[id] = faultValue == fault::StuckValue::SA1;
      }
      continue;
    }

    auto read = [&](std::size_t i) -> bool {
      if (faultPin != nullptr && faultPin->kind == fault::PinKind::Input && faultPin->gate == id &&
          faultPin->inputIndex == i) {
        return faultValue == fault::StuckValue::SA1;
      }
      return values[gate.fanin[i]];
    };

    Result<bool> result = evaluateGate(gate, read);
    if (!result) {
      return Error(result.error());
    }

    bool v = result.value();
    if (faultPin != nullptr && faultPin->kind == fault::PinKind::Output && faultPin->gate == id) {
      v = faultValue == fault::StuckValue::SA1;
    }
    values[id] = v;
  }

  std::vector<bool> outputs;
  outputs.reserve(graph.primaryOutputs().size());
  for (const ir::GateId id : graph.primaryOutputs()) {
    outputs.push_back(values[id]);
  }
  return outputs;
}

} // namespace

Result<std::vector<bool>> simulate(const ir::Graph& graph, const std::vector<bool>& piValues) {
  return simulateImpl(graph, piValues, nullptr, fault::StuckValue::SA0);
}

Result<std::vector<bool>> simulateWithFault(const ir::Graph& graph,
                                            const std::vector<bool>& piValues,
                                            const fault::PinRef& pin, fault::StuckValue value) {
  return simulateImpl(graph, piValues, &pin, value);
}

} // namespace atpg::sim
