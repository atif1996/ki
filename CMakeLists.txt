# The upper bound adopts newer policies where available, such as de-duplicating libraries on the link line.
cmake_minimum_required(VERSION 3.25...4.0)

project(ki
  VERSION 0.1.0
  DESCRIPTION "chi-style router and Go net/http-style HTTP server for C++23 coroutines"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(cmake/Warnings.cmake)
include(cmake/AddLibrary.cmake)
include(cmake/Modules.cmake)

option(KI_BUILD_TESTS "Build the unit, conformance and integration tests" ${PROJECT_IS_TOP_LEVEL})
option(KI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(KI_CLANG_TIDY "Run clang-tidy on the libraries" OFF)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  # Shared-library tests on Windows find the DLLs only when they sit next to the executables.
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

if(KI_CLANG_TIDY)
  find_program(KI_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

find_package(asio CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
ki_detect_modules(KI_MODULES_ENABLED)

if(KI_BUILD_TESTS)
  enable_testing()
  find_package(Catch2 3 CONFIG REQUIRED)
  # No library uses llhttp until the server arrives; a test keeps the toolchain proven.
  find_package(llhttp CONFIG REQUIRED)
  include(Catch)
endif()

add_subdirectory(ki)

if(KI_BUILD_TESTS)
  add_subdirectory(tests)
endif()

install(EXPORT ki-targets NAMESPACE ki:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki")
configure_package_config_file(cmake/ki-config.cmake.in "${PROJECT_BINARY_DIR}/ki-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki")
write_basic_package_version_file("${PROJECT_BINARY_DIR}/ki-config-version.cmake"
  COMPATIBILITY SameMinorVersion)
install(FILES "${PROJECT_BINARY_DIR}/ki-config.cmake" "${PROJECT_BINARY_DIR}/ki-config-version.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki")
if(KI_MODULES_ENABLED)
  install(EXPORT ki-module-targets NAMESPACE ki:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki"
    CXX_MODULES_DIRECTORY modules)
endif()
