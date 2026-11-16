cmake_minimum_required(VERSION 3.25)

project(ki_subproject_check LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_subdirectory("${KI_SOURCE_DIR}" ki)

if(TARGET ki_tests OR TARGET ki_integration_tests)
  message(FATAL_ERROR "ki built its test suite while used as a subproject")
endif()

add_executable(subproject_check "${KI_SOURCE_DIR}/tests/consumer/main.cpp")
target_link_libraries(subproject_check PRIVATE ki::ki)
