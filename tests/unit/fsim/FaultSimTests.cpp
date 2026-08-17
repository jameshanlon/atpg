#include "atpg/fault/Fault.hpp"
#include "atpg/fault/FaultList.hpp"
#include "atpg/fsim/FaultSim.hpp"
#include "atpg/ir/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using namespace atpg::fault;
using namespace atpg::fsim;
using namespace atpg::ir;

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

TEST_CASE("a 2-input AND gate's output SA0 is detected only by the all-ones pattern",
          "[FaultSim]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId g = graph.addGate(GateType::And, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(b, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{g, PinKind::Output, 0}, StuckValue::SA0}, {}});

  // Patterns 00, 01, 10, 11 - only the last drives g's output to 1, so only
  // it can expose output/SA0.
  const std::vector<std::vector<bool>> patterns = {
      {false, false}, {false, true}, {true, false}, {true, true}};

  const atpg::Result<SimResult> result = simulateFaults(graph, faults, patterns);
  REQUIRE(result.ok());
  REQUIRE(result.value().size() == 1);

  const FaultStatus& status = *result.value().begin();
  CHECK(status.detected == true);
  CHECK(status.firstDetectingPattern == 3);
  CHECK(result.value().detectedCount() == 1);
  CHECK(result.value().coverage() == 1.0);
}

TEST_CASE("a fault in dead logic is never detected", "[FaultSim]") {
  // a, b -> dead (And, unread); a -> live (Buf) -> y
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId dead = graph.addGate(GateType::And, "dead");
  const GateId live = graph.addGate(GateType::Buf, "live");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, dead);
  graph.addEdge(b, dead);
  graph.addEdge(a, live);
  graph.addEdge(live, y);
  REQUIRE(graph.levelize().ok());

  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{dead, PinKind::Output, 0}, StuckValue::SA1}, {}});

  const std::vector<std::vector<bool>> patterns = {
      {false, false}, {false, true}, {true, false}, {true, true}};

  const atpg::Result<SimResult> result = simulateFaults(graph, faults, patterns);
  REQUIRE(result.ok());
  REQUIRE(result.value().size() == 1);
  CHECK(result.value().begin()->detected == false);
  CHECK(result.value().coverage() == 0.0);
}

TEST_CASE("an input-pin fault affects only the faulted gate's own reading of that net",
          "[FaultSim]") {
  // a -> g1 (Buf) -> y1
  //   -> g2 (Buf) -> y2
  // Forcing g1's input pin to 0 must change y1 only, never y2.
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId g1 = graph.addGate(GateType::Buf, "g1");
  const GateId g2 = graph.addGate(GateType::Buf, "g2");
  const GateId y1 = graph.addGate(GateType::Po, "y1");
  const GateId y2 = graph.addGate(GateType::Po, "y2");
  graph.addEdge(a, g1);
  graph.addEdge(a, g2);
  graph.addEdge(g1, y1);
  graph.addEdge(g2, y2);
  REQUIRE(graph.levelize().ok());

  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{g1, PinKind::Input, 0}, StuckValue::SA0}, {}});

  // a=0 cannot expose a stuck-at-0; a=1 can.
  const std::vector<std::vector<bool>> patterns = {{false}, {true}};

  const atpg::Result<SimResult> result = simulateFaults(graph, faults, patterns);
  REQUIRE(result.ok());
  REQUIRE(result.value().size() == 1);
  CHECK(result.value().begin()->detected == true);
  CHECK(result.value().begin()->firstDetectingPattern == 1);
}

TEST_CASE("an output-pin fault on a primary input is detected", "[FaultSim]") {
  // The PI branch of the levelized loop must apply output-pin faults rather
  // than skipping straight past them.
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId g = graph.addGate(GateType::Buf, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{a, PinKind::Output, 0}, StuckValue::SA0}, {}});

  const std::vector<std::vector<bool>> patterns = {{false}, {true}};

  const atpg::Result<SimResult> result = simulateFaults(graph, faults, patterns);
  REQUIRE(result.ok());
  REQUIRE(result.value().size() == 1);
  CHECK(result.value().begin()->detected == true);
  CHECK(result.value().begin()->firstDetectingPattern == 1);
}

