from conan import ConanFile

class AosCommonCpp(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires("gtest/1.14.0")
        self.requires("poco/1.13.2")
        self.requires("grpc/1.54.3")

    def build_requirements(self):
        self.tool_requires("protobuf/3.21.12")
        self.tool_requires("grpc/1.54.3")
