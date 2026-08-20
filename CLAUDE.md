# CLAUDE.md

Project-specific rules for atpg, in addition to global CLAUDE.md rules.

## Project

atpg is a gate-level ATPG (Automatic Test Pattern Generation) tool: a
slang-based SystemVerilog frontend flattens a gate-primitive netlist into
`atpg::ir::Graph`, `atpg::sim` simulates it, `atpg::fault` generates and
collapses its stuck-at fault list, `atpg::fsim` bit-parallel
fault-simulates a pattern set to report coverage, and `atpg::gen`
generates (or proves redundant) a test pattern per fault class using a
SAT-based miter construction on OR-Tools CP-SAT - either solving every
fault independently, or running a generate-and-drop loop that fault-
simulates each pattern to skip the faults it already covers. Finally,
`atpg::compact` shrinks a finished pattern set to an irredundant subset
with identical coverage, and `atpg::stil` renders a pattern set as an IEEE
1450 STIL program carrying each vector's expected response. See
`README.md` for build/usage and architecture details.

## Error handling

- No exceptions. Every fallible function returns `atpg::Result<T>` (a value
  or an error message) or `atpg::Status` (success or an error message, no
  value) - see `include/atpg/Result.hpp`. Propagate failures with the
  `ATPG_RETURN_IF_ERROR`/`ATPG_ASSIGN_OR_RETURN` macros rather than manual
  `if (!x) return ...;` checks.
- `Result`/`Status` are `[[nodiscard]]` - always check them.
- An unavoidable exception at a third-party API boundary (e.g. CLI11's
  `App::parse()`, which has no non-throwing entry point) must be caught
  immediately at that boundary and converted to atpg's own Result/Status/
  exit-code reporting. It must never propagate into atpg's own code.

## Formatting and I/O

- Use `fmt::format`/`fmt::print`, not stream operators (`<<`) or manual
  string concatenation. (Catch2's `INFO`/`FAIL` macros are the one
  exception - they require `<<` by design.)
- Formatting itself is enforced by `.clang-format` (LLVM style, 100-column
  limit) via pre-commit - see `## Tooling` below. Don't hand-format against
  it; run the hook or `clang-format -i`.

## Command-line tools

- Parse arguments with CLI11 (`CLI::App`), not a hand-rolled argv loop.

## Style

- Always brace control-flow bodies (`if`, `for`, `while`, `else`), even
  single-line ones.

## Build system

- CMake, C++20. Dependencies (slang, Catch2, CLI11, fmt) are pulled via
  `FetchContent` in `cmake/Dependencies.cmake`, pinned to specific release
  tags - never a floating branch or HEAD. fmt is resolved first, at the
  root scope, so its target stays visible to every subdirectory (slang
  resolves its own copy from inside its own subdirectory, which is too
  narrow a scope for atpg's own targets to see).
- OR-Tools is a required system dependency resolved via `find_package`, not
  `FetchContent`, because building it from source is far slower than this
  project's other dependencies (see `cmake/Dependencies.cmake`'s comment for
  the pkg-config gotcha).
- One static library, `atpg-core`, with one namespace, `atpg`, split into
  sub-namespaces by directory/responsibility (`atpg::ir`, `atpg::sim`,
  `atpg::frontend`, `atpg::fault`, `atpg::gen`, `atpg::fsim`,
  `atpg::compact`, `atpg::stil`) rather than separate CMake targets.

## Testing

- Catch2 v3. Tests live in `tests/unit/`, mirroring `src/`'s directory
  structure (e.g. `src/fault/FaultList.cpp` -> `tests/unit/fault/
  FaultTests.cpp`). Small hand-verifiable fixtures live in `tests/data/`.
- For an algorithm where hand-derived expected values are error-prone to
  trust beyond a single small case (the fault-list collapsing algorithm is
  the current example - see `src/fault/FaultList.cpp`'s history), prefer a
  randomized/property-based test that checks an invariant against an
  independent ground truth over many generated cases, alongside the usual
  hand-traced small-fixture tests. Keep the default iteration count fast
  enough for routine `ctest` runs; a much larger sweep is for local use
  when actually touching that algorithm, not for every CI run.
- Mixing Catch2 with OR-Tools in one test file has two include-order
  constraints (see `tests/unit/gen/TestGenTests.cpp`): (a) OR-Tools
  transitively pulls in absl's fatal `CHECK`, which shadows Catch2's -
  include the OR-Tools headers first, then `#undef CHECK`, then Catch2's
  header; (b) `#include "../Test.hpp"` must come after the Catch2 include,
  since `Test.hpp` transitively includes Catch2 too and would otherwise
  satisfy its include guard before the `#undef CHECK` above runs, leaving
  `CHECK` permanently undefined.

## Tooling

- `.clang-format` (LLVM style) formats the codebase; `.pre-commit-config.yaml`
  runs it, plus standard file-hygiene hooks, before each commit. Run
  `pre-commit install` once per clone to enable the git hook.

## Docs and planning

- Design specs and implementation plans (from the brainstorming/
  writing-plans/subagent-driven-development skills) live under
  `docs/superpowers/specs/` and `docs/superpowers/plans/`. Per the global
  CLAUDE.md rule, these are not committed - key implementation details
  that need to persist belong in code comments, this file, or `README.md`
  instead.
