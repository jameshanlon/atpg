#pragma once

#include "atpg/ir/Gate.hpp"

#include <cstddef>
#include <vector>

namespace atpg::fault {

enum class StuckValue { SA0, SA1 };

enum class PinKind { Output, Input };

/// A single pin of a gate: either its output, or one of its numbered
/// input pins. `inputIndex` is unused when `kind == Output`.
struct PinRef {
  ir::GateId gate;
  PinKind kind;
  std::size_t inputIndex = 0;

  friend bool operator==(const PinRef&, const PinRef&) = default;
};

/// A stuck-at fault: `pin` held permanently at `value`.
struct Fault {
  PinRef pin;
  StuckValue value;

  friend bool operator==(const Fault&, const Fault&) = default;
};

/// A collapsed equivalence class of faults. `representative` is the fault
/// used for test generation and simulation; `equivalent` lists every other
/// fault proven to be detected by exactly the same test as
/// `representative`. A fanout stem's own output fault (a gate driving 2+
/// readers) is never merged into any branch's class, since it isn't
/// provably equivalent to any single branch when branches can reconverge
/// downstream - each polarity is either dropped entirely, when it was
/// proven an exact equivalence to one of the gate's own input faults
/// (e.g. both polarities for Buf/Not, one polarity for
/// And/Nand/Or/Nor), or kept as its own class otherwise (a primary
/// input, an Xor/Xnor gate, or the non-equivalence polarity of
/// And/Nand/Or/Nor, where the output fault only *dominates* the gate's
/// own input faults rather than being equivalent to them - dominance
/// alone isn't enough to drop a fault, since the dominated fault that
/// would stand in for it could itself be redundant). Other faults that
/// happen to be locally equivalent to a dropped stem polarity (e.g. a
/// primary input feeding that gate, or one of the gate's own input pins)
/// still get their own class, since the checkpoint theorem requires every
/// primary input and every fanout branch to remain represented regardless
/// of what it's locally equivalent to.
struct FaultClass {
  Fault representative;
  std::vector<Fault> equivalent;
};

} // namespace atpg::fault
