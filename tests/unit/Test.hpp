#pragma once

#include "atpg/Result.hpp"
#include "atpg/frontend/Frontend.hpp"
#include "atpg/ir/Graph.hpp"

#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>

/// Compiles inline SystemVerilog text and flattens `topModuleName` into a
/// gate graph. Fails the current test if compilation reports any errors;
/// otherwise returns the frontend's raw Result, so tests that specifically
/// expect flattening to fail can check it themselves.
inline atpg::Result<atpg::ir::Graph> tryBuildTestGraph(std::string_view text,
                                                       std::string_view topModuleName) {
  auto tree = slang::syntax::SyntaxTree::fromText(text);

  slang::ast::Compilation compilation;
  compilation.addSyntaxTree(tree);

  const atpg::Status diagStatus = atpg::frontend::requireNoErrors(compilation);
  if (!diagStatus) {
    FAIL(diagStatus.error());
  }

  return atpg::frontend::buildGraph(compilation, topModuleName);
}

/// Same as tryBuildTestGraph, but also fails the current test if flattening
/// itself fails - for the common case where a test only cares about a
/// successfully-flattened graph.
inline atpg::ir::Graph buildTestGraph(std::string_view text, std::string_view topModuleName) {
  atpg::Result<atpg::ir::Graph> result = tryBuildTestGraph(text, topModuleName);
  REQUIRE(result.ok());
  return std::move(result.value());
}

/// Compiles a SystemVerilog file from disk and flattens `topModuleName` into
/// a gate graph. Fails the current test if compilation reports any errors;
/// otherwise returns the frontend's raw Result, so tests that specifically
/// expect flattening to fail can check it themselves.
inline atpg::Result<atpg::ir::Graph> tryBuildTestGraphFromFile(const std::string& path,
                                                               std::string_view topModuleName) {
  auto treeResult = slang::syntax::SyntaxTree::fromFile(path);
  if (!treeResult) {
    FAIL("could not read " << path << ": " << treeResult.error().second);
  }

  slang::ast::Compilation compilation;
  compilation.addSyntaxTree(*treeResult);

  const atpg::Status diagStatus = atpg::frontend::requireNoErrors(compilation);
  if (!diagStatus) {
    FAIL(diagStatus.error());
  }

  return atpg::frontend::buildGraph(compilation, topModuleName);
}

/// Same as tryBuildTestGraphFromFile, but also fails the current test if
/// flattening itself fails.
inline atpg::ir::Graph buildTestGraphFromFile(const std::string& path,
                                              std::string_view topModuleName) {
  atpg::Result<atpg::ir::Graph> result = tryBuildTestGraphFromFile(path, topModuleName);
  REQUIRE(result.ok());
  return std::move(result.value());
}
