#include "../Test.hpp"

#include "atpg/ir/Graph.hpp"
#include "atpg/sim/LogicSim.hpp"

#include <catch2/catch_test_macros.hpp>

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
