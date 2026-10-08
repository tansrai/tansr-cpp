"""Conan 2 recipe for a versioned, checksum-verified Tansr source release."""
import os
import re
from pathlib import Path
from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get


class TansrSDK(ConanFile):
    name = "tansr-sdk"
    version = "0.1.0"
    license = "MIT"
    homepage = "https://github.com/tansrai/tansr-cpp"
    url = "https://github.com/tansrai/tansr-cpp"
    description = "Native C++17 client of the Tansr unified API"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def source(self):
        source = self.conan_data.get("sources", {}).get(str(self.version), {})
        url, checksum = source.get("url", ""), source.get("sha256", "")
        prefix = f"https://github.com/tansrai/tansr-cpp/releases/download/v{self.version}/"
        if (not isinstance(url, str) or not url.startswith(prefix) or len(url) <= len(prefix)
                or not isinstance(checksum, str) or not re.fullmatch(r"[0-9a-f]{64}", checksum)):
            raise ConanInvalidConfiguration(
                "This version has no verified source release URL/SHA256 in conandata.yml; "
                "use the completed recipe from the matching GitHub Release")
        get(self, url=url, sha256=checksum, strip_root=True)
        if not Path(self.source_folder, "include/tansr/api.hpp").is_file():
            raise ConanInvalidConfiguration("Source archive does not contain the SDK root")

    def generate(self):
        prefix = self.conf.get("user.tansr:dependency_prefix", default=None)
        if not prefix or not Path(prefix).is_absolute():
            raise ConanInvalidConfiguration("Set -c user.tansr:dependency_prefix to the prepared absolute curl/OpenSSL prefix")
        toolchain = CMakeToolchain(self)
        # 在工具链加载前通过 configure cache 提供前缀。晚写 variables 会被
        # Conan find_paths 的普通变量遮蔽，导致已准备的 CURL/OpenSSL 无法发现。
        toolchain.cache_variables["CMAKE_PREFIX_PATH"] = prefix
        toolchain.cache_variables["OPENSSL_ROOT_DIR"] = prefix
        toolchain.variables["TANSR_BUILD_TESTS"] = False
        toolchain.variables["TANSR_BUILD_DEMOS"] = False
        toolchain.variables["TANSR_BUILD_INTEGRATION"] = False
        toolchain.variables["TANSR_WARNINGS_AS_ERRORS"] = True
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()
        copy(self, "LICENSE", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "RIGHTS.txt", src=os.path.join(self.source_folder, "packaging"),
             dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "NOTICE.md", src=os.path.join(self.source_folder, "packaging"),
             dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "*", src=os.path.join(self.source_folder, "packaging/licenses"),
             dst=os.path.join(self.package_folder, "licenses/third-party"))

    def package_info(self):
        # Exported CMake config carries CURL/OpenSSL/Threads and Windows system libs.
        # Consumers must use it rather than a fabricated private-ABI link list.
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = ["lib/cmake/TansrSDK"]
        self.cpp_info.libs = ["tansr_sdk"]
