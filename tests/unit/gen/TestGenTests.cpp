#include <catch2/catch_test_macros.hpp>

#include "atpg/fault/Fault.hpp"
#include "atpg/gen/TestGen.hpp"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/sat_parameters.pb.h"

using operations_research::sat::BoolVar;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::SatParameters;
using operations_research::sat::SolutionBooleanValue;
using operations_research::sat::SolveWithParameters;

using namespace atpg::fault;
using namespace atpg::gen;

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
