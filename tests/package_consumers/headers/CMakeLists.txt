cmake_minimum_required(VERSION 3.21)

project(vsqlitepp_header_consumers LANGUAGES CXX)

find_package(vsqlitepp CONFIG REQUIRED)

# Every installed public header must compile on its own, in its own translation
# unit, using only what the installed package declares. Regression coverage for
# headers that sneak in includes of files which are not installed.

get_target_property(_vsqlitepp_include_dirs vsqlite::vsqlitepp
                    INTERFACE_INCLUDE_DIRECTORIES)

set(_vsqlitepp_include_dir "")
foreach(_dir IN LISTS _vsqlitepp_include_dirs)
  if(EXISTS "${_dir}/sqlite/function.hpp")
    set(_vsqlitepp_include_dir "${_dir}")
    break()
  endif()
endforeach()

if(NOT _vsqlitepp_include_dir)
  message(FATAL_ERROR "Could not locate installed sqlite headers via "
                      "INTERFACE_INCLUDE_DIRECTORIES of vsqlite::vsqlitepp")
endif()

file(GLOB_RECURSE _vsqlitepp_headers
     RELATIVE "${_vsqlitepp_include_dir}"
     "${_vsqlitepp_include_dir}/*.hpp")

if(NOT _vsqlitepp_headers)
  message(FATAL_ERROR "No headers found below ${_vsqlitepp_include_dir}")
endif()

enable_testing()

foreach(_header IN LISTS _vsqlitepp_headers)
  string(REPLACE "/" "_" _name "${_header}")
  string(REGEX REPLACE "\\.hpp$" "" _name "${_name}")
  set(_target "vsqlitepp_header_${_name}")

  set(_source "${CMAKE_CURRENT_BINARY_DIR}/generated/${_target}.cpp")
  file(WRITE "${_source}" "#include <${_header}>

int main() {
  return 0;
}
")

  add_executable(${_target} "${_source}")
  target_compile_features(${_target} PRIVATE cxx_std_20)
  target_link_libraries(${_target} PRIVATE vsqlite::vsqlitepp)
  add_test(NAME ${_target} COMMAND ${_target})
endforeach()
