#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "../Test.hpp"

using namespace atpg::fault;
using namespace atpg::ir;

namespace {

std::size_t totalAtoms(const FaultList& faults) {
  std::size_t total = 0;
  for (const auto& faultClass : faults) {
    total += 1 + faultClass.equivalent.size();
  }
  return total;
}

} // namespace

TEST_CASE("a 2-input AND gate collapses to 4 fault classes", "[FaultList]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(GateType::And, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  const FaultList faults = generateFaultList(graph);

  CHECK(faults.size() == 4);
  CHECK(totalAtoms(faults) == 12);
}

TEST_CASE("PinRef equality compares all fields", "[Fault]") {
  const PinRef a{5, PinKind::Input, 2};
  const PinRef b{5, PinKind::Input, 2};
  const PinRef c{5, PinKind::Input, 3};
  const PinRef d{5, PinKind::Output, 2};

  CHECK(a == b);
  CHECK_FALSE(a == c);
  CHECK_FALSE(a == d);
}

TEST_CASE("Fault equality compares pin and stuck value", "[Fault]") {
  const Fault a{PinRef{5, PinKind::Output, 0}, StuckValue::SA0};
  const Fault b{PinRef{5, PinKind::Output, 0}, StuckValue::SA0};
  const Fault c{PinRef{5, PinKind::Output, 0}, StuckValue::SA1};

  CHECK(a == b);
  CHECK_FALSE(a == c);
}

namespace {

FaultList generateForBinaryGate(GateType type) {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(type, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());
  return generateFaultList(graph);
}

FaultList generateForUnaryGate(GateType type) {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId g = graph.addGate(type, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());
  return generateFaultList(graph);
}

} // namespace

TEST_CASE("a 2-input NAND gate collapses to 4 fault classes", "[FaultList]") {
  const auto faults = generateForBinaryGate(GateType::Nand);
  CHECK(faults.size() == 4);
  CHECK(totalAtoms(faults) == 12);
}

TEST_CASE("a 2-input OR gate collapses to 4 fault classes", "[FaultList]") {
  const auto faults = generateForBinaryGate(GateType::Or);
  CHECK(faults.size() == 4);
  CHECK(totalAtoms(faults) == 12);
}

TEST_CASE("a 2-input NOR gate collapses to 4 fault classes", "[FaultList]") {
  const auto faults = generateForBinaryGate(GateType::Nor);
  CHECK(faults.size() == 4);
  CHECK(totalAtoms(faults) == 12);
}

TEST_CASE("a 2-input XOR gate collapses to 6 fault classes", "[FaultList]") {
  const auto faults = generateForBinaryGate(GateType::Xor);
  CHECK(faults.size() == 6);
  CHECK(totalAtoms(faults) == 12);
}

TEST_CASE("a 2-input XNOR gate collapses to 6 fault classes", "[FaultList]") {
  const auto faults = generateForBinaryGate(GateType::Xnor);
  CHECK(faults.size() == 6);
  CHECK(totalAtoms(faults) == 12);
}

TEST_CASE("a BUF gate collapses to 2 fault classes", "[FaultList]") {
  const auto faults = generateForUnaryGate(GateType::Buf);
  CHECK(faults.size() == 2);
  CHECK(totalAtoms(faults) == 8);
}

TEST_CASE("a NOT gate collapses to 2 fault classes", "[FaultList]") {
  const auto faults = generateForUnaryGate(GateType::Not);
  CHECK(faults.size() == 2);
  CHECK(totalAtoms(faults) == 8);
}

TEST_CASE("fanout branches through NOT gates stay independent", "[FaultList]") {
  // a -> n1 -> y1
  //   -> n2 -> y2
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId n1 = graph.addGate(GateType::Not, "n1");
  const GateId n2 = graph.addGate(GateType::Not, "n2");
  const GateId y1 = graph.addGate(GateType::Po, "y1");
  const GateId y2 = graph.addGate(GateType::Po, "y2");
  graph.addEdge(a, n1);
  graph.addEdge(a, n2);
  graph.addEdge(n1, y1);
  graph.addEdge(n2, y2);
  REQUIRE(graph.levelize().ok());

  const FaultList faults = generateFaultList(graph);

  CHECK(faults.size() == 4);
  CHECK(totalAtoms(faults) == 14);
}

