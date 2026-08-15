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
  exhausted) via a CP-SAT miter formulation.

Not yet implemented: fault simulation, pattern compaction, and STIL
output.

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
atpg <file.sv> --top <module> [--dump-graph out.dot] [--dump-faults out.txt] [--generate-tests out.txt] [--time-limit seconds] [--stimulus vectors.txt]
```

- `--dump-graph <path>` writes the flattened gate graph as Graphviz dot.
- `--dump-faults <path>` writes the collapsed fault list as plain text,
  one line per fault class, e.g. `n1/out/SA1 = g5/in0/SA1`.
- `--generate-tests <path>` writes a generated test pattern (or
  `redundant`/`aborted`) per fault class to a file, one line per fault,
  e.g. `n1/out/SA1: testable 10110`.
- `--time-limit <seconds>` sets the per-fault CP-SAT solver time limit
  used by `--generate-tests` (default 5 seconds).
- `--stimulus <path>` reads newline-separated `0`/`1` vectors (one bit per
  primary input, in port order) and prints the simulated primary-output
  bits for each one.

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
tests/
  unit/         Catch2 tests, mirrors src/
  data/         hand-verifiable .sv fixtures (half adder, full adder, c17)
```

Everything lives in one `atpg-core` static library and the `atpg`
namespace, split into sub-namespaces by module (`atpg::ir`, `atpg::sim`,
`atpg::frontend`, `atpg::fault`) rather than separate CMake targets.

Error handling is exception-free throughout: fallible functions return
`atpg::Result<T>` or `atpg::Status` (see `include/atpg/Result.hpp`). See
`CLAUDE.md` for this and other project conventions.

## Development

Formatting is enforced by [`clang-format`](https://clang.llvm.org/docs/ClangFormat.html)
(LLVM style) via [pre-commit](https://pre-commit.com). After cloning:

```sh
pre-commit install
```

## License

MIT - see [LICENSE](LICENSE).
