cmake_minimum_required(VERSION 3.21)

project(vsqlitepp_cpm_consumer LANGUAGES CXX)

include("${CPM_SOURCE_CACHE}/CPM.cmake")

CPMAddPackage(
  NAME vsqlitepp
  SOURCE_DIR "${VSQLITEPP_SOURCE_DIR}"
)

add_executable(vsqlitepp_cpm_consumer ../main.cpp)
target_compile_features(vsqlitepp_cpm_consumer PRIVATE cxx_std_20)
target_link_libraries(vsqlitepp_cpm_consumer PRIVATE vsqlite::vsqlitepp)
