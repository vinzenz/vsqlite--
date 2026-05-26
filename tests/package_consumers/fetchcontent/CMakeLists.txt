cmake_minimum_required(VERSION 3.21)

project(vsqlitepp_fetchcontent_consumer LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
  vsqlitepp
  SOURCE_DIR "${VSQLITEPP_SOURCE_DIR}"
)
FetchContent_MakeAvailable(vsqlitepp)

add_executable(vsqlitepp_fetchcontent_consumer ../main.cpp)
target_compile_features(vsqlitepp_fetchcontent_consumer PRIVATE cxx_std_20)
target_link_libraries(vsqlitepp_fetchcontent_consumer PRIVATE vsqlite::vsqlitepp)
