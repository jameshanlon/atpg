#include "atpg/compact/Compact.hpp"

#include "atpg/fsim/FaultSim.hpp"

#include <cstddef>
#include <vector>

namespace atpg::compact {

namespace {

/// For each pattern, the indices of the fault classes it detects. Both the
/// greedy selection and the irredundancy pass walk the matrix this way
/// round, so it is transposed once here rather than rescanned per round.
std::vector<std::vector<std::size_t>> detectionsByPattern(const fsim::DetectionMatrix& matrix) {
  std::vector<std::vector<std::size_t>> byPattern(matrix.patternCount());
  for (std::size_t f = 0; f < matrix.faultCount(); ++f) {
    for (std::size_t p = 0; p < matrix.patternCount(); ++p) {
      if (matrix.detects(f, p)) {
        byPattern[p].push_back(f);
      }
    }
  }
  return byPattern;
}

} // namespace

Result<CompactResult> compact(const ir::Graph& graph, const fault::FaultList& faults,
                              const std::vector<std::vector<bool>>& patterns) {
  ATPG_ASSIGN_OR_RETURN(const fsim::DetectionMatrix matrix,
                        fsim::detectAll(graph, faults, patterns));

  const std::size_t patternCount = matrix.patternCount();
  const std::size_t faultCount = matrix.faultCount();
  const std::vector<std::vector<std::size_t>> detectedBy = detectionsByPattern(matrix);

  // -- greedy cover ---------------------------------------------------------
  // Runs until no pattern adds anything, which covers exactly the faults some
  // input pattern detects. Faults nothing detects are outside compaction's
  // remit, and leaving them out is what makes coverage preservation exact
  // rather than approximate.
  std::vector<bool> covered(faultCount, false);
  std::vector<bool> selected(patternCount, false);
  std::vector<std::size_t> order;
  std::size_t coveredCount = 0;

  while (true) {
    std::size_t best = 0;
    std::size_t bestGain = 0;
    // Scanning ascending with a strict `>` keeps the lowest index on a tie,
    // so the selection is deterministic.
    for (std::size_t p = 0; p < patternCount; ++p) {
      if (selected[p]) {
        continue;
      }
      std::size_t gain = 0;
      for (const std::size_t f : detectedBy[p]) {
        if (!covered[f]) {
          ++gain;
        }
      }
      if (gain > bestGain) {
        bestGain = gain;
        best = p;
      }
    }
    if (bestGain == 0) {
      break;
    }

    selected[best] = true;
    order.push_back(best);
    for (const std::size_t f : detectedBy[best]) {
      if (!covered[f]) {
        covered[f] = true;
        ++coveredCount;
      }
    }
  }

  // -- irredundancy pass ----------------------------------------------------
  // One pass suffices: a pattern kept here had some fault covered only by
  // it, counts only ever decrease afterwards, and that count cannot reach
  // zero while the pattern itself is still counted.
  std::vector<std::size_t> coverCount(faultCount, 0);
  for (const std::size_t p : order) {
    for (const std::size_t f : detectedBy[p]) {
      ++coverCount[f];
    }
  }
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    const std::size_t p = *it;
    bool removable = true;
    for (const std::size_t f : detectedBy[p]) {
      if (coverCount[f] < 2) {
        removable = false;
        break;
      }
    }
    if (removable) {
      selected[p] = false;
      for (const std::size_t f : detectedBy[p]) {
        --coverCount[f];
      }
    }
  }

  CompactResult result;
  for (std::size_t p = 0; p < patternCount; ++p) {
    if (selected[p]) {
      result.keptIndices.push_back(p);
      result.patterns.push_back(patterns[p]);
    }
  }

  // Self-check on this function's own bookkeeping, in the style the SAT
  // engine and the dropping loop already use: recompute the kept set's
  // coverage from scratch and confirm nothing was lost.
  std::vector<bool> finalCovered(faultCount, false);
  std::size_t finalCount = 0;
  for (const std::size_t p : result.keptIndices) {
    for (const std::size_t f : detectedBy[p]) {
      if (!finalCovered[f]) {
        finalCovered[f] = true;
        ++finalCount;
      }
    }
  }
  if (finalCount != coveredCount) {
    return Error("compact: the compacted set does not preserve the input set's coverage");
  }

  result.detectedFaults = coveredCount;
  result.faultCount = faultCount;
  return result;
}

} // namespace atpg::compact
