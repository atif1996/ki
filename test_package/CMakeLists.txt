cmake_minimum_required(VERSION 3.25)

project(ki_test_package LANGUAGES CXX)

find_package(ki CONFIG REQUIRED)

add_executable(test_package main.cpp)
target_link_libraries(test_package PRIVATE ki::ki)
