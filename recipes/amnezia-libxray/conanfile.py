from conan import ConanFile
from conan.tools.files import get, copy, apply_conandata_patches, export_conandata_patches
from conan.tools.layout import basic_layout
from conan.errors import ConanInvalidConfiguration
from conan.tools.env import Environment

import os
import stat

from pathlib import Path

class AmneziaLibxray(ConanFile):
    name = "amnezia-libxray"
    # Strict Split Tunneling: built from the fork that adds the RegisterUidFilter
    # bridge, pinned by commit. Back to the upstream tag once the bridge is merged.
    version = "1.0.3-strict.1"
    _commit = "b9ac455aaa139fcdbfec96c6fb58f92605f294ad"
    settings = "os", "arch", "compiler"

    def export_sources(self):
        export_conandata_patches(self)

    def configure(self):
        self.settings.rm_safe("compiler.libcxx")
        self.settings.rm_safe("compiler.cppstd")

    def layout(self):
        basic_layout(self, build_folder=".")

    def build_requirements(self):
        self.tool_requires("go/1.26.0")
    
    def validate(self):
        if self.settings.os != "Android":
            raise ConanInvalidConfiguration(f"{self.name} v{self.version} does not support {self.settings.os}")

    def source(self):
        get(self, f"https://github.com/amnezia-tun0-fix/amnezia-libxray/archive/{self._commit}.zip",
            sha256="7b4c54c8cee1eecb0c37da7ece4df9706caabafbcc6fe7f8210899db0d7a96f2", strip_root=True
        )

    def generate(self):
        env = Environment()
        ndk_path_str = self.conf.get("tools.android:ndk_path")
        if ndk_path_str:
            ndk_path = Path(ndk_path_str)
            if len(ndk_path.parts) > 2:
                sdk_path = ndk_path.parents[1]
                env.define("ANDROID_HOME", str(sdk_path))
        env.vars(self).save_script("conan_provide_androidhome")

    def _patch_sources(self):
        apply_conandata_patches(self)
        build_path = os.path.join(self.build_folder, "build.sh")
        build_stat = os.stat(build_path)
        os.chmod(build_path, build_stat.st_mode | stat.S_IEXEC)

    def build(self):
        self._patch_sources()
        if self.settings_build.os == "Windows":
            self.run("bash build.sh android")
        else:
            self.run("./build.sh android")

    def package(self):
        copy(self, "libxray.aar", src=self.build_folder, dst=os.path.join(self.package_folder, "aar"))

    def package_info(self):
        self.cpp_info.set_property("cmake_extra_variables", {
            "AMNEZIA_LIBXRAY_PATH": Path(self.package_folder, "aar", "libxray.aar").as_posix(),
        })
