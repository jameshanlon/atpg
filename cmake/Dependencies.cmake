include(FetchContent)

# fmt - resolved here, before slang, so the resulting fmt::fmt target is
# visible project-wide. Targets that find_package()/FetchContent create
# inside a subdirectory are only visible to that subdirectory and its
# children, and slang resolves its own copy of fmt from within its own
# external/ subdirectory - too narrow a scope for atpg's own targets to see.
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 12.2.0
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS 12.1)
FetchContent_MakeAvailable(fmt)

# slang - SystemVerilog frontend. Tools/tests/mimalloc are disabled to keep
# the foundation build fast; none of them are needed by atpg.
set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  slang
  GIT_REPOSITORY https://github.com/MikePopoloski/slang.git
  GIT_TAG v11.0
  GIT_SHALLOW TRUE)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.15.3
  GIT_SHALLOW TRUE)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.7.2
  GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(slang Catch2 CLI11)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

# OR-Tools - CP-SAT solver, required by atpg::gen. NOT fetched via
# FetchContent: building it from source pulls in ~14 transitive C++
# dependencies and can take 15-45+ minutes on a clean machine, unlike every
# other dependency here. Install a prebuilt package instead. Its own CMake
# config shells out to pkg-config for a few of its dependencies, so
# pkg-config itself must be installed too, not just or-tools.
find_package(ortools CONFIG QUIET)
if(NOT ortools_FOUND)
  message(FATAL_ERROR
    "OR-Tools not found. Install a prebuilt package before configuring atpg:\n"
    "  macOS:  brew install or-tools pkg-config\n"
    "  other:  see https://developers.google.com/optimization/install/cpp\n")
endif()
