#include "atpg/compact/Compact.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/gen/TestGen.hpp"
#include "atpg/ir/Graph.hpp"
#include "atpg/stil/Stil.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "../Test.hpp"
#include "../stil/StilReader.hpp"

using namespace atpg::ir;
using namespace atpg::stil;

namespace {

/// A 2-input AND driving one primary output, named as a design would be.
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

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("writeStil emits the program skeleton", "[Stil]") {
  const Graph graph = andGraph();
  const atpg::Result<std::string> stil = writeStil(graph, {}, "demo");
  REQUIRE(stil.ok());

  const std::string& text = stil.value();
  CHECK(contains(text, "STIL 1.0;"));
  CHECK(contains(text, "Title \"atpg-generated test patterns for demo\";"));
  CHECK(contains(text, "Timing \"timing\" {"));
  CHECK(contains(text, "Period '100ns';"));
  CHECK(contains(text, "PatternBurst \"burst\" {"));
  CHECK(contains(text, "PatternExec {"));
  CHECK(contains(text, "Pattern \"patterns\" {"));
  CHECK(contains(text, "W \"wft\";"));
}

TEST_CASE("writeStil declares inputs then outputs, with directions", "[Stil]") {
  const Graph graph = andGraph();
  const atpg::Result<std::string> stil = writeStil(graph, {}, "demo");
  REQUIRE(stil.ok());

  const std::string& text = stil.value();
  CHECK(contains(text, "  \"a\" In;"));
  CHECK(contains(text, "  \"b\" In;"));
  CHECK(contains(text, "  \"y\" Out;"));
  CHECK(contains(text, "\"PI\" = '\"a\" + \"b\"';"));
  CHECK(contains(text, "\"PO\" = '\"y\"';"));
}

TEST_CASE("writeStil emits one vector per pattern with its expected response", "[Stil]") {
  const Graph graph = andGraph();
  // An AND outputs 1 only for 11, so the responses are L, L, L, H in order.
  const std::vector<std::vector<bool>> patterns = {
      {false, false}, {false, true}, {true, false}, {true, true}};

  const atpg::Result<std::string> stil = writeStil(graph, patterns, "demo");
  REQUIRE(stil.ok());

  const std::string& text = stil.value();
  CHECK(contains(text, "V { \"PI\"=00; \"PO\"=L; }"));
  CHECK(contains(text, "V { \"PI\"=01; \"PO\"=L; }"));
  CHECK(contains(text, "V { \"PI\"=10; \"PO\"=L; }"));
  CHECK(contains(text, "V { \"PI\"=11; \"PO\"=H; }"));
}

TEST_CASE("writeStil rejects a pattern whose width does not match the inputs", "[Stil]") {
  const Graph graph = andGraph();
  CHECK_FALSE(writeStil(graph, {{true}}, "demo").ok());
}

TEST_CASE("writeStil rejects duplicate signal names", "[Stil]") {
  // STIL requires signal names to be unique; a design whose input and
  // output share a name would emit a file no tester can load.
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "dup");
  const GateId n = graph.addGate(GateType::Not, "n");
  const GateId y = graph.addGate(GateType::Po, "dup");
  graph.addEdge(a, n);
  graph.addEdge(n, y);
  REQUIRE(graph.levelize().ok());

  CHECK_FALSE(writeStil(graph, {}, "demo").ok());
}

TEST_CASE("writeStil rejects a signal name that cannot be quoted", "[Stil]") {
  // SystemVerilog escaped identifiers may contain a double quote, which
  // would terminate the STIL string it is written into and silently produce
  // a broken program.
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a\"q");
  const GateId n = graph.addGate(GateType::Not, "n");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, n);
  graph.addEdge(n, y);
  REQUIRE(graph.levelize().ok());

  CHECK_FALSE(writeStil(graph, {}, "demo").ok());
}

TEST_CASE("writeStil rejects a signal named after a signal group", "[Stil]") {
  // A port called PI or PO makes every vector's group assignment ambiguous:
  // a reader could resolve it to the 1-bit signal instead of the group.
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "PI");
  const GateId n = graph.addGate(GateType::Not, "n");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, n);
  graph.addEdge(n, y);
  REQUIRE(graph.levelize().ok());

  CHECK_FALSE(writeStil(graph, {}, "demo").ok());
}

TEST_CASE("writeStil rejects an output named after a signal group", "[Stil]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId n = graph.addGate(GateType::Not, "n");
  const GateId y = graph.addGate(GateType::Po, "PO");
  graph.addEdge(a, n);
  graph.addEdge(n, y);
  REQUIRE(graph.levelize().ok());

  CHECK_FALSE(writeStil(graph, {}, "demo").ok());
}

TEST_CASE("writeStil rejects a design with no primary outputs", "[Stil]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId n = graph.addGate(GateType::Not, "n");
  graph.addEdge(a, n);
  REQUIRE(graph.levelize().ok());

  CHECK_FALSE(writeStil(graph, {}, "demo").ok());
}

TEST_CASE("writeStil rejects a design with no primary inputs", "[Stil]") {
  Graph graph;
  const GateId g = graph.addGate(GateType::And, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  CHECK_FALSE(writeStil(graph, {}, "demo").ok());
}

TEST_CASE("c17's compacted patterns render to a STIL program that reads back", "[Stil]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");
  REQUIRE(graph.levelize().ok());

  const atpg::fault::FaultList faults = atpg::fault::generateFaultList(graph);
  const atpg::Result<atpg::gen::TestSet> tests = atpg::gen::generateTests(graph, faults);
  REQUIRE(tests.ok());

  std::vector<std::vector<bool>> patterns;
  for (const atpg::gen::TestResult& test : tests.value()) {
    REQUIRE(test.outcome == atpg::gen::TestOutcome::Testable);
    patterns.push_back(test.pattern);
  }

  const atpg::Result<atpg::compact::CompactResult> compacted =
      atpg::compact::compact(graph, faults, patterns);
  REQUIRE(compacted.ok());

  const atpg::Result<std::string> stil = writeStil(graph, compacted.value().patterns, "c17");
  REQUIRE(stil.ok());

  const atpg::Result<atpg::testing::StilProgram> program = atpg::testing::readStil(stil.value());
  REQUIRE(program.ok());

  // c17 has five inputs and two outputs, so a transposed or misaligned
  // field would be visible here where an equal-width design hides it.
  CHECK(program.value().piGroup.size() == 5);
  CHECK(program.value().poGroup.size() == 2);
  REQUIRE(program.value().vectors.size() == compacted.value().patterns.size());

  for (std::size_t p = 0; p < program.value().vectors.size(); ++p) {
    CHECK(program.value().vectors[p].inputs.size() == 5);
    CHECK(program.value().vectors[p].outputs.size() == 2);
  }
}
