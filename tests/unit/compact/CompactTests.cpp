#include "atpg/compact/Compact.hpp"
#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/fsim/FaultSim.hpp"
#include "atpg/gen/TestGen.hpp"
#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "../Test.hpp"

using namespace atpg::compact;
using namespace atpg::fault;
using namespace atpg::ir;

namespace {

/// The gate driving `graph`'s first primary output.
GateId outputDriver(const Graph& graph) {
  return graph.gate(graph.primaryOutputs().front()).fanin[0];
}

/// A 2-input AND driving one primary output.
Graph andGraph() {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(GateType::And, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());
  return graph;
}

/// Three faults of andGraph()'s AND gate, each detected by exactly one of
/// the four input patterns:
///   g/out/SA0  needs the good output to be 1     -> 11
///   g/in0/SA1  needs a=0 with b sensitising      -> 01
///   g/in1/SA1  needs b=0 with a sensitising      -> 10
/// Pattern 00 therefore detects nothing at all.
FaultList andFaults(const Graph& graph) {
  const GateId g = outputDriver(graph);
  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{g, PinKind::Output, 0}, StuckValue::SA0}, {}});
  faults.add(FaultClass{Fault{PinRef{g, PinKind::Input, 0}, StuckValue::SA1}, {}});
  faults.add(FaultClass{Fault{PinRef{g, PinKind::Input, 1}, StuckValue::SA1}, {}});
  return faults;
}

} // namespace

TEST_CASE("compact drops a pattern that detects nothing", "[Compact]") {
  const Graph graph = andGraph();
  const FaultList faults = andFaults(graph);

  const std::vector<std::vector<bool>> patterns = {
      {false, false}, {false, true}, {true, false}, {true, true}};

  const atpg::Result<CompactResult> result = compact(graph, faults, patterns);
  REQUIRE(result.ok());

  CHECK(result.value().keptIndices == std::vector<std::size_t>{1, 2, 3});
  REQUIRE(result.value().patterns.size() == 3);
  CHECK(result.value().patterns[0] == patterns[1]);
  CHECK(result.value().patterns[2] == patterns[3]);
}

TEST_CASE("compact drops a duplicated pattern", "[Compact]") {
  const Graph graph = andGraph();
  const FaultList faults = andFaults(graph);

  // 11 appears twice; the second copy adds no coverage.
  const std::vector<std::vector<bool>> patterns = {
      {true, true}, {false, true}, {true, false}, {true, true}};

  const atpg::Result<CompactResult> result = compact(graph, faults, patterns);
  REQUIRE(result.ok());
  CHECK(result.value().keptIndices == std::vector<std::size_t>{0, 1, 2});
}

TEST_CASE("compact keeps its result in the input's order", "[Compact]") {
  const Graph graph = andGraph();
  const FaultList faults = andFaults(graph);

  const std::vector<std::vector<bool>> patterns = {
      {true, true}, {false, false}, {true, false}, {false, false}, {false, true}};

  const atpg::Result<CompactResult> result = compact(graph, faults, patterns);
  REQUIRE(result.ok());

  const std::vector<std::size_t>& kept = result.value().keptIndices;
  REQUIRE(kept.size() == 3);
  for (std::size_t i = 1; i < kept.size(); ++i) {
    CHECK(kept[i - 1] < kept[i]);
  }
  for (std::size_t i = 0; i < kept.size(); ++i) {
    CHECK(result.value().patterns[i] == patterns[kept[i]]);
  }
}

