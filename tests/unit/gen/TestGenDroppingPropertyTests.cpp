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

struct CheckOutcome {
  std::vector<std::string> violations;
  std::size_t patternCount = 0;
  std::size_t testableCount = 0;
};

CheckOutcome checkAgainstExhaustive(const Graph& graph) {
  CheckOutcome outcome;
  std::vector<std::string>& violations = outcome.violations;

  const FaultList faults = generateFaultList(graph);

  const atpg::Result<TestSet> exhaustive = generateTests(graph, faults);
  if (!exhaustive.ok()) {
    violations.push_back("generateTests returned an error: " + exhaustive.error());
    return outcome;
  }
  const atpg::Result<TestPlan> dropped = generateTestsWithDropping(graph, faults);
  if (!dropped.ok()) {
    violations.push_back("generateTestsWithDropping returned an error: " + dropped.error());
    return outcome;
  }
  const TestPlan& plan = dropped.value();

  if (plan.resolutions().size() != exhaustive.value().size()) {
    violations.push_back("resolution count does not match the exhaustive result count");
    return outcome;
  }

  const std::vector<int> piIndex = primaryInputIndex(graph);

  // Every fault must be resolved the same way by both paths.
  auto it = exhaustive.value().begin();
  std::size_t testableCount = 0;
  for (const FaultResolution& resolution : plan.resolutions()) {
    if (keyOf(resolution.fault) != keyOf(it->fault)) {
      violations.push_back("resolutions are not in the input fault list's order");
      return outcome;
    }
    if (resolution.outcome != it->outcome) {
      violations.push_back("dropping and exhaustive generation disagree on a fault's outcome");
    }
    ++it;

    if (resolution.outcome != TestOutcome::Testable) {
      continue;
    }
    ++testableCount;

    // The pattern this fault points at must genuinely detect it, checked
    // with the independent oracle rather than atpg::sim - the loop already
    // verifies drops with atpg::sim internally, so reusing it here would
    // only confirm the loop agrees with itself.
    if (resolution.patternIndex >= plan.patterns().size()) {
      violations.push_back("patternIndex is out of range");
      continue;
    }
    const std::vector<bool>& pattern = plan.patterns()[resolution.patternIndex];
    if (pattern.size() != graph.primaryInputs().size()) {
      violations.push_back("pattern width does not match the primary-input count");
      continue;
    }
    const std::vector<bool> good = simulate(graph, piIndex, pattern, InjectedFault{});
    const std::vector<bool> faulty =
        simulate(graph, piIndex, pattern, injectedFrom(resolution.fault));
    if (good == faulty) {
      violations.push_back("the pattern a fault points at does not actually detect it");
    }

    // Dropping must be *complete*, not merely sound: this fault must point
    // at the earliest pattern that detects it. The loop drops every fault a
    // new pattern covers before generating the next one, so an earlier
    // pattern detecting this fault would mean the loop failed to drop it
    // and paid for a solver call it did not need.
    //
    // This is the only assertion here that fails when the loop under-drops.
    // Outcome agreement and "the pattern really detects it" are both
    // satisfied by a loop that drops nothing at all, so without this the
    // milestone's entire point could regress with the suite still green.
    for (std::size_t earlier = 0; earlier < resolution.patternIndex; ++earlier) {
      const std::vector<bool>& other = plan.patterns()[earlier];
      const std::vector<bool> otherGood = simulate(graph, piIndex, other, InjectedFault{});
      const std::vector<bool> otherFaulty =
          simulate(graph, piIndex, other, injectedFrom(resolution.fault));
      if (otherGood != otherFaulty) {
        violations.push_back("an earlier pattern already detects this fault - it should have "
                             "been dropped rather than given its own pattern");
        break;
      }
    }
  }

  outcome.patternCount = plan.patterns().size();
  outcome.testableCount = testableCount;

  return outcome;
}

} // namespace

TEST_CASE("dropping agrees with exhaustive generation on random circuits", "[TestGen][property]") {
  // Each iteration runs the SAT solver once per undropped fault, so this is
  // far heavier than the other property tests - keep the count modest and
  // raise it locally when touching the loop.
  constexpr int kIterations = 40;
  constexpr unsigned kSeed = 20260818;

  std::mt19937 rng(kSeed);
  std::size_t totalPatterns = 0;
  std::size_t totalTestable = 0;

  for (int i = 0; i < kIterations; ++i) {
    Graph graph = randomCircuit(rng);
    REQUIRE(graph.levelize().ok());

    const CheckOutcome outcome = checkAgainstExhaustive(graph);
    if (!outcome.violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << "):\n" << dumpCircuit(graph));
      for (const auto& violation : outcome.violations) {
        INFO(violation);
      }
      FAIL("generateTestsWithDropping disagreed with generateTests");
    }
    totalPatterns += outcome.patternCount;
    totalTestable += outcome.testableCount;
  }

  // Dropping, in aggregate: across a corpus with real fanout, it must
  // produce materially fewer patterns than testable faults. The
  // earliest-pattern check inside checkAgainstExhaustive is what actually
  // guards completeness fault-by-fault; this is a cheap corpus-level sanity
  // bound on top of it.
  INFO("patterns=" << totalPatterns << " testable=" << totalTestable);
  CHECK(totalPatterns * 2 < totalTestable);
}
