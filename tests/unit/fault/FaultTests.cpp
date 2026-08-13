#include "atpg/fault/Fault.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace atpg::fault;

TEST_CASE("PinRef equality compares all fields", "[Fault]") {
  const PinRef a{5, PinKind::Input, 2};
  const PinRef b{5, PinKind::Input, 2};
  const PinRef c{5, PinKind::Input, 3};
  const PinRef d{5, PinKind::Output, 2};

  CHECK(a == b);
  CHECK_FALSE(a == c);
  CHECK_FALSE(a == d);
}

TEST_CASE("Fault equality compares pin and stuck value", "[Fault]") {
  const Fault a{PinRef{5, PinKind::Output, 0}, StuckValue::SA0};
  const Fault b{PinRef{5, PinKind::Output, 0}, StuckValue::SA0};
  const Fault c{PinRef{5, PinKind::Output, 0}, StuckValue::SA1};

  CHECK(a == b);
  CHECK_FALSE(a == c);
}
