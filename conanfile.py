from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy

import os


class VsqliteppConan(ConanFile):
    name = "vsqlitepp"
    license = "BSD-3-Clause"
    author = "Vinzenz Feenstra <vinzenz.feenstra@gmail.com>"
    url = "https://github.com/vinzenz/vsqlite--"
    homepage = "https://github.com/vinzenz/vsqlite--"
    description = "VSQLite++ - a portable SQLite3 wrapper for C++"
    topics = ("sqlite", "database", "cpp20", "wrapper")
    package_type = "library"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "allow_follow_symlinks": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "allow_follow_symlinks": True,
        "sqlite3/*:threadsafe": 1,
    }

    exports_sources = (
        "CMakeLists.txt",
        "VERSION",
        "COPYING",
        "README.md",
        "README",
        "NEWS",
        "TODO",
        "ChangeLog",
        "AUTHORS",
        "cmake/*",
        "include/*",
        "src/*",
        "examples/*",
    )

    def set_version(self):
        with open(os.path.join(self.recipe_folder, "VERSION"), encoding="utf-8") as version_file:
            self.version = version_file.read().strip()

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def validate(self):
        check_min_cppstd(self, "20")

    def requirements(self):
        self.requires("sqlite3/3.53.1")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_SHARED_LIBS"] = bool(self.options.shared)
        toolchain.variables["VSQLITE_BUILD_EXAMPLES"] = False
        toolchain.variables["VSQLITE_BUILD_TESTS"] = False
        toolchain.variables["VSQLITE_BUNDLED_SQLITE"] = False
        toolchain.variables["VSQLITE_ALLOW_FOLLOW_SYMLINKS"] = bool(
            self.options.allow_follow_symlinks
        )
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "COPYING", self.source_folder, os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "vsqlitepp")
        self.cpp_info.set_property("cmake_target_name", "vsqlite::vsqlitepp")
        self.cpp_info.libs = ["vsqlitepp"]
        self.cpp_info.requires = ["sqlite3::sqlite3"]
