#include "atpg/compact/Compact.hpp"
#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "../RandomCircuit.hpp"

using namespace atpg::compact;
using namespace atpg::fault;
using namespace atpg::ir;
using namespace atpg::testing;

namespace {

/// Which of `atoms` at least one of `patterns` detects, derived with the
/// independent oracle rather than atpg::fsim - the module under test is
/// built on atpg::fsim, so a shared bug would agree with itself.
std::vector<bool> detectedSet(const Graph& graph, const std::vector<int>& piIndex,
                              const std::vector<Fault>& atoms,
                              const std::vector<std::vector<bool>>& patterns) {
  std::vector<bool> detected(atoms.size(), false);
  for (const std::vector<bool>& pattern : patterns) {
    const std::vector<bool> good = simulate(graph, piIndex, pattern, InjectedFault{});
    for (std::size_t f = 0; f < atoms.size(); ++f) {
      if (detected[f]) {
        continue;
      }
      if (simulate(graph, piIndex, pattern, injectedFrom(atoms[f])) != good) {
        detected[f] = true;
      }
    }
  }
  return detected;
}

struct CheckOutcome {
  std::vector<std::string> violations;
  std::size_t inputCount = 0;
  std::size_t keptCount = 0;
};

CheckOutcome checkCompaction(const Graph& graph, const std::vector<std::vector<bool>>& patterns) {
  CheckOutcome outcome;
  std::vector<std::string>& violations = outcome.violations;

  const FaultList faults = allAtomsAsClasses(graph);
  const std::vector<Fault> atoms = enumerateAtoms(graph);
  const std::vector<int> piIndex = primaryInputIndex(graph);

  const atpg::Result<CompactResult> result = compact(graph, faults, patterns);
  if (!result.ok()) {
    violations.push_back("compact returned an error: " + result.error());
    return outcome;
  }
  const CompactResult& compacted = result.value();

  outcome.inputCount = patterns.size();
  outcome.keptCount = compacted.patterns.size();

  // The result is a subset of the input, in the input's order.
  if (compacted.patterns.size() != compacted.keptIndices.size()) {
    violations.push_back("patterns and keptIndices have different lengths");
    return outcome;
  }
  for (std::size_t i = 0; i < compacted.keptIndices.size(); ++i) {
    if (compacted.keptIndices[i] >= patterns.size()) {
      violations.push_back("a kept index is out of range");
      return outcome;
    }
    if (i > 0 && compacted.keptIndices[i - 1] >= compacted.keptIndices[i]) {
      violations.push_back("keptIndices is not strictly ascending");
    }
    if (compacted.patterns[i] != patterns[compacted.keptIndices[i]]) {
      violations.push_back("a kept pattern is not the input pattern its index names");
    }
  }

  // Coverage is preserved exactly - equality, not merely a lower bound.
  const std::vector<bool> before = detectedSet(graph, piIndex, atoms, patterns);
  const std::vector<bool> after = detectedSet(graph, piIndex, atoms, compacted.patterns);
  if (before != after) {
    violations.push_back("the compacted set does not detect exactly the input set's faults");
    return outcome;
  }

  // Irredundance: removing any one kept pattern must lose a fault. This is
  // the only check here that fails when compaction does no work - coverage
  // preservation and the subset property are both satisfied by returning
  // the input unchanged.
  for (std::size_t skip = 0; skip < compacted.patterns.size(); ++skip) {
    std::vector<std::vector<bool>> reduced;
    for (std::size_t i = 0; i < compacted.patterns.size(); ++i) {
      if (i != skip) {
        reduced.push_back(compacted.patterns[i]);
      }
    }
    if (detectedSet(graph, piIndex, atoms, reduced) == after) {
      violations.push_back("a kept pattern can be removed without losing coverage");
      break;
    }
  }

  return outcome;
}

} // namespace

TEST_CASE("compaction preserves coverage and leaves no removable pattern", "[Compact][property]") {
  // No SAT solving here, so this affords many more iterations than the
  // test-generation property tests.
  constexpr int kIterations = 150;
  constexpr unsigned kSeed = 20260819;

  std::mt19937 rng(kSeed);
  std::uniform_int_distribution<std::size_t> patternCountDist(4, 24);

  std::size_t totalInput = 0;
  std::size_t totalKept = 0;

  for (int i = 0; i < kIterations; ++i) {
    Graph graph = randomCircuit(rng);
    REQUIRE(graph.levelize().ok());

    const std::size_t patternCount = patternCountDist(rng);
    const std::vector<std::vector<bool>> patterns = randomPatterns(rng, graph, patternCount);

    const CheckOutcome outcome = checkCompaction(graph, patterns);
    if (!outcome.violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << "):\n" << dumpCircuit(graph));
      for (const auto& violation : outcome.violations) {
        UNSCOPED_INFO(violation);
      }
      FAIL("compact violated an invariant");
    }
    totalInput += outcome.inputCount;
    totalKept += outcome.keptCount;
  }

  // A corpus-level sanity bound on top of the per-circuit irredundance
  // check: random pattern sets over small circuits overlap heavily, so
  // compaction must remove a large fraction of them.
  INFO("input=" << totalInput << " kept=" << totalKept);
  CHECK(totalKept * 2 < totalInput);
}
