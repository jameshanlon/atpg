#pragma once

#include "atpg/Result.hpp"
#include "atpg/ir/Graph.hpp"

#include <string_view>

namespace slang::ast {
class Compilation;
}

namespace atpg::frontend {

/// Fails, with slang's full diagnostic report as the error message, if
/// `compilation` has any error diagnostics.
Status requireNoErrors(slang::ast::Compilation& compilation);

/// Flattens the elaborated design rooted at the module named
/// `topModuleName` into a combinational gate graph.
///
/// Only built-in SystemVerilog gate primitives (and, nand, or, nor, xor,
/// xnor, buf, not) are supported. Anything else encountered in the
/// hierarchy - library cell instances, sequential elements, non-scalar
/// port connections with dynamic indices - is reported as an error.
/// Calling compilation.getRoot() elaborates the design, so `compilation`
/// must already have its syntax tree(s) added.
Result<ir::Graph> buildGraph(slang::ast::Compilation& compilation, std::string_view topModuleName);

} // namespace atpg::frontend
