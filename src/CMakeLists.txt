add_library(
  atpg-core STATIC
  ir/Gate.cpp
  ir/Graph.cpp
  frontend/Frontend.cpp
  sim/LogicSim.cpp)

target_include_directories(atpg-core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(atpg-core PUBLIC slang::slang PRIVATE fmt::fmt)

add_executable(atpg cli/main.cpp)
target_link_libraries(atpg PRIVATE atpg-core slang::slang fmt::fmt CLI11::CLI11)
