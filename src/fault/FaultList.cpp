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
  // A stem fault (a gate's output, before branching) is the same physical
  // defect as the corresponding fault on every branch at once, so it's
  // merged into the first branch's input fault (chasing forward through any
  // run of fanout-1 gates). Every other branch (fanout index >= 1) stays
  // independent, since a defect there wouldn't affect its siblings. A gate
  // with fanout 0 (dead logic) keeps its own class, since there's nothing to
  // merge it into.
  //
  // Known limitation: if a single gate reads the same net on more than one
  // of its own input pins (e.g. `and(y, a, a);`), this loop maps every one
  // of that net's fanout edges to the *first* matching input pin it finds,
  // since Gate::fanin doesn't record which edge produced which entry.

  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));
    if (gate.type == ir::GateType::Po || gate.fanout.empty()) {
      continue;
    }

    const ir::GateId consumer = gate.fanout[0];
    const auto& consumerFanin = graph.gate(consumer).fanin;
    const auto it = std::find(consumerFanin.begin(), consumerFanin.end(), gate.id);
    const std::size_t pin = static_cast<std::size_t>(it - consumerFanin.begin());

    uf.unite(outIdx(gate.id, StuckValue::SA0), inIdx(consumer, pin, StuckValue::SA0));
    uf.unite(outIdx(gate.id, StuckValue::SA1), inIdx(consumer, pin, StuckValue::SA1));
  }

  // -- materialize collapsed classes ---------------------------------------

  std::vector<std::vector<std::size_t>> membersByRoot(atoms.size());
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    membersByRoot[uf.find(i)].push_back(i);
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
