"""
Pre-build script: install custom XIAO nRF52840 Sense variant into
the Adafruit nRF52 framework package so the builder can find it.
"""
Import("env")
import os
import shutil

framework_dir = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")
variant_name = env.BoardConfig().get("build.variant", "")
src = os.path.join(env["PROJECT_DIR"], "variants", variant_name)
dst = os.path.join(framework_dir, "variants", variant_name)

if os.path.isdir(src) and not os.path.isdir(dst):
    shutil.copytree(src, dst)
    print("Installed custom variant '%s' into framework" % variant_name)
