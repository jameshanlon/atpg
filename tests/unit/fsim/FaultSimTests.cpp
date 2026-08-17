#include "atpg/fault/Fault.hpp"
#include "atpg/fsim/FaultSim.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace atpg::fault;
using namespace atpg::fsim;

TEST_CASE("SimResult reports counts and coverage over added statuses", "[FaultSim]") {
  SimResult results;
  CHECK(results.size() == 0);
  CHECK(results.detectedCount() == 0);
  CHECK(results.coverage() == 0.0);

  const Fault f1{PinRef{0, PinKind::Output, 0}, StuckValue::SA0};
  const Fault f2{PinRef{1, PinKind::Output, 0}, StuckValue::SA1};
  const Fault f3{PinRef{2, PinKind::Output, 0}, StuckValue::SA0};
  results.add(FaultStatus{f1, true, 7});
  results.add(FaultStatus{f2, false, 0});
  results.add(FaultStatus{f3, true, 2});

  REQUIRE(results.size() == 3);
  CHECK(results.detectedCount() == 2);
  CHECK(results.coverage() == 2.0 / 3.0);

  auto it = results.begin();
  CHECK(it->fault == f1);
  CHECK(it->detected == true);
  CHECK(it->firstDetectingPattern == 7);
  ++it;
  CHECK(it->fault == f2);
  CHECK(it->detected == false);
}
