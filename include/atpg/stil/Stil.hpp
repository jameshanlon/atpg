#pragma once

#include "atpg/Result.hpp"
#include "atpg/ir/Graph.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace atpg::stil {

/// Renders `patterns` as an IEEE 1450 STIL program for `graph`.
///
/// Each emitted vector carries both the stimulus applied to the primary
/// inputs and the response expected at the primary outputs, which this
/// computes by simulating the good machine - that is what makes the output
/// a test rather than stimulus alone.
///
/// Each entry of `patterns` holds one bit per graph.primaryInputs(), in
/// that order - the same shape sim::simulate() takes. `graph` must already
/// be levelized (graph.levelize() called and ok()). `designName` names the
/// design in the emitted Header title.
///
/// The program declares one WaveformTable with fixed timing: inputs drive
/// at time 0, outputs strobe at mid-period. Nothing is configurable, since
/// there is no tester attached to this project to configure it for.
///
/// The whole program is returned as a string rather than streamed to an
/// ostream, unlike the CLI's own report writers, so that a failure part-way
/// through leaves no output at all: a half-written STIL file would look
/// plausible enough to load. It also costs one scalar simulation per
/// pattern, where the bit-parallel engine in atpg::fsim evaluates 64 at a
/// time - immaterial for a single terminal step over an already-compacted
/// set, and the reason no batched good-machine entry point was added for it.
///
/// The emitted syntax follows the IEEE 1450-1999 structure but has not been
/// validated against a real STIL reader - no parser is available here as a
/// dependency. The tests verify the program's semantics thoroughly; they
/// cannot verify that a tester would load it.
///
/// Returns an Error if a pattern's width does not match the primary-input
/// count, if two signals share a name (STIL requires uniqueness), or if the
/// design has no primary inputs or no primary outputs - each of which would
/// otherwise produce a structurally invalid program. An empty `patterns` is
/// not an error: it yields a valid program with an empty Pattern block.
Result<std::string> writeStil(const ir::Graph& graph,
                              const std::vector<std::vector<bool>>& patterns,
                              std::string_view designName);

} // namespace atpg::stil
