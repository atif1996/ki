# Tests that are not tied to one source file. Unit tests live next to the code they test, as <name>.test.cpp.
# tests/consumer is a separate project, built against an installed package.

# conformance: chi's and Go's router test suites, ported.
add_executable(ki_conformance_tests
  conformance/chi_mux.test.cpp
  conformance/chi_tree.test.cpp
  conformance/go_servemux.test.cpp)
target_include_directories(ki_conformance_tests PRIVATE support)
target_link_libraries(ki_conformance_tests PRIVATE ki::ki ki::test Catch2::Catch2WithMain)
ki_target_warnings(ki_conformance_tests)
catch_discover_tests(ki_conformance_tests DISCOVERY_MODE PRE_TEST)

# Always shared: the context test needs a library boundary between where a value is set and where it is read.
add_library(ki_context_probe SHARED integration/shared_library_context/probe.cpp)
generate_export_header(ki_context_probe BASE_NAME ki_probe
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/ki_probe_export.hpp")
target_include_directories(ki_context_probe PUBLIC integration/shared_library_context "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ki_context_probe PUBLIC ki::ki)
set_target_properties(ki_context_probe PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
ki_target_warnings(ki_context_probe)

# integration: several libraries together through their public headers.
add_executable(ki_integration_tests
  integration/form.test.cpp
  integration/readme.test.cpp
  integration/router_api.test.cpp
  integration/shared_library_context/context.test.cpp
  integration/toolchain.test.cpp)
target_include_directories(ki_integration_tests PRIVATE support)
target_link_libraries(ki_integration_tests PRIVATE
  ki::ki ki::test ki_context_probe llhttp::llhttp Catch2::Catch2WithMain)
ki_target_warnings(ki_integration_tests)
catch_discover_tests(ki_integration_tests DISCOVERY_MODE PRE_TEST)

# modules: the libraries used through import alone.
if(KI_MODULES_ENABLED)
  add_executable(ki_module_tests modules/import.test.cpp)
  target_link_libraries(ki_module_tests PRIVATE ki::ki_module ki::log_module ki::test_module Catch2::Catch2WithMain)
  ki_target_warnings(ki_module_tests)
  catch_discover_tests(ki_module_tests DISCOVERY_MODE PRE_TEST)
endif()
