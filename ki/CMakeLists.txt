configure_file("${PROJECT_SOURCE_DIR}/cmake/version.hpp.in" "${PROJECT_BINARY_DIR}/ki/version.hpp" @ONLY)

# The main library: the HTTP types and the router. Sub-directories with their own CMakeLists.txt are
# separate libraries; detail/ holds this library's private headers and sources.
ki_add_library(ki
  SOURCES
    detail/tree.cpp
    headers.cpp
    host_router.cpp
    query.cpp
    redirect.cpp
    request.cpp
    response.cpp
    route_context.cpp
    router.cpp
    status.cpp
    strip_prefix.cpp
    url.cpp
    version.cpp
  HEADERS
    context.hpp
    error.hpp
    handler.hpp
    headers.hpp
    ki.hpp
    query.hpp
    redirect.hpp
    request.hpp
    response.hpp
    route_context.hpp
    router.hpp
    status.hpp
    strip_prefix.hpp
    transport.hpp
    url.hpp
  MODULE ki.cppm
  TESTS
    context.test.cpp
    detail/strings.test.cpp
    headers.test.cpp
    query.test.cpp
    response.test.cpp
    url.test.cpp
    version.test.cpp
  PUBLIC_DEPS asio::asio
  PRIVATE_DEPS re2::re2
  TEST_DEPS ki::test)

target_sources(ki PUBLIC
  FILE_SET generated_headers TYPE HEADERS FILES "${PROJECT_BINARY_DIR}/ki/version.hpp")

if(WIN32)
  # 0x0A00 is Windows 10; without it Asio guesses a target version and warns.
  target_compile_definitions(ki PUBLIC _WIN32_WINNT=0x0A00)
  target_link_libraries(ki PUBLIC ws2_32 mswsock)
endif()

add_subdirectory(log)
add_subdirectory(test)
