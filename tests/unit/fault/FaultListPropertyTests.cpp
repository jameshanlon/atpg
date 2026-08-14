#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <cstdint>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace atpg::fault;
using namespace atpg::ir;

namespace {

// Deterministic pseudo-random circuit generator biased towards fanout
// stems and reconvergent cones - the structures generateFaultList's
// checkpoint-theorem collapsing has to get exactly right. Capped at 6
// primary inputs so every input vector fits in one std::uint64_t bitmask
// below.
Graph randomCircuit(std::mt19937& rng) {
  std::uniform_int_distribution<int> piCountDist(2, 6);
  std::uniform_int_distribution<int> gateCountDist(1, 10);
  const int piCount = piCountDist(rng);
  const int gateCount = gateCountDist(rng);

  Graph graph;
  std::vector<GateId> pool;
  for (int i = 0; i < piCount; ++i) {
    pool.push_back(graph.addGate(GateType::Pi, "i" + std::to_string(i)));
  }

  static constexpr GateType kTypes[] = {GateType::And, GateType::Nand, GateType::Or,
                                        GateType::Nor, GateType::Xor,  GateType::Xnor,
                                        GateType::Buf, GateType::Not};
  std::uniform_int_distribution<int> typeDist(0, 7);
  std::uniform_real_distribution<double> unit(0.0, 1.0);

  for (int k = 0; k < gateCount; ++k) {
    const GateType type = kTypes[typeDist(rng)];
    const bool unary = type == GateType::Buf || type == GateType::Not;
    const int arity = unary ? 1 : (unit(rng) < 0.2 ? 3 : 2);
    const GateId id = graph.addGate(type, "g" + std::to_string(k));
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
  for (const GateId id : pool) {
    const double p = graph.gate(id).type == GateType::Pi ? 0.1 : 0.3;
    if (unit(rng) < p) {
      const GateId po = graph.addGate(GateType::Po, "o" + std::to_string(poCount++));
      graph.addEdge(id, po);
    }
  }
  if (poCount == 0) {
    const GateId po = graph.addGate(GateType::Po, "o0");
    graph.addEdge(pool.back(), po);
  }
  return graph;
}

// Injects a stuck-at fault (or none, for the good-circuit baseline) and
// simulates one input vector. Deliberately a second, independent
// implementation of gate semantics rather than a reuse of atpg::sim::
// simulate() - the value of this check is an independent ground truth; a
// shared implementation could hide a bug present in both places.
struct InjectedFault {
  bool active = false;
  GateId gate = 0;
  PinKind kind = PinKind::Output;
  std::size_t pin = 0;
  bool stuckValue = false;
};

std::vector<bool> simulateWithFault(const Graph& graph, const std::vector<int>& piIndex,
                                    const std::vector<bool>& piValues, const InjectedFault& fault) {
  std::vector<char> values(graph.size(), 0);
  for (const GateId id : graph.levelOrder()) {
    const Gate& gate = graph.gate(id);
    auto read = [&](std::size_t i) {
      if (fault.active && fault.kind == PinKind::Input && fault.gate == id && fault.pin == i) {
        return fault.stuckValue;
      }
      return values[gate.fanin[i]] != 0;
    };
    bool v = false;
    switch (gate.type) {
      case GateType::Pi:
        v = piValues[static_cast<std::size_t>(piIndex[id])];
        break;
      case GateType::Po:
      case GateType::Buf:
        v = read(0);
        break;
      case GateType::Not:
        v = !read(0);
        break;
      case GateType::And:
      case GateType::Nand: {
        bool r = true;
        for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
          r = r && read(i);
        }
        v = (gate.type == GateType::And) ? r : !r;
        break;
      }
      case GateType::Or:
      case GateType::Nor: {
        bool r = false;
        for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
          r = r || read(i);
        }
        v = (gate.type == GateType::Or) ? r : !r;
        break;
      }
      case GateType::Xor:
      case GateType::Xnor: {
        bool r = false;
        for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
          r = (r != read(i));
        }
        v = (gate.type == GateType::Xor) ? r : !r;
        break;
      }
    }
    if (fault.active && fault.kind == PinKind::Output && fault.gate == id) {
      v = fault.stuckValue;
    }
    values[id] = v ? 1 : 0;
  }
  std::vector<bool> outputs;
  outputs.reserve(graph.primaryOutputs().size());
  for (const GateId id : graph.primaryOutputs()) {
    outputs.push_back(values[id] != 0);
  }
  return outputs;
}

