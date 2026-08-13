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
/// `representative`. Fanout-stem faults (a gate driving 2+ readers) are
/// never members of any class - they're dropped, not merged, since a stem
/// fault isn't provably equivalent to any single branch when branches can
/// reconverge downstream.
struct FaultClass {
  Fault representative;
  std::vector<Fault> equivalent;
};

} // namespace atpg::fault
