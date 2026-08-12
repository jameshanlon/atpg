#include "atpg/Result.hpp"
#include "atpg/frontend/Frontend.hpp"
#include "atpg/ir/Graph.hpp"
#include "atpg/sim/LogicSim.hpp"

#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

#include <fstream>
#include <iostream>
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
        return atpg::Error("missing value for " + std::string(arg));
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
  os << "digraph atpg {\n";
  for (std::size_t i = 0; i < graph.size(); ++i) {
    const auto& gate = graph.gate(static_cast<atpg::ir::GateId>(i));
    os << "  n" << gate.id << " [label=\"" << gate.name << "\\n" << atpg::ir::gateTypeName(gate.type)
       << "\"];\n";
  }
  for (std::size_t i = 0; i < graph.size(); ++i) {
    const auto& gate = graph.gate(static_cast<atpg::ir::GateId>(i));
    for (const atpg::ir::GateId succ : gate.fanout) {
      os << "  n" << gate.id << " -> n" << succ << ";\n";
    }
  }
  os << "}\n";
}

atpg::Status runStimulus(const atpg::ir::Graph& graph, const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) {
    return atpg::Error("could not open stimulus file: " + path);
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
    for (const bool bit : outputs) {
      std::cout << (bit ? '1' : '0');
    }
    std::cout << '\n';
  }
  return {};
}

} // namespace

int main(int argc, char** argv) {
  const atpg::Result<Args> argsResult = parseArgs(argc, argv);
  if (!argsResult) {
    std::cerr << "error: " << argsResult.error() << '\n';
    return 1;
  }
  const Args& args = argsResult.value();

  auto treeResult = slang::syntax::SyntaxTree::fromFile(args.file);
  if (!treeResult) {
    std::cerr << "error: " << treeResult.error().second << '\n';
    return 1;
  }

  slang::ast::Compilation compilation;
  compilation.addSyntaxTree(*treeResult);

  const atpg::Status diagStatus = atpg::frontend::requireNoErrors(compilation);
  if (!diagStatus) {
    std::cerr << diagStatus.error();
    return 1;
  }

  const atpg::Result<atpg::ir::Graph> graphResult = atpg::frontend::buildGraph(compilation, args.top);
  if (!graphResult) {
    std::cerr << "error: " << graphResult.error() << '\n';
    return 1;
  }
  const atpg::ir::Graph& graph = graphResult.value();

  if (!args.dumpGraphPath.empty()) {
    std::ofstream ofs(args.dumpGraphPath);
    if (!ofs) {
      std::cerr << "error: could not open " << args.dumpGraphPath << " for writing\n";
      return 1;
    }
    writeDot(graph, ofs);
  }

  if (!args.stimulusPath.empty()) {
    const atpg::Status simStatus = runStimulus(graph, args.stimulusPath);
    if (!simStatus) {
      std::cerr << "error: " << simStatus.error() << '\n';
      return 1;
    }
  }

  return 0;
}