// Identifies an atomic fault independent of enumeration order, so the
// faults generateFaultList returns can be matched back to this file's own
// brute-force enumeration below.
struct FaultKey {
  GateId gate;
  PinKind kind;
  std::size_t pin;
  StuckValue value;

  friend auto operator<=>(const FaultKey&, const FaultKey&) = default;
};

FaultKey keyOf(const Fault& fault) {
  return FaultKey{fault.pin.gate, fault.pin.kind, fault.pin.inputIndex, fault.value};
}

std::vector<Fault> enumerateAtoms(const Graph& graph) {
  std::vector<Fault> atoms;
  for (std::size_t g = 0; g < graph.size(); ++g) {
    const Gate& gate = graph.gate(static_cast<GateId>(g));
    if (gate.type != GateType::Po) {
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Output, 0}, StuckValue::SA0});
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Output, 0}, StuckValue::SA1});
    }
    for (std::size_t pin = 0; pin < gate.fanin.size(); ++pin) {
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Input, pin}, StuckValue::SA0});
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Input, pin}, StuckValue::SA1});
    }
  }
  return atoms;
}

std::string dumpCircuit(const Graph& graph) {
  std::string text;
  for (std::size_t i = 0; i < graph.size(); ++i) {
    const Gate& gate = graph.gate(static_cast<GateId>(i));
    fmt::format_to(std::back_inserter(text), "  {} {}({})", gateTypeName(gate.type), gate.name, i);
    if (!gate.fanin.empty()) {
      fmt::format_to(std::back_inserter(text), " <-");
      for (const GateId in : gate.fanin) {
        fmt::format_to(std::back_inserter(text), " {}", graph.gate(in).name);
      }
    }
    fmt::format_to(std::back_inserter(text), " [fanout={}]\n", gate.fanout.size());
  }
  return text;
}

