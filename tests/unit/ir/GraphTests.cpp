#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace atpg::ir;

TEST_CASE("levelize assigns level 0 to primary inputs", "[Graph]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(GateType::And, "g");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  REQUIRE(graph.levelize().ok());

  CHECK(graph.gate(a).level == 0);
  CHECK(graph.gate(b).level == 0);
  CHECK(graph.gate(g).level == 1);
}

TEST_CASE("levelize takes the max level across multiple fanin paths", "[Graph]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId n1 = graph.addGate(GateType::Not, "n1");
  const GateId n2 = graph.addGate(GateType::Not, "n2");
  const GateId g = graph.addGate(GateType::And, "g");
  graph.addEdge(a, n1);
  graph.addEdge(n1, n2);
  graph.addEdge(b, g);
  graph.addEdge(n2, g);
  REQUIRE(graph.levelize().ok());

  CHECK(graph.gate(n1).level == 1);
  CHECK(graph.gate(n2).level == 2);
  CHECK(graph.gate(g).level == 3);
}

TEST_CASE("levelize orders gates non-decreasing by level", "[Graph]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId n = graph.addGate(GateType::Not, "n");
  graph.addEdge(a, n);
  REQUIRE(graph.levelize().ok());

  int lastLevel = -1;
  for (const GateId id : graph.levelOrder()) {
    CHECK(graph.gate(id).level >= lastLevel);
    lastLevel = graph.gate(id).level;
  }
}

TEST_CASE("levelize detects a combinational cycle", "[Graph]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::And, "a");
  const GateId b = graph.addGate(GateType::And, "b");
  graph.addEdge(a, b);
  graph.addEdge(b, a);

  CHECK_FALSE(graph.levelize().ok());
}

TEST_CASE("addGate registers Pi/Po gates automatically", "[Graph]") {
  Graph graph;
  const GateId pi = graph.addGate(GateType::Pi, "a");
  const GateId po = graph.addGate(GateType::Po, "y");
  graph.addGate(GateType::And, "g");

  REQUIRE(graph.primaryInputs().size() == 1);
  CHECK(graph.primaryInputs()[0] == pi);
  REQUIRE(graph.primaryOutputs().size() == 1);
  CHECK(graph.primaryOutputs()[0] == po);
}

TEST_CASE("inputIndex returns the fanin slot a driver occupies", "[Graph]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId c = graph.addGate(GateType::Pi, "c");
  const GateId g = graph.addGate(GateType::And, "g");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(c, g);

  CHECK(graph.inputIndex(g, a) == 0);
  CHECK(graph.inputIndex(g, b) == 1);
  CHECK(graph.inputIndex(g, c) == 2);
}
