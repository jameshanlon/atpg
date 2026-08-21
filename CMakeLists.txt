cmake_minimum_required(VERSION 3.21)

project(atpg CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      RelWithDebInfo
      CACHE STRING "Choose the type of build." FORCE)
endif()

option(ATPG_BUILD_TESTS "Build the atpg test suite" ${PROJECT_IS_TOP_LEVEL})
# Off by default: the docs target needs doxygen installed, and requiring it
# of everyone who builds the tool would be a poor trade. Use the `docs`
# preset, or -DATPG_BUILD_DOCS=ON.
option(ATPG_BUILD_DOCS "Build the Doxygen documentation" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_subdirectory(src)

if(ATPG_BUILD_DOCS)
  add_subdirectory(docs)
endif()

if(ATPG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
