from pathlib import Path
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.build import can_run
from conan.tools.env import VirtualRunEnv


class Consumer(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.cache_variables["CMAKE_PREFIX_PATH"] = self.conf.get("user.tansr:dependency_prefix")
        toolchain.cache_variables["OPENSSL_ROOT_DIR"] = self.conf.get("user.tansr:dependency_prefix")
        toolchain.generate()
        VirtualRunEnv(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not can_run(self):
            raise RuntimeError("This native consumer cannot run here; it is not a passing package test")
        executable = Path(self.cpp.build.bindirs[0]) / "tansr-conan-consumer"
        if self.settings.os == "Windows":
            executable = executable.with_suffix(".exe")
        self.run('"' + str(executable) + '"', env="conanrun")
