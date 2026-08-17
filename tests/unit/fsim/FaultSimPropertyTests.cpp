#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/fsim/FaultSim.hpp"
#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "../RandomCircuit.hpp"

using namespace atpg::fault;
using namespace atpg::fsim;
using namespace atpg::ir;
using namespace atpg::testing;

namespace {

// Scalar ground truth: the index of the first pattern that exposes `fault`,
// or patterns.size() if none does. Uses this file's own independent
// simulator, never atpg::sim - a bug shared between the bit-parallel engine
// and the production scalar simulator would otherwise be invisible.
std::size_t oracleFirstDetecting(const Graph& graph, const std::vector<int>& piIndex,
                                 const std::vector<std::vector<bool>>& patterns,
                                 const Fault& fault) {
  for (std::size_t p = 0; p < patterns.size(); ++p) {
    const std::vector<bool> good = simulate(graph, piIndex, patterns[p], InjectedFault{});
    const std::vector<bool> faulty = simulate(graph, piIndex, patterns[p], injectedFrom(fault));
    if (good != faulty) {
      return p;
    }
  }
  return patterns.size();
}

std::vector<std::string> checkAgainstGroundTruth(const Graph& graph,
                                                 const std::vector<std::vector<bool>>& patterns) {
  std::vector<std::string> violations;

  const std::vector<int> piIndex = primaryInputIndex(graph);
  const FaultList faults = generateFaultList(graph);

  const atpg::Result<SimResult> simResult = simulateFaults(graph, faults, patterns);
  if (!simResult.ok()) {
    violations.push_back("simulateFaults returned an error: " + simResult.error());
    return violations;
  }
  const SimResult& results = simResult.value();

  if (results.size() != faults.size()) {
    violations.push_back("result count does not match the fault-class count");
    return violations;
  }

  auto faultClass = faults.begin();
  for (const FaultStatus& status : results) {
    const Fault& expected = faultClass->representative;
    ++faultClass;

    if (keyOf(status.fault) != keyOf(expected)) {
      violations.push_back("result does not carry its fault class's representative, in order");
      continue;
    }

    const std::size_t oracle = oracleFirstDetecting(graph, piIndex, patterns, status.fault);
    const bool oracleDetected = oracle < patterns.size();

    if (status.detected != oracleDetected) {
      violations.push_back(status.detected ? "fault reported detected, but no pattern exposes it"
                                           : "fault reported undetected, but a pattern exposes it");
      continue;
    }
    if (status.detected && status.firstDetectingPattern != oracle) {
      violations.push_back("firstDetectingPattern does not match the earliest detecting pattern");
    }
  }

  return violations;
}

} // namespace

TEST_CASE("simulateFaults agrees with scalar simulation on random circuits",
          "[FaultSim][property]") {
  // Deliberately independent of atpg::sim - see oracleFirstDetecting above.
  // Pattern counts deliberately straddle the 64-pattern packet boundary so
  // random circuits exercise partial final packets and multi-packet runs,
  // not just the single-packet case.
  constexpr int kIterations = 120;
  constexpr unsigned kSeed = 20260817;

  std::mt19937 rng(kSeed);
  std::uniform_int_distribution<std::size_t> patternCountDist(1, 130);

  for (int i = 0; i < kIterations; ++i) {
    Graph graph = randomCircuit(rng);
    REQUIRE(graph.levelize().ok());

    const std::size_t piCount = graph.primaryInputs().size();
    const std::size_t patternCount = patternCountDist(rng);
    std::vector<std::vector<bool>> patterns(patternCount, std::vector<bool>(piCount, false));
    for (auto& pattern : patterns) {
      for (std::size_t b = 0; b < piCount; ++b) {
        pattern[b] = (rng() & 1) != 0;
      }
    }

    const std::vector<std::string> violations = checkAgainstGroundTruth(graph, patterns);
    if (!violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << ", " << patternCount << " patterns):\n"
                       << dumpCircuit(graph));
      for (const auto& violation : violations) {
        INFO(violation);
      }
      FAIL("simulateFaults disagreed with scalar simulation");
    }
  }
}
