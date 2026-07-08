"""
System Conan recipe for vitasdk-toolchain when building inside vitasdk/vitasdk Docker image.

Instead of downloading vitasdk (which is a glibc-linked binary incompatible with
Alpine/musl), this recipe exposes the pre-installed vitasdk from the container
at $VITASDK (typically /usr/local/vitasdk).

Usage: export this package BEFORE conan-vitasdk in CI, so the cached version
satisfies the vitasdk-toolchain/2.527.1@blastrock/pkgj requirement.
"""
import os

from conan import ConanFile


class VitasdkDockerToolchainConan(ConanFile):
    name = "vitasdk-toolchain"
    version = "2.527.1"
    user = "blastrock"
    channel = "pkgj"
    # No settings — this recipe wraps whatever vitasdk the container provides
    package_type = "application"

    def package_id(self):
        # Always produce the same package id regardless of host/build settings
        self.info.clear()

    def package(self):
        # Nothing to package — the SDK lives at $VITASDK in the container
        pass

    def package_info(self):
        vitasdk = os.environ.get("VITASDK", "/usr/local/vitasdk")
        self.buildenv_info.define_path("VITASDK", vitasdk)
        self.buildenv_info.append_path("PATH", os.path.join(vitasdk, "bin"))
        toolchain = os.path.join(vitasdk, "share", "vita.toolchain.cmake")
        self.buildenv_info.define_path("CONAN_CMAKE_TOOLCHAIN_FILE", toolchain)
        self.conf_info.define(
            "tools.cmake.cmaketoolchain:user_toolchain",
            [toolchain],
        )
