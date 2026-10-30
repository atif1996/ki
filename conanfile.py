import os
import re

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, load, rmdir


class KiConan(ConanFile):
    name = "ki"
    description = "chi-style router and Go net/http-style HTTP server for C++23 coroutines"
    license = "TBD"
    topics = ("http", "router", "coroutines", "asio")
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = "CMakeLists.txt", "cmake/*", "include/*", "src/*", "tests/*", "LICENSE*"

    def set_version(self):
        cmakelists = load(self, os.path.join(self.recipe_folder, "CMakeLists.txt"))
        self.version = re.search(r"project\(ki\s+VERSION\s+(\S+)", cmakelists).group(1)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires("asio/1.38.2", transitive_headers=True)
        self.requires("llhttp/9.3.0")

    def build_requirements(self):
        self.test_requires("catch2/3.16.1")

    def validate(self):
        check_min_cppstd(self, 23)

    def generate(self):
        CMakeDeps(self).generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["KI_BUILD_TESTS"] = not self.conf.get("tools.build:skip_test", default=False,
                                                                 check_type=bool)
        tc.cache_variables["KI_WARNINGS_AS_ERRORS"] = self.conf.get("user.ki:werror", default=False,
                                                                    check_type=bool)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if not self.conf.get("tools.build:skip_test", default=False, check_type=bool):
            cmake.test()

    def package(self):
        copy(self, "LICENSE*", self.source_folder, os.path.join(self.package_folder, "licenses"))
        CMake(self).install()
        # Consumers get Conan-generated config files; the ones CMake installs would shadow them.
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))

    def package_info(self):
        self.cpp_info.libs = ["ki"]
        self.cpp_info.set_property("cmake_file_name", "ki")
        self.cpp_info.set_property("cmake_target_name", "ki::ki")
        self.cpp_info.requires = ["asio::asio", "llhttp::llhttp"]
        if not self.options.shared:
            self.cpp_info.defines = ["KI_STATIC_DEFINE"]
        if self.settings.os == "Windows":
            self.cpp_info.defines.append("_WIN32_WINNT=0x0A00")
            self.cpp_info.system_libs = ["ws2_32", "mswsock"]
