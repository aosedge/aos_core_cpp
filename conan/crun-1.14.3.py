from conan import ConanFile
from conan.tools.gnu import Autotools, AutotoolsToolchain
from conan.tools.scm import Git
from conan.tools.layout import basic_layout
from conan.tools.env import VirtualBuildEnv


class Crun(ConanFile):
    name = "crun"
    revision = "1.14.3"
    version = "1.14.3"
    settings = "os", "compiler", "build_type", "arch"

    def layout(self):
        basic_layout(self)

    def source(self):
        git = Git(self)
        clone_args = ["--branch", self.revision, "--recurse-submodules", "--depth", "1"]
        git.clone("https://github.com/containers/crun.git", args=clone_args, target=self.source_folder)

    def generate(self):
        env = VirtualBuildEnv(self)
        env.generate()

        tc = AutotoolsToolchain(self)
        tc.generate()

    def build(self):
        autotools = Autotools(self)
        self.run("./autogen.sh", cwd=self.source_folder)
        autotools.configure(args=["--enable-shared"])
        autotools.make()

    def package(self):
        autotools = Autotools(self)
        autotools.install()

    def package_info(self):
        self.cpp_info.libs = ["crun"]
        self.cpp_info.includedirs = []
