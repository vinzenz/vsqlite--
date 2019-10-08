cmake_minimum_required (VERSION 2.8)

project(VSQLite++)

set(CMAKE_BUILD_TYPE Release)
set(LIB_SUFFIX '' CACHE STRING "empty by default - can be used to override to e.g. lib64 for systems like Fedora etc that use these for alt archs")

set(VSQLITE_SOVERSION "3.0.0" CACHE STRING "Shared object version to use")
file(STRINGS "VERSION" VSQLITE_VERSION)

set(CMAKE_MODULE_PATH
  ${CMAKE_SOURCE_DIR}/cmake
  ${CMAKE_MODULE_PATH}
)

find_package(Boost COMPONENTS system filesystem REQUIRED)

find_package(SQLite3 REQUIRED)

# Compiler-specific C++11 activation.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
    if (NOT (GCC_VERSION VERSION_GREATER 4.7 OR GCC_VERSION VERSION_EQUAL 4.7))
        message(FATAL_ERROR "${PROJECT_NAME} requires g++ 4.7 or greater.")
    endif ()
    set(COMPILER_IS_GNU_COMPATIBLE TRUE)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(COMPILER_IS_GNU_COMPATIBLE TRUE)
elseif (MSVC AND MSVC_VERSION GREATER 1800)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
else ()
    message(FATAL_ERROR "Your C++ compiler does not support C++11.")
endif ()

if (COMPILER_IS_GNU_COMPATIBLE)
  # Initialize CXXFLAGS.
  set(CMAKE_CXX_FLAGS                "-Wall -std=c++11 -Werror -Wall -pedantic -W")
  set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g")
  set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
endif ()

include_directories(.
  ${PROJECT_SOURCE_DIR}/include
)

set(VSQLITE_SRC
  src/sqlite/backup.cpp
  src/sqlite/command.cpp
  src/sqlite/connection.cpp
  src/sqlite/savepoint.cpp
  src/sqlite/execute.cpp
  src/sqlite/query.cpp
  src/sqlite/result.cpp
  src/sqlite/transaction.cpp
  src/sqlite/view.cpp
)

if(NOT LIBRARY_TYPE)
message(WARNING "Library type not specified, falling back to Shared.
To specify library type use:
-DLIBRARY_TYPE=<type> where <type> is Shared or Static.")
set(LIBRARY_TYPE "Shared" CACHE STRING
     "Choose the type of library, options are: Shared Static."
            FORCE)
endif(NOT LIBRARY_TYPE)

if (LIBRARY_TYPE STREQUAL "Shared")
  add_library(vsqlitepp SHARED ${VSQLITE_SRC})
elseif(LIBRARY_TYPE STREQUAL "Static")
  add_library(vsqlitepp STATIC ${VSQLITE_SRC})
else()
  message(FATAL "Unknown Library type specified: ${LIBRARY_TYPE}")
endif()

set(VSQLITE_LINK_LIBS ${SQLITE3_LIBRARIES} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY})

target_link_libraries(vsqlitepp ${VSQLITE_LINK_LIBS})

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set_target_properties(vsqlitepp PROPERTIES LINK_FLAGS "-Wl,--no-as-needed")
endif ()

set_target_properties(vsqlitepp
                      PROPERTIES VERSION    ${VSQLITE_VERSION}
                                 SOVERSION  ${VSQLITE_SOVERSION}
                                 INSTALL_NAME_DIR  "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}")

if (LIBRARY_TYPE STREQUAL "Shared")
install(TARGETS vsqlitepp
    LIBRARY DESTINATION lib${LIB_SUFFIX})
else()
install(TARGETS vsqlitepp
    ARCHIVE DESTINATION lib${LIB_SUFFIX})
endif()

add_executable(vsqlitepp_example
  examples/sqlite_wrapper.cpp)

target_link_libraries(vsqlitepp_example 
        vsqlitepp
        ${VSQLITE_LINK_LIBS}
)

install(FILES NEWS INSTALL COPYING README.md VERSION TODO ChangeLog AUTHORS 
        DESTINATION "share/doc/libvsqlite3")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/" 
        DESTINATION "include" 
        COMPONENT dev 
        FILES_MATCHING PATTERN "*.hpp"
        PATTERN "private" EXCLUDE)