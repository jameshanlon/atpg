#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "../RandomCircuit.hpp"

using namespace atpg::fault;
using namespace atpg::ir;
using namespace atpg::testing;

namespace {

// Brute-force verifies generateFaultList(graph) against ground-truth
// detection sets (every atomic fault, simulated over every input vector)
// and returns a description of every violation found.
std::vector<std::string> checkAgainstGroundTruth(const Graph& graph) {
  std::vector<std::string> violations;

  const std::size_t piCount = graph.primaryInputs().size();
  const std::size_t vectorCount = std::size_t{1} << piCount;

  const std::vector<int> piIndex = primaryInputIndex(graph);

  std::vector<std::vector<bool>> goodOutputs(vectorCount);
  for (std::size_t v = 0; v < vectorCount; ++v) {
    goodOutputs[v] = simulate(graph, piIndex, patternOf(v, piCount), InjectedFault{});
  }

  const std::vector<Fault> atoms = enumerateAtoms(graph);
  std::map<FaultKey, std::size_t> atomIndex;
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    atomIndex[keyOf(atoms[i])] = i;
  }

  // detects[a] has bit v set iff input vector v detects atom a.
  std::vector<std::uint64_t> detects(atoms.size(), 0);
  for (std::size_t a = 0; a < atoms.size(); ++a) {
    const InjectedFault fault = injectedFrom(atoms[a]);
    for (std::size_t v = 0; v < vectorCount; ++v) {
      if (simulate(graph, piIndex, patternOf(v, piCount), fault) != goodOutputs[v]) {
        detects[a] |= std::uint64_t{1} << v;
      }
    }
  }

  const FaultList faults = generateFaultList(graph);

  std::vector<char> listed(atoms.size(), 0);
  std::vector<std::vector<std::size_t>> classes;
  for (const auto& faultClass : faults) {
    std::vector<Fault> members{faultClass.representative};
    members.insert(members.end(), faultClass.equivalent.begin(), faultClass.equivalent.end());
    std::vector<std::size_t> indices;
    for (const auto& member : members) {
      const auto it = atomIndex.find(keyOf(member));
      if (it == atomIndex.end()) {
        violations.push_back("listed fault is not a valid atomic fault of this circuit");
        continue;
      }
      listed[it->second] = 1;
      indices.push_back(it->second);
    }
    classes.push_back(std::move(indices));
  }

  // Every class's members must share an identical detection set - an
  // unsound merge otherwise.
  for (const auto& members : classes) {
    for (std::size_t k = 1; k < members.size(); ++k) {
      if (detects[members[k]] != detects[members[0]]) {
        violations.push_back("unsound merge: two faults in one class have different "
                             "detection sets");
      }
    }
  }

  // Every dropped atom must have a listed atom with an *identical*
  // detection set - generateFaultList only ever drops on exact
  // equivalence, never mere dominance (see src/fault/FaultList.cpp).
  for (std::size_t a = 0; a < atoms.size(); ++a) {
    if (listed[a]) {
      continue;
    }
    bool hasEquivalentSurvivor = false;
    for (std::size_t b = 0; b < atoms.size() && !hasEquivalentSurvivor; ++b) {
      hasEquivalentSurvivor = listed[b] && detects[b] == detects[a];
    }
    if (!hasEquivalentSurvivor) {
      violations.push_back("dropped fault has no equivalent surviving fault");
    }
  }

  // The union of detection sets must be unchanged - no coverage loss.
  std::uint64_t unionAll = 0;
  std::uint64_t unionListed = 0;
  for (std::size_t a = 0; a < atoms.size(); ++a) {
    unionAll |= detects[a];
    if (listed[a]) {
      unionListed |= detects[a];
    }
  }
  if (unionAll != unionListed) {
    violations.push_back("detection-set union differs between the full and collapsed "
                         "fault lists");
  }

  return violations;
}

} // namespace

TEST_CASE("generateFaultList collapsing preserves detection-set coverage and soundness",
          "[FaultList][property]") {
  // Default corpus size keeps this a fast, deterministic ctest run; bump
  // kIterations locally for a deeper sweep when touching the collapsing
  // algorithm - this module's history includes bugs only exhaustive runs
  // of thousands of circuits caught (see src/fault/FaultList.cpp).
  constexpr int kIterations = 500;
  constexpr unsigned kSeed = 20260813;

  std::mt19937 rng(kSeed);
  for (int i = 0; i < kIterations; ++i) {
    Graph graph = randomCircuit(rng);
    REQUIRE(graph.levelize().ok());

    const std::vector<std::string> violations = checkAgainstGroundTruth(graph);
    if (!violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << "):\n" << dumpCircuit(graph));
      for (const auto& violation : violations) {
        UNSCOPED_INFO(violation);
      }
      FAIL("generateFaultList violated a soundness/completeness invariant");
    }
  }
}
