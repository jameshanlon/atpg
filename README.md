# atpg

A gate-level Automatic Test Pattern Generation (ATPG) tool for digital
circuits, built around a [slang](https://github.com/MikePopoloski/slang)
SystemVerilog frontend.

## What it does today

- **Frontend** (`atpg::frontend`): flattens a hierarchical SystemVerilog
  design built from SV gate primitives (`and`, `nand`, `or`, `nor`, `xor`,
  `xnor`, `buf`, `not`) into a single combinational gate-level netlist.
  Multi-bit ports are expanded into individually-named per-bit signals.
- **Gate graph** (`atpg::ir`): a flattened `Graph` of `Gate` nodes (with
  primary-input/output pseudo-gates for uniform traversal), levelized into
  a topological evaluation order.
- **Logic simulation** (`atpg::sim`): 2-valued (0/1) simulation of the
  gate graph for a given input vector.
- **Fault-list generation** (`atpg::fault`): enumerates every stuck-at
  (SA0/SA1) fault on every gate pin and collapses them via local
  per-gate equivalence and the checkpoint theorem, roughly halving the
  list a downstream ATPG/fault-simulation stage would need to target.
- **Test generation** (`atpg::gen`): generates a detecting test pattern
  for every fault class in the collapsed fault list (or proves it
  redundant, or reports that the per-fault solver time limit was
  exhausted) via a CP-SAT miter formulation. Also offers a
  generate-and-drop loop, which fault-simulates each generated pattern to
  skip the solver entirely for every fault that pattern already covers -
  the same verdicts from a fraction of the solver calls, and a much
  smaller pattern set.
- **Fault simulation** (`atpg::fsim`): given a pattern set, reports which
  fault classes it detects, which pattern detected each one first, and the
  overall coverage. Bit-parallel: 64 patterns are evaluated per machine
  word, and each fault re-simulates only its own transitive fanout cone.
- **Pattern compaction** (`atpg::compact`): shrinks a pattern set to a
  subset detecting exactly the same fault classes, by greedy set covering
  over a full fault-by-pattern detection matrix followed by a pass removing
  any pattern the rest already cover. The result is irredundant - no single
  pattern can be dropped without losing a fault - though not guaranteed
  minimum.
- **STIL output** (`atpg::stil`): renders a pattern set as an IEEE 1450
  STIL program, each vector carrying both the stimulus and the expected
  primary-output response, so the generated tests can be consumed by a
  tester flow. Timing is fixed: inputs drive at time 0, outputs strobe at
  mid-period.

The emitted STIL follows the IEEE 1450-1999 structure, but has not been
validated against a real STIL reader - the tests check the program's
semantics, not that a tester would load it.

## Building

Requires a C++20 compiler and CMake 3.21+. Dependencies ([slang](https://github.com/MikePopoloski/slang),
[Catch2](https://github.com/catchorg/Catch2), [CLI11](https://github.com/CLIUtils/CLI11),
[fmt](https://github.com/fmtlib/fmt)) are fetched automatically via CMake
`FetchContent` on first configure, so a network connection is needed then.

[OR-Tools](https://developers.google.com/optimization) (v9.15+) and
`pkg-config` are required system packages, resolved via `find_package`
instead - building OR-Tools from source is far slower than this project's
other dependencies. Install them before configuring:

```sh
brew install or-tools pkg-config
```

```sh
cmake -B build
cmake --build build -j
```

Run the test suite:

```sh
ctest --test-dir build
```

## Usage

```sh
atpg <file.sv> --top <module> [--dump-graph out.dot] [--dump-faults out.txt] [--generate-tests out.txt] [--time-limit seconds] [--stimulus vectors.txt] [--fault-sim out.txt] [--drop] [--compact out.txt] [--stil out.stil]
```

- `--dump-graph <path>` writes the flattened gate graph as Graphviz dot.
- `--dump-faults <path>` writes the collapsed fault list as plain text,
  one line per fault class, e.g. `n1/out/SA1 = g5/in0/SA1`.
- `--generate-tests <path>` writes a generated test pattern (or
  `redundant`/`aborted`) per fault class to a file, one line per fault,
  e.g. `n1/out/SA1: testable 10110`.
- `--drop` makes `--generate-tests` use the generate-and-drop loop:
  each generated pattern is fault-simulated so faults it already covers
  never reach the solver. Far fewer solver calls (8 rather than 22 on
  c17), plus a summary line reporting the saving. Outcomes match the
  exhaustive path, except that a dropped fault is reported `testable`
  even where the solver would have hit `--time-limit` and reported
  `aborted`. Requires `--generate-tests`.
- `--time-limit <seconds>` sets the per-fault CP-SAT solver time limit
  used by `--generate-tests` (default 5 seconds).
- `--stimulus <path>` reads newline-separated `0`/`1` vectors (one bit per
  primary input, in port order) and prints the simulated primary-output
  bits for each one.
- `--fault-sim <path>` fault-simulates the `--stimulus` patterns and writes
  a coverage report, one line per fault class plus a summary, e.g.
  `n1/out/SA0: detected by pattern 0` and `coverage: 12/22 (54.5%)`.
  Requires `--stimulus` to supply the patterns.
- `--compact <path>` compacts the pattern set the run produced - the
  `--generate-tests` set if present, otherwise the `--stimulus` set - and
  writes it one pattern per line in the format `--stimulus` reads, so the
  result can be fed straight back in for a coverage check. A
  `patterns: 22 -> 5 (coverage 22/22 preserved)` summary goes to stdout.
  Requires `--generate-tests` or `--stimulus`.
- `--stil <path>` writes the run's pattern set as an IEEE 1450 STIL
  program. The run's pattern set is the compacted set if `--compact` ran,
  otherwise the generated set if `--generate-tests` ran, otherwise the
  `--stimulus` set - so `--generate-tests --compact --stil out.stil` is one
  pipeline. Requires one of those three options.

Example, using the bundled ISCAS c17 benchmark fixture:

```sh
atpg tests/data/c17.sv --top c17 --dump-faults c17-faults.txt
```

## Project layout

```
include/atpg/   public headers, one subdirectory per module
src/            implementation, mirrors include/atpg/
  cli/          the `atpg` executable
  frontend/     SystemVerilog -> Graph
  ir/           the gate-graph data model
  sim/          logic simulation
  fault/        fault-list generation and collapsing
  gen/          SAT-based test generation, with and without fault dropping
  fsim/         bit-parallel fault simulation
  compact/      pattern-set compaction
  stil/         IEEE 1450 STIL output
tests/
  unit/         Catch2 tests, mirrors src/
  data/         hand-verifiable .sv fixtures (half adder, full adder, c17)
```

Everything lives in one `atpg-core` static library and the `atpg`
namespace, split into sub-namespaces by module (`atpg::ir`, `atpg::sim`,
`atpg::frontend`, `atpg::fault`, `atpg::gen`, `atpg::fsim`,
`atpg::compact`, `atpg::stil`) rather than separate CMake targets.

Error handling is exception-free throughout: fallible functions return
`atpg::Result<T>` or `atpg::Status` (see `include/atpg/Result.hpp`). See
`CLAUDE.md` for this and other project conventions.

## Development

Formatting is enforced by [`clang-format`](https://clang.llvm.org/docs/ClangFormat.html)
(LLVM style) via [pre-commit](https://pre-commit.com). After cloning:

```sh
pre-commit install
```

### Documentation

API documentation is generated with [Doxygen](https://www.doxygen.nl) and
published to GitHub Pages on every push to `main`. Building it locally needs
Doxygen installed; the theme is fetched automatically:

```sh
cmake --preset=docs
cmake --build build/docs --target docs
```

The result lands in `build/docs/docs/doxygen/html`. The Doxyfile sets
`WARN_AS_ERROR = FAIL_ON_WARNINGS`, so a broken cross-reference or a malformed
doc comment fails the build rather than being quietly published.

Documentation is off by default in an ordinary build, so Doxygen is not needed
to build or test the tool.

## License

MIT - see [LICENSE](LICENSE).
