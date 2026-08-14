#include <catch2/catch_test_macros.hpp>

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
