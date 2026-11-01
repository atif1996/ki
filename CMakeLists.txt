cmake_minimum_required(VERSION 3.25)

project(ki
  VERSION 0.1.0
  DESCRIPTION "chi-style router and Go net/http-style HTTP server for C++23 coroutines"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(cmake/KiWarnings.cmake)

option(KI_BUILD_TESTS "Build the ki test suite" ${PROJECT_IS_TOP_LEVEL})
option(KI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(KI_CLANG_TIDY "Run clang-tidy on the ki library sources" OFF)

if(PROJECT_IS_TOP_LEVEL)
  # Shared-library tests on Windows find ki.dll only when it sits next to the executables.
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

find_package(asio CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

add_library(ki)
add_library(ki::ki ALIAS ki)

generate_export_header(ki EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/include/ki/export.hpp")
configure_file(cmake/version.hpp.in "${PROJECT_BINARY_DIR}/include/ki/version.hpp" @ONLY)

target_sources(ki
  PRIVATE
    src/headers.cpp
    src/helpers.cpp
    src/host_router.cpp
    src/log.cpp
    src/query.cpp
    src/request.cpp
    src/response.cpp
    src/router.cpp
    src/status.cpp
    src/test/recorder.cpp
    src/tree.cpp
    src/url.cpp
    src/version.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include "${PROJECT_BINARY_DIR}/include"
    FILES
      include/ki/context.hpp
      include/ki/error.hpp
      include/ki/handler.hpp
      include/ki/headers.hpp
      include/ki/helpers.hpp
      include/ki/ki.hpp
      include/ki/log.hpp
      include/ki/query.hpp
      include/ki/request.hpp
      include/ki/response.hpp
      include/ki/route_context.hpp
      include/ki/router.hpp
      include/ki/status.hpp
      include/ki/test/recorder.hpp
      include/ki/transport.hpp
      include/ki/url.hpp
      "${PROJECT_BINARY_DIR}/include/ki/export.hpp"
      "${PROJECT_BINARY_DIR}/include/ki/version.hpp")

target_include_directories(ki PRIVATE src)
target_compile_features(ki PUBLIC cxx_std_23)
target_link_libraries(ki PUBLIC asio::asio PRIVATE llhttp::llhttp re2::re2)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(ki PUBLIC KI_STATIC_DEFINE)
endif()

if(WIN32)
  # 0x0A00 is Windows 10; without it Asio guesses a target version and warns.
  target_compile_definitions(ki PUBLIC _WIN32_WINNT=0x0A00)
  target_link_libraries(ki PUBLIC ws2_32 mswsock)
endif()

set_target_properties(ki PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  VERSION ${PROJECT_VERSION}
  # Before 1.0 a minor release may break the ABI.
  SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})

ki_target_warnings(ki)

if(KI_CLANG_TIDY)
  find_program(KI_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
  set_target_properties(ki PROPERTIES CXX_CLANG_TIDY "${KI_CLANG_TIDY_EXE}")
endif()

install(TARGETS ki EXPORT ki-targets FILE_SET HEADERS)
install(EXPORT ki-targets NAMESPACE ki:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki")
configure_package_config_file(cmake/ki-config.cmake.in "${PROJECT_BINARY_DIR}/ki-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki")
write_basic_package_version_file("${PROJECT_BINARY_DIR}/ki-config-version.cmake"
  COMPATIBILITY SameMinorVersion)
install(FILES "${PROJECT_BINARY_DIR}/ki-config.cmake" "${PROJECT_BINARY_DIR}/ki-config-version.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki")

if(KI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
