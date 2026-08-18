#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/gen/TestGen.hpp"
#include "atpg/ir/Graph.hpp"
#include "atpg/sim/LogicSim.hpp"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/sat_parameters.pb.h"

// Must come after the OR-Tools includes above: ortools transitively includes
// absl/log/check.h, which #defines CHECK as its own (fatal, SIGABRT-on-failure)
// macro with no include guard on the name. Undefining it first and including
// Catch2's header last makes Catch2's CHECK() win, so failures are reported
// by Catch2 instead of aborting the whole test binary.
#undef CHECK
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// Must come after the Catch2 include above, not before: Test.hpp
// transitively includes Catch2's header too, so including it first would
// satisfy Catch2's include guard before the #undef CHECK above runs,
// permanently leaving CHECK undefined and breaking every CHECK() below.
#include "../Test.hpp"

using operations_research::sat::BoolVar;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::SatParameters;
using operations_research::sat::SolutionBooleanValue;
using operations_research::sat::SolveWithParameters;

using namespace atpg::fault;
using namespace atpg::gen;
using namespace atpg::ir;

TEST_CASE("CP-SAT solves a trivial satisfiable boolean model", "[gen][smoke]") {
  CpModelBuilder builder;
  const BoolVar a = builder.NewBoolVar();
  const BoolVar b = builder.NewBoolVar();
  // b == NOT(a); force a == true -> b must solve to false.
  builder.AddEquality(b, a.Not());
  builder.AddEquality(a, 1);

  SatParameters params;
  params.set_max_time_in_seconds(5.0);
  const CpSolverResponse response = SolveWithParameters(builder.Build(), params);

  REQUIRE((response.status() == CpSolverStatus::OPTIMAL ||
           response.status() == CpSolverStatus::FEASIBLE));
  CHECK(SolutionBooleanValue(response, a) == true);
  CHECK(SolutionBooleanValue(response, b) == false);
}

TEST_CASE("TestSet stores results in insertion order", "[TestGen]") {
  TestSet results;
  const Fault f1{PinRef{0, PinKind::Output, 0}, StuckValue::SA0};
  const Fault f2{PinRef{1, PinKind::Output, 0}, StuckValue::SA1};
  results.add(TestResult{f1, TestOutcome::Testable, {true, false}});
  results.add(TestResult{f2, TestOutcome::Redundant, {}});

  REQUIRE(results.size() == 2);
  auto it = results.begin();
  CHECK(it->fault == f1);
  CHECK(it->outcome == TestOutcome::Testable);
  CHECK((it->pattern == std::vector<bool>{true, false}));
  ++it;
  CHECK(it->fault == f2);
  CHECK(it->outcome == TestOutcome::Redundant);
}

TEST_CASE("generateTests finds the unique pattern that detects a 2-input And gate's output SA0",
          "[TestGen]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(GateType::And, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  // g's output SA0 is only detected when the good circuit's output is 1,
  // which for a 2-input AND happens only when both inputs are 1 - a unique
  // satisfying pattern, so the exact returned pattern can be asserted.
  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{g, PinKind::Output, 0}, StuckValue::SA0}, {}});

  const atpg::Result<TestSet> testsResult = generateTests(graph, faults);
  REQUIRE(testsResult.ok());
  REQUIRE(testsResult.value().size() == 1);

  const TestResult& result = *testsResult.value().begin();
  CHECK(result.outcome == TestOutcome::Testable);
  REQUIRE(result.pattern.size() == 2);
  CHECK(result.pattern[0] == true);
  CHECK(result.pattern[1] == true);
}

TEST_CASE("generateTests detects a stuck-at fault on a primary input's own output pin",
          "[TestGen]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, y);
  REQUIRE(graph.levelize().ok());

  // y == a, so a's own output SA1 is only detected when a's good value is 0
  // - a unique satisfying pattern, so the exact returned pattern can be
  // asserted.
  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{a, PinKind::Output, 0}, StuckValue::SA1}, {}});

  const atpg::Result<TestSet> testsResult = generateTests(graph, faults);
  REQUIRE(testsResult.ok());
  REQUIRE(testsResult.value().size() == 1);

  const TestResult& result = *testsResult.value().begin();
  CHECK(result.outcome == TestOutcome::Testable);
  REQUIRE(result.pattern.size() == 1);
  CHECK(result.pattern[0] == false);
}

