#pragma once

#include "atpg/fault/Fault.hpp"
#include "atpg/ir/Graph.hpp"

#include <utility>
#include <vector>

namespace atpg::fault {

/// An ordered collection of collapsed fault classes.
class FaultList {
public:
  /// Adds a collapsed fault class to the list.
  void add(FaultClass faultClass) { classes_.push_back(std::move(faultClass)); }

  /// Returns the number of collapsed fault classes.
  std::size_t size() const { return classes_.size(); }
  /// Returns the fault class at `index`. `index` must be < size().
  const FaultClass& at(std::size_t index) const { return classes_[index]; }

  std::vector<FaultClass>::const_iterator begin() const { return classes_.begin(); }
  std::vector<FaultClass>::const_iterator end() const { return classes_.end(); }

private:
  std::vector<FaultClass> classes_;
};

/// Enumerates every stuck-at fault in `graph` and collapses them via local
/// per-gate equivalence and the checkpoint theorem. See
/// docs/superpowers/specs/2026-08-13-fault-list-design.md for the algorithm.
FaultList generateFaultList(const ir::Graph& graph);

} // namespace atpg::fault
