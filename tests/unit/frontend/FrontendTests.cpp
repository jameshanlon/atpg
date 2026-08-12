#include "../Test.hpp"

#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace atpg::ir;

TEST_CASE("half adder built from primitives flattens correctly", "[Frontend]") {
  auto graph = buildTestGraph(R"(
    module half_adder(input a, input b, output sum, output cout);
      xor(sum, a, b);
      and(cout, a, b);
    endmodule
  )",
                              "half_adder");

  CHECK(graph.primaryInputs().size() == 2);
  CHECK(graph.primaryOutputs().size() == 2);
  // 2 PI + 2 PO + xor + and.
  CHECK(graph.size() == 6);
}

TEST_CASE("full adder built from primitives flattens correctly", "[Frontend]") {
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

  CHECK(graph.primaryInputs().size() == 3);
  CHECK(graph.primaryOutputs().size() == 2);
  // 3 PI + 2 PO + 5 internal gates.
  CHECK(graph.size() == 10);
}

TEST_CASE("module hierarchy is flattened across scalar ports", "[Frontend]") {
  auto graph = buildTestGraph(R"(
    module inv(input a, output y);
      not(y, a);
    endmodule

    module top(input a, output y);
      inv u1(.a(a), .y(y));
    endmodule
  )",
                              "top");

  CHECK(graph.primaryInputs().size() == 1);
  CHECK(graph.primaryOutputs().size() == 1);
  // PI + not + PO.
  CHECK(graph.size() == 3);
}

TEST_CASE("multi-bit buses are connected bit-by-bit across an instance boundary", "[Frontend]") {
  auto graph = buildTestGraph(R"(
    module inv2(input [1:0] a, output [1:0] y);
      not(y[0], a[0]);
      not(y[1], a[1]);
    endmodule

    module top(input [1:0] a, output [1:0] y);
      inv2 u1(.a(a), .y(y));
    endmodule
  )",
                              "top");

  REQUIRE(graph.primaryInputs().size() == 2);
  REQUIRE(graph.primaryOutputs().size() == 2);
  // 2 PI + 2 not + 2 PO.
  CHECK(graph.size() == 6);
}

TEST_CASE("an undriven net is reported as an error", "[Frontend]") {
  auto result = tryBuildTestGraph(R"(
    module top(input a, output y);
      and(y, a, b);
    endmodule
  )",
                                  "top");
  CHECK_FALSE(result.ok());
}

TEST_CASE("the c17 benchmark loads from file and flattens correctly", "[Frontend]") {
  auto graph = buildTestGraphFromFile(std::string(ATPG_TEST_DATA_DIR) + "/c17.sv", "c17");

  CHECK(graph.primaryInputs().size() == 5);
  CHECK(graph.primaryOutputs().size() == 2);
  // 5 PI + 2 PO + 6 NAND gates.
  CHECK(graph.size() == 13);
}

TEST_CASE("a continuous assign is reported as an unsupported construct", "[Frontend]") {
  auto result = tryBuildTestGraph(R"(
    module top(input a, output y);
      assign y = a;
    endmodule
  )",
                                  "top");
  CHECK_FALSE(result.ok());
}

TEST_CASE("a procedural block is reported as an unsupported construct", "[Frontend]") {
  auto result = tryBuildTestGraph(R"(
    module top(input a, output y);
      logic r;
      always_comb r = a;
      buf(y, r);
    endmodule
  )",
                                  "top");
  CHECK_FALSE(result.ok());
}

TEST_CASE("an unsupported top module name is reported as an error", "[Frontend]") {
  auto result = tryBuildTestGraph(R"(
    module top(input a, output y);
      buf(y, a);
    endmodule
  )",
                                  "does_not_exist");
  CHECK_FALSE(result.ok());
}
