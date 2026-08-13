#include "atpg/Result.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/frontend/Frontend.hpp"
#include "atpg/ir/Graph.hpp"
#include "atpg/sim/LogicSim.hpp"

#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

void writeDot(const atpg::ir::Graph& graph, std::ostream& os) {
  fmt::print(os, "digraph atpg {{\n");
  for (std::size_t i = 0; i < graph.size(); ++i) {
    const auto& gate = graph.gate(static_cast<atpg::ir::GateId>(i));
    fmt::print(os, "  n{} [label=\"{}\\n{}\"];\n", gate.id, gate.name,
              atpg::ir::gateTypeName(gate.type));
  }
  for (std::size_t i = 0; i < graph.size(); ++i) {
    const auto& gate = graph.gate(static_cast<atpg::ir::GateId>(i));
    for (const atpg::ir::GateId succ : gate.fanout) {
      fmt::print(os, "  n{} -> n{};\n", gate.id, succ);
    }
  }
  fmt::print(os, "}}\n");
}

std::string describeFault(const atpg::ir::Graph& graph, const atpg::fault::Fault& fault) {
  const atpg::ir::Gate& gate = graph.gate(fault.pin.gate);
  const std::string gateName = gate.name.empty() ? fmt::format("g{}", gate.id) : gate.name;
  const char* stuck = fault.value == atpg::fault::StuckValue::SA0 ? "SA0" : "SA1";
  if (fault.pin.kind == atpg::fault::PinKind::Output) {
    return fmt::format("{}/out/{}", gateName, stuck);
  }
  return fmt::format("{}/in{}/{}", gateName, fault.pin.inputIndex, stuck);
}

void writeFaultList(const atpg::ir::Graph& graph, const atpg::fault::FaultList& faults,
                    std::ostream& os) {
  for (const auto& faultClass : faults) {
    fmt::print(os, "{}", describeFault(graph, faultClass.representative));
    for (const auto& equiv : faultClass.equivalent) {
      fmt::print(os, " = {}", describeFault(graph, equiv));
    }
    fmt::print(os, "\n");
  }
}

atpg::Status runStimulus(const atpg::ir::Graph& graph, const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) {
    return atpg::Error(fmt::format("could not open stimulus file: {}", path));
  }

  std::string line;
  while (std::getline(ifs, line)) {
    std::vector<bool> piValues;
    for (const char c : line) {
      if (c == '0') {
        piValues.push_back(false);
      } else if (c == '1') {
        piValues.push_back(true);
      }
    }
    if (piValues.empty()) {
      continue;
    }

    ATPG_ASSIGN_OR_RETURN(const std::vector<bool> outputs, atpg::sim::simulate(graph, piValues));

    std::string bits;
    bits.reserve(outputs.size());
    for (const bool bit : outputs) {
      bits.push_back(bit ? '1' : '0');
    }
    fmt::print("{}\n", bits);
  }
  return {};
}

} // namespace

int main(int argc, char** argv) {
  CLI::App app{"atpg - flattens a gate-level SystemVerilog design (built from SV gate "
              "primitives) into a combinational gate graph and simulates it"};
  argv = app.ensure_utf8(argv);

  std::string file;
  std::string top;
  std::string dumpGraphPath;
  std::string dumpFaultsPath;
  std::string stimulusPath;

  app.add_option("file", file, "SystemVerilog file containing the design")
      ->required()
      ->check(CLI::ExistingFile);
  app.add_option("--top", top, "Top module name")->required();
  app.add_option("--dump-graph", dumpGraphPath, "Write the flattened gate graph as Graphviz dot");
  app.add_option("--dump-faults", dumpFaultsPath,
                "Write the collapsed fault list as plain text");
  app.add_option("--stimulus", stimulusPath,
                "Read newline-separated 0/1 stimulus vectors and simulate each one");

  // CLI11's App::parse() is the one place in atpg that reports failure via a
  // thrown CLI::ParseError rather than atpg::Status/Result - it has no
  // non-throwing entry point. CLI11_PARSE is the library's own standard
  // idiom for catching that at the boundary and turning it into a plain
  // exit code, matching how atpg reports every other error.
  CLI11_PARSE(app, argc, argv);

  auto treeResult = slang::syntax::SyntaxTree::fromFile(file);
  if (!treeResult) {
    fmt::print(stderr, "error: {}\n", treeResult.error().second);
    return 1;
  }

  slang::ast::Compilation compilation;
  compilation.addSyntaxTree(*treeResult);

  const atpg::Status diagStatus = atpg::frontend::requireNoErrors(compilation);
  if (!diagStatus) {
    fmt::print(stderr, "{}", diagStatus.error());
    return 1;
  }

  const atpg::Result<atpg::ir::Graph> graphResult = atpg::frontend::buildGraph(compilation, top);
  if (!graphResult) {
    fmt::print(stderr, "error: {}\n", graphResult.error());
    return 1;
  }
  const atpg::ir::Graph& graph = graphResult.value();

  if (!dumpGraphPath.empty()) {
    std::ofstream ofs(dumpGraphPath);
    if (!ofs) {
      fmt::print(stderr, "error: could not open {} for writing\n", dumpGraphPath);
      return 1;
    }
    writeDot(graph, ofs);
  }

  if (!dumpFaultsPath.empty()) {
    std::ofstream ofs(dumpFaultsPath);
    if (!ofs) {
      fmt::print(stderr, "error: could not open {} for writing\n", dumpFaultsPath);
      return 1;
    }
    writeFaultList(graph, atpg::fault::generateFaultList(graph), ofs);
  }

  if (!stimulusPath.empty()) {
    const atpg::Status simStatus = runStimulus(graph, stimulusPath);
    if (!simStatus) {
      fmt::print(stderr, "error: {}\n", simStatus.error());
      return 1;
    }
  }

  return 0;
}