TEST_CASE("a gate with no fanout keeps its own fault class", "[FaultList]") {
  // a, b -> dead (And, unread)
  // a -> live (Not) -> y
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId dead = graph.addGate(GateType::And, "dead");
  const GateId live = graph.addGate(GateType::Not, "live");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, dead);
  graph.addEdge(b, dead);
  graph.addEdge(a, live);
  graph.addEdge(live, y);
  REQUIRE(graph.levelize().ok());

  const FaultList faults = generateFaultList(graph);

  CHECK(faults.size() == 6);
  CHECK(totalAtoms(faults) == 16);

  const Fault deadOutputSA1{PinRef{dead, PinKind::Output, 0}, StuckValue::SA1};
  const bool hasUntestableSingleton = std::any_of(
      faults.begin(), faults.end(), [&](const FaultClass& faultClass) {
        return faultClass.representative == deadOutputSA1 && faultClass.equivalent.empty();
      });
  CHECK(hasUntestableSingleton);
}

TEST_CASE("a half adder collapses to 10 fault classes", "[FaultList]") {
  auto graph = buildTestGraph(R"(
    module half_adder(input a, input b, output sum, output cout);
      xor(sum, a, b);
      and(cout, a, b);
    endmodule
  )",
                              "half_adder");

  const FaultList faults = generateFaultList(graph);

  CHECK(faults.size() == 10);
  CHECK(totalAtoms(faults) == 20);
}

TEST_CASE("a full adder's fault list covers every atomic fault exactly once", "[FaultList]") {
  auto graph = buildTestGraph(R"(
    module full_adder(input a, input b, input cin, output sum, output cout);
      wire x1, x2, x3;
      xor(x1, a, b);
      xor(sum, x1, cin);
      and(x2, x1, cin);
      and(x3, a, b);
      or(cout, x2, x3);
    endmodule
  )",
                              "full_adder");

  const FaultList faults = generateFaultList(graph);

  // 3 PIs x 2 + 5 internal 2-input gates x 6 + 2 POs x 2 = 40 atomic faults.
  CHECK(totalAtoms(faults) == 40);
  // Collapsing must have merged something.
  CHECK(faults.size() < 40);
}

namespace {

GateId findGate(const Graph& graph, std::string_view name) {
  for (std::size_t i = 0; i < graph.size(); ++i) {
    const auto& gate = graph.gate(static_cast<GateId>(i));
    if (gate.name == name) {
      return gate.id;
    }
  }
  FAIL("no gate named " << name);
  return 0;
}

bool sameClass(const FaultList& faults, const Fault& x, const Fault& y) {
  for (const auto& faultClass : faults) {
    bool hasX = faultClass.representative == x;
    bool hasY = faultClass.representative == y;
    for (const auto& equiv : faultClass.equivalent) {
      hasX = hasX || equiv == x;
      hasY = hasY || equiv == y;
    }
    if (hasX && hasY) {
      return true;
    }
  }
  return false;
}

std::size_t pinIndexOf(const Graph& graph, GateId consumer, GateId driver) {
  const auto& fanin = graph.gate(consumer).fanin;
  const auto it = std::find(fanin.begin(), fanin.end(), driver);
  return static_cast<std::size_t>(it - fanin.begin());
}

} // namespace

TEST_CASE("c17's fault list covers every atomic fault exactly once", "[FaultList]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");
  const FaultList faults = generateFaultList(graph);

  // 5 PIs x 2 + 6 NAND gates x 6 + 2 POs x 2 = 50 atomic faults.
  CHECK(totalAtoms(faults) == 50);
}

TEST_CASE("c17's n1 output fault is equivalent to its consumer's input fault", "[FaultList]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");
  const FaultList faults = generateFaultList(graph);

  const GateId n1 = findGate(graph, "n1");
  REQUIRE(graph.gate(n1).fanout.size() == 1);
  const GateId consumer = graph.gate(n1).fanout[0];
  const std::size_t pin = pinIndexOf(graph, consumer, n1);

  const Fault n1Out{PinRef{n1, PinKind::Output, 0}, StuckValue::SA0};
  const Fault consumerIn{PinRef{consumer, PinKind::Input, pin}, StuckValue::SA0};
  CHECK(sameClass(faults, n1Out, consumerIn));
}

TEST_CASE("c17's n3 fanout branches are not merged with each other", "[FaultList]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");
  const FaultList faults = generateFaultList(graph);

  const GateId n3 = findGate(graph, "n3");
  REQUIRE(graph.gate(n3).fanout.size() == 2);
  const GateId consumer0 = graph.gate(n3).fanout[0];
  const GateId consumer1 = graph.gate(n3).fanout[1];

  const Fault firstBranch{PinRef{consumer0, PinKind::Input, pinIndexOf(graph, consumer0, n3)},
                          StuckValue::SA0};
  const Fault secondBranch{PinRef{consumer1, PinKind::Input, pinIndexOf(graph, consumer1, n3)},
                           StuckValue::SA0};
  CHECK_FALSE(sameClass(faults, firstBranch, secondBranch));
}
