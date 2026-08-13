#pragma once

#include "atpg/fault/Fault.hpp"
#include "atpg/ir/Graph.hpp"

#include <utility>
#include <vector>

namespace atpg::fault {

/// An ordered collection of collapsed fault classes.
class FaultList {
public:
  void add(FaultClass faultClass) { classes_.push_back(std::move(faultClass)); }

  std::size_t size() const { return classes_.size(); }
  const FaultClass& at(std::size_t index) const { return classes_.at(index); }

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
