#include "atpg/fault/FaultList.hpp"

#include <algorithm>
#include <numeric>

namespace atpg::fault {

namespace {

/// Union-find over atomic fault indices (path-compressed, iterative).
class UnionFind {
public:
  explicit UnionFind(std::size_t n) : parent_(n) {
    std::iota(parent_.begin(), parent_.end(), std::size_t{0});
  }

  std::size_t find(std::size_t x) {
    while (parent_[x] != x) {
      parent_[x] = parent_[parent_[x]];
      x = parent_[x];
    }
    return x;
  }

  void unite(std::size_t a, std::size_t b) {
    const std::size_t ra = find(a);
    const std::size_t rb = find(b);
    if (ra != rb) {
      parent_[ra] = rb;
    }
  }

private:
  std::vector<std::size_t> parent_;
};

} // namespace

FaultList generateFaultList(const ir::Graph& graph) {
  // -- enumerate atomic faults, with O(1) index lookup per pin+value ------

  std::vector<Fault> atoms;
  std::vector<std::size_t> outputSA0(graph.size());
  std::vector<std::vector<std::size_t>> inputSA0(graph.size());

  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));

    if (gate.type != ir::GateType::Po) {
      outputSA0[g] = atoms.size();
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Output, 0}, StuckValue::SA0});
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Output, 0}, StuckValue::SA1});
    }

    inputSA0[g].resize(gate.fanin.size());
    for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
      inputSA0[g][i] = atoms.size();
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Input, i}, StuckValue::SA0});
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Input, i}, StuckValue::SA1});
    }
  }

  UnionFind uf(atoms.size());

  auto outIdx = [&](ir::GateId g, StuckValue v) {
    return outputSA0[g] + (v == StuckValue::SA1 ? 1 : 0);
  };
  auto inIdx = [&](ir::GateId g, std::size_t pin, StuckValue v) {
    return inputSA0[g][pin] + (v == StuckValue::SA1 ? 1 : 0);
  };

  // -- phase 1: local per-gate equivalence ---------------------------------

  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));
    const std::size_t n = gate.fanin.size();

    auto mergeAllInputs = [&](StuckValue inputValue, StuckValue outputValue) {
      for (std::size_t i = 0; i < n; ++i) {
        uf.unite(inIdx(gate.id, i, inputValue), outIdx(gate.id, outputValue));
      }
    };

    switch (gate.type) {
      case ir::GateType::And:
        mergeAllInputs(StuckValue::SA0, StuckValue::SA0);
        break;
      case ir::GateType::Nand:
        mergeAllInputs(StuckValue::SA0, StuckValue::SA1);
        break;
      case ir::GateType::Or:
        mergeAllInputs(StuckValue::SA1, StuckValue::SA1);
        break;
      case ir::GateType::Nor:
        mergeAllInputs(StuckValue::SA1, StuckValue::SA0);
        break;
      case ir::GateType::Buf:
        uf.unite(inIdx(gate.id, 0, StuckValue::SA0), outIdx(gate.id, StuckValue::SA0));
        uf.unite(inIdx(gate.id, 0, StuckValue::SA1), outIdx(gate.id, StuckValue::SA1));
        break;
      case ir::GateType::Not:
        uf.unite(inIdx(gate.id, 0, StuckValue::SA1), outIdx(gate.id, StuckValue::SA0));
        uf.unite(inIdx(gate.id, 0, StuckValue::SA0), outIdx(gate.id, StuckValue::SA1));
        break;
      case ir::GateType::Xor:
      case ir::GateType::Xnor:
      case ir::GateType::Pi:
      case ir::GateType::Po:
        break; // no local merging
    }
  }

  // -- phase 2: checkpoint theorem ------------------------------------------
  //
  // For every gate with an output pin (not Po):
  //   - fanout.size() == 1: the output net has a single reader, so the
  //     output fault and that reader's input fault are the same physical
  //     wire - merge them. Always sound (transitively through a run of
  //     fanout-1 gates), regardless of what happens further downstream.
  //   - fanout.size() == 0: dead logic. Keep the output-fault class as its
  //     own class - nothing to merge it into.
  //   - fanout.size() >= 2: a real fanout branch point (a stem). Do NOT
  //     merge the stem into any branch - drop it entirely instead, and
  //     leave every branch as its own independent class. Merging into one
  //     arbitrarily-chosen branch is unsound whenever branches reconverge
  //     downstream: a test that detects the branch fault does not
  //     necessarily detect the stem fault, since propagating through every
  //     branch simultaneously (what the stem fault does) can be cancelled
  //     at the reconvergence point in ways propagating through a single
  //     branch (the branch fault) is not. See the design doc for the full
  //     argument.
  //
  // Known limitation: if a single gate reads the same net on more than one
  // of its own input pins (e.g. `and(y, a, a);`), the fanout.size()==1
  // case's std::find always resolves to that net's *first* matching input
  // pin, since Gate::fanin doesn't record which edge produced which entry.

  // Pass 2a: merge every fanout==1 gate into its consumer. Fanout>=2 (stem)
  // and fanout==0 (dead) gates are untouched here - handled in pass 2b.
  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));
    if (gate.type == ir::GateType::Po || gate.fanout.size() != 1) {
      continue;
    }

    const ir::GateId consumer = gate.fanout[0];
    const auto& consumerFanin = graph.gate(consumer).fanin;
    // Graph::addEdge always keeps fanin/fanout in sync, so `it` is never
    // consumerFanin.end() here.
    const auto it = std::find(consumerFanin.begin(), consumerFanin.end(), gate.id);
    const std::size_t pin = static_cast<std::size_t>(it - consumerFanin.begin());

    uf.unite(outIdx(gate.id, StuckValue::SA0), inIdx(consumer, pin, StuckValue::SA0));
    uf.unite(outIdx(gate.id, StuckValue::SA1), inIdx(consumer, pin, StuckValue::SA1));
  }

  // Pass 2b: now that every fanout==1 merge from pass 2a is final, exclude
  // the whole final equivalence class rooted at each fanout>=2 stem's
  // output - not just the bare output atom. A stem's output atom is often
  // already unioned (via phase 1's controlling-value rule, or transitively
  // through an upstream fanout-1 predecessor merged in by pass 2a) with
  // other atoms that are electrically the identical defect. Those atoms
  // carry the exact same reconvergence-masking risk as the output atom
  // itself and must be dropped together. This must run after pass 2a, not
  // interleaved with it or before it, so uf.find() here returns the final,
  // stable root rather than one that a later union in pass 2a could still
  // change.
  std::vector<bool> excludedRoot(atoms.size(), false);
  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));
    if (gate.type == ir::GateType::Po || gate.fanout.size() < 2) {
      continue;
    }
    excludedRoot[uf.find(outIdx(gate.id, StuckValue::SA0))] = true;
    excludedRoot[uf.find(outIdx(gate.id, StuckValue::SA1))] = true;
  }

  // -- materialize collapsed classes ---------------------------------------

  std::vector<std::vector<std::size_t>> membersByRoot(atoms.size());
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    const std::size_t root = uf.find(i);
    if (excludedRoot[root]) {
      continue;
    }
    membersByRoot[root].push_back(i);
  }

  FaultList result;
  for (std::size_t root = 0; root < atoms.size(); ++root) {
    if (membersByRoot[root].empty()) {
      continue;
    }
    FaultClass faultClass;
    faultClass.representative = atoms[membersByRoot[root][0]];
    for (std::size_t k = 1; k < membersByRoot[root].size(); ++k) {
      faultClass.equivalent.push_back(atoms[membersByRoot[root][k]]);
    }
    result.add(std::move(faultClass));
  }

  return result;
}

} // namespace atpg::fault
