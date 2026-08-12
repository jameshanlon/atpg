# CLAUDE.md

Project-specific rules for atpg, in addition to global CLAUDE.md rules.

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
  string concatenation.

## Command-line tools

- Parse arguments with CLI11 (`CLI::App`), not a hand-rolled argv loop.

## Style

- Always brace control-flow bodies (`if`, `for`, `while`, `else`), even
  single-line ones.
