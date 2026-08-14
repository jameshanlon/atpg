#pragma once

#include "atpg/Result.hpp"
#include "atpg/fault/Fault.hpp"
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

/// Same as simulate(), but forces `pin` to `value` during evaluation -
/// computes a faulty circuit's response to a specific stuck-at fault.
/// `pin.gate` must be a valid gate id in `graph`. For an input-pin fault,
/// only that one gate's own reading of that pin is affected - any other
/// consumer of the same driving net still sees its real value.
Result<std::vector<bool>> simulateWithFault(const ir::Graph& graph,
                                            const std::vector<bool>& piValues,
                                            const fault::PinRef& pin, fault::StuckValue value);

} // namespace atpg::sim
