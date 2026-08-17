#pragma once

#include "atpg/Result.hpp"
#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace atpg::fsim {

/// The outcome for one fault class across a whole pattern set.
struct FaultStatus {
  /// The fault class's representative - the fault actually simulated.
  fault::Fault fault;
  bool detected = false;
  /// Index into the simulated pattern set of the first pattern that
  /// detected this fault. Only meaningful when `detected` is true.
  std::size_t firstDetectingPattern = 0;
};

/// Per-fault simulation results, one entry per fault class in the order of
/// the FaultList that was simulated.
class SimResult {
public:
  /// Appends one fault's result.
  void add(FaultStatus status) { statuses_.push_back(std::move(status)); }

  /// Returns the number of fault classes simulated.
  std::size_t size() const { return statuses_.size(); }

  /// Returns how many of those fault classes were detected.
  std::size_t detectedCount() const {
    std::size_t n = 0;
    for (const FaultStatus& status : statuses_) {
      if (status.detected) {
        ++n;
      }
    }
    return n;
  }

  /// Returns detectedCount() / size(), or 0.0 when nothing was simulated.
  double coverage() const {
    if (statuses_.empty()) {
      return 0.0;
    }
    return static_cast<double>(detectedCount()) / static_cast<double>(statuses_.size());
  }

  std::vector<FaultStatus>::const_iterator begin() const { return statuses_.begin(); }
  std::vector<FaultStatus>::const_iterator end() const { return statuses_.end(); }

private:
  std::vector<FaultStatus> statuses_;
};

/// Simulates `patterns` against every fault class's representative fault in
/// `faults`, reporting which were detected and by which pattern first.
///
/// Each entry of `patterns` holds one bit per graph.primaryInputs(), in that
/// order - the same shape sim::simulate() takes. `graph` must already be
/// levelized (graph.levelize() called and ok()).
///
/// Returns an Error if any pattern's width does not match the primary-input
/// count. An empty `patterns` is not an error: every fault simply comes back
/// undetected. See `src/fsim/FaultSim.cpp` for the implementation.
Result<SimResult> simulateFaults(const ir::Graph& graph, const fault::FaultList& faults,
                                 const std::vector<std::vector<bool>>& patterns);

} // namespace atpg::fsim
