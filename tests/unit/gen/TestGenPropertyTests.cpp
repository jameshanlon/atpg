#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/gen/TestGen.hpp"
#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "../RandomCircuit.hpp"

using namespace atpg::fault;
using namespace atpg::gen;
using namespace atpg::ir;
using namespace atpg::testing;

namespace {

// Whether any input vector at all distinguishes the faulty circuit from the
// good one - the ground truth `generateTests`' Testable/Redundant verdict
// must agree with. Computed with this file's own simulator, never
// atpg::sim, so a bug shared between the SAT encoding and the production
// simulator cannot hide here.
bool oracleDetectable(const Graph& graph, const std::vector<int>& piIndex,
                      const std::vector<std::vector<bool>>& goodOutputs, const Fault& fault) {
  const std::size_t piCount = graph.primaryInputs().size();
  for (std::size_t v = 0; v < goodOutputs.size(); ++v) {
    if (simulate(graph, piIndex, patternOf(v, piCount), injectedFrom(fault)) != goodOutputs[v]) {
      return true;
    }
  }
  return false;
}

// Checks generateTests(graph, generateFaultList(graph)) against exhaustive
// brute force, returning a description of every violation found.
std::vector<std::string> checkAgainstGroundTruth(const Graph& graph) {
  std::vector<std::string> violations;

  const std::size_t piCount = graph.primaryInputs().size();
  const std::size_t vectorCount = std::size_t{1} << piCount;
  const std::vector<int> piIndex = primaryInputIndex(graph);

  std::vector<std::vector<bool>> goodOutputs(vectorCount);
  for (std::size_t v = 0; v < vectorCount; ++v) {
    goodOutputs[v] = simulate(graph, piIndex, patternOf(v, piCount), InjectedFault{});
  }

  const FaultList faults = generateFaultList(graph);
  const atpg::Result<TestSet> testsResult = generateTests(graph, faults);
  if (!testsResult.ok()) {
    violations.push_back("generateTests returned an error: " + testsResult.error());
    return violations;
  }
  const TestSet& tests = testsResult.value();

  if (tests.size() != faults.size()) {
    violations.push_back("result count does not match the fault-class count");
    return violations;
  }

  auto faultClass = faults.begin();
  for (const auto& result : tests) {
    const Fault& expected = faultClass->representative;
    ++faultClass;

    // Results must line up one-to-one with the input fault classes, in
    // order, each carrying back its class's representative.
    if (keyOf(result.fault) != keyOf(expected)) {
      violations.push_back("result does not carry its fault class's representative, in order");
      continue;
    }

    // These circuits are tiny; the default time limit is seconds. An abort
    // here would mean the solver is behaving very differently than assumed,
    // which is worth failing on rather than skipping past.
    if (result.outcome == TestOutcome::Aborted) {
      violations.push_back("solver aborted on a circuit small enough to solve trivially");
      continue;
    }

    const bool detectable = oracleDetectable(graph, piIndex, goodOutputs, result.fault);

    if (result.outcome == TestOutcome::Redundant) {
      if (detectable) {
        violations.push_back("fault reported Redundant, but brute force finds a detecting pattern");
      }
      continue;
    }

    // Testable: the outcome must match the oracle, and the returned pattern
    // must genuinely expose the fault at a primary output.
    if (!detectable) {
      violations.push_back("fault reported Testable, but no input vector detects it");
      continue;
    }
    if (result.pattern.size() != piCount) {
      violations.push_back("returned pattern width does not match the primary-input count");
      continue;
    }
    if (simulate(graph, piIndex, result.pattern, injectedFrom(result.fault)) ==
        simulate(graph, piIndex, result.pattern, InjectedFault{})) {
      violations.push_back("returned pattern does not actually detect its fault");
    }
  }

  return violations;
}

} // namespace

TEST_CASE("generateTests agrees with exhaustive brute force on random circuits",
          "[TestGen][property]") {
  // Deliberately independent of atpg::sim: generateTests already
  // self-verifies its Testable patterns with sim::simulateWithFault, so
  // reusing that here would only confirm the encoder agrees with itself. A
  // bug shared between the two - the exact shape that occurred twice while
  // this engine was built - is only visible against a separate oracle.
  //
  // Default corpus size keeps ctest fast; bump kIterations locally for a
  // deeper sweep when touching the encoding in src/gen/TestGen.cpp.
  constexpr int kIterations = 150;
  constexpr unsigned kSeed = 20260815;

  std::mt19937 rng(kSeed);
  for (int i = 0; i < kIterations; ++i) {
    Graph graph = randomCircuit(rng);
    REQUIRE(graph.levelize().ok());

    const std::vector<std::string> violations = checkAgainstGroundTruth(graph);
    if (!violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << "):\n" << dumpCircuit(graph));
      for (const auto& violation : violations) {
        INFO(violation);
      }
      FAIL("generateTests disagreed with exhaustive brute force");
    }
  }
}
