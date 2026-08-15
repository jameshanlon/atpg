#pragma once

#include "atpg/Result.hpp"
#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <cstddef>
#include <vector>

namespace atpg::gen {

/// The result of attempting to generate a test for one fault.
enum class TestOutcome {
  Testable,  ///< `pattern` detects the fault.
  Redundant, ///< The solver proved no test can detect the fault.
  Aborted,   ///< The solver hit its time limit before resolving either way.
};

/// The outcome of generating a test for one fault class's representative.
struct TestResult {
  fault::Fault fault;
  TestOutcome outcome = TestOutcome::Aborted;
  /// One bit per graph.primaryInputs(), in that order. Only meaningful
  /// when `outcome == TestOutcome::Testable`.
  std::vector<bool> pattern;
};

/// Per-fault CP-SAT solver limits.
struct Options {
  /// Wall-clock time limit given to the solver for each individual fault.
  double timeLimitSeconds = 5.0;
};

/// An ordered collection of test-generation results, one per fault class in
/// the FaultList passed to generateTests().
class TestSet {
public:
  /// Adds a test-generation result to the set.
  void add(TestResult result) { results_.push_back(std::move(result)); }

  /// Returns the number of results.
  std::size_t size() const { return results_.size(); }

  std::vector<TestResult>::const_iterator begin() const { return results_.begin(); }
  std::vector<TestResult>::const_iterator end() const { return results_.end(); }

private:
  std::vector<TestResult> results_;
};

/// Generates a test pattern for every fault class's representative fault in
/// `faults` (or determines it's redundant, or aborts within
/// options.timeLimitSeconds) using a SAT-based miter construction. `graph`
/// must already be levelized (graph.levelize() called and ok()). See
/// `src/gen/TestGen.cpp` for the implementation.
///
/// Returns an Error for two distinct reasons: an invalid argument (e.g. a
/// negative `options.timeLimitSeconds`), naming the bad value - fix the
/// call site; or a self-verification failure, where CP-SAT's answer for a
/// fault disagrees with an independent simulation of its own reported
/// pattern - this should never happen and indicates a bug in atpg's SAT
/// encoding itself, not in the caller's input.
Result<TestSet> generateTests(const ir::Graph& graph, const fault::FaultList& faults,
                              Options options = {});

} // namespace atpg::gen
