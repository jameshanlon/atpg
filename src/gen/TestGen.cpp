#include "atpg/gen/TestGen.hpp"

#include "atpg/sim/LogicSim.hpp"

#include <fmt/format.h>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/sat_parameters.pb.h"

#include <cstdint>
#include <vector>

namespace atpg::gen {

namespace {

using operations_research::sat::BoolVar;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::SatParameters;
using operations_research::sat::SolutionBooleanValue;
using operations_research::sat::SolveWithParameters;

// Bounds the exhaustive Redundant-path self-verification below: at this many
// primary inputs, 2^16 patterns (each two simulations) is still cheap, but
// the search space doubles with every additional input, so larger circuits
// skip the check rather than pay for it.
constexpr std::size_t kMaxExhaustiveVerificationInputs = 16;

// For circuits small enough to afford it (see kMaxExhaustiveVerificationInputs),
// exhaustively simulates every input pattern to check whether any of them
// actually detects `fault` - a cheap tripwire on the Redundant path, mirroring
// the Testable-path self-verification below, that INFEASIBLE alone can't
// provide: CP-SAT wrongly reporting a testable fault as redundant would
// otherwise go uncaught.
Result<bool> anyPatternDetects(const ir::Graph& graph, const fault::Fault& fault) {
  const std::size_t numInputs = graph.primaryInputs().size();
  const std::uint64_t numPatterns = std::uint64_t{1} << numInputs;
  std::vector<bool> pattern(numInputs);
  for (std::uint64_t bits = 0; bits < numPatterns; ++bits) {
    for (std::size_t i = 0; i < numInputs; ++i) {
      pattern[i] = ((bits >> i) & 1) != 0;
    }
    ATPG_ASSIGN_OR_RETURN(const std::vector<bool> goodOutputs, sim::simulate(graph, pattern));
    ATPG_ASSIGN_OR_RETURN(const std::vector<bool> faultyOutputs,
                          sim::simulateWithFault(graph, pattern, fault.pin, fault.value));
    if (goodOutputs != faultyOutputs) {
      return true;
    }
  }
  return false;
}

// out == AND(ins).
void encodeAnd(CpModelBuilder& builder, const std::vector<BoolVar>& ins, BoolVar out) {
  builder.AddBoolAnd(ins).OnlyEnforceIf(out);
  std::vector<BoolVar> clause;
  clause.reserve(ins.size() + 1);
  for (const BoolVar& in : ins) {
    clause.push_back(in.Not());
  }
  clause.push_back(out);
  builder.AddBoolOr(clause);
}

// out == OR(ins).
void encodeOr(CpModelBuilder& builder, const std::vector<BoolVar>& ins, BoolVar out) {
  builder.AddBoolOr(ins).OnlyEnforceIf(out);
  for (const BoolVar& in : ins) {
    builder.AddImplication(in, out);
  }
}

// out == XOR(ins) (true iff an odd number of ins are true).
void encodeXor(CpModelBuilder& builder, const std::vector<BoolVar>& ins, BoolVar out) {
  std::vector<BoolVar> literals = ins;
  literals.push_back(out.Not());
  builder.AddBoolXor(literals);
}

// Encodes out == f(ins) for gate's function, for every gate type except Pi
// (a free variable, nothing to encode). `pin(i)` returns the BoolVar to
// read for fanin index i - normally the driving gate's own output
// variable, but the caller substitutes a fixed constant for exactly one
// (gate, pin index) when injecting an input-pin fault.
template <typename PinFn>
void encodeGate(CpModelBuilder& builder, const ir::Gate& gate, PinFn&& pin, BoolVar out) {
  if (gate.type == ir::GateType::Pi) {
    return;
  }

  std::vector<BoolVar> ins;
  ins.reserve(gate.fanin.size());
  for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
    ins.push_back(pin(i));
  }

