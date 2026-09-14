cmake_minimum_required(VERSION 3.21)

project(vsqlitepp_vcpkg_consumer LANGUAGES CXX)

# Consumer for the vcpkg overlay port (packaging/vcpkg). The port installs the
# pinned released version of vsqlitepp, not the current checkout, so this
# consumer sticks to the headers that released versions advertise. Compile each
# public header and the SQL function API coverage against the current source
# lives in tests/package_consumers/{install,headers}.

find_package(vsqlitepp CONFIG REQUIRED)

add_executable(vsqlitepp_vcpkg_consumer main.cpp)
target_compile_features(vsqlitepp_vcpkg_consumer PRIVATE cxx_std_20)
target_link_libraries(vsqlitepp_vcpkg_consumer PRIVATE vsqlite::vsqlitepp)
