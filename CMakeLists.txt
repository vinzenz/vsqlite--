cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VSQLITEPP_VERSION_RAW)
string(STRIP "${VSQLITEPP_VERSION_RAW}" VSQLITEPP_VERSION)

project(VSQLite++
  VERSION ${VSQLITEPP_VERSION}
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(VSQLITE_ALLOW_FOLLOW_SYMLINKS "When on, allow SQLite to follow symlinks when opening databases" ON)

set(VSQLITE_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})

option(VSQLITE_BUILD_EXAMPLES "Build the sqlite wrapper example" ${VSQLITE_IS_TOP_LEVEL})
option(VSQLITE_ENABLE_INSTALL "Install VSQLite++ targets and package configuration" ${VSQLITE_IS_TOP_LEVEL})

if(VSQLITE_IS_TOP_LEVEL)
  include(CTest)
  set(VSQLITE_BUILD_TESTS_DEFAULT ${BUILD_TESTING})
else()
  set(VSQLITE_BUILD_TESTS_DEFAULT OFF)
endif()
option(VSQLITE_BUILD_TESTS "Build unit tests" ${VSQLITE_BUILD_TESTS_DEFAULT})
option(VSQLITE_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

set(VSQLITE_BUNDLED_SQLITE_DEFAULT OFF)
if(VSQLITE_BUILD_TESTS)
  set(VSQLITE_BUNDLED_SQLITE_DEFAULT ON)
endif()
option(VSQLITE_BUNDLED_SQLITE "Build and link bundled SQLite3" ${VSQLITE_BUNDLED_SQLITE_DEFAULT})

set(VSQLITEPP_SOVERSION "${PROJECT_VERSION_MAJOR}" CACHE STRING "Shared object version to use")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(VSQLITE_BUNDLED_SQLITE)
  enable_language(C)
  include(FetchContent)
  FetchContent_Declare(
    sqlite3_amalgamation
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3510200.zip
    URL_HASH SHA3_256=9a9dd4eef7a97809bfacd84a7db5080a5c0eff7aaf1fc1aca20a6dc9a0c26f96
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(sqlite3_amalgamation)

  add_library(sqlite3 STATIC "${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.c")
  set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(sqlite3
    PUBLIC
      $<BUILD_INTERFACE:${sqlite3_amalgamation_SOURCE_DIR}>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_compile_definitions(sqlite3
    PRIVATE
      SQLITE_ENABLE_FTS5
      SQLITE_ENABLE_JSON1
      SQLITE_ENABLE_SESSION
      SQLITE_ENABLE_SNAPSHOT
      SQLITE_ENABLE_PREUPDATE_HOOK
      SQLITE_THREADSAFE=1
  )
  add_library(SQLite::SQLite3 ALIAS sqlite3)
  add_library(SQLite3::SQLite3 ALIAS sqlite3)
else()
  find_package(SQLite3 REQUIRED)
endif()

# Newer CMake releases provide SQLite3::SQLite3 and deprecate SQLite::SQLite3,
# older ones only provide the latter. Make both names resolve so the exported
# targets keep working on consumers using either CMake version.
if(NOT TARGET SQLite3::SQLite3 AND TARGET SQLite::SQLite3)
  add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()
if(NOT TARGET SQLite::SQLite3 AND TARGET SQLite3::SQLite3)
  add_library(SQLite::SQLite3 ALIAS SQLite3::SQLite3)
endif()

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
  src/sqlite/function.cpp
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
  src/sqlite/prepared_statement.cpp
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

# Link the SQLite target by name so exported targets stay relocatable:
# consumers re-resolve the dependency instead of keeping a build-machine path.
target_link_libraries(vsqlitepp PUBLIC SQLite3::SQLite3)

# Detection of the optional SQLite APIs used by the session, snapshot, and
# serialization wrappers.
#
# When the build system verified that the selected SQLite implementation provides
# an API group, the wrapper resolves those functions through direct references
# (VSQLITE_HAVE_SQLITE3_*), which work for static and shared linking alike. APIs
# that could not be confirmed fall back to a runtime lookup in the address space
# of the running process, so implementations that only expose them dynamically
# keep working.
include(CheckSourceCompiles)

# Links a probe program that references @p symbol_name against the selected SQLite
# library and stores a truthy result in @p out_var when the reference resolved.
function(vsqlitepp_link_check_symbol symbol_name out_var)
  set(CMAKE_REQUIRED_QUIET ON)
  set(CMAKE_REQUIRED_LIBRARIES SQLite3::SQLite3)
  check_source_compiles(CXX "
    extern \"C\" void ${symbol_name}();
    int main() { ${symbol_name}(); return 0; }
  " ${out_var})
endfunction()

if(VSQLITE_BUNDLED_SQLITE)
  # The bundled SQLite is compiled with all optional APIs enabled above.
  set(VSQLITEPP_HAVE_SQLITE3_SESSION TRUE)
  set(VSQLITEPP_HAVE_SQLITE3_SNAPSHOT TRUE)
  set(VSQLITEPP_HAVE_SQLITE3_SERIALIZE TRUE)
else()
  set(VSQLITEPP_HAVE_SQLITE3_SESSION FALSE)
  set(VSQLITEPP_HAVE_SQLITE3_SNAPSHOT FALSE)
  set(VSQLITEPP_HAVE_SQLITE3_SERIALIZE FALSE)
  # The probe results are cached; invalidate them when the selected SQLite
  # implementation changes so an existing build directory re-detects. The target
  # name is constant, so the identity includes the imported library location,
  # which does change when the underlying library is swapped.
  set(VSQLITEPP_SQLITE_IDENTITY "SQLite3::SQLite3")
  if(TARGET SQLite3::SQLite3)
    get_target_property(sqlite_imported_location SQLite3::SQLite3 IMPORTED_LOCATION)
    if(sqlite_imported_location)
      string(APPEND VSQLITEPP_SQLITE_IDENTITY "@" "${sqlite_imported_location}")
    endif()
  endif()
  if(NOT "${VSQLITEPP_LINKCHECK_TARGET}" STREQUAL "${VSQLITEPP_SQLITE_IDENTITY}")
    foreach(cached_check IN ITEMS
        VSQLITEPP_LINKCHECK_SANITY
        VSQLITEPP_LINKCHECK_SESSION
        VSQLITEPP_LINKCHECK_SNAPSHOT
        VSQLITEPP_LINKCHECK_SERIALIZE
      )
      unset(${cached_check} CACHE)
    endforeach()
    set(VSQLITEPP_LINKCHECK_TARGET "${VSQLITEPP_SQLITE_IDENTITY}"
      CACHE INTERNAL "SQLite implementation used by the last optional API probes" FORCE)
  endif()
  if(CMAKE_TRY_COMPILE_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
    # The probes cannot link a real program with this toolchain, so their
    # results would not prove anything. Keep the runtime-only detection.
    message(WARNING
      "Configure-time checks cannot verify symbol availability for this "
      "toolchain (try-compile builds static libraries only). Using runtime-only "
      "detection of the optional session, snapshot, and serialization APIs."
    )
  else()
    # Sanity check: the remaining checks only make sense when linking against the
    # selected SQLite library works at configure time at all. Otherwise keep the
    # pre-existing runtime-only detection instead of failing the configuration.
    vsqlitepp_link_check_symbol(sqlite3_open VSQLITEPP_LINKCHECK_SANITY)
    if(VSQLITEPP_LINKCHECK_SANITY)
      vsqlitepp_link_check_symbol(sqlite3session_create VSQLITEPP_LINKCHECK_SESSION)
      vsqlitepp_link_check_symbol(sqlite3_snapshot_get VSQLITEPP_LINKCHECK_SNAPSHOT)
      vsqlitepp_link_check_symbol(sqlite3_serialize VSQLITEPP_LINKCHECK_SERIALIZE)
      if(VSQLITEPP_LINKCHECK_SESSION)
        set(VSQLITEPP_HAVE_SQLITE3_SESSION TRUE)
      endif()
      if(VSQLITEPP_LINKCHECK_SNAPSHOT)
        set(VSQLITEPP_HAVE_SQLITE3_SNAPSHOT TRUE)
      endif()
      if(VSQLITEPP_LINKCHECK_SERIALIZE)
        set(VSQLITEPP_HAVE_SQLITE3_SERIALIZE TRUE)
      endif()
    else()
      message(WARNING
        "Could not link a configure-time check against the selected SQLite library. "
        "Falling back to runtime-only detection of the optional session, snapshot, and "
        "serialization APIs."
      )
    endif()
  endif()
endif()

foreach(optional_api IN ITEMS SESSION SNAPSHOT SERIALIZE)
  if(VSQLITEPP_HAVE_SQLITE3_${optional_api})
    target_compile_definitions(vsqlitepp PRIVATE VSQLITE_HAVE_SQLITE3_${optional_api}=1)
  endif()
endforeach()

message(STATUS "Optional SQLite APIs: session=${VSQLITEPP_HAVE_SQLITE3_SESSION}, snapshot=${VSQLITEPP_HAVE_SQLITE3_SNAPSHOT}, serialization=${VSQLITEPP_HAVE_SQLITE3_SERIALIZE}")

if(VSQLITEPP_HAVE_SQLITE3_SESSION)
  # sqlite3.h only declares the session API when these are defined; the pre-update
  # hook is the foundation of the session module.
  target_compile_definitions(vsqlitepp PRIVATE SQLITE_ENABLE_SESSION=1 SQLITE_ENABLE_PREUPDATE_HOOK=1)
endif()
if(VSQLITEPP_HAVE_SQLITE3_SNAPSHOT)
  target_compile_definitions(vsqlitepp PRIVATE SQLITE_ENABLE_SNAPSHOT=1)
endif()

set_target_properties(vsqlitepp
  PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${VSQLITEPP_SOVERSION}
    EXPORT_NAME vsqlitepp
)

if(VSQLITE_BUILD_EXAMPLES)
  add_executable(vsqlitepp_example examples/sqlite_wrapper.cpp)
  target_link_libraries(vsqlitepp_example PRIVATE vsqlite::vsqlitepp)
  add_executable(vsqlitepp_prepared_statement_example examples/prepared_statement.cpp)
  target_link_libraries(vsqlitepp_prepared_statement_example PRIVATE vsqlite::vsqlitepp)
  if(VSQLITE_ENABLE_INSTALL)
    install(TARGETS vsqlitepp_example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()

if(VSQLITE_ALLOW_FOLLOW_SYMLINKS)
  target_compile_definitions(vsqlitepp PRIVATE VSQLITE_ALLOW_FOLLOW_SYMLINKS=1)
endif()


if(VSQLITE_ENABLE_INSTALL)
  install(TARGETS vsqlitepp
    EXPORT vsqliteppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  if(VSQLITE_BUNDLED_SQLITE)
    install(TARGETS sqlite3
      EXPORT vsqliteppTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES
      "${sqlite3_amalgamation_SOURCE_DIR}/sqlite3.h"
      "${sqlite3_amalgamation_SOURCE_DIR}/sqlite3ext.h"
      DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
  endif()

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
endif()

if(VSQLITE_BUILD_TESTS)
  enable_testing()
  set(VSQLITE_OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  set(INSTALL_GTEST OFF)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)
  set(BUILD_SHARED_LIBS ${VSQLITE_OLD_BUILD_SHARED_LIBS})

  set(VSQLITE_TEST_SOURCES
    tests/test_backup.cpp
    tests/test_command_query.cpp
    tests/test_common.hpp
    tests/test_connection.cpp
    tests/test_connection_thread_safety.cpp
    tests/test_connection_pool.cpp
    tests/test_function.cpp
    tests/test_json_fts.cpp
    tests/test_prepared_statement.cpp
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

  # Micro-benchmark for the prepared_statement facade; not part of ctest.
  # Build and run it explicitly: cmake --build build --target vsqlitepp_benchmark_prepared_statement
  add_executable(vsqlitepp_benchmark_prepared_statement tests/benchmark_prepared_statement.cpp)
  target_link_libraries(vsqlitepp_benchmark_prepared_statement PRIVATE vsqlite::vsqlitepp)

  if(NOT BUILD_SHARED_LIBS)
    # Reproduction of GH issue #59: a consumer that statically links VSQLite++ and
    # SQLite without exporting the executable's own symbols must keep APIs that the
    # build confirmed available usable, and must reject the absent ones cleanly.
    add_executable(vsqlitepp_static_link_test tests/static_link_consumer.cpp)
    target_link_libraries(vsqlitepp_static_link_test PRIVATE vsqlite::vsqlitepp)
    set_target_properties(vsqlitepp_static_link_test PROPERTIES ENABLE_EXPORTS FALSE)
    if(VSQLITE_BUNDLED_SQLITE)
      # The bundled SQLite always provides the optional APIs.
      target_compile_definitions(vsqlitepp_static_link_test
        PRIVATE VSQLITE_EXPECT_ALL_OPTIONAL_APIS=1)
    endif()
    add_test(NAME vsqlitepp_static_link_test COMMAND vsqlitepp_static_link_test)
  endif()
endif()

set(CPACK_PACKAGE_NAME "vsqlitepp")
set(CPACK_PACKAGE_VENDOR "virtuosic bytes")
set(CPACK_PACKAGE_CONTACT "Vinzenz Feenstra <vinzenz.feenstra@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "VSQLite++ - a portable SQLite3 wrapper for C++")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/vinzenz/vsqlite--")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ;TXZ;ZIP")

set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsqlite3-0")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_RPM_PACKAGE_LICENSE "BSD-3-Clause")
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_PACKAGE_REQUIRES "sqlite")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

include(CPack)
