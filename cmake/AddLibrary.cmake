# ki_add_library(<name> SOURCES <files> HEADERS <files> [MODULE <file>]
#                [TESTS <files>] [PUBLIC_DEPS <targets>] [PRIVATE_DEPS <targets>] [TEST_DEPS <targets>])
#
# Adds the library in ki/<name> as the target ki::<name>, with an export header at <ki/<name>/export.hpp>,
# C++23 required of its consumers, and install rules. The name "ki" is the main library: it lives in ki/
# itself, its target is ki::ki and its export header is <ki/export.hpp>. HEADERS are the public headers;
# other headers in the directory stay private and are not installed. When modules are on (cmake/Modules.cmake),
# MODULE is built into ki::<name>_module, a static library linking ki::<name>, so a library's own users never
# depend on module support. When tests are enabled, TESTS are built into one executable
# and registered with CTest, and every public header is compiled alone to prove it includes what it uses.
# Paths are relative to the calling directory.
function(ki_add_library name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "MODULE" "SOURCES;HEADERS;TESTS;PUBLIC_DEPS;PRIVATE_DEPS;TEST_DEPS")
  if(name STREQUAL "ki")
    set(target ki)
    set(include_dir ki)
  else()
    set(target ki_${name})
    set(include_dir ki/${name})
  endif()
  string(TOUPPER ${target} macro_prefix)
  set(export_header "${PROJECT_BINARY_DIR}/${include_dir}/export.hpp")

  add_library(${target})
  add_library(ki::${name} ALIAS ${target})
  generate_export_header(${target} BASE_NAME ${target} EXPORT_FILE_NAME "${export_header}")

  # Generated headers get their own file set: the build directory is usually inside the source directory,
  # and one file set cannot have a base directory nested in another.
  target_sources(${target}
    PRIVATE
      ${arg_SOURCES}
    PUBLIC
      FILE_SET HEADERS
      BASE_DIRS "${PROJECT_SOURCE_DIR}"
      FILES ${arg_HEADERS}
    PUBLIC
      FILE_SET generated_headers
      TYPE HEADERS
      BASE_DIRS "${PROJECT_BINARY_DIR}"
      FILES "${export_header}")

  target_compile_features(${target} PUBLIC cxx_std_23)
  target_link_libraries(${target} PUBLIC ${arg_PUBLIC_DEPS} PRIVATE ${arg_PRIVATE_DEPS})
  if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${target} PUBLIC ${macro_prefix}_STATIC_DEFINE)
  endif()
  set_target_properties(${target} PROPERTIES
    EXPORT_NAME ${name}
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    # Before 1.0 a minor release may break the ABI.
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
  ki_target_warnings(${target})
  if(KI_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${KI_CLANG_TIDY_EXE}")
  endif()

  install(TARGETS ${target} EXPORT ki-targets FILE_SET HEADERS FILE_SET generated_headers)

  if(KI_MODULES_ENABLED AND arg_MODULE)
    add_library(${target}_module STATIC)
    add_library(ki::${name}_module ALIAS ${target}_module)
    target_sources(${target}_module PUBLIC
      FILE_SET CXX_MODULES BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES ${arg_MODULE})
    target_link_libraries(${target}_module PUBLIC ki::${name})
    target_compile_features(${target}_module PUBLIC cxx_std_23)
    set_target_properties(${target}_module PROPERTIES
      EXPORT_NAME ${name}_module
      CXX_EXTENSIONS OFF
      POSITION_INDEPENDENT_CODE ON)
    # No warning flags: they would be exported with the module and imposed on consumers' builds of it.
    install(TARGETS ${target}_module EXPORT ki-module-targets
      FILE_SET CXX_MODULES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  endif()

  if(NOT KI_BUILD_TESTS)
    return()
  endif()

  if(arg_TESTS)
    add_executable(${target}_tests ${arg_TESTS})
    target_link_libraries(${target}_tests PRIVATE ki::${name} ${arg_TEST_DEPS} Catch2::Catch2WithMain)
    ki_target_warnings(${target}_tests)
    catch_discover_tests(${target}_tests DISCOVERY_MODE PRE_TEST)
  endif()

  set(header_checks "")
  foreach(header IN LISTS arg_HEADERS)
    string(MAKE_C_IDENTIFIER "${header}" header_id)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/header_check/${header_id}.cpp")
    file(CONFIGURE OUTPUT "${source}" CONTENT "#include <${include_dir}/${header}>\n")
    list(APPEND header_checks "${source}")
  endforeach()
  add_library(${target}_header_check OBJECT ${header_checks})
  target_link_libraries(${target}_header_check PRIVATE ki::${name})
  ki_target_warnings(${target}_header_check)
endfunction()
