from conan import ConanFile
from conan.tools.gnu import Autotools, AutotoolsToolchain, PkgConfigDeps
from conan.tools.layout import basic_layout
from conan.tools.env import VirtualBuildEnv
from conan.tools.scm import Git
from conan.tools.files import copy

import os

class SoftHSMConan(ConanFile):
    name = "softhsm2"
    version = "2.6.1"
    license = "BSD-2-Clause & ISC"
    url = "https://www.opendnssec.org/softhsm/"
    description = "PKCS#11 HSM/Token Emulator"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("openssl/3.2.1")
        self.requires("sqlite3/3.45.0")

    def configure(self):
        self.options["openssl"].no_dso = False
        self.options["openssl"].shared = True

    def build_requirements(self):
        self.build_requires("autoconf/2.71")
        self.build_requires("automake/1.16.5")
        self.build_requires("libtool/2.4.7")
        self.build_requires("pkgconf/1.9.5")

    def layout(self):
        basic_layout(self)

    def source(self):
        git = Git(self)
        git.clone("https://github.com/softhsm/SoftHSMv2.git", target=self.source_folder)
        git.checkout(self.version)  # no 'v' prefix

    def generate(self):
        env = VirtualBuildEnv(self)
        env.generate()

        deps = PkgConfigDeps(self)
        deps.generate()

        tc = AutotoolsToolchain(self)
        tc.generate()

    def build(self):
        autotools = Autotools(self)
        autotools.autoreconf()
        autotools.configure()
        autotools.make()

    def package(self):
        autotools = Autotools(self)
        autotools.install()

        copy(self, "LICENSE", dst=os.path.join(self.package_folder, "licenses"),
             src=self.source_folder)

    def package_info(self):
        self.cpp_info.libs = ["softhsm2"]
