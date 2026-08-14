#include "../Test.hpp"

#include "atpg/fault/Fault.hpp"
#include "atpg/ir/Graph.hpp"
#include "atpg/sim/LogicSim.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace atpg::fault;
using namespace atpg::ir;
using namespace atpg::sim;

namespace {

std::vector<bool> requireSim(const Graph& graph, const std::vector<bool>& piValues) {
  atpg::Result<std::vector<bool>> result = simulate(graph, piValues);
  REQUIRE(result.ok());
  return std::move(result.value());
}

} // namespace

TEST_CASE("simulate evaluates a half adder's truth table", "[LogicSim]") {
  auto graph = buildTestGraph(R"(
    module half_adder(input a, input b, output sum, output cout);
      xor(sum, a, b);
      and(cout, a, b);
    endmodule
  )",
                              "half_adder");

  struct Case {
    bool a, b, sum, cout;
  };
  const Case cases[] = {
      {false, false, false, false},
      {false, true, true, false},
      {true, false, true, false},
      {true, true, false, true},
  };

  for (const auto& c : cases) {
    const auto outputs = requireSim(graph, {c.a, c.b});
    REQUIRE(outputs.size() == 2);
    CHECK(outputs[0] == c.sum);
    CHECK(outputs[1] == c.cout);
  }
}

TEST_CASE("simulate evaluates a full adder's truth table", "[LogicSim]") {
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

  for (int bits = 0; bits < 8; ++bits) {
    const bool a = (bits & 1) != 0;
    const bool b = (bits & 2) != 0;
    const bool cin = (bits & 4) != 0;
    const int sum = static_cast<int>(a) + static_cast<int>(b) + static_cast<int>(cin);

    const auto outputs = requireSim(graph, {a, b, cin});
    REQUIRE(outputs.size() == 2);
    CHECK(outputs[0] == static_cast<bool>(sum & 1));
    CHECK(outputs[1] == static_cast<bool>(sum >> 1));
  }
}

TEST_CASE("simulate evaluates the c17 benchmark for known vectors", "[LogicSim]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");

  // Inputs are (n1, n2, n3, n6, n7); outputs are (n22, n23). Hand-computed
  // from the netlist's NAND equations.
  {
    const auto outputs = requireSim(graph, {false, false, false, false, false});
    REQUIRE(outputs.size() == 2);
    CHECK(outputs[0] == false);
    CHECK(outputs[1] == false);
  }
  {
    const auto outputs = requireSim(graph, {true, true, true, true, true});
    REQUIRE(outputs.size() == 2);
    CHECK(outputs[0] == true);
    CHECK(outputs[1] == false);
  }
}

TEST_CASE("simulate rejects a stimulus vector of the wrong width", "[LogicSim]") {
  auto graph = buildTestGraph(R"(
    module buf1(input a, output y);
      buf(y, a);
    endmodule
  )",
                              "buf1");

  CHECK_FALSE(simulate(graph, std::vector<bool>{true, false}).ok());
}

TEST_CASE("simulateWithFault matches simulate when the stuck value equals the good value",
          "[LogicSim]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(GateType::And, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  const PinRef gOutput{g, PinKind::Output, 0};
  const auto good = simulate(graph, {true, true});
  const auto faulted = simulateWithFault(graph, {true, true}, gOutput, StuckValue::SA1);
  REQUIRE(good.ok());
  REQUIRE(faulted.ok());
  CHECK(faulted.value() == good.value());
}

TEST_CASE("simulateWithFault forces a gate's output-pin fault", "[LogicSim]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(GateType::And, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  const auto good = simulate(graph, {true, true});
  REQUIRE(good.ok());
  CHECK(good.value()[0] == true);

  const PinRef gOutput{g, PinKind::Output, 0};
  const auto faulted = simulateWithFault(graph, {true, true}, gOutput, StuckValue::SA0);
  REQUIRE(faulted.ok());
  CHECK(faulted.value()[0] == false);
}

TEST_CASE("simulateWithFault forces a primary input's output-pin fault", "[LogicSim]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId g = graph.addGate(GateType::Buf, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  const auto good = simulate(graph, {true});
  REQUIRE(good.ok());
  CHECK(good.value()[0] == true);

  const PinRef aOutput{a, PinKind::Output, 0};
  const auto faulted = simulateWithFault(graph, {true}, aOutput, StuckValue::SA0);
  REQUIRE(faulted.ok());
  CHECK(faulted.value()[0] == false);
}

TEST_CASE("simulateWithFault only overrides the targeted consumer's read of an input-pin fault",
          "[LogicSim]") {
  // a -> g1 (Buf) -> y1
  //   -> g2 (Buf) -> y2
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId g1 = graph.addGate(GateType::Buf, "g1");
  const GateId g2 = graph.addGate(GateType::Buf, "g2");
  const GateId y1 = graph.addGate(GateType::Po, "y1");
  const GateId y2 = graph.addGate(GateType::Po, "y2");
  graph.addEdge(a, g1);
  graph.addEdge(a, g2);
  graph.addEdge(g1, y1);
  graph.addEdge(g2, y2);
  REQUIRE(graph.levelize().ok());

  // a = 1, but g1's read of it is stuck at 0: g1's output (y1) should read
  // 0, while g2 (y2), which reads the same net normally, must still see
  // the real value 1.
  const PinRef g1Input{g1, PinKind::Input, 0};
  const auto result = simulateWithFault(graph, {true}, g1Input, StuckValue::SA0);
  REQUIRE(result.ok());
  REQUIRE(result.value().size() == 2);
  CHECK(result.value()[0] == false); // y1
  CHECK(result.value()[1] == true);  // y2
}
