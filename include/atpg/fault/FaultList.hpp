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

  std::vector<FaultClass>::const_iterator begin() const { return classes_.begin(); }
  std::vector<FaultClass>::const_iterator end() const { return classes_.end(); }

private:
  std::vector<FaultClass> classes_;
};

/// Enumerates every stuck-at fault in `graph` and collapses them in two
/// phases: local per-gate equivalence (e.g. an And gate's output SA0 and
/// every input's SA0 are the same fault), then the checkpoint theorem
/// (a fanout-1 gate's output fault is merged into its sole reader's input
/// fault; a fanout>=2 stem's own output fault is dropped, for whichever
/// polarity phase 1 proved an exact equivalence for, rather than merged
/// into any one branch, since that would be unsound whenever branches
/// reconverge downstream - every branch remains its own checkpoint). See
/// `src/fault/FaultList.cpp` for the implementation.
FaultList generateFaultList(const ir::Graph& graph);

} // namespace atpg::fault
