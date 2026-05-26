cmake_minimum_required(VERSION 3.21)

project(vsqlitepp_install_consumer LANGUAGES CXX)

find_package(vsqlitepp CONFIG REQUIRED)

add_executable(vsqlitepp_install_consumer ../main.cpp)
target_compile_features(vsqlitepp_install_consumer PRIVATE cxx_std_20)
target_link_libraries(vsqlitepp_install_consumer PRIVATE vsqlite::vsqlitepp)