TEST_CASE("compact removes a greedily-selected pattern the rest already cover", "[Compact]") {
  // Two ANDs sharing input b. Gate ids: a=0, b=1, c=2, g1=3, g2=4, y1=5,
  // y2=6, with g1 = a & b and g2 = b & c.
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId c = graph.addGate(GateType::Pi, "c");
  const GateId g1 = graph.addGate(GateType::And, "g1");
  const GateId g2 = graph.addGate(GateType::And, "g2");
  const GateId y1 = graph.addGate(GateType::Po, "y1");
  const GateId y2 = graph.addGate(GateType::Po, "y2");
  graph.addEdge(a, g1);
  graph.addEdge(b, g1);
  graph.addEdge(b, g2);
  graph.addEdge(c, g2);
  graph.addEdge(g1, y1);
  graph.addEdge(g2, y2);
  REQUIRE(graph.levelize().ok());

  // F1 g1/out/SA0  detected when a & b == 1
  // F2 g2/out/SA0  detected when b & c == 1
  // F3 g1/in0/SA1  detected when a == 0 && b == 1
  // F4 g2/in1/SA1  detected when b == 1 && c == 0
  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{g1, PinKind::Output, 0}, StuckValue::SA0}, {}});
  faults.add(FaultClass{Fault{PinRef{g2, PinKind::Output, 0}, StuckValue::SA0}, {}});
  faults.add(FaultClass{Fault{PinRef{g1, PinKind::Input, 0}, StuckValue::SA1}, {}});
  faults.add(FaultClass{Fault{PinRef{g2, PinKind::Input, 1}, StuckValue::SA1}, {}});

  // Bits are (a, b, c), so the three patterns detect {F1,F2}, {F1,F4} and
  // {F2,F3}. Greedy takes all three - 111 ties on a gain of 2 and wins on
  // the lower index, after which 110 and 011 each still contribute one new
  // fault. But 111's faults are then both covered by the other two, so the
  // irredundancy pass must drop it.
  const std::vector<std::vector<bool>> patterns = {
      {true, true, true}, {true, true, false}, {false, true, true}};

  const atpg::Result<CompactResult> result = compact(graph, faults, patterns);
  REQUIRE(result.ok());
  CHECK(result.value().keptIndices == std::vector<std::size_t>{1, 2});
}

TEST_CASE("compact accepts an empty pattern set", "[Compact]") {
  const Graph graph = andGraph();
  const FaultList faults = andFaults(graph);

  const atpg::Result<CompactResult> result = compact(graph, faults, {});
  REQUIRE(result.ok());
  CHECK(result.value().patterns.empty());
  CHECK(result.value().keptIndices.empty());
}

TEST_CASE("compact rejects a pattern whose width does not match the inputs", "[Compact]") {
  const Graph graph = andGraph();
  const FaultList faults = andFaults(graph);

  const atpg::Result<CompactResult> result = compact(graph, faults, {{true}});
  CHECK_FALSE(result.ok());
}

TEST_CASE("compacting c17's generated patterns preserves full coverage", "[Compact]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");
  REQUIRE(graph.levelize().ok());

  const FaultList faults = generateFaultList(graph);
  const atpg::Result<atpg::gen::TestSet> tests = atpg::gen::generateTests(graph, faults);
  REQUIRE(tests.ok());

  std::vector<std::vector<bool>> patterns;
  for (const atpg::gen::TestResult& test : tests.value()) {
    REQUIRE(test.outcome == atpg::gen::TestOutcome::Testable);
    patterns.push_back(test.pattern);
  }
  REQUIRE(patterns.size() == faults.size());

  const atpg::Result<CompactResult> compacted = compact(graph, faults, patterns);
  REQUIRE(compacted.ok());

  // Closed loop over three independently built modules: fault-simulate both
  // sets and confirm the coverage figure is untouched.
  const atpg::Result<atpg::fsim::SimResult> before =
      atpg::fsim::simulateFaults(graph, faults, patterns);
  const atpg::Result<atpg::fsim::SimResult> after =
      atpg::fsim::simulateFaults(graph, faults, compacted.value().patterns);
  REQUIRE(before.ok());
  REQUIRE(after.ok());
  CHECK(after.value().detectedCount() == before.value().detectedCount());
  CHECK(after.value().detectedCount() == faults.size());

  // c17 has heavy reconvergent fanout, so one pattern per fault is far more
  // than it needs.
  CHECK(compacted.value().patterns.size() * 2 < patterns.size());
}