// Brute-force verifies generateFaultList(graph) against ground-truth
// detection sets (every atomic fault, simulated over every input vector)
// and returns a description of every violation found.
std::vector<std::string> checkAgainstGroundTruth(const Graph& graph) {
  std::vector<std::string> violations;

  const std::size_t piCount = graph.primaryInputs().size();
  const std::size_t vectorCount = std::size_t{1} << piCount;

  std::vector<int> piIndex(graph.size(), -1);
  for (std::size_t i = 0; i < piCount; ++i) {
    piIndex[graph.primaryInputs()[i]] = static_cast<int>(i);
  }

  std::vector<std::vector<bool>> goodOutputs(vectorCount);
  for (std::size_t v = 0; v < vectorCount; ++v) {
    std::vector<bool> pi(piCount);
    for (std::size_t b = 0; b < piCount; ++b) {
      pi[b] = ((v >> b) & 1) != 0;
    }
    goodOutputs[v] = simulateWithFault(graph, piIndex, pi, InjectedFault{});
  }

  const std::vector<Fault> atoms = enumerateAtoms(graph);
  std::map<FaultKey, std::size_t> atomIndex;
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    atomIndex[keyOf(atoms[i])] = i;
  }

  // detects[a] has bit v set iff input vector v detects atom a.
  std::vector<std::uint64_t> detects(atoms.size(), 0);
  for (std::size_t a = 0; a < atoms.size(); ++a) {
    InjectedFault fault;
    fault.active = true;
    fault.gate = atoms[a].pin.gate;
    fault.kind = atoms[a].pin.kind;
    fault.pin = atoms[a].pin.inputIndex;
    fault.stuckValue = atoms[a].value == StuckValue::SA1;
    for (std::size_t v = 0; v < vectorCount; ++v) {
      std::vector<bool> pi(piCount);
      for (std::size_t b = 0; b < piCount; ++b) {
        pi[b] = ((v >> b) & 1) != 0;
      }
      if (simulateWithFault(graph, piIndex, pi, fault) != goodOutputs[v]) {
        detects[a] |= std::uint64_t{1} << v;
      }
    }
  }

  const FaultList faults = generateFaultList(graph);

  std::vector<char> listed(atoms.size(), 0);
  std::vector<std::vector<std::size_t>> classes;
  for (const auto& faultClass : faults) {
    std::vector<Fault> members{faultClass.representative};
    members.insert(members.end(), faultClass.equivalent.begin(), faultClass.equivalent.end());
    std::vector<std::size_t> indices;
    for (const auto& member : members) {
      const auto it = atomIndex.find(keyOf(member));
      if (it == atomIndex.end()) {
        violations.push_back("listed fault is not a valid atomic fault of this circuit");
        continue;
      }
      listed[it->second] = 1;
      indices.push_back(it->second);
    }
    classes.push_back(std::move(indices));
  }

  // Every class's members must share an identical detection set - an
  // unsound merge otherwise.
  for (const auto& members : classes) {
    for (std::size_t k = 1; k < members.size(); ++k) {
      if (detects[members[k]] != detects[members[0]]) {
        violations.push_back("unsound merge: two faults in one class have different "
                             "detection sets");
      }
    }
  }

  // Every dropped atom must have a listed atom with an *identical*
  // detection set - generateFaultList only ever drops on exact
  // equivalence, never mere dominance (see src/fault/FaultList.cpp).
  for (std::size_t a = 0; a < atoms.size(); ++a) {
    if (listed[a]) {
      continue;
    }
    bool hasEquivalentSurvivor = false;
    for (std::size_t b = 0; b < atoms.size() && !hasEquivalentSurvivor; ++b) {
      hasEquivalentSurvivor = listed[b] && detects[b] == detects[a];
    }
    if (!hasEquivalentSurvivor) {
      violations.push_back("dropped fault has no equivalent surviving fault");
    }
  }

  // The union of detection sets must be unchanged - no coverage loss.
  std::uint64_t unionAll = 0;
  std::uint64_t unionListed = 0;
  for (std::size_t a = 0; a < atoms.size(); ++a) {
    unionAll |= detects[a];
    if (listed[a]) {
      unionListed |= detects[a];
    }
  }
  if (unionAll != unionListed) {
    violations.push_back("detection-set union differs between the full and collapsed "
                         "fault lists");
  }

  return violations;
}

} // namespace

TEST_CASE("generateFaultList collapsing preserves detection-set coverage and soundness",
          "[FaultList][property]") {
  // Default corpus size keeps this a fast, deterministic ctest run; bump
  // kIterations locally for a deeper sweep when touching the collapsing
  // algorithm - this module's history includes bugs only exhaustive runs
  // of thousands of circuits caught (see src/fault/FaultList.cpp).
  constexpr int kIterations = 500;
  constexpr unsigned kSeed = 20260813;

  std::mt19937 rng(kSeed);
  for (int i = 0; i < kIterations; ++i) {
    Graph graph = randomCircuit(rng);
    REQUIRE(graph.levelize().ok());

    const std::vector<std::string> violations = checkAgainstGroundTruth(graph);
    if (!violations.empty()) {
      INFO("circuit #" << i << " (seed " << kSeed << "):\n" << dumpCircuit(graph));
      for (const auto& violation : violations) {
        INFO(violation);
      }
      FAIL("generateFaultList violated a soundness/completeness invariant");
    }
  }
}
