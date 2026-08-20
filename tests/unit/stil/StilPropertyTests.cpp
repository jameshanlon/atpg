#include "atpg/ir/Graph.hpp"
#include "atpg/stil/Stil.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "../RandomCircuit.hpp"
#include "../stil/StilReader.hpp"

using namespace atpg::ir;
using namespace atpg::stil;
using namespace atpg::testing;

namespace {

std::vector<std::string> checkProgram(const Graph& graph,
                                      const std::vector<std::vector<bool>>& patterns) {
  std::vector<std::string> violations;

  const atpg::Result<std::string> stil = writeStil(graph, patterns, "random");
  if (!stil.ok()) {
    violations.push_back("writeStil returned an error: " + stil.error());
    return violations;
  }

  const atpg::Result<StilProgram> program = readStil(stil.value());
  if (!program.ok()) {
    violations.push_back("the emitted program could not be scanned: " + program.error());
    return violations;
  }

  const std::vector<GateId>& pis = graph.primaryInputs();
  const std::vector<GateId>& pos = graph.primaryOutputs();

  // Signal declarations: inputs then outputs, names and directions in the
  // graph's port order.
  if (program.value().signals.size() != pis.size() + pos.size()) {
    violations.push_back("the Signals block does not declare every primary input and output");
    return violations;
  }
  for (std::size_t i = 0; i < pis.size(); ++i) {
    const StilSignal& signal = program.value().signals[i];
    if (signal.name != graph.gate(pis[i]).name || !signal.isInput) {
      violations.push_back("a primary input is misdeclared or out of order");
    }
  }
  for (std::size_t i = 0; i < pos.size(); ++i) {
    const StilSignal& signal = program.value().signals[pis.size() + i];
    if (signal.name != graph.gate(pos[i]).name || signal.isInput) {
      violations.push_back("a primary output is misdeclared or out of order");
    }
  }

  // Group membership must agree with the declarations. A writer whose
  // Signals order and SignalGroups order drift apart silently transposes
  // every vector's columns.
  std::vector<std::string> expectedPi;
  for (const GateId id : pis) {
    expectedPi.push_back(graph.gate(id).name);
  }
  std::vector<std::string> expectedPo;
  for (const GateId id : pos) {
    expectedPo.push_back(graph.gate(id).name);
  }
  if (program.value().piGroup != expectedPi) {
    violations.push_back("the PI group does not match the declared inputs, in order");
  }
  if (program.value().poGroup != expectedPo) {
    violations.push_back("the PO group does not match the declared outputs, in order");
  }

  if (program.value().vectors.size() != patterns.size()) {
    violations.push_back("the Pattern block does not hold one vector per pattern");
    return violations;
  }

  const std::vector<int> piIndex = primaryInputIndex(graph);
  for (std::size_t p = 0; p < patterns.size(); ++p) {
    const StilVector& vector = program.value().vectors[p];

    std::string expectedStimulus;
    for (const bool bit : patterns[p]) {
      expectedStimulus.push_back(bit ? '1' : '0');
    }
    if (vector.inputs != expectedStimulus) {
      violations.push_back("a vector's stimulus is not the pattern it was given");
    }

    // Derived with the independent oracle, never atpg::sim - writeStil uses
    // atpg::sim internally, so a shared bug would agree with itself.
    const std::vector<bool> outputs = simulate(graph, piIndex, patterns[p], InjectedFault{});
    std::string expectedResponse;
    for (const bool bit : outputs) {
      expectedResponse.push_back(bit ? 'H' : 'L');
    }
    if (vector.outputs != expectedResponse) {
      violations.push_back("a vector's expected response is not the circuit's actual response");
    }
  }

  return violations;
}

} // namespace

TEST_CASE("emitted STIL round-trips to its patterns and their responses", "[Stil][property]") {
  // No solver here, so this affords many iterations. Circuits vary in input
  // and output count, which is what exposes column-alignment errors that a
  // uniform-width fixture never would.
  constexpr int kIterations = 200;
  constexpr unsigned kSeed = 20260820;

  std::mt19937 rng(kSeed);
  std::uniform_int_distribution<std::size_t> patternCountDist(0, 12);

  for (int i = 0; i < kIterations; ++i) {
    Graph graph = randomCircuit(rng);
    REQUIRE(graph.levelize().ok());

    const std::vector<std::vector<bool>> patterns =
        randomPatterns(rng, graph, patternCountDist(rng));

    const std::vector<std::string> violations = checkProgram(graph, patterns);
    if (!violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << "):\n" << dumpCircuit(graph));
      for (const auto& violation : violations) {
        UNSCOPED_INFO(violation);
      }
      FAIL("writeStil violated an invariant");
    }
  }
}
