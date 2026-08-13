#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

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
