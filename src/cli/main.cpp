#include "atpg/Result.hpp"
#include "atpg/frontend/Frontend.hpp"
#include "atpg/ir/Graph.hpp"
#include "atpg/sim/LogicSim.hpp"

#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Args {
  std::string file;
  std::string top;
  std::string dumpGraphPath;
  std::string stimulusPath;
};

atpg::Result<Args> parseArgs(int argc, char** argv) {
  Args args;
  std::vector<std::string_view> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];

    if (arg == "--top" || arg == "--dump-graph" || arg == "--stimulus") {
      if (i + 1 >= argc) {
        return atpg::Error(fmt::format("missing value for {}", arg));
      }
      const std::string value = argv[++i];
      if (arg == "--top") {
        args.top = value;
      } else if (arg == "--dump-graph") {
        args.dumpGraphPath = value;
      } else {
        args.stimulusPath = value;
      }
    } else {
      positional.push_back(arg);
    }
  }

  if (positional.size() != 1) {
    return atpg::Error("usage: atpg <file.sv> --top <module> "
                       "[--dump-graph out.dot] [--stimulus vectors.txt]");
  }
  args.file = positional.front();

  if (args.top.empty()) {
    return atpg::Error("--top <module> is required");
  }

  return args;
}

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
  const atpg::Result<Args> argsResult = parseArgs(argc, argv);
  if (!argsResult) {
    fmt::print(stderr, "error: {}\n", argsResult.error());
    return 1;
  }
  const Args& args = argsResult.value();

  auto treeResult = slang::syntax::SyntaxTree::fromFile(args.file);
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

  const atpg::Result<atpg::ir::Graph> graphResult = atpg::frontend::buildGraph(compilation, args.top);
  if (!graphResult) {
    fmt::print(stderr, "error: {}\n", graphResult.error());
    return 1;
  }
  const atpg::ir::Graph& graph = graphResult.value();

  if (!args.dumpGraphPath.empty()) {
    std::ofstream ofs(args.dumpGraphPath);
    if (!ofs) {
      fmt::print(stderr, "error: could not open {} for writing\n", args.dumpGraphPath);
      return 1;
    }
    writeDot(graph, ofs);
  }

  if (!args.stimulusPath.empty()) {
    const atpg::Status simStatus = runStimulus(graph, args.stimulusPath);
    if (!simStatus) {
      fmt::print(stderr, "error: {}\n", simStatus.error());
      return 1;
    }
  }

  return 0;
}
