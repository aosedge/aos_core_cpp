import os
from conan import ConanFile
from conan.tools.cmake import cmake_layout

class AosCoreIAMCPP(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"
    options = { "with_poco": [True, False] }
    default_options = { "with_poco": True }

    def requirements(self):
        self.requires("gtest/1.14.0")
        self.requires("grpc/1.54.3")
        self.requires("openssl/3.2.1")

        if self.options.with_poco :
            self.requires("poco/1.13.2")

        libp11path = os.path.join(self.recipe_folder, "libp11conan.py")
        self.run("conan export %s --user user --channel stable" % libp11path, cwd=self.recipe_folder)
        self.requires("libp11/0.4.11@user/stable")

    def build_requirements(self):
        self.tool_requires("protobuf/3.21.12")
        self.tool_requires("grpc/1.54.3")

    def configure(self):
        self.options["openssl"].no_dso = False
        self.options["openssl"].shared = True
