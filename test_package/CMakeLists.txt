cmake_minimum_required(VERSION 3.21)
project(vsqlitepp_test_package LANGUAGES CXX)

find_package(vsqlitepp CONFIG REQUIRED)

add_executable(test_package test_package.cpp)
target_link_libraries(test_package PRIVATE vsqlite::vsqlitepp)
