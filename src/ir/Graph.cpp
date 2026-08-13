#include "atpg/ir/Graph.hpp"

#include <algorithm>

namespace atpg::ir {

GateId Graph::addGate(GateType type, std::string name) {
  const auto id = static_cast<GateId>(gates_.size());

  Gate gate;
  gate.id = id;
  gate.type = type;
  gate.name = std::move(name);
  gates_.push_back(std::move(gate));

  if (type == GateType::Pi) {
    primaryInputs_.push_back(id);
  } else if (type == GateType::Po) {
    primaryOutputs_.push_back(id);
  }

  return id;
}

void Graph::addEdge(GateId from, GateId to) {
  gates_[from].fanout.push_back(to);
  gates_[to].fanin.push_back(from);
}

std::size_t Graph::inputIndex(GateId consumer, GateId driver) const {
  const auto& fanin = gates_[consumer].fanin;
  const auto it = std::find(fanin.begin(), fanin.end(), driver);
  return static_cast<std::size_t>(it - fanin.begin());
}

Status Graph::levelize() {
  std::vector<int> indegree(gates_.size());
  for (const auto& gate : gates_) {
    indegree[gate.id] = static_cast<int>(gate.fanin.size());
  }

  levelOrder_.clear();
  levelOrder_.reserve(gates_.size());

  for (auto& gate : gates_) {
    gate.level = indegree[gate.id] == 0 ? 0 : -1;
    if (gate.level == 0) {
      levelOrder_.push_back(gate.id);
    }
  }

  for (std::size_t head = 0; head < levelOrder_.size(); ++head) {
    const GateId id = levelOrder_[head];
    for (const GateId succ : gates_[id].fanout) {
      Gate& succGate = gates_[succ];
      const int candidateLevel = gates_[id].level + 1;
      if (candidateLevel > succGate.level) {
        succGate.level = candidateLevel;
      }
      if (--indegree[succ] == 0) {
        levelOrder_.push_back(succ);
      }
    }
  }

  if (levelOrder_.size() != gates_.size()) {
    return Error("Graph::levelize: combinational cycle detected");
  }
  return {};
}

} // namespace atpg::ir