TEST_CASE("every gate type's bitwise evaluation matches its truth table", "[FaultSim]") {
  // For a single 2-input gate wired PI,PI -> g -> PO, output/SA0 is detected
  // by exactly those patterns whose good-circuit output is 1. Asserting the
  // detecting-pattern set therefore pins down `evaluateGate`'s bitwise
  // operator for each type - a wrong operator (say ^ instead of |) changes
  // which patterns detect.
  struct Case {
    GateType type;
    // Good output for inputs 00, 01, 10, 11 - the pattern order used below.
    bool expected[4];
  };
  const Case cases[] = {
      {GateType::And, {false, false, false, true}}, {GateType::Nand, {true, true, true, false}},
      {GateType::Or, {false, true, true, true}},    {GateType::Nor, {true, false, false, false}},
      {GateType::Xor, {false, true, true, false}},  {GateType::Xnor, {true, false, false, true}},
  };

  const std::vector<std::vector<bool>> patterns = {
      {false, false}, {false, true}, {true, false}, {true, true}};

  for (const Case& c : cases) {
    Graph graph;
    const GateId a = graph.addGate(GateType::Pi, "a");
    const GateId b = graph.addGate(GateType::Pi, "b");
    const GateId g = graph.addGate(c.type, "g");
    const GateId y = graph.addGate(GateType::Po, "y");
    graph.addEdge(a, g);
    graph.addEdge(b, g);
    graph.addEdge(g, y);
    REQUIRE(graph.levelize().ok());

    FaultList faults;
    faults.add(FaultClass{Fault{PinRef{g, PinKind::Output, 0}, StuckValue::SA0}, {}});

    const atpg::Result<SimResult> result = simulateFaults(graph, faults, patterns);
    INFO("gate type " << static_cast<int>(c.type));
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);

    // The first pattern whose good output is 1 is the first to expose SA0.
    std::size_t firstOne = patterns.size();
    for (std::size_t p = 0; p < 4; ++p) {
      if (c.expected[p]) {
        firstOne = p;
        break;
      }
    }
    const FaultStatus& status = *result.value().begin();
    REQUIRE(firstOne < patterns.size()); // every type above outputs 1 somewhere
    CHECK(status.detected == true);
    CHECK(status.firstDetectingPattern == firstOne);
  }
}

TEST_CASE("a unary gate's fault is detected on the expected pattern", "[FaultSim]") {
  // Buf and Not, wired PI -> g -> PO. output/SA0 is exposed by the pattern
  // whose good output is 1: a=1 for Buf, a=0 for Not.
  struct Case {
    GateType type;
    std::size_t firstDetecting; // index into {{false}, {true}}
  };
  const Case cases[] = {{GateType::Buf, 1}, {GateType::Not, 0}};

  const std::vector<std::vector<bool>> patterns = {{false}, {true}};

  for (const Case& c : cases) {
    Graph graph;
    const GateId a = graph.addGate(GateType::Pi, "a");
    const GateId g = graph.addGate(c.type, "g");
    const GateId y = graph.addGate(GateType::Po, "y");
    graph.addEdge(a, g);
    graph.addEdge(g, y);
    REQUIRE(graph.levelize().ok());

    FaultList faults;
    faults.add(FaultClass{Fault{PinRef{g, PinKind::Output, 0}, StuckValue::SA0}, {}});

    const atpg::Result<SimResult> result = simulateFaults(graph, faults, patterns);
    INFO("gate type " << static_cast<int>(c.type));
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    CHECK(result.value().begin()->detected == true);
    CHECK(result.value().begin()->firstDetectingPattern == c.firstDetecting);
  }
}

TEST_CASE("a half adder's full fault list is fully covered by exhaustive patterns", "[FaultSim]") {
  // sum = a XOR b, cout = a AND b. Both a and b are fanout-2 stems whose
  // cones reach both primary outputs, so this exercises multi-output
  // detection and a cone wider than one gate. All four input patterns are
  // applied, which is exhaustive for 2 inputs, so every testable fault must
  // be detected.
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId b = graph.addGate(GateType::Pi, "b");
  const GateId sumGate = graph.addGate(GateType::Xor, "sumGate");
  const GateId coutGate = graph.addGate(GateType::And, "coutGate");
  const GateId sum = graph.addGate(GateType::Po, "sum");
  const GateId cout = graph.addGate(GateType::Po, "cout");
  graph.addEdge(a, sumGate);
  graph.addEdge(b, sumGate);
  graph.addEdge(a, coutGate);
  graph.addEdge(b, coutGate);
  graph.addEdge(sumGate, sum);
  graph.addEdge(coutGate, cout);
  REQUIRE(graph.levelize().ok());

  const FaultList faults = generateFaultList(graph);
  const std::vector<std::vector<bool>> patterns = {
      {false, false}, {false, true}, {true, false}, {true, true}};

  const atpg::Result<SimResult> result = simulateFaults(graph, faults, patterns);
  REQUIRE(result.ok());
  REQUIRE(result.value().size() == faults.size());
  for (const FaultStatus& status : result.value()) {
    INFO("fault on gate " << status.fault.pin.gate);
    CHECK(status.detected == true);
  }
  CHECK(result.value().coverage() == 1.0);
}

TEST_CASE("simulateFaults rejects a pattern of the wrong width", "[FaultSim]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId g = graph.addGate(GateType::Buf, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{g, PinKind::Output, 0}, StuckValue::SA0}, {}});

  const std::vector<std::vector<bool>> patterns = {{true, false}}; // 2 bits, 1 PI
  CHECK_FALSE(simulateFaults(graph, faults, patterns).ok());
}

