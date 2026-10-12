import os

from conan import ConanFile
from conan.tools.env import Environment, VirtualBuildEnv
from conan.tools.files import copy, save
from conan.tools.gnu import Autotools, AutotoolsToolchain, PkgConfigDeps
from conan.tools.layout import basic_layout
from conan.tools.scm import Git


class Conmon(ConanFile):
    name = "conmon"
    version = "2.1.13"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"

    # conmon only needs glib core, so the optional glib features (and their transitive dependencies) are disabled to
    # keep the dependency tree small. glib is linked statically to get a self-contained conmon binary.
    default_options = {
        "glib/*:shared": False,
        "glib/*:with_elf": False,
        "glib/*:with_mount": False,
        "glib/*:with_selinux": False,
    }

    def layout(self):
        basic_layout(self)

    def requirements(self):
        self.requires("glib/2.81.0")

    def build_requirements(self):
        self.tool_requires("pkgconf/2.1.0")

    def source(self):
        git = Git(self)
        clone_args = ["--branch", f"v{self.version}", "--depth", "1"]
        git.clone("https://github.com/containers/conmon.git", args=clone_args, target=self.source_folder)

    def generate(self):
        env = VirtualBuildEnv(self)
        env.generate()

        tc = AutotoolsToolchain(self)
        tc.generate()

        deps = PkgConfigDeps(self)
        deps.generate()

        # The Makefile resolves glib through $(shell pkg-config ...), which only sees the real environment.
        pkgconfig = Environment()
        pkgconfig.prepend_path("PKG_CONFIG_PATH", self.generators_folder)
        pkgconfig.vars(self, scope="build").save_script("conanpkgconfig")

    def build(self):
        # conmon is built with its plain Makefile (the meson build makes libseccomp mandatory, while the Makefile only
        # requires glib). The Makefile builds in-tree, so build from a copy to keep the source folder pristine.
        copy(self, "*", self.source_folder, self.build_folder)

        # The seccomp notify probe queries the host compiler and pkg-config directly, which makes the build depend on
        # the build machine. seccomp notify isn't used, so the probe is stubbed out to always disable it.
        save(self, os.path.join(self.build_folder, "hack", "seccomp-notify.sh"), "#!/bin/sh\n")

        autotools = Autotools(self)
        autotools.make(target="bin/conmon")

    def package(self):
        copy(self, "LICENSE", self.build_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "conmon", os.path.join(self.build_folder, "bin"), os.path.join(self.package_folder, "bin"))

    def package_info(self):
        self.cpp_info.includedirs = []
        self.cpp_info.libdirs = []
