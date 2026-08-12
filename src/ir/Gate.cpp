#include "atpg/ir/Gate.hpp"

#include <array>
#include <utility>

namespace atpg::ir {

namespace {

constexpr std::array<std::pair<GateType, std::string_view>, 10> kGateTypeNames{{
    {GateType::And, "and"},
    {GateType::Nand, "nand"},
    {GateType::Or, "or"},
    {GateType::Nor, "nor"},
    {GateType::Xor, "xor"},
    {GateType::Xnor, "xnor"},
    {GateType::Buf, "buf"},
    {GateType::Not, "not"},
    {GateType::Pi, "PI"},
    {GateType::Po, "PO"},
}};

} // namespace

std::string_view gateTypeName(GateType type) {
  for (const auto& [candidate, name] : kGateTypeNames) {
    if (candidate == type)
      return name;
  }
  return "?";
}

std::optional<GateType> gateTypeFromPrimitiveName(std::string_view name) {
  for (const auto& [type, primitiveName] : kGateTypeNames) {
    if (type != GateType::Pi && type != GateType::Po && primitiveName == name)
      return type;
  }
  return std::nullopt;
}

} // namespace atpg::ir
