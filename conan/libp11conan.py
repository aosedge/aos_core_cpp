from conan import ConanFile
from conan.tools.build import cross_building
from conan.tools.gnu import AutotoolsToolchain, Autotools, AutotoolsDeps, PkgConfigDeps
from conan.tools.scm import Git
from conan.tools.layout import basic_layout

class LibP11(ConanFile):
    name = "libp11"
    branch = "libp11-0.4.11"
    version = "0.4.11"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("openssl/3.2.1")

    def configure(self):
        self.options["openssl"].shared = True

    def source(self):
        git = Git(self)
        clone_args = ['--depth', '1', '--branch', self.branch]
        git.clone("https://github.com/OpenSC/libp11.git", args=clone_args, target=".")

    def layout(self):
        basic_layout(self, src_folder="src")

    def generate(self):
        tc = AutotoolsToolchain(self)
        tc.generate()

        tc = AutotoolsDeps(self)
        tc.generate()

        deps = PkgConfigDeps(self)
        deps.generate()

    def build(self):
        autotools = Autotools(self)
        autotools.autoreconf()
        autotools.configure()
        autotools.make()

    def package(self):
        autotools = Autotools(self)
        autotools.install()
