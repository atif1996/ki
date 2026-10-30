find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(ki-tests
  toolchain_test.cpp
  version_test.cpp)
target_link_libraries(ki-tests PRIVATE ki::ki llhttp::llhttp Catch2::Catch2WithMain)
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
