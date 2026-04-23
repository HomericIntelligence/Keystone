from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps


class ProjectKeystoneConan(ConanFile):
    name = "projectkeystone"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {"with_grpc": [True, False]}
    default_options = {"with_grpc": False}

    def requirements(self) -> None:
        self.requires("spdlog/1.12.0")
        self.requires("concurrentqueue/1.0.4")
        self.requires("cnats/3.9.3")
        if self.options.with_grpc:
            self.requires("yaml-cpp/0.8.0")

    def build_requirements(self) -> None:
        self.test_requires("gtest/1.14.0")
        self.test_requires("benchmark/1.8.3")

    def generate(self) -> None:
        CMakeDeps(self).generate()
        CMakeToolchain(self).generate()
