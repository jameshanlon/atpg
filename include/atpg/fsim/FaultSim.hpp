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

/// Which patterns detect which fault classes: one row per fault class, in
/// the simulated FaultList's order, one bit per pattern.
///
/// The whole matrix, unlike SimResult, is what a caller needs in order to
/// choose between patterns - see atpg::compact.
class DetectionMatrix {
public:
  DetectionMatrix() = default;

  /// Returns the number of fault classes, i.e. the number of rows.
  std::size_t faultCount() const { return faults_.size(); }

  /// Returns the number of patterns, i.e. the number of columns.
  std::size_t patternCount() const { return patternCount_; }

  /// Returns whether `pattern` detects fault class `fault`. Both indices
  /// must be in range.
  bool detects(std::size_t fault, std::size_t pattern) const {
    return bits_[fault * patternCount_ + pattern];
  }

  /// Returns row `fault`'s fault class representative. The index must be in
  /// range.
  const fault::Fault& faultAt(std::size_t fault) const { return faults_[fault]; }

private:
  // Only detectAll builds a matrix: to every other caller it is an immutable
  // snapshot, so construction and mutation stay out of the public interface.
  friend Result<DetectionMatrix> detectAll(const ir::Graph& graph, const fault::FaultList& faults,
                                           const std::vector<std::vector<bool>>& patterns);

  DetectionMatrix(std::vector<fault::Fault> faults, std::size_t patternCount)
      : faults_(std::move(faults)), patternCount_(patternCount),
        bits_(faults_.size() * patternCount, false) {}

  void setDetected(std::size_t fault, std::size_t pattern) {
    bits_[fault * patternCount_ + pattern] = true;
  }

  std::vector<fault::Fault> faults_;
  std::size_t patternCount_ = 0;
  /// Row-major: faults_.size() rows of patternCount_ bits.
  std::vector<bool> bits_;
};

/// Simulates `patterns` against every fault class's representative fault in
/// `faults`, reporting which were detected and by which pattern first.
///
/// Each entry of `patterns` holds one bit per graph.primaryInputs(), in that
/// order - the same shape sim::simulate() takes. `graph` must already be
/// levelized (graph.levelize() called and ok()), and every representative's
/// `pin.gate` must be a valid gate id in `graph`.
///
/// Returns an Error if any pattern's width does not match the primary-input
/// count. An empty `patterns` is not an error: every fault simply comes back
/// undetected. See `src/fsim/FaultSim.cpp` for the implementation.
Result<SimResult> simulateFaults(const ir::Graph& graph, const fault::FaultList& faults,
                                 const std::vector<std::vector<bool>>& patterns);

/// Simulates `patterns` against every fault class's representative fault in
/// `faults`, recording every pattern that detects each one.
///
/// Same inputs and caller contract as simulateFaults: one bit per
/// graph.primaryInputs() per pattern, `graph` already levelized, and every
/// representative's `pin.gate` a valid gate id in `graph`.
///
/// Unlike simulateFaults this cannot drop a fault once detected, since the
/// whole matrix is wanted, so it simulates every fault against every pattern
/// and costs correspondingly more. Prefer simulateFaults when only coverage
/// and the first detecting pattern are needed.
///
/// Returns an Error if any pattern's width does not match the primary-input
/// count. An empty `patterns` is not an error: the matrix simply has no
/// columns.
Result<DetectionMatrix> detectAll(const ir::Graph& graph, const fault::FaultList& faults,
                                  const std::vector<std::vector<bool>>& patterns);

} // namespace atpg::fsim
