find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# Always shared: T1.5 needs a library boundary between where a context value is set and where it is read.
add_library(ki-context-probe SHARED context_probe/probe.cpp)
generate_export_header(ki-context-probe BASE_NAME ki_probe
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/ki_probe_export.hpp")
target_include_directories(ki-context-probe PUBLIC context_probe "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ki-context-probe PUBLIC ki::ki)
set_target_properties(ki-context-probe PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
ki_target_warnings(ki-context-probe)

add_executable(ki-tests
  context_test.cpp
  form_test.cpp
  headers_test.cpp
  query_test.cpp
  readme_test.cpp
  response_test.cpp
  strings_test.cpp
  toolchain_test.cpp
  url_test.cpp
  version_test.cpp)
target_link_libraries(ki-tests PRIVATE ki::ki ki-context-probe llhttp::llhttp Catch2::Catch2WithMain)
target_include_directories(ki-tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
ki_target_warnings(ki-tests)
catch_discover_tests(ki-tests DISCOVERY_MODE PRE_TEST)

# T0.11: every public header compiles alone in its own translation unit.
file(GLOB_RECURSE ki_public_headers RELATIVE "${PROJECT_SOURCE_DIR}/include" CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/include/ki/*.hpp")
set(ki_header_check_sources "")
foreach(header IN LISTS ki_public_headers)
  string(MAKE_C_IDENTIFIER "${header}" header_id)
  set(source "${CMAKE_CURRENT_BINARY_DIR}/header_check/${header_id}.cpp")
  file(CONFIGURE OUTPUT "${source}" CONTENT "#include <${header}>\n")
  list(APPEND ki_header_check_sources "${source}")
endforeach()
add_library(ki-header-check OBJECT ${ki_header_check_sources})
target_link_libraries(ki-header-check PRIVATE ki::ki)
ki_target_warnings(ki-header-check)
