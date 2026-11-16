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
    exports_sources = (
        "CMakeLists.txt", ".clang-tidy", "cmake/*", "ki/*", "tests/*", "!tests/consumer/*", "LICENSE*")
    test_package_folder = "tests/consumer"

    # One entry per ki_add_library call: the ki libraries it links, then its external requirements.
    # "ki" is the main library; applications link ki::ki, and their tests add ki::test.
    _libraries = {
        "ki": ((), ("asio::asio", "re2::re2")),
        "log": ((), ()),
        "test": (("ki",), ()),
    }

    def set_version(self):
        cmakelists = load(self, os.path.join(self.recipe_folder, "CMakeLists.txt"))
        self.version = re.search(r"project\(\s*\w+\s+VERSION\s+(\S+)", cmakelists).group(1)

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
        self.requires("re2/20251105")

    def build_requirements(self):
        self.test_requires("catch2/3.16.1")
        # No library uses llhttp until the server arrives; a test keeps the toolchain proven.
        self.test_requires("llhttp/9.3.0")

    def validate(self):
        check_min_cppstd(self, 23)

    def _skip_tests(self):
        return self.conf.get("tools.build:skip_test", default=False, check_type=bool)

    def generate(self):
        CMakeDeps(self).generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["KI_BUILD_TESTS"] = not self._skip_tests()
        # user.project:werror and user.project:clang_tidy are this project's own switches, named without the
        # package so the CI workflow is the same as the moderncpp template's.
        tc.cache_variables["KI_WARNINGS_AS_ERRORS"] = self.conf.get(
            "user.project:werror", default=False, check_type=bool)
        tc.cache_variables["KI_CLANG_TIDY"] = self.conf.get(
            "user.project:clang_tidy", default=False, check_type=bool)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if not self._skip_tests():
            cmake.test()

    def package(self):
        copy(self, "LICENSE*", self.source_folder, os.path.join(self.package_folder, "licenses"))
        CMake(self).install()
        # Consumers get Conan-generated config files; the ones CMake installs would shadow them.
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "ki")
        # Conan's target for the whole package would otherwise take the name ki::ki and link every library,
        # test helpers included; ki::ki must be the main library alone, as it is in ki's own CMake package.
        self.cpp_info.set_property("cmake_target_name", "ki::all")
        for library, (ki_libraries, external) in self._libraries.items():
            stem = "ki" if library == "ki" else f"ki_{library}"
            component = self.cpp_info.components[library]
            component.set_property("cmake_target_name", f"ki::{library}")
            component.libs = [stem]
            component.requires = [*ki_libraries, *external]
            if not self.options.shared:
                component.defines = [f"{stem.upper()}_STATIC_DEFINE"]
        if self.settings.os == "Windows":
            self.cpp_info.components["ki"].defines.append("_WIN32_WINNT=0x0A00")
            self.cpp_info.components["ki"].system_libs = ["ws2_32", "mswsock"]
