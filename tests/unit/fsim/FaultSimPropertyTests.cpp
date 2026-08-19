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

// Scalar ground truth for a whole matrix row: which patterns expose `fault`.
// Like oracleFirstDetecting it uses this file's own independent simulator,
// never atpg::sim, and unlike it never stops early.
std::vector<bool> oracleDetectionRow(const Graph& graph, const std::vector<int>& piIndex,
                                     const std::vector<std::vector<bool>>& patterns,
                                     const Fault& fault) {
  std::vector<bool> row(patterns.size(), false);
  for (std::size_t p = 0; p < patterns.size(); ++p) {
    const std::vector<bool> good = simulate(graph, piIndex, patterns[p], InjectedFault{});
    const std::vector<bool> faulty = simulate(graph, piIndex, patterns[p], injectedFrom(fault));
    row[p] = good != faulty;
  }
  return row;
}

std::vector<std::string> checkAgainstGroundTruth(const Graph& graph, const FaultList& faults,
                                                 const std::vector<std::vector<bool>>& patterns) {
  std::vector<std::string> violations;

  const std::vector<int> piIndex = primaryInputIndex(graph);

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

    const std::size_t patternCount = patternCountDist(rng);
    const std::vector<std::vector<bool>> patterns = randomPatterns(rng, graph, patternCount);

    // Both fault-list shapes the API accepts: the collapsed list a caller
    // normally passes, and every atomic fault as its own class. The latter
    // reaches fault shapes collapsing discards, which the collapsed list
    // alone would never present to the simulator.
    struct Corpus {
      const char* what;
      FaultList faults;
    };
    const Corpus corpora[] = {{"collapsed fault list", generateFaultList(graph)},
                              {"all atomic faults", allAtomsAsClasses(graph)}};

    for (const Corpus& corpus : corpora) {
      const std::vector<std::string> violations =
          checkAgainstGroundTruth(graph, corpus.faults, patterns);
      if (!violations.empty()) {
        INFO("circuit #" << i << " (seed " << kSeed << ", " << patternCount << " patterns, "
                         << corpus.what << "):\n"
                         << dumpCircuit(graph));
        for (const auto& violation : violations) {
          UNSCOPED_INFO(violation);
        }
        FAIL("simulateFaults disagreed with scalar simulation");
      }
    }
  }
}

TEST_CASE("detectAll agrees with scalar simulation and with simulateFaults",
          "[FaultSim][property]") {
  // Two checks per circuit: every matrix cell against the independent
  // oracle, and the matrix's derived summary against simulateFaults, the
  // already-verified entry point it now shares an engine with. Recording
  // every cell costs more than the first-detection sweep, so this runs
  // fewer and smaller cases than the simulateFaults property test.
  constexpr int kIterations = 60;
  constexpr unsigned kSeed = 20260819;

  std::mt19937 rng(kSeed);
  std::uniform_int_distribution<std::size_t> patternCountDist(1, 80);

  for (int i = 0; i < kIterations; ++i) {
    Graph graph = randomCircuit(rng);
    REQUIRE(graph.levelize().ok());

    const std::size_t patternCount = patternCountDist(rng);
    const std::vector<std::vector<bool>> patterns = randomPatterns(rng, graph, patternCount);

    const FaultList faults = allAtomsAsClasses(graph);
    const std::vector<int> piIndex = primaryInputIndex(graph);

    const atpg::Result<DetectionMatrix> matrixResult = detectAll(graph, faults, patterns);
    REQUIRE(matrixResult.ok());
    const DetectionMatrix& matrix = matrixResult.value();

    const atpg::Result<SimResult> simResult = simulateFaults(graph, faults, patterns);
    REQUIRE(simResult.ok());

    REQUIRE(matrix.faultCount() == faults.size());
    REQUIRE(matrix.patternCount() == patternCount);

    std::vector<std::string> violations;
    std::size_t f = 0;
    for (const FaultStatus& status : simResult.value()) {
      if (keyOf(matrix.faultAt(f)) != keyOf(status.fault)) {
        violations.push_back("detectAll and simulateFaults disagree on a row's fault");
        break;
      }

      const std::vector<bool> oracle = oracleDetectionRow(graph, piIndex, patterns, status.fault);

      bool any = false;
      std::size_t first = patternCount;
      for (std::size_t p = 0; p < patternCount; ++p) {
        if (matrix.detects(f, p) != oracle[p]) {
          violations.push_back("a matrix cell disagrees with independent simulation");
        }
        if (matrix.detects(f, p) && !any) {
          any = true;
          first = p;
        }
      }

      if (any != status.detected) {
        violations.push_back("a row's emptiness disagrees with simulateFaults' detected flag");
      }
      if (any && first != status.firstDetectingPattern) {
        violations.push_back("the row's first set bit is not simulateFaults' first detection");
      }
      ++f;
    }

    if (!violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << "):\n" << dumpCircuit(graph));
      for (const auto& violation : violations) {
        UNSCOPED_INFO(violation);
      }
      FAIL("detectAll disagreed with its ground truth");
    }
  }
}
