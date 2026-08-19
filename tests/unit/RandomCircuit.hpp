#pragma once

#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <random>
#include <string>
#include <vector>

/// Shared support for the property-based tests.
///
/// The simulator here is deliberately a *second*, independent implementation
/// of gate semantics rather than a call into atpg::sim - a property test is
/// only worth its runtime if its ground truth was derived separately from
/// the code under test. A bug present in both the production simulator and
/// the oracle would be invisible. Never reimplement anything here by
/// delegating to atpg::sim.
namespace atpg::testing {

/// Deterministic pseudo-random circuit generator biased towards fanout
/// stems and reconvergent cones - the structures fault collapsing and the
/// SAT encoding both have to get exactly right. Primary inputs are capped
/// low enough that callers can brute-force all 2^n input vectors.
inline ir::Graph randomCircuit(std::mt19937& rng) {
  std::uniform_int_distribution<int> piCountDist(2, 6);
  std::uniform_int_distribution<int> gateCountDist(1, 10);
  const int piCount = piCountDist(rng);
  const int gateCount = gateCountDist(rng);

  ir::Graph graph;
  std::vector<ir::GateId> pool;
  for (int i = 0; i < piCount; ++i) {
    pool.push_back(graph.addGate(ir::GateType::Pi, "i" + std::to_string(i)));
  }

  static constexpr ir::GateType kTypes[] = {
      ir::GateType::And, ir::GateType::Nand, ir::GateType::Or,  ir::GateType::Nor,
      ir::GateType::Xor, ir::GateType::Xnor, ir::GateType::Buf, ir::GateType::Not};
  std::uniform_int_distribution<int> typeDist(0, 7);
  // Wide gates matter: an N-ary encoding bug that only bites at 4+ inputs is
  // invisible to a corpus of 2-input gates.
  std::uniform_int_distribution<int> arityDist(2, 5);
  std::uniform_real_distribution<double> unit(0.0, 1.0);

  for (int k = 0; k < gateCount; ++k) {
    const ir::GateType type = kTypes[typeDist(rng)];
    const bool unary = type == ir::GateType::Buf || type == ir::GateType::Not;
    const int arity = unary ? 1 : arityDist(rng);
    const ir::GateId id = graph.addGate(type, "g" + std::to_string(k));
    for (int p = 0; p < arity; ++p) {
      std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
      // Bias towards recently-added gates so branches reconverge and stems
      // form, rather than every gate just reading straight from the PIs.
      std::size_t idx = pick(rng);
      if (unit(rng) < 0.4 && pool.size() > 2) {
        idx = pool.size() - 1 - (pick(rng) % std::min<std::size_t>(3, pool.size()));
      }
      graph.addEdge(pool[idx], id);
    }
    pool.push_back(id);
  }

  int poCount = 0;
  for (const ir::GateId id : pool) {
    const double p = graph.gate(id).type == ir::GateType::Pi ? 0.1 : 0.3;
    if (unit(rng) < p) {
      const ir::GateId po = graph.addGate(ir::GateType::Po, "o" + std::to_string(poCount++));
      graph.addEdge(id, po);
    }
  }
  if (poCount == 0) {
    const ir::GateId po = graph.addGate(ir::GateType::Po, "o0");
    graph.addEdge(pool.back(), po);
  }
  return graph;
}

/// A random stimulus set of `patternCount` vectors, one bit per primary
/// input of `graph`.
inline std::vector<std::vector<bool>> randomPatterns(std::mt19937& rng, const ir::Graph& graph,
                                                     std::size_t patternCount) {
  const std::size_t piCount = graph.primaryInputs().size();
  std::vector<std::vector<bool>> patterns(patternCount, std::vector<bool>(piCount, false));
  for (auto& pattern : patterns) {
    for (std::size_t b = 0; b < piCount; ++b) {
      pattern[b] = (rng() & 1) != 0;
    }
  }
  return patterns;
}

/// A stuck-at fault to force during simulation, or `active == false` for the
/// good-circuit baseline.
struct InjectedFault {
  bool active = false;
  ir::GateId gate = 0;
  fault::PinKind kind = fault::PinKind::Output;
  std::size_t pin = 0;
  bool stuckValue = false;
};

/// Maps each primary input's gate id to its index in a stimulus vector.
inline std::vector<int> primaryInputIndex(const ir::Graph& graph) {
  std::vector<int> piIndex(graph.size(), -1);
  const auto& pis = graph.primaryInputs();
  for (std::size_t i = 0; i < pis.size(); ++i) {
    piIndex[pis[i]] = static_cast<int>(i);
  }
  return piIndex;
}

/// Simulates one input vector, optionally forcing a stuck-at fault.
///
/// An input-pin fault affects only the named gate's own reading of that pin;
/// every other consumer of the same driving net still sees its real value.
inline std::vector<bool> simulate(const ir::Graph& graph, const std::vector<int>& piIndex,
                                  const std::vector<bool>& piValues, const InjectedFault& fault) {
  std::vector<char> values(graph.size(), 0);
  for (const ir::GateId id : graph.levelOrder()) {
    const ir::Gate& gate = graph.gate(id);
    auto read = [&](std::size_t i) {
      if (fault.active && fault.kind == fault::PinKind::Input && fault.gate == id &&
          fault.pin == i) {
        return fault.stuckValue;
      }
      return values[gate.fanin[i]] != 0;
    };
    bool v = false;
    switch (gate.type) {
      case ir::GateType::Pi:
        v = piValues[static_cast<std::size_t>(piIndex[id])];
        break;
      case ir::GateType::Po:
      case ir::GateType::Buf:
        v = read(0);
        break;
      case ir::GateType::Not:
        v = !read(0);
        break;
      case ir::GateType::And:
      case ir::GateType::Nand: {
        bool r = true;
        for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
          r = r && read(i);
        }
        v = (gate.type == ir::GateType::And) ? r : !r;
        break;
      }
      case ir::GateType::Or:
      case ir::GateType::Nor: {
        bool r = false;
        for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
          r = r || read(i);
        }
        v = (gate.type == ir::GateType::Or) ? r : !r;
        break;
      }
      case ir::GateType::Xor:
      case ir::GateType::Xnor: {
        bool r = false;
        for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
          r = (r != read(i));
        }
        v = (gate.type == ir::GateType::Xor) ? r : !r;
        break;
      }
    }
    if (fault.active && fault.kind == fault::PinKind::Output && fault.gate == id) {
      v = fault.stuckValue;
    }
    values[id] = v ? 1 : 0;
  }

  std::vector<bool> outputs;
  outputs.reserve(graph.primaryOutputs().size());
  for (const ir::GateId id : graph.primaryOutputs()) {
    outputs.push_back(values[id] != 0);
  }
  return outputs;
}

