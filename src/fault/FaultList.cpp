#include "atpg/fault/FaultList.hpp"

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

  // gateBase[g] is the atom index of gate g's output/SA0 (or, for a Po
  // with no output atoms, its input/0/SA0) - every other atom for g sits
  // at a fixed offset from it, computed in outIdx/inIdx below.
  std::vector<Fault> atoms;
  std::vector<std::size_t> gateBase(graph.size());

  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));
    gateBase[g] = atoms.size();

    if (gate.type != ir::GateType::Po) {
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Output, 0}, StuckValue::SA0});
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Output, 0}, StuckValue::SA1});
    }

    for (std::size_t i = 0; i < gate.fanin.size(); ++i) {
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Input, i}, StuckValue::SA0});
      atoms.push_back(Fault{PinRef{gate.id, PinKind::Input, i}, StuckValue::SA1});
    }
  }

  UnionFind uf(atoms.size());

  auto outIdx = [&](ir::GateId g, StuckValue v) {
    return gateBase[g] + (v == StuckValue::SA1 ? 1 : 0);
  };
  auto inIdx = [&](ir::GateId g, std::size_t pin, StuckValue v) {
    const std::size_t base = gateBase[g] + (graph.gate(g).type != ir::GateType::Po ? 2 : 0);
    return base + 2 * pin + (v == StuckValue::SA1 ? 1 : 0);
  };

  // -- phase 1: local per-gate equivalence ---------------------------------
  //
  // outputIsEquivalence[outIdx(gate, value)] records that this exact output
  // atom was proven *equivalent* (not merely dominating) at least one of
  // the gate's own input atoms - phase 2 uses this to decide whether a
  // stem's output atom is safe to drop (see below).

  std::vector<bool> outputIsEquivalence(atoms.size(), false);

  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));
    const std::size_t n = gate.fanin.size();

    // A gate with no inputs has nothing to be equivalent to, regardless of
    // type. The SV frontend never produces a gate primitive with 0 fanin,
    // so this never triggers today - but skipping it would both mark a
    // false equivalence and read out of bounds from an empty input-pin
    // array below.
    auto mergeAllInputs = [&](StuckValue inputValue, StuckValue outputValue) {
      if (n == 0) {
        return;
      }
      outputIsEquivalence[outIdx(gate.id, outputValue)] = true;
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
        mergeAllInputs(StuckValue::SA0, StuckValue::SA0);
        mergeAllInputs(StuckValue::SA1, StuckValue::SA1);
        break;
      case ir::GateType::Not:
        mergeAllInputs(StuckValue::SA1, StuckValue::SA0);
        mergeAllInputs(StuckValue::SA0, StuckValue::SA1);
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
  //   - fanout.size() >= 2: a real fanout branch point (a stem). Merging
  //     the stem into one arbitrarily-chosen branch is unsound whenever
  //     branches reconverge downstream (a test that detects the branch
  //     fault doesn't necessarily detect the stem fault - propagating
  //     through every branch at once, what the stem fault does, can be
  //     cancelled at the reconvergence point in ways propagating through a
  //     single branch is not - see the design doc). So the stem is never
  //     merged into a branch. Whether a specific output polarity can be
  //     safely *dropped* instead (rather than kept as its own class)
  //     depends on whether phase 1 already proved it *equivalent* (not
  //     merely dominating) one of the gate's own input faults, i.e.
  //     `outputIsEquivalence` above. Dominance alone isn't enough: for the
  //     other polarity, the output fault only *dominates* the gate's own
  //     input faults (every test detecting an input fault also detects
  //     the output fault, but not vice versa), so the input fault - the
  //     one that would survive as the output fault's stand-in - can
  //     itself be redundant (undetectable) while the output fault remains
  //     detectable. Since nothing can prove a fault non-redundant without
  //     full ATPG, well outside what this stage does, dropping on
  //     dominance alone risks losing the output fault with nothing left
  //     to stand in for it. Only an exact, unconditional equivalence
  //     removes that risk, so only the `outputIsEquivalence` polarity is
  //     dropped; the other polarity is kept as its own class, exactly
  //     like a fanout==0 gate's or a Pi's.
  //     For a dropped polarity, only that bare output atom is excluded,
  //     never the rest of whatever class phase 1 placed it in - that
  //     class typically also contains the gate's own input pins, and
  //     transitively any fanout-1 predecessor chained into them, which
  //     are themselves checkpoints (primary inputs or *other* fanout
  //     branches) that the checkpoint theorem requires to remain
  //     represented regardless of what they happen to be locally
  //     equivalent to.
  //
  // (A net read on more than one of a single gate's own input pins, e.g.
  // `and(y, a, a);`, gives that net's driver fanout.size() >= 2 - it goes
  // through the stem case above, not the single-edge fanout==1 case below,
  // so Graph::inputIndex there is never asked to disambiguate between
  // edges to the same consumer.)

  std::vector<bool> isStemFault(atoms.size(), false);

  for (std::size_t g = 0; g < graph.size(); ++g) {
    const ir::Gate& gate = graph.gate(static_cast<ir::GateId>(g));
    if (gate.type == ir::GateType::Po) {
      continue;
    }

    if (gate.fanout.size() >= 2) {
      const std::size_t sa0 = outIdx(gate.id, StuckValue::SA0);
      const std::size_t sa1 = outIdx(gate.id, StuckValue::SA1);
      isStemFault[sa0] = outputIsEquivalence[sa0];
      isStemFault[sa1] = outputIsEquivalence[sa1];
      continue;
    }

    if (gate.fanout.empty()) {
      continue;
    }

    const ir::GateId consumer = gate.fanout[0];
    const std::size_t pin = graph.inputIndex(consumer, gate.id);

    uf.unite(outIdx(gate.id, StuckValue::SA0), inIdx(consumer, pin, StuckValue::SA0));
    uf.unite(outIdx(gate.id, StuckValue::SA1), inIdx(consumer, pin, StuckValue::SA1));
  }

  // -- materialize collapsed classes ---------------------------------------

  std::vector<std::vector<std::size_t>> membersByRoot(atoms.size());
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    if (isStemFault[i]) {
      continue;
    }
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