TEST_CASE("generateTests reports a genuinely redundant fault as Redundant", "[TestGen]") {
  // y = a AND (a OR b), which by absorption equals a for every input - so
  // forcing the OR gate's output to 1 changes nothing observable: the AND
  // still reduces to a AND 1 = a as before the fault, and to a AND 0 = 0
  // only where the good circuit already produced 0. This exercises the
  // exhaustive tripwire on the Redundant path (small enough to be checked)
  // without it firing a false positive.
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId orGate = graph.addGate(GateType::Or, "orGate");
  const GateId andGate = graph.addGate(GateType::And, "andGate");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, orGate);
  graph.addEdge(b, orGate);
  graph.addEdge(a, andGate);
  graph.addEdge(orGate, andGate);
  graph.addEdge(andGate, y);
  REQUIRE(graph.levelize().ok());

  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{orGate, PinKind::Output, 0}, StuckValue::SA1}, {}});

  const atpg::Result<TestSet> testsResult = generateTests(graph, faults);
  REQUIRE(testsResult.ok());
  REQUIRE(testsResult.value().size() == 1);

  const TestResult& result = *testsResult.value().begin();
  CHECK(result.outcome == TestOutcome::Redundant);
}

namespace {

using atpg::Result;
using atpg::sim::simulate;
using atpg::sim::simulateWithFault;

Graph buildBinaryGateGraph(GateType type) {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(type, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());
  return graph;
}

Graph buildUnaryGateGraph(GateType type) {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId g = graph.addGate(type, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());
  return graph;
}

void checkEveryFaultTestable(const Graph& graph) {
  const FaultList faults = generateFaultList(graph);
  const Result<TestSet> testsResult = generateTests(graph, faults);
  REQUIRE(testsResult.ok());
  const TestSet& tests = testsResult.value();
  REQUIRE(tests.size() == faults.size());

  for (const auto& result : tests) {
    INFO("fault on gate " << result.fault.pin.gate);
    REQUIRE(result.outcome == TestOutcome::Testable);
    REQUIRE(result.pattern.size() == graph.primaryInputs().size());

    const auto good = simulate(graph, result.pattern);
    const auto faulted =
        simulateWithFault(graph, result.pattern, result.fault.pin, result.fault.value);
    REQUIRE(good.ok());
    REQUIRE(faulted.ok());
    CHECK(good.value() != faulted.value());
  }
}

} // namespace

TEST_CASE("generateTests finds a detecting pattern for every fault of a 2-input AND gate",
          "[TestGen]") {
  checkEveryFaultTestable(buildBinaryGateGraph(GateType::And));
}

TEST_CASE("generateTests finds a detecting pattern for every fault of a 2-input NAND gate",
          "[TestGen]") {
  checkEveryFaultTestable(buildBinaryGateGraph(GateType::Nand));
}

TEST_CASE("generateTests finds a detecting pattern for every fault of a 2-input OR gate",
          "[TestGen]") {
  checkEveryFaultTestable(buildBinaryGateGraph(GateType::Or));
}

TEST_CASE("generateTests finds a detecting pattern for every fault of a 2-input NOR gate",
          "[TestGen]") {
  checkEveryFaultTestable(buildBinaryGateGraph(GateType::Nor));
}

TEST_CASE("generateTests finds a detecting pattern for every fault of a 2-input XOR gate",
          "[TestGen]") {
  checkEveryFaultTestable(buildBinaryGateGraph(GateType::Xor));
}

TEST_CASE("generateTests finds a detecting pattern for every fault of a 2-input XNOR gate",
          "[TestGen]") {
  checkEveryFaultTestable(buildBinaryGateGraph(GateType::Xnor));
}

TEST_CASE("generateTests finds a detecting pattern for every fault of a BUF gate", "[TestGen]") {
  checkEveryFaultTestable(buildUnaryGateGraph(GateType::Buf));
}

TEST_CASE("generateTests finds a detecting pattern for every fault of a NOT gate", "[TestGen]") {
  checkEveryFaultTestable(buildUnaryGateGraph(GateType::Not));
}

