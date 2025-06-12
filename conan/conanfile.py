import os
from conan import ConanFile

class AosCommonCpp(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires("gtest/1.14.0")
        self.requires("libcurl/8.8.0")
        self.requires("poco/1.13.2")
        self.requires("grpc/1.54.3")
        self.requires("openssl/3.2.1")

        pkcs11path = os.path.join(self.recipe_folder, "pkcs11provider-1.0.py")
        self.run("conan export %s --user user --channel stable" % pkcs11path, cwd=self.recipe_folder)
        self.requires("pkcs11provider/1.0@user/stable")

    def build_requirements(self):
        self.tool_requires("protobuf/3.21.12")
        self.tool_requires("grpc/1.54.3")
        self.tool_requires("gtest/1.14.0")

    def configure(self):
        self.options["openssl"].no_dso = False
        self.options["openssl"].shared = True
