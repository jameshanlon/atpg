#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "../stil/StilReader.hpp"

using namespace atpg::testing;

namespace {

/// A hand-written STIL program for a 2-input AND with one output. Written
/// out here rather than produced by atpg::stil, so the scanner is proven
/// against text the writer had no hand in.
const char* kSample = R"(STIL 1.0;

Header {
  Title "atpg-generated test patterns for demo";
}

Signals {
  "a" In;
  "b" In;
  "y" Out;
}

SignalGroups {
  "PI" = '"a" + "b"';
  "PO" = '"y"';
}

Timing "timing" {
  WaveformTable "wft" {
    Period '100ns';
    Waveforms {
      "PI" { 01 { '0ns' D/U; } }
      "PO" { LH { '50ns' L/H; } }
    }
  }
}

PatternBurst "burst" {
  PatList { "patterns" { } }
}

PatternExec {
  Timing "timing";
  PatternBurst "burst";
}

Pattern "patterns" {
  W "wft";
  V { "PI"=00; "PO"=L; }
  V { "PI"=11; "PO"=H; }
}
)";

} // namespace

TEST_CASE("readStil recovers the signal declarations in order", "[StilReader]") {
  const atpg::Result<StilProgram> program = readStil(kSample);
  REQUIRE(program.ok());

  REQUIRE(program.value().signals.size() == 3);
  CHECK(program.value().signals[0].name == "a");
  CHECK(program.value().signals[0].isInput);
  CHECK(program.value().signals[1].name == "b");
  CHECK(program.value().signals[1].isInput);
  CHECK(program.value().signals[2].name == "y");
  CHECK_FALSE(program.value().signals[2].isInput);
}

TEST_CASE("readStil recovers signal group membership in order", "[StilReader]") {
  const atpg::Result<StilProgram> program = readStil(kSample);
  REQUIRE(program.ok());

  CHECK(program.value().piGroup == std::vector<std::string>{"a", "b"});
  CHECK(program.value().poGroup == std::vector<std::string>{"y"});
}

TEST_CASE("readStil recovers every vector, and is not confused by PatternBurst", "[StilReader]") {
  // "PatternBurst" and "PatternExec" both begin with "Pattern"; a scanner
  // that matches on that prefix alone finds the wrong block and reports no
  // vectors at all - which would make every property test using it vacuous.
  const atpg::Result<StilProgram> program = readStil(kSample);
  REQUIRE(program.ok());

  REQUIRE(program.value().vectors.size() == 2);
  CHECK(program.value().vectors[0].inputs == "00");
  CHECK(program.value().vectors[0].outputs == "L");
  CHECK(program.value().vectors[1].inputs == "11");
  CHECK(program.value().vectors[1].outputs == "H");
}

TEST_CASE("readStil reports text that is not a STIL program", "[StilReader]") {
  CHECK_FALSE(readStil("this is not STIL at all").ok());
}

TEST_CASE("readStil accepts a program with no vectors", "[StilReader]") {
  const std::string empty = R"(STIL 1.0;
Signals {
  "a" In;
  "y" Out;
}
SignalGroups {
  "PI" = '"a"';
  "PO" = '"y"';
}
Pattern "patterns" {
  W "wft";
}
)";
  const atpg::Result<StilProgram> program = readStil(empty);
  REQUIRE(program.ok());
  CHECK(program.value().vectors.empty());
  CHECK(program.value().signals.size() == 2);
}