TEST_CASE("generateTests reports a structurally redundant fault as Redundant", "[TestGen]") {
  // y = (a AND b) OR (a AND NOT b), which simplifies to y == a for every
  // input - by the OR-absorption law, a OR (a AND X) == a for any X. So
  // g1's b-input (pin 1) stuck-at-1 can never be detected: hand-verified
  // via truth table (a=0: y=0 both ways; a=1,b=0: y=1 both ways; a=1,b=1:
  // y=1 both ways).
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId nb = graph.addGate(GateType::Not, "nb");
  const GateId g1 = graph.addGate(GateType::And, "g1");
  const GateId g2 = graph.addGate(GateType::And, "g2");
  const GateId y = graph.addGate(GateType::Or, "y");
  const GateId po = graph.addGate(GateType::Po, "po");
  graph.addEdge(b, nb);
  graph.addEdge(a, g1);
  graph.addEdge(b, g1);
  graph.addEdge(a, g2);
  graph.addEdge(nb, g2);
  graph.addEdge(g1, y);
  graph.addEdge(g2, y);
  graph.addEdge(y, po);
  REQUIRE(graph.levelize().ok());

  const FaultList faults = generateFaultList(graph);
  const Result<TestSet> testsResult = generateTests(graph, faults);
  REQUIRE(testsResult.ok());

  const Fault redundantFault{PinRef{g1, PinKind::Input, 1}, StuckValue::SA1};
  bool found = false;
  for (const auto& result : testsResult.value()) {
    if (result.fault == redundantFault) {
      found = true;
      CHECK(result.outcome == TestOutcome::Redundant);
    }
  }
  REQUIRE(found);
}

TEST_CASE("generateTests rejects a negative time limit with an actionable error", "[TestGen]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");
  REQUIRE(graph.levelize().ok());
  const FaultList faults = generateFaultList(graph);

  Options options;
  options.timeLimitSeconds = -1.0;
  const Result<TestSet> testsResult = generateTests(graph, faults, options);
  REQUIRE_FALSE(testsResult.ok());
  CHECK_THAT(testsResult.error(), Catch::Matchers::ContainsSubstring("timeLimitSeconds"));
  CHECK_THAT(testsResult.error(), Catch::Matchers::ContainsSubstring("-1"));
  // The invalid-parameter error must not be mistaken for an encoding bug.
  CHECK_THAT(testsResult.error(), !Catch::Matchers::ContainsSubstring("encoding bug"));
}

TEST_CASE("generateTests reports Aborted when the time limit is exhausted", "[TestGen]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");
  REQUIRE(graph.levelize().ok());
  const FaultList faults = generateFaultList(graph);

  Options options;
  options.timeLimitSeconds = 0.0;
  const Result<TestSet> testsResult = generateTests(graph, faults, options);
  REQUIRE(testsResult.ok());

  bool anyAborted = false;
  for (const auto& result : testsResult.value()) {
    anyAborted = anyAborted || result.outcome == TestOutcome::Aborted;
  }
  CHECK(anyAborted);
}

TEST_CASE("generateTests produces a genuinely detecting pattern for every c17 fault", "[TestGen]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");
  REQUIRE(graph.levelize().ok());
  const FaultList faults = generateFaultList(graph);

  const Result<TestSet> testsResult = generateTests(graph, faults);
  REQUIRE(testsResult.ok());
  const TestSet& tests = testsResult.value();
  REQUIRE(tests.size() == faults.size());

  for (const auto& result : tests) {
    INFO("fault on gate " << result.fault.pin.gate);
    // c17 is small enough, and has no known structurally-redundant faults,
    // that every fault should resolve to Testable well within the default
    // time limit - this is checked directly rather than assumed.
    REQUIRE(result.outcome == TestOutcome::Testable);

    const auto good = simulate(graph, result.pattern);
    const auto faulted =
        simulateWithFault(graph, result.pattern, result.fault.pin, result.fault.value);
    REQUIRE(good.ok());
    REQUIRE(faulted.ok());
    CHECK(good.value() != faulted.value());
  }
}

TEST_CASE("TestPlan stores patterns and resolutions independently", "[TestGen]") {
  TestPlan plan;
  CHECK(plan.patterns().empty());
  CHECK(plan.resolutions().empty());

  plan.addPattern({true, false});
  plan.addPattern({false, true});

  const Fault f1{PinRef{0, PinKind::Output, 0}, StuckValue::SA0};
  const Fault f2{PinRef{1, PinKind::Output, 0}, StuckValue::SA1};
  plan.addResolution(FaultResolution{f1, TestOutcome::Testable, 1});
  plan.addResolution(FaultResolution{f2, TestOutcome::Redundant, 0});

  REQUIRE(plan.patterns().size() == 2);
  CHECK(plan.patterns()[0] == std::vector<bool>{true, false});
  CHECK(plan.patterns()[1] == std::vector<bool>{false, true});

  REQUIRE(plan.resolutions().size() == 2);
  CHECK(plan.resolutions()[0].fault == f1);
  CHECK(plan.resolutions()[0].outcome == TestOutcome::Testable);
  CHECK(plan.resolutions()[0].patternIndex == 1);
  CHECK(plan.resolutions()[1].outcome == TestOutcome::Redundant);
}
