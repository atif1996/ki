cmake_minimum_required(VERSION 3.25)

project(ki_consumer LANGUAGES CXX)

find_package(ki CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE ki::ki)
