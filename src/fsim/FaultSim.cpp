#include "atpg/fsim/FaultSim.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace atpg::fsim {

namespace {

/// One machine word holds one gate's value under this many patterns at once.
constexpr std::size_t kPacketBits = 64;

using Word = std::uint64_t;

constexpr Word kAllOnes = ~Word{0};

/// Evaluates one gate for every lane of a packet at once. `read(i)` supplies
/// the word to use for fanin index `i`.
///
/// Pi is unreachable here - callers seed primary inputs directly rather than
/// evaluating them - but the case is listed so the switch stays exhaustive
/// and the compiler warns if a new GateType is added.
template <typename Read> Word evaluateGate(const ir::Gate& gate, Read&& read) {
  switch (gate.type) {
    case ir::GateType::And:
    case ir::GateType::Nand: {
      Word r = kAllOnes;
      for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
        r &= read(i);
      }
      return gate.type == ir::GateType::And ? r : ~r;
    }
    case ir::GateType::Or:
    case ir::GateType::Nor: {
      Word r = 0;
      for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
        r |= read(i);
      }
      return gate.type == ir::GateType::Or ? r : ~r;
    }
    case ir::GateType::Xor:
    case ir::GateType::Xnor: {
      Word r = 0;
      for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
        r ^= read(i);
      }
      return gate.type == ir::GateType::Xor ? r : ~r;
    }
    case ir::GateType::Buf:
    case ir::GateType::Po:
      return read(0);
    case ir::GateType::Not:
      return ~read(0);
    case ir::GateType::Pi:
      return 0;
  }
  return 0;
}

/// The gates whose values can change when a fault at `site` is injected:
/// `site` itself plus everything reachable from it through fanout, returned
/// in levelized order so re-evaluating them in sequence respects
/// dependencies.
///
/// levelOrder() is topological, so one forward sweep suffices: a gate's
/// fanin is always visited before the gate itself, so "any fanin already in
/// the cone" is decidable on first visit.
std::vector<ir::GateId> fanoutCone(const ir::Graph& graph, ir::GateId site) {
  std::vector<char> marked(graph.size(), 0);
  marked[site] = 1;

  std::vector<ir::GateId> cone;
  for (const ir::GateId id : graph.levelOrder()) {
    if (marked[id] == 0) {
      for (const ir::GateId in : graph.gate(id).fanin) {
        if (marked[in] != 0) {
          marked[id] = 1;
          break;
        }
      }
    }
    if (marked[id] != 0) {
      cone.push_back(id);
    }
  }
  return cone;
}

} // namespace

Result<SimResult> simulateFaults(const ir::Graph& graph, const fault::FaultList& faults,
                                 const std::vector<std::vector<bool>>& patterns) {
  const auto& pis = graph.primaryInputs();
  for (const auto& pattern : patterns) {
    if (pattern.size() != pis.size()) {
      return Error("simulateFaults: stimulus width does not match primary input count");
    }
  }

  // Per-fault state, in the fault list's order. The fanout cone is a
  // structural property of the graph, so it is computed once per fault here
  // rather than once per packet.
  std::vector<FaultStatus> statuses;
  std::vector<std::vector<ir::GateId>> cones;
  for (const auto& faultClass : faults) {
    FaultStatus status;
    status.fault = faultClass.representative;
    statuses.push_back(std::move(status));
    cones.push_back(fanoutCone(graph, faultClass.representative.pin.gate));
  }

  std::vector<Word> good(graph.size(), 0);
  std::vector<Word> faulty(graph.size(), 0);
  // Scratch cone-membership mask. Marked and unmarked around each faulty
  // pass, touching only that fault's own cone, so it costs O(cone) per pass
  // rather than O(gates).
  std::vector<char> inCone(graph.size(), 0);

  for (std::size_t base = 0; base < patterns.size(); base += kPacketBits) {
    const std::size_t lanes = std::min(kPacketBits, patterns.size() - base);
    // Which lanes of this packet hold real patterns. A partially filled
    // final packet must not let padding lanes report detections.
    const Word activeMask = lanes == kPacketBits ? kAllOnes : ((Word{1} << lanes) - 1);

    // -- good-circuit pass ---------------------------------------------------
    for (std::size_t i = 0; i < pis.size(); ++i) {
      Word w = 0;
      for (std::size_t lane = 0; lane < lanes; ++lane) {
        if (patterns[base + lane][i]) {
          w |= Word{1} << lane;
        }
      }
      good[pis[i]] = w;
    }
    for (const ir::GateId id : graph.levelOrder()) {
      const ir::Gate& gate = graph.gate(id);
      if (gate.type == ir::GateType::Pi) {
        continue;
      }
      good[id] = evaluateGate(gate, [&](std::size_t i) { return good[gate.fanin[i]]; });
    }

    // -- faulty passes, one per still-undetected fault ------------------------
    for (std::size_t f = 0; f < statuses.size(); ++f) {
      if (statuses[f].detected) {
        continue; // dropped: already finished in an earlier packet
      }

      const fault::Fault& target = statuses[f].fault;
      const Word stuck = target.value == fault::StuckValue::SA1 ? kAllOnes : Word{0};
      const std::vector<ir::GateId>& cone = cones[f];

      for (const ir::GateId id : cone) {
        inCone[id] = 1;
      }

      for (const ir::GateId id : cone) {
        const ir::Gate& gate = graph.gate(id);

        // Checked before the Pi case, so an output-pin fault on a primary
        // input is injected rather than skipped.
        if (target.pin.kind == fault::PinKind::Output && target.pin.gate == id) {
          faulty[id] = stuck;
          continue;
        }
        if (gate.type == ir::GateType::Pi) {
          faulty[id] = good[id];
          continue;
        }
        faulty[id] = evaluateGate(gate, [&](std::size_t i) {
          if (target.pin.kind == fault::PinKind::Input && target.pin.gate == id &&
              target.pin.inputIndex == i) {
            return stuck;
          }
          const ir::GateId in = gate.fanin[i];
          // Outside the cone, the faulty circuit is provably identical to
          // the good one - and `faulty` holds a stale value from a previous
          // fault, so reading it would be a bug.
          return inCone[in] != 0 ? faulty[in] : good[in];
        });
      }

      Word diff = 0;
      for (const ir::GateId id : graph.primaryOutputs()) {
        if (inCone[id] != 0) {
          diff |= faulty[id] ^ good[id];
        }
      }
      diff &= activeMask;

      for (const ir::GateId id : cone) {
        inCone[id] = 0;
      }

      if (diff != 0) {
        statuses[f].detected = true;
        statuses[f].firstDetectingPattern = base + static_cast<std::size_t>(std::countr_zero(diff));
      }
    }
  }

  SimResult result;
  for (FaultStatus& status : statuses) {
    result.add(std::move(status));
  }
  return result;
}

} // namespace atpg::fsim
