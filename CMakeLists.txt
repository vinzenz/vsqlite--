cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VSQLITEPP_VERSION_RAW)
string(STRIP "${VSQLITEPP_VERSION_RAW}" VSQLITEPP_VERSION)

project(VSQLite++
  VERSION ${VSQLITEPP_VERSION}
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(VSQLITE_BUILD_EXAMPLES "Build the sqlite wrapper example" ON)
include(CTest)

option(VSQLITE_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})
option(VSQLITE_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

set(VSQLITEPP_SOVERSION "${PROJECT_VERSION_MAJOR}" CACHE STRING "Shared object version to use")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(SQLite3 REQUIRED)

if(VSQLITE_ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
  else()
    message(WARNING "Coverage instrumentation requested but not supported for ${CMAKE_CXX_COMPILER_ID}.")
  endif()
endif()

add_library(vsqlitepp)
add_library(vsqlite::vsqlitepp ALIAS vsqlitepp)

target_sources(vsqlitepp
  PRIVATE
    src/sqlite/backup.cpp
    src/sqlite/command.cpp
    src/sqlite/connection.cpp
    src/sqlite/execute.cpp
  src/sqlite/query.cpp
  src/sqlite/result.cpp
  src/sqlite/savepoint.cpp
  src/sqlite/transaction.cpp
  src/sqlite/view.cpp
  src/sqlite/threading.cpp
  src/sqlite/connection_pool.cpp
  src/sqlite/snapshot.cpp
  src/sqlite/session.cpp
  src/sqlite/serialization.cpp
  src/sqlite/json_fts.cpp
  src/sqlite/statement_cache.cpp
)

target_include_directories(vsqlitepp
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(vsqlitepp PUBLIC cxx_std_20)

target_compile_options(vsqlitepp
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive- /W4>
)

if(TARGET SQLite::SQLite3)
  set(VSQLITEPP_SQLITE_TARGET SQLite::SQLite3)
else()
  set(VSQLITEPP_SQLITE_TARGET ${SQLITE3_LIBRARIES})
  target_include_directories(vsqlitepp PUBLIC ${SQLITE3_INCLUDE_DIRS})
endif()

target_link_libraries(vsqlitepp PUBLIC ${VSQLITEPP_SQLITE_TARGET})

set_target_properties(vsqlitepp
  PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${VSQLITEPP_SOVERSION}
    EXPORT_NAME vsqlitepp
)

if(VSQLITE_BUILD_EXAMPLES)
  add_executable(vsqlitepp_example examples/sqlite_wrapper.cpp)
  target_link_libraries(vsqlitepp_example PRIVATE vsqlite::vsqlitepp)
  install(TARGETS vsqlitepp_example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS vsqlitepp
  EXPORT vsqliteppTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.hpp"
  PATTERN "private" EXCLUDE
)

install(FILES NEWS COPYING README README.md VERSION TODO ChangeLog AUTHORS
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

set(VSQLITEPP_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/vsqlitepp" CACHE STRING "Directory for CMake package configuration files")

install(EXPORT vsqliteppTargets
  FILE vsqliteppTargets.cmake
  NAMESPACE vsqlite::
  DESTINATION "${VSQLITEPP_INSTALL_CMAKEDIR}"
)

configure_package_config_file(
  cmake/vsqliteppConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/vsqliteppConfig.cmake"
  INSTALL_DESTINATION "${VSQLITEPP_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/vsqliteppConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/vsqliteppConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/vsqliteppConfigVersion.cmake"
  DESTINATION "${VSQLITEPP_INSTALL_CMAKEDIR}"
)

if(VSQLITE_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)

  set(VSQLITE_TEST_SOURCES
    tests/test_backup.cpp
    tests/test_command_query.cpp
    tests/test_common.hpp
    tests/test_connection.cpp
    tests/test_connection_pool.cpp
    tests/test_function.cpp
    tests/test_json_fts.cpp
    tests/test_serialization.cpp
    tests/test_session.cpp
    tests/test_snapshot.cpp
    tests/test_statement_cache.cpp
    tests/test_threading.cpp
    tests/test_transaction.cpp
    tests/test_view.cpp
  )
  add_executable(vsqlitepp_tests
    ${VSQLITE_TEST_SOURCES}
  )
  target_link_libraries(vsqlitepp_tests
    PRIVATE
      vsqlite::vsqlitepp
      GTest::gtest_main
  )
  add_test(NAME vsqlitepp_tests COMMAND vsqlitepp_tests)
endif()

set(CPACK_PACKAGE_NAME "vsqlitepp")
set(CPACK_PACKAGE_VENDOR "virtuosic bytes")
set(CPACK_PACKAGE_CONTACT "Vinzenz Feenstra <vinzenz.feenstra@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "VSQLite++ - a portable SQLite3 wrapper for C++")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "TGZ;TXZ;ZIP")

include(CPack)