  switch (gate.type) {
    case ir::GateType::And:
      encodeAnd(builder, ins, out);
      return;
    case ir::GateType::Nand:
      encodeAnd(builder, ins, out.Not());
      return;
    case ir::GateType::Or:
      encodeOr(builder, ins, out);
      return;
    case ir::GateType::Nor:
      encodeOr(builder, ins, out.Not());
      return;
    case ir::GateType::Xor:
      encodeXor(builder, ins, out);
      return;
    case ir::GateType::Xnor:
      encodeXor(builder, ins, out.Not());
      return;
    case ir::GateType::Buf:
    case ir::GateType::Po:
      builder.AddEquality(out, ins[0]);
      return;
    case ir::GateType::Not:
      builder.AddEquality(out, ins[0].Not());
      return;
    case ir::GateType::Pi:
      return;
  }
}

Result<TestResult> generateOne(const ir::Graph& graph, const fault::Fault& fault,
                               const Options& options) {
  CpModelBuilder builder;
  const std::size_t n = graph.size();

  std::vector<BoolVar> good(n);
  std::vector<BoolVar> faulty(n);

  for (const ir::GateId id : graph.primaryInputs()) {
    good[id] = builder.NewBoolVar();
    faulty[id] = good[id]; // same variable: a test pattern is one shared stimulus
  }

  const BoolVar trueVar = builder.TrueVar();
  const BoolVar falseVar = builder.FalseVar();

  for (const ir::GateId id : graph.levelOrder()) {
    const ir::Gate& gate = graph.gate(id);
    if (gate.type == ir::GateType::Pi) {
      if (fault.pin.kind == fault::PinKind::Output && fault.pin.gate == id) {
        faulty[id] = fault.value == fault::StuckValue::SA1 ? trueVar : falseVar;
      }
      continue;
    }

    good[id] = builder.NewBoolVar();
    encodeGate(builder, gate, [&](std::size_t i) { return good[gate.fanin[i]]; }, good[id]);

    faulty[id] = builder.NewBoolVar();
    const bool isOutputFault = fault.pin.kind == fault::PinKind::Output && fault.pin.gate == id;
    if (isOutputFault) {
      builder.AddEquality(faulty[id], fault.value == fault::StuckValue::SA1 ? trueVar : falseVar);
    } else {
      encodeGate(
          builder, gate,
          [&](std::size_t i) -> BoolVar {
            const bool isThisInputFault = fault.pin.kind == fault::PinKind::Input &&
                                          fault.pin.gate == id && fault.pin.inputIndex == i;
            if (isThisInputFault) {
              return fault.value == fault::StuckValue::SA1 ? trueVar : falseVar;
            }
            return faulty[gate.fanin[i]];
          },
          faulty[id]);
    }
  }

  std::vector<BoolVar> diffs;
  diffs.reserve(graph.primaryOutputs().size());
  for (const ir::GateId id : graph.primaryOutputs()) {
    const BoolVar diff = builder.NewBoolVar();
    encodeXor(builder, {good[id], faulty[id]}, diff);
    diffs.push_back(diff);
  }
  builder.AddBoolOr(diffs);

  SatParameters params;
  params.set_max_time_in_seconds(options.timeLimitSeconds);
  const CpSolverResponse response = SolveWithParameters(builder.Build(), params);

  TestResult result;
  result.fault = fault;

  if (response.status() == CpSolverStatus::MODEL_INVALID) {
    return Error("generateTests: CP-SAT rejected the model as invalid");
  }
  if (response.status() == CpSolverStatus::INFEASIBLE) {
    if (graph.primaryInputs().size() <= kMaxExhaustiveVerificationInputs) {
      ATPG_ASSIGN_OR_RETURN(const bool anyDetects, anyPatternDetects(graph, fault));
      if (anyDetects) {
        return Error(
            fmt::format("generateTests: CP-SAT reported the fault at gate {} as redundant, but "
                        "exhaustive simulation found a pattern that detects it - this is an "
                        "encoding bug",
                        fault.pin.gate));
      }
    }
    result.outcome = TestOutcome::Redundant;
    return result;
  }
  if (response.status() != CpSolverStatus::OPTIMAL &&
      response.status() != CpSolverStatus::FEASIBLE) {
    result.outcome = TestOutcome::Aborted;
    return result;
  }

  result.outcome = TestOutcome::Testable;
  result.pattern.reserve(graph.primaryInputs().size());
  for (const ir::GateId id : graph.primaryInputs()) {
    result.pattern.push_back(SolutionBooleanValue(response, good[id]));
  }

  // Self-verification: an independently-implemented simulator must agree
  // this pattern actually detects the fault, or the encoding above has a
  // bug. This should never fire - see the design doc's "Self-verification"
  // section.
  ATPG_ASSIGN_OR_RETURN(const std::vector<bool> goodOutputs, sim::simulate(graph, result.pattern));
  ATPG_ASSIGN_OR_RETURN(const std::vector<bool> faultyOutputs,
                        sim::simulateWithFault(graph, result.pattern, fault.pin, fault.value));
  if (goodOutputs == faultyOutputs) {
    return Error(
        fmt::format("generateTests: CP-SAT reported a detecting pattern for gate {} that the "
                    "independent simulator disagrees with - this is an encoding bug",
                    fault.pin.gate));
  }

  return result;
}

} // namespace

Result<TestSet> generateTests(const ir::Graph& graph, const fault::FaultList& faults,
                              Options options) {
  TestSet results;
  for (const auto& faultClass : faults) {
    ATPG_ASSIGN_OR_RETURN(TestResult result,
                          generateOne(graph, faultClass.representative, options));
    results.add(std::move(result));
  }
  return results;
}

} // namespace atpg::gen
