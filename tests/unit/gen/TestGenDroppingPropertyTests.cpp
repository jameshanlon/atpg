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

std::vector<std::string> checkAgainstExhaustive(const Graph& graph) {
  std::vector<std::string> violations;

  const FaultList faults = generateFaultList(graph);

  const atpg::Result<TestSet> exhaustive = generateTests(graph, faults);
  if (!exhaustive.ok()) {
    violations.push_back("generateTests returned an error: " + exhaustive.error());
    return violations;
  }
  const atpg::Result<TestPlan> dropped = generateTestsWithDropping(graph, faults);
  if (!dropped.ok()) {
    violations.push_back("generateTestsWithDropping returned an error: " + dropped.error());
    return violations;
  }
  const TestPlan& plan = dropped.value();

  if (plan.resolutions().size() != exhaustive.value().size()) {
    violations.push_back("resolution count does not match the exhaustive result count");
    return violations;
  }

  const std::vector<int> piIndex = primaryInputIndex(graph);

  // Every fault must be resolved the same way by both paths.
  auto it = exhaustive.value().begin();
  std::size_t testableCount = 0;
  for (const FaultResolution& resolution : plan.resolutions()) {
    if (keyOf(resolution.fault) != keyOf(it->fault)) {
      violations.push_back("resolutions are not in the input fault list's order");
      return violations;
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
  }

  // Dropping must actually happen - a loop that silently never drops
  // anything would satisfy everything above.
  if (plan.patterns().size() > testableCount) {
    violations.push_back("more patterns than testable faults");
  }

  return violations;
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

    const std::vector<std::string> violations = checkAgainstExhaustive(graph);
    if (!violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << "):\n" << dumpCircuit(graph));
      for (const auto& violation : violations) {
        INFO(violation);
      }
      FAIL("generateTestsWithDropping disagreed with generateTests");
    }

    const FaultList faults = generateFaultList(graph);
    const atpg::Result<TestPlan> plan = generateTestsWithDropping(graph, faults);
    REQUIRE(plan.ok());
    totalPatterns += plan.value().patterns().size();
    for (const FaultResolution& r : plan.value().resolutions()) {
      if (r.outcome == TestOutcome::Testable) {
        ++totalTestable;
      }
    }
  }

  // Dropping, in aggregate: across a corpus with real fanout, it must
  // produce materially fewer patterns than testable faults. A per-circuit
  // strict inequality would be flaky (a tiny circuit can legitimately need
  // one pattern per fault), but over the whole corpus the gap is large.
  INFO("patterns=" << totalPatterns << " testable=" << totalTestable);
  CHECK(totalPatterns * 2 < totalTestable);
}
