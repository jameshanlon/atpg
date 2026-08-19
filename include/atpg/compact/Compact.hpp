#pragma once

#include "atpg/Result.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <cstddef>
#include <vector>

namespace atpg::compact {

/// A compacted pattern set: the kept patterns, and where each came from.
struct CompactResult {
  /// The kept patterns, in their original relative order.
  std::vector<std::vector<bool>> patterns;
  /// Index into the input pattern set for each kept pattern, ascending.
  std::vector<std::size_t> keptIndices;
  /// How many fault classes the kept patterns detect - by construction the
  /// same number the input set detected.
  std::size_t detectedFaults = 0;
  /// How many fault classes were considered, i.e. `faults.size()`.
  std::size_t faultCount = 0;
};

/// Selects a subset of `patterns` detecting exactly the same fault classes
/// of `faults` as the whole set does.
///
/// Greedily takes the pattern covering the most still-uncovered faults,
/// breaking ties towards the lower index, then removes any selected pattern
/// whose every fault is also detected by another. The result is therefore
/// *irredundant* - dropping any one of its patterns loses at least one
/// detected fault - but not necessarily *minimum*: minimum set cover is
/// NP-hard, and a smaller hand-found set is not a bug.
///
/// Faults no input pattern detects stay undetected: compaction neither loses
/// coverage nor gains it. `graph` must already be levelized
/// (graph.levelize() called and ok()).
///
/// Returns an Error if any pattern's width does not match the primary-input
/// count, or if the compacted set fails the internal check that it preserves
/// the input set's coverage - the latter should never happen and would
/// indicate a bug in atpg rather than in the caller's input. An empty
/// `patterns` is not an error: it yields an empty result.
Result<CompactResult> compact(const ir::Graph& graph, const fault::FaultList& faults,
                              const std::vector<std::vector<bool>>& patterns);

} // namespace atpg::compact
