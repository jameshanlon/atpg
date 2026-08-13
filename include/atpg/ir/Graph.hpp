#pragma once

#include "atpg/Result.hpp"
#include "atpg/ir/Gate.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace atpg::ir {

/// A flattened, purely combinational gate-level netlist.
class Graph {
public:
  /// Adds a new gate node and returns its id. If `type` is Pi or Po, the
  /// gate is also registered in primaryInputs()/primaryOutputs().
  GateId addGate(GateType type, std::string name);

  /// Adds a directed edge from a driving gate's output to a consuming gate's
  /// input pin.
  void addEdge(GateId from, GateId to);

  /// Returns the index into `gate(consumer).fanin` at which `driver`
  /// appears. `driver` must be one of `consumer`'s fanin (e.g. via a prior
  /// addEdge(driver, consumer) call).
  std::size_t inputIndex(GateId consumer, GateId driver) const;

  /// Assigns levels to every gate (primary inputs are level 0, every other
  /// gate is one more than the maximum level of its fanin) and computes a
  /// topological evaluation order. Fails if the graph contains a
  /// combinational cycle.
  Status levelize();

  /// Returns the gate with the given id. `id` must have come from this
  /// graph (e.g. via addGate(), primaryInputs(), levelOrder()).
  const Gate& gate(GateId id) const { return gates_[id]; }
  /// Returns the gate with the given id. `id` must have come from this
  /// graph (e.g. via addGate(), primaryInputs(), levelOrder()).
  Gate& gate(GateId id) { return gates_[id]; }

  /// Returns the number of gates in the graph.
  std::size_t size() const { return gates_.size(); }

  /// Returns the ids of every Pi gate, in the order they were added.
  const std::vector<GateId>& primaryInputs() const { return primaryInputs_; }
  /// Returns the ids of every Po gate, in the order they were added.
  const std::vector<GateId>& primaryOutputs() const { return primaryOutputs_; }

  /// Gates in non-decreasing level order. Valid after levelize().
  const std::vector<GateId>& levelOrder() const { return levelOrder_; }

private:
  std::vector<Gate> gates_;
  std::vector<GateId> primaryInputs_;
  std::vector<GateId> primaryOutputs_;
  std::vector<GateId> levelOrder_;
};

} // namespace atpg::ir
