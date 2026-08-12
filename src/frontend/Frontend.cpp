#include "atpg/frontend/Frontend.hpp"

#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Scope.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/ValuePath.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/syntax/SyntaxTree.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace atpg::frontend {

using namespace slang;
using namespace slang::ast;

// atpg::ir::GateType etc. would otherwise collide with unqualified names
// pulled in by `using namespace slang::ast` above.
using ir::Gate;
using ir::GateId;
using ir::GateType;
using ir::Graph;

namespace {

Result<GateType> gateTypeForPrimitive(std::string_view primitiveName) {
  if (auto type = ir::gateTypeFromPrimitiveName(primitiveName)) {
    return *type;
  }
  return Error("unsupported primitive type: " + std::string(primitiveName));
}

std::string joinPath(std::string_view prefix, std::string_view name) {
  if (prefix.empty()) {
    return std::string(name);
  }
  return std::string(prefix) + "." + std::string(name);
}

std::string bitName(const Symbol& symbol, uint64_t bit, uint64_t width) {
  if (width <= 1) {
    return std::string(symbol.name);
  }
  return std::string(symbol.name) + "[" + std::to_string(bit) + "]";
}

/// A statically-resolved reference to a contiguous bit range of a value
/// symbol, e.g. from a plain net reference or a constant bit-select.
struct ResolvedPath {
  const ValueSymbol* root;
  uint64_t lo;        // first selected bit, 0-based from the LSB
  uint64_t width;      // number of bits selected
  uint64_t rootWidth; // total declared width of root
};

/// A single primitive gate instance, collected with all of its port nets
/// already resolved to hierarchical (pre-union-find) net keys.
struct GateSpec {
  GateType type;
  std::string name;
  std::vector<std::pair<PrimitivePortDirection, std::string>> ports;
};

/// Flattens an elaborated instance hierarchy into a gate graph.
///
/// Netlist flattening is done in two phases. `collect*` walks the hierarchy
/// once, recording every primitive gate's port nets and, at every module
/// instance boundary, unioning the instance's internal port net with
/// whatever net it is connected to in the parent scope (module ports don't
/// carry signal state of their own - the two sides of a port connection are
/// the same electrical net). `materialize` then resolves every net key to
/// its union-find representative and builds the graph, so it doesn't matter
/// whether a net's driver or its aliases were discovered first during the
/// walk.
class Builder {
public:
  explicit Builder(const Symbol& evalContextSymbol) : evalCtx_(evalContextSymbol) {}

  Result<Graph> build(const InstanceSymbol& top) {
    ATPG_RETURN_IF_ERROR(collectTopPorts(top));
    ATPG_RETURN_IF_ERROR(collectInstance(top, ""));
    return materialize();
  }

private:
  EvalContext evalCtx_;
  std::vector<GateSpec> gateSpecs_;
  std::vector<std::pair<std::string, std::string>> piPorts_; // net key, display name
  std::vector<std::pair<std::string, std::string>> poPorts_; // net key, display name
  std::unordered_map<std::string, std::string> unionParent_;

  // -- union-find over net keys -------------------------------------------

  // Iterative (not recursive) so a long chain of aliased nets - e.g. a deep
  // stack of single-bit pass-through submodule instances - can't overflow
  // the call stack before path compression catches up.
  std::string find(const std::string& key) {
    std::string root = key;
    while (true) {
      auto it = unionParent_.find(root);
      if (it == unionParent_.end() || it->second == root) {
        break;
      }
      root = it->second;
    }

    std::string cur = key;
    while (cur != root) {
      auto it = unionParent_.find(cur);
      std::string next = it->second;
      it->second = root;
      cur = next;
    }

    return root;
  }

  void unite(const std::string& a, const std::string& b) {
    const std::string ra = find(a);
    const std::string rb = find(b);
    if (ra != rb) {
      unionParent_[ra] = rb;
    }
  }

  // -- expression resolution ------------------------------------------------

  Result<ResolvedPath> resolvePath(const Expression& expr) {
    // Output (and inout) port/argument connections are bound as an
    // AssignmentExpression whose left-hand side is the real lvalue - the
    // right-hand side is an unused placeholder. See
    // Expression::bindLValue in slang.
    const Expression& target = expr.kind == ExpressionKind::Assignment
                                    ? expr.as<AssignmentExpression>().left()
                                    : expr;

    ValuePath path(target, evalCtx_);
    const ValueSymbol* root = path.rootSymbol();
    if (root == nullptr || !path.isFullyStatic()) {
      return Error("unsupported connection expression: only plain nets "
                   "and constant bit-selects are supported");
    }
    const uint64_t lo = path.lspBounds.first;
    const uint64_t width = path.lspBounds.second - path.lspBounds.first + 1;
    return ResolvedPath{root, lo, width, root->getType().getBitWidth()};
  }

