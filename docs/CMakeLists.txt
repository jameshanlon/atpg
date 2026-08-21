# Doxyfile variables, substituted by configure_file below.
set(DOXYGEN_PROJECT_NAME atpg)
set(DOXYGEN_PROJECT_VERSION ${atpg_VERSION})
set(DOXYGEN_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/doxygen")
set(DOXYGEN_AWESOME_DIR "${doxygen-awesome-css_SOURCE_DIR}")

configure_file(${CMAKE_CURRENT_LIST_DIR}/Doxyfile
               ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

find_program(DOXYGEN_EXECUTABLE doxygen REQUIRED)

add_custom_target(
  docs
  ${CMAKE_COMMAND} -E make_directory "${DOXYGEN_OUTPUT_DIRECTORY}"
  COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
  COMMAND echo "Docs written to: ${DOXYGEN_OUTPUT_DIRECTORY}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