TEST_CASE("an empty pattern set leaves every fault undetected", "[FaultSim]") {
  Graph graph;
  const GateId a = graph.addGate(GateType::Pi, "a");
  const GateId g = graph.addGate(GateType::Buf, "g");
  const GateId y = graph.addGate(GateType::Po, "y");
  graph.addEdge(a, g);
  graph.addEdge(g, y);
  REQUIRE(graph.levelize().ok());

  FaultList faults;
  faults.add(FaultClass{Fault{PinRef{g, PinKind::Output, 0}, StuckValue::SA0}, {}});

  const atpg::Result<SimResult> result = simulateFaults(graph, faults, {});
  REQUIRE(result.ok());
  REQUIRE(result.value().size() == 1);
  CHECK(result.value().begin()->detected == false);
}

namespace {

// a -> g (Buf) -> y, with g's output stuck at 0: detected by exactly those
// patterns where a == 1, which makes the expected detection set trivial to
// state for any pattern count.
struct BufFixture {
  Graph graph;
  GateId a = 0;
  GateId g = 0;
  FaultList faults;
};

BufFixture makeBufFixture() {
  BufFixture fixture;
  fixture.a = fixture.graph.addGate(GateType::Pi, "a");
  fixture.g = fixture.graph.addGate(GateType::Buf, "g");
  const GateId y = fixture.graph.addGate(GateType::Po, "y");
  fixture.graph.addEdge(fixture.a, fixture.g);
  fixture.graph.addEdge(fixture.g, y);
  REQUIRE(fixture.graph.levelize().ok());
  fixture.faults.add(FaultClass{Fault{PinRef{fixture.g, PinKind::Output, 0}, StuckValue::SA0}, {}});
  return fixture;
}

} // namespace

TEST_CASE("detection is reported at the correct index across packet boundaries", "[FaultSim]") {
  // For each total pattern count, put the single a==1 pattern at a position
  // chosen to sit just before, on, or just after a 64-pattern boundary.
  struct Case {
    std::size_t total;
    std::size_t detectingIndex;
  };
  const Case cases[] = {
      {1, 0},   {63, 62},   {64, 63},   {64, 0},   {65, 64},
      {65, 63}, {128, 127}, {129, 128}, {129, 64}, {129, 0},
  };

  for (const Case& c : cases) {
    BufFixture fixture = makeBufFixture();
    std::vector<std::vector<bool>> patterns(c.total, std::vector<bool>{false});
    patterns[c.detectingIndex] = {true};

    const atpg::Result<SimResult> result = simulateFaults(fixture.graph, fixture.faults, patterns);
    INFO("total=" << c.total << " detectingIndex=" << c.detectingIndex);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    CHECK(result.value().begin()->detected == true);
    CHECK(result.value().begin()->firstDetectingPattern == c.detectingIndex);
  }
}

TEST_CASE("padding lanes in a partial final packet do not report detections", "[FaultSim]") {
  // Pattern counts that leave a partially filled final packet, with no real
  // pattern detecting the fault.
  //
  // The fault must be SA1 here, not SA0: unused lanes hold zeros, so a
  // stuck-at-0 agrees with the good circuit even in padding, and the test
  // could not tell a broken active-pattern mask from a working one. With
  // SA1, padding lanes have good == 0 and faulty == 1, so a mask that fails
  // to exclude them reports a detection no real pattern produced.
  for (const std::size_t total :
       {std::size_t{1}, std::size_t{63}, std::size_t{65}, std::size_t{100}, std::size_t{129}}) {
    Graph graph;
    const GateId a = graph.addGate(GateType::Pi, "a");
    const GateId g = graph.addGate(GateType::Buf, "g");
    const GateId y = graph.addGate(GateType::Po, "y");
    graph.addEdge(a, g);
    graph.addEdge(g, y);
    REQUIRE(graph.levelize().ok());

    FaultList faults;
    faults.add(FaultClass{Fault{PinRef{g, PinKind::Output, 0}, StuckValue::SA1}, {}});

    // Every real pattern drives a=1, so the good output is already 1 and
    // stuck-at-1 is invisible on all of them.
    const std::vector<std::vector<bool>> patterns(total, std::vector<bool>{true});

    const atpg::Result<SimResult> result = simulateFaults(graph, faults, patterns);
    INFO("total=" << total);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1);
    CHECK(result.value().begin()->detected == false);
  }
}

TEST_CASE("the first detecting pattern is reported, not a later one", "[FaultSim]") {
  // Several patterns detect; only the earliest index may be reported, and it
  // must be found even when earlier packets contain no detection at all.
  BufFixture fixture = makeBufFixture();
  std::vector<std::vector<bool>> patterns(200, std::vector<bool>{false});
  patterns[70] = {true};
  patterns[71] = {true};
  patterns[150] = {true};

  const atpg::Result<SimResult> result = simulateFaults(fixture.graph, fixture.faults, patterns);
  REQUIRE(result.ok());
  REQUIRE(result.value().size() == 1);
  CHECK(result.value().begin()->detected == true);
  CHECK(result.value().begin()->firstDetectingPattern == 70);
}