  Result<std::string> resolveScalarNet(const Expression& expr, std::string_view pathPrefix) {
    ATPG_ASSIGN_OR_RETURN(const ResolvedPath rp, resolvePath(expr));
    if (rp.width != 1) {
      return Error("primitive gate ports must connect to a single bit");
    }
    return joinPath(pathPrefix, bitName(*rp.root, rp.lo, rp.rootWidth));
  }

  // A port symbol connected to something other than a plain internal net
  // (e.g. an interface port) is outside the primitives-only scope this
  // frontend supports.
  static Result<const PortSymbol*> asSupportedPort(const Symbol& portSymbol) {
    if (portSymbol.kind != SymbolKind::Port) {
      return Error("unsupported port kind on '" + std::string(portSymbol.name) + "'");
    }

    const auto& port = portSymbol.as<PortSymbol>();
    if (port.internalSymbol == nullptr) {
      return Error("unsupported port '" + std::string(port.name) + "': no internal net");
    }
    return &port;
  }

  // -- collection -------------------------------------------------------

  Status collectTopPorts(const InstanceSymbol& top) {
    for (const Symbol* portSym : top.body.getPortList()) {
      ATPG_ASSIGN_OR_RETURN(const PortSymbol* port, asSupportedPort(*portSym));
      const auto& internal = port->internalSymbol->as<ValueSymbol>();
      const uint64_t width = internal.getType().getBitWidth();

      for (uint64_t bit = 0; bit < width; ++bit) {
        std::string key = bitName(internal, bit, width);
        std::string displayName = bitName(*port, bit, width);

        if (port->direction == ArgumentDirection::In) {
          piPorts_.emplace_back(std::move(key), std::move(displayName));
        } else if (port->direction == ArgumentDirection::Out) {
          poPorts_.emplace_back(std::move(key), std::move(displayName));
        } else {
          return Error("unsupported port direction on '" + std::string(port->name) +
                       "': only input and output are supported");
        }
      }
    }
    return {};
  }

  Status aliasChildPorts(const InstanceSymbol& child, std::string_view parentPrefix,
                         std::string_view childPrefix) {
    for (const PortConnection* pc : child.getPortConnections()) {
      ATPG_ASSIGN_OR_RETURN(const PortSymbol* childPort, asSupportedPort(pc->port));
      const auto& internal = childPort->internalSymbol->as<ValueSymbol>();
      const uint64_t width = internal.getType().getBitWidth();

      const Expression* expr = pc->getExpression();
      if (expr == nullptr) {
        return Error("unconnected port '" + std::string(childPort->name) + "' on instance '" +
                     std::string(child.name) + "'");
      }

      ATPG_ASSIGN_OR_RETURN(const ResolvedPath rp, resolvePath(*expr));
      if (rp.width != width) {
        return Error("port width mismatch connecting '" + std::string(childPort->name) +
                     "' on instance '" + std::string(child.name) + "'");
      }

      for (uint64_t bit = 0; bit < width; ++bit) {
        std::string childKey = joinPath(childPrefix, bitName(internal, bit, width));
        std::string parentKey = joinPath(parentPrefix, bitName(*rp.root, rp.lo + bit, rp.rootWidth));
        unite(childKey, parentKey);
      }
    }
    return {};
  }

  Status collectPrimitive(const PrimitiveInstanceSymbol& prim, std::string_view pathPrefix) {
    ATPG_ASSIGN_OR_RETURN(const GateType type, gateTypeForPrimitive(prim.primitiveType.name));
    const auto connections = prim.getPortConnections();
    if (connections.empty()) {
      return Error("primitive '" + std::string(prim.name) + "': no port connections");
    }

    GateSpec spec;
    spec.type = type;
    spec.name = joinPath(pathPrefix, prim.name);
    spec.ports.reserve(connections.size());

    // Built-in and/nand/or/nor/xor/xnor are n-input gates: one output
    // followed by two or more inputs. buf/not are n-output gates: one or
    // more outputs followed by a single input. Either way slang's
    // PrimitiveSymbol::ports only holds a two-entry [out, in] template, not
    // one entry per actual connection, so ports are classified directly
    // from GateType and connection position rather than zipped with it.
    if (type == GateType::Buf || type == GateType::Not) {
      for (std::size_t i = 0; i + 1 < connections.size(); ++i) {
        ATPG_ASSIGN_OR_RETURN(std::string net, resolveScalarNet(*connections[i], pathPrefix));
        spec.ports.emplace_back(PrimitivePortDirection::Out, std::move(net));
      }
      ATPG_ASSIGN_OR_RETURN(std::string net, resolveScalarNet(*connections.back(), pathPrefix));
      spec.ports.emplace_back(PrimitivePortDirection::In, std::move(net));
    } else {
      ATPG_ASSIGN_OR_RETURN(std::string net, resolveScalarNet(*connections[0], pathPrefix));
      spec.ports.emplace_back(PrimitivePortDirection::Out, std::move(net));
      for (std::size_t i = 1; i < connections.size(); ++i) {
        ATPG_ASSIGN_OR_RETURN(std::string in, resolveScalarNet(*connections[i], pathPrefix));
        spec.ports.emplace_back(PrimitivePortDirection::In, std::move(in));
      }
    }

    gateSpecs_.push_back(std::move(spec));
    return {};
  }

