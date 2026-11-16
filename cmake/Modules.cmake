# Each library can also be imported as a C++ module (import ki; import ki.log; import ki.test;) that re-exports
# its header API; the headers stay the definition of the API. KI_MODULES=AUTO builds the modules when this
# toolchain can, ON requires them, and OFF skips them.
set(KI_MODULES AUTO CACHE STRING "Build the C++ module interfaces: AUTO, ON or OFF")
set_property(CACHE KI_MODULES PROPERTY STRINGS AUTO ON OFF)

# ki_detect_modules(<out>) sets <out> to ON when the toolchain builds a module that exports an Asio coroutine,
# imported by a file that also includes Asio, which is how applications use ki. Compilers that list module
# support still fail that case (in November 2026, GCC 14 crashes and GCC 16.2 cannot read the module it wrote),
# so the check compiles it instead of trusting version numbers.
function(ki_detect_modules out)
  set(${out} OFF PARENT_SCOPE)
  if(KI_MODULES STREQUAL "OFF")
    message(STATUS "ki: C++ modules off (KI_MODULES=OFF)")
    return()
  endif()

  set(reason "")
  if(CMAKE_VERSION VERSION_LESS 3.28)
    set(reason "CMake ${CMAKE_VERSION} is older than 3.28")
  elseif(NOT CMAKE_GENERATOR MATCHES "^(Ninja|Visual Studio)")
    set(reason "the ${CMAKE_GENERATOR} generator cannot build modules; Ninja and Visual Studio can")
  elseif(NOT CMAKE_CXX_SCANDEP_SOURCE)
    # Internal to CMake, but set exactly for the compilers it can scan; a try_compile without it is a fatal
    # configure error rather than a failed check.
    set(reason "CMake cannot scan ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} sources for modules")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
    set(reason "clang-scan-deps was not found next to ${CMAKE_CXX_COMPILER}")
  else()
    if(CMAKE_BUILD_TYPE)
      # Package managers' imported targets often carry include paths for the build type only.
      set(CMAKE_TRY_COMPILE_CONFIGURATION ${CMAKE_BUILD_TYPE})
    endif()
    try_compile(KI_MODULES_COMPILE
      SOURCES_TYPE CXX_MODULE SOURCES "${PROJECT_SOURCE_DIR}/cmake/module_check/check.cppm"
      SOURCES_TYPE NORMAL SOURCES "${PROJECT_SOURCE_DIR}/cmake/module_check/main.cpp"
      LINK_LIBRARIES asio::asio
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
      LOG_DESCRIPTION "ki: build a module that exports an Asio coroutine")
    if(NOT KI_MODULES_COMPILE)
      set(reason "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} failed to build a module that exports an \
Asio coroutine (the attempt is in CMakeConfigureLog.yaml)")
    endif()
  endif()

  if(reason)
    if(KI_MODULES STREQUAL "ON")
      message(FATAL_ERROR "ki: KI_MODULES=ON, but ${reason}")
    endif()
    message(STATUS "ki: C++ modules off: ${reason}")
    return()
  endif()
  message(STATUS "ki: C++ modules on")
  set(${out} ON PARENT_SCOPE)
endfunction()
