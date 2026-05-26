vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vinzenz/vsqlite--
    REF "v${VERSION}"
    SHA512 2f18ff65aa0a216afc7ba630feca2f699647d11e4d14bee501e866f288ca8fb4138e89a90ee616c6fbdc6dd41e139f3870ac848e3808c3b323bdac0f533c60d3
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DVSQLITE_BUILD_EXAMPLES=OFF
        -DVSQLITE_BUILD_TESTS=OFF
        -DVSQLITE_BUNDLED_SQLITE=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME vsqlitepp CONFIG_PATH lib/cmake/vsqlitepp)
vcpkg_fixup_pkgconfig()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
