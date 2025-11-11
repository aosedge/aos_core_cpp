import os

from conan import ConanFile


class AosCoreCpp(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires("grpc/1.54.3")
        self.requires("gtest/1.14.0")
        self.requires("libcurl/8.8.0")
        self.requires("openssl/3.2.1")
        self.requires("poco/1.13.2")

        pkcs11path = os.path.join(self.recipe_folder, "pkcs11provider-1.0.py")
        softhsm2path = os.path.join(self.recipe_folder, "softhsm2.py")

        self.run(
            f"conan export {pkcs11path} --user user --channel stable",
            cwd=self.recipe_folder,
        )
        self.run(
            f"conan export {softhsm2path} --user user --channel stable",
            cwd=self.recipe_folder,
        )

        self.requires("pkcs11provider/1.0@user/stable")
        self.requires("softhsm2/2.6.1@user/stable")

    def build_requirements(self):
        self.tool_requires("grpc/1.54.3")
        self.tool_requires("gtest/1.14.0")
        self.tool_requires("protobuf/3.21.12")

    def configure(self):
        self.options["openssl"].no_dso = False
        self.options["openssl"].shared = True