/// Converts a vector index into its stimulus vector, LSB = first input.
inline std::vector<bool> patternOf(std::size_t vectorIndex, std::size_t piCount) {
  std::vector<bool> pattern(piCount);
  for (std::size_t b = 0; b < piCount; ++b) {
    pattern[b] = ((vectorIndex >> b) & 1) != 0;
  }
  return pattern;
}

/// Converts an atpg::fault::Fault into the InjectedFault this oracle takes.
inline InjectedFault injectedFrom(const fault::Fault& f) {
  InjectedFault injected;
  injected.active = true;
  injected.gate = f.pin.gate;
  injected.kind = f.pin.kind;
  injected.pin = f.pin.inputIndex;
  injected.stuckValue = f.value == fault::StuckValue::SA1;
  return injected;
}

/// Identifies an atomic fault independently of enumeration order.
struct FaultKey {
  ir::GateId gate;
  fault::PinKind kind;
  std::size_t pin;
  fault::StuckValue value;

  friend auto operator<=>(const FaultKey&, const FaultKey&) = default;
};

inline FaultKey keyOf(const fault::Fault& f) {
  return FaultKey{f.pin.gate, f.pin.kind, f.pin.inputIndex, f.value};
}

/// Every atomic stuck-at fault in `graph`: both polarities on each gate's
/// output pin (except Po, whose output is the same net as its input) and on
/// each of its input pins.
inline std::vector<fault::Fault> enumerateAtoms(const ir::Graph& graph) {
  std::vector<fault::Fault> atoms;
  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));
    if (gate.type != ir::GateType::Po) {
      atoms.push_back(
          fault::Fault{fault::PinRef{gate.id, fault::PinKind::Output, 0}, fault::StuckValue::SA0});
      atoms.push_back(
          fault::Fault{fault::PinRef{gate.id, fault::PinKind::Output, 0}, fault::StuckValue::SA1});
    }
    for (std::size_t pin = 0; pin < gate.fanin.size(); ++pin) {
      atoms.push_back(
          fault::Fault{fault::PinRef{gate.id, fault::PinKind::Input, pin}, fault::StuckValue::SA0});
      atoms.push_back(
          fault::Fault{fault::PinRef{gate.id, fault::PinKind::Input, pin}, fault::StuckValue::SA1});
    }
  }
  return atoms;
}

/// Every atomic fault as its own single-member class. The fault simulator
/// accepts any FaultList, not just a collapsed one, and collapsing discards
/// whole fault shapes (a fanout-1 gate's own output fault, for instance)
/// that would otherwise never reach it from a test.
inline fault::FaultList allAtomsAsClasses(const ir::Graph& graph) {
  fault::FaultList faults;
  for (const fault::Fault& atom : enumerateAtoms(graph)) {
    faults.add(fault::FaultClass{atom, {}});
  }
  return faults;
}

/// Renders a circuit as text, for failure diagnostics.
inline std::string dumpCircuit(const ir::Graph& graph) {
  std::string text;
  for (std::size_t i = 0; i < graph.size(); ++i) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(i));
    fmt::format_to(std::back_inserter(text), "  {} {}({})", ir::gateTypeName(gate.type), gate.name,
                   i);
    if (!gate.fanin.empty()) {
      fmt::format_to(std::back_inserter(text), " <-");
      for (const ir::GateId in : gate.fanin) {
        fmt::format_to(std::back_inserter(text), " {}", graph.gate(in).name);
      }
    }
    fmt::format_to(std::back_inserter(text), " [fanout={}]\n", gate.fanout.size());
  }
  return text;
}

} // namespace atpg::testing
