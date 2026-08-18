#include "atpg/gen/TestGen.hpp"

#include "atpg/fsim/FaultSim.hpp"
#include "atpg/sim/LogicSim.hpp"

#include <fmt/format.h>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/sat_parameters.pb.h"

#include <cstddef>
#include <cstdint>
#include <string>
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

/// Confirms `pattern` really does expose `fault` at a primary output.
///
/// Fault simulation is what claimed it does; this re-checks with the scalar
/// simulator before a fault is dropped without ever being handed to the
/// solver. A false detection here would silently overstate coverage, and a
/// differential test against generateTests could not catch it - both would
/// report Testable, just for different reasons.
Status verifyDetects(const ir::Graph& graph, const std::vector<bool>& pattern,
                     const std::vector<bool>& goodOutputs, const fault::Fault& fault) {
  ATPG_ASSIGN_OR_RETURN(const std::vector<bool> faultyOutputs,
                        sim::simulateWithFault(graph, pattern, fault.pin, fault.value));
  if (goodOutputs == faultyOutputs) {
    return Error(fmt::format(
        "generateTestsWithDropping: fault simulation reported a detection of {}/{}{}/{} that "
        "independent simulation disagrees with - this is a bug in atpg's fault simulator "
        "or its SAT encoding, not in the caller's input",
        fault.pin.gate, fault.pin.kind == fault::PinKind::Output ? "out" : "in",
        fault.pin.kind == fault::PinKind::Output ? std::string()
                                                 : std::to_string(fault.pin.inputIndex),
        fault.value == fault::StuckValue::SA0 ? "SA0" : "SA1"));
  }
  return {};
}

} // namespace

Result<TestSet> generateTests(const ir::Graph& graph, const fault::FaultList& faults,
                              Options options) {
  if (options.timeLimitSeconds < 0.0) {
    return Error(fmt::format("generateTests: options.timeLimitSeconds must not be negative, got {}",
                             options.timeLimitSeconds));
  }

  TestSet results;
  for (const auto& faultClass : faults) {
    ATPG_ASSIGN_OR_RETURN(TestResult result,
                          generateOne(graph, faultClass.representative, options));
    results.add(std::move(result));
  }
  return results;
}

Result<TestPlan> generateTestsWithDropping(const ir::Graph& graph, const fault::FaultList& faults,
                                           Options options) {
  if (options.timeLimitSeconds < 0.0) {
    return Error(fmt::format("generateTestsWithDropping: options.timeLimitSeconds must not be "
                             "negative, got {}",
                             options.timeLimitSeconds));
  }

  std::vector<fault::Fault> representatives;
  for (const auto& faultClass : faults) {
    representatives.push_back(faultClass.representative);
  }

  std::vector<FaultResolution> resolutions(representatives.size());
  std::vector<char> resolved(representatives.size(), 0);
  for (std::size_t i = 0; i < representatives.size(); ++i) {
    resolutions[i].fault = representatives[i];
  }

  TestPlan plan;

  for (std::size_t i = 0; i < representatives.size(); ++i) {
    if (resolved[i] != 0) {
      continue; // dropped by an earlier pattern - no solver call needed
    }

    ATPG_ASSIGN_OR_RETURN(TestResult result, generateOne(graph, representatives[i], options));
    resolutions[i].outcome = result.outcome;
    resolved[i] = 1;

    if (result.outcome != TestOutcome::Testable) {
      continue; // redundant or aborted: no pattern, nothing to drop with
    }

    const std::size_t patternIndex = plan.patterns().size();
    resolutions[i].patternIndex = patternIndex;
    plan.addPattern(result.pattern);

    // Everything before i is already resolved: the loop resolves each fault
    // it reaches, so only later indices can still be open.
    fault::FaultList remaining;
    std::vector<std::size_t> remainingIndex;
    for (std::size_t j = i + 1; j < representatives.size(); ++j) {
      if (resolved[j] == 0) {
        remaining.add(fault::FaultClass{representatives[j], {}});
        remainingIndex.push_back(j);
      }
    }
    if (remainingIndex.empty()) {
      continue;
    }

    ATPG_ASSIGN_OR_RETURN(const fsim::SimResult detected,
                          fsim::simulateFaults(graph, remaining, {result.pattern}));
    ATPG_ASSIGN_OR_RETURN(const std::vector<bool> goodOutputs,
                          sim::simulate(graph, result.pattern));

    std::size_t k = 0;
    for (const fsim::FaultStatus& status : detected) {
      if (k >= remainingIndex.size()) {
        return Error("generateTestsWithDropping: fault simulation returned more results than "
                     "faults it was given");
      }
      const std::size_t j = remainingIndex[k];
      ++k;
      // The whole drop step rests on simulateFaults reporting results in the
      // order of the FaultList it was handed. That holds structurally, but a
      // silent drift would attribute detections to the wrong faults - and
      // those faults are usually still plausibly Testable, so the outcome
      // comparison against generateTests would not notice. Check it here
      // rather than trust it.
      if (!(status.fault == representatives[j])) {
        return Error("generateTestsWithDropping: fault simulation returned results out of order");
      }
      if (!status.detected) {
        continue;
      }
      ATPG_RETURN_IF_ERROR(verifyDetects(graph, result.pattern, goodOutputs, representatives[j]));
      resolutions[j].outcome = TestOutcome::Testable;
      resolutions[j].patternIndex = patternIndex;
      resolved[j] = 1;
    }
  }

  for (FaultResolution& resolution : resolutions) {
    plan.addResolution(std::move(resolution));
  }
  return plan;
}

} // namespace atpg::gen
