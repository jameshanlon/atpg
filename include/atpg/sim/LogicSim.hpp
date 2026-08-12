#pragma once

#include "atpg/Result.hpp"
#include "atpg/ir/Graph.hpp"

#include <vector>

namespace atpg::sim {

/// Evaluates a purely combinational graph for one input vector.
///
/// `piValues` must have one entry per graph.primaryInputs(), in that order.
/// On success, returns one value per graph.primaryOutputs(), in that order.
/// (std::vector<bool> is taken by reference rather than std::span, since its
/// bit-packed storage isn't span-compatible.)
Result<std::vector<bool>> simulate(const ir::Graph& graph, const std::vector<bool>& piValues);

} // namespace atpg::sim
