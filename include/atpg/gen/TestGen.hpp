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

/// How one fault class was resolved by the generate-and-drop loop.
struct FaultResolution {
  /// The fault class's representative.
  fault::Fault fault;
  TestOutcome outcome = TestOutcome::Aborted;
  /// Index into TestPlan::patterns() of a pattern that detects this fault.
  /// Only meaningful when `outcome == TestOutcome::Testable`. Many faults
  /// share an index: that is the point of the loop.
  std::size_t patternIndex = 0;
};

/// The pattern set produced by the generate-and-drop loop, plus how each
/// fault class was resolved.
///
/// Two named accessors rather than the size()/begin()/end() shape used by
/// TestSet and FaultList: this holds two collections, so a bare begin()
/// would be ambiguous about which one it iterates.
class TestPlan {
public:
  /// Appends a generated pattern.
  void addPattern(std::vector<bool> pattern) { patterns_.push_back(std::move(pattern)); }
  /// Appends one fault class's resolution.
  void addResolution(FaultResolution resolution) { resolutions_.push_back(std::move(resolution)); }

  /// Generated patterns, in generation order. Normally far fewer than the
  /// fault count, since one pattern typically resolves many faults.
  const std::vector<std::vector<bool>>& patterns() const { return patterns_; }

  /// One entry per fault class, in the input FaultList's order.
  const std::vector<FaultResolution>& resolutions() const { return resolutions_; }

private:
  std::vector<std::vector<bool>> patterns_;
  std::vector<FaultResolution> resolutions_;
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

/// Same result as generateTests, reached with far fewer solver calls.
///
/// Walks `faults` in order; for each class not already resolved, solves it
/// with the SAT engine, then fault-simulates the resulting pattern against
/// the remaining unresolved faults and marks every fault it also detects as
/// Testable sharing that pattern.
///
/// Outcomes match generateTests, with one asymmetry: a fault dropped by
/// simulation never reaches the solver, so it is reported Testable even
/// where generateTests would have exhausted its time limit and reported
/// Aborted. The difference only ever runs that way - dropping can turn an
/// Aborted into a Testable, never the reverse, and a Redundant fault is
/// never dropped, since only a pattern that actually detects a fault can
/// drop it.
///
/// `graph` must already be levelized (graph.levelize() called and ok()).
///
/// Returns an Error for an invalid argument (e.g. a negative
/// `options.timeLimitSeconds`), for any error propagated out of the SAT
/// engine or the fault simulator, or if a fault the simulator reported as
/// detected turns out not to be detected by that pattern under independent
/// simulation - the last of these should never happen and indicates a bug
/// in atpg rather than in the caller's input.
Result<TestPlan> generateTestsWithDropping(const ir::Graph& graph, const fault::FaultList& faults,
                                           Options options = {});

} // namespace atpg::gen
