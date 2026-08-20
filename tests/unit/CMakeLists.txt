add_executable(
  atpg-tests
  Test.hpp
  ir/GraphTests.cpp
  frontend/FrontendTests.cpp
  sim/LogicSimTests.cpp
  fault/FaultTests.cpp
  fault/FaultListPropertyTests.cpp
  gen/TestGenTests.cpp
  gen/TestGenPropertyTests.cpp
  gen/TestGenDroppingPropertyTests.cpp
  fsim/FaultSimTests.cpp
  fsim/FaultSimPropertyTests.cpp
  compact/CompactTests.cpp
  compact/CompactPropertyTests.cpp
  stil/StilReaderTests.cpp)

target_link_libraries(atpg-tests PRIVATE atpg-core slang::slang fmt::fmt ortools::ortools Catch2::Catch2WithMain)
target_include_directories(atpg-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(
  atpg-tests PRIVATE ATPG_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")

include(Catch)
catch_discover_tests(atpg-tests)