  // Declaration-only member kinds carry no logic of their own and are safe
  // to skip while flattening. Everything else - continuous assigns,
  // procedural blocks, generate constructs, instance arrays, and so on - is
  // outside this frontend's primitives-only scope and must be rejected
  // rather than silently dropped, per buildGraph's documented contract.
  static bool isHarmlessDeclaration(SymbolKind kind) {
    switch (kind) {
      case SymbolKind::Port:
      case SymbolKind::Net:
      case SymbolKind::Variable:
      case SymbolKind::Parameter:
      case SymbolKind::TypeParameter:
      case SymbolKind::TypeAlias:
      case SymbolKind::Genvar:
        return true;
      default:
        return false;
    }
  }

  Status collectInstance(const InstanceSymbol& inst, std::string_view pathPrefix) {
    for (const Symbol& member : inst.body.members()) {
      if (member.kind == SymbolKind::Instance) {
        const auto& child = member.as<InstanceSymbol>();
        const std::string childPrefix = joinPath(pathPrefix, child.name);
        ATPG_RETURN_IF_ERROR(aliasChildPorts(child, pathPrefix, childPrefix));
        ATPG_RETURN_IF_ERROR(collectInstance(child, childPrefix));
      } else if (member.kind == SymbolKind::PrimitiveInstance) {
        ATPG_RETURN_IF_ERROR(collectPrimitive(member.as<PrimitiveInstanceSymbol>(), pathPrefix));
      } else if (!isHarmlessDeclaration(member.kind)) {
        return Error("unsupported construct '" + std::string(member.name) + "' (" +
                     std::string(toString(member.kind)) +
                     "): only gate primitives and module instances are supported");
      }
    }
    return {};
  }

  // -- materialization ----------------------------------------------------

  Result<Graph> materialize() {
    Graph graph;
    std::unordered_map<std::string, GateId> netToDriver;

    auto registerDriver = [&](const std::string& key, GateId gid) -> Status {
      const std::string canon = find(key);
      if (!netToDriver.try_emplace(canon, gid).second) {
        return Error("net driven by multiple sources: " + canon);
      }
      return {};
    };

    for (auto& [key, displayName] : piPorts_) {
      ATPG_RETURN_IF_ERROR(registerDriver(key, graph.addGate(GateType::Pi, displayName)));
    }

    std::vector<GateId> specGateIds(gateSpecs_.size());
    for (std::size_t i = 0; i < gateSpecs_.size(); ++i) {
      const GateSpec& spec = gateSpecs_[i];
      specGateIds[i] = graph.addGate(spec.type, spec.name);
      for (auto& [direction, key] : spec.ports) {
        if (direction == PrimitivePortDirection::Out) {
          ATPG_RETURN_IF_ERROR(registerDriver(key, specGateIds[i]));
        }
      }
    }

    for (std::size_t i = 0; i < gateSpecs_.size(); ++i) {
      const GateSpec& spec = gateSpecs_[i];
      for (auto& [direction, key] : spec.ports) {
        if (direction != PrimitivePortDirection::In) {
          continue;
        }
        const std::string canon = find(key);
        auto it = netToDriver.find(canon);
        if (it == netToDriver.end()) {
          return Error("net '" + canon + "' is read before it is driven");
        }
        graph.addEdge(it->second, specGateIds[i]);
      }
    }

    for (auto& [key, displayName] : poPorts_) {
      const std::string canon = find(key);
      auto it = netToDriver.find(canon);
      if (it == netToDriver.end()) {
        return Error("output '" + displayName + "' is never driven");
      }
      graph.addEdge(it->second, graph.addGate(GateType::Po, displayName));
    }

    ATPG_RETURN_IF_ERROR(graph.levelize());
    return graph;
  }
};

} // namespace

Status requireNoErrors(Compilation& compilation) {
  const auto diagnostics = compilation.getAllDiagnostics();
  for (const auto& diag : diagnostics) {
    if (diag.isError()) {
      return Error(slang::DiagnosticEngine::reportAll(
          slang::syntax::SyntaxTree::getDefaultSourceManager(), diagnostics));
    }
  }
  return {};
}

Result<ir::Graph> buildGraph(Compilation& compilation, std::string_view topModuleName) {
  const RootSymbol& root = compilation.getRoot();

  const InstanceSymbol* top = nullptr;
  for (const InstanceSymbol* inst : root.topInstances) {
    if (inst->getDefinition().name == topModuleName || inst->name == topModuleName) {
      top = inst;
      break;
    }
  }
  if (top == nullptr) {
    return Error("top module not found: " + std::string(topModuleName));
  }

  Builder builder(top->body);
  return builder.build(*top);
}

} // namespace atpg::frontend
