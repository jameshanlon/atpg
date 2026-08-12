#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atpg::ir {

using GateId = std::uint32_t;

/// The boolean function of a gate node. Pi/Po are pseudo-gates representing
/// primary inputs/outputs so the graph can treat every node uniformly.
enum class GateType {
  And,
  Nand,
  Or,
  Nor,
  Xor,
  Xnor,
  Buf,
  Not,
  Pi,
  Po,
};

/// The canonical display name for a gate type: the SV primitive keyword for
/// And/Nand/Or/Nor/Xor/Xnor/Buf/Not, or "PI"/"PO" for the pseudo-gates.
std::string_view gateTypeName(GateType type);

/// Maps a built-in SV gate primitive keyword (and/nand/or/nor/xor/xnor/buf/not)
/// to its GateType. Returns std::nullopt for anything else, including "PI"/"PO"
/// which are not real primitives.
std::optional<GateType> gateTypeFromPrimitiveName(std::string_view name);

/// A node in a flattened, purely combinational gate-level netlist.
struct Gate {
  GateId id = 0;
  GateType type = GateType::Pi;
  std::vector<GateId> fanin;
  std::vector<GateId> fanout;
  int level = -1;
  std::string name;
};

} // namespace atpg::ir
