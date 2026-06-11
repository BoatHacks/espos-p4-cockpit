"""PlatformIO pre-script: remove LVGL's helium .S asm directory.

LVGL 9.5 ships `src/draw/sw/blend/helium/lv_blend_helium.S` which the
RISC-V toolchain can't assemble (it includes <stdint.h> which becomes
typedef tokens the assembler doesn't recognise). The file's body is
already `#if`-guarded out for non-Helium builds, but PIO compiles every
source it finds regardless. Stripping the directory at the lib level is
the simplest workaround until either LVGL guards the file itself or PIO
respects an exclude pattern at lib_deps granularity.
"""
import os
import shutil

Import("env")  # noqa: F821 — provided by SCons

project_dir = env["PROJECT_DIR"]  # noqa: F821
build_env = env["PIOENV"]  # noqa: F821
helium_dir = os.path.join(
    project_dir, ".pio", "libdeps", build_env,
    "lvgl", "src", "draw", "sw", "blend", "helium",
)
if os.path.isdir(helium_dir):
    try:
        shutil.rmtree(helium_dir)
        print(f"strip_lvgl_helium: removed {helium_dir}")
    except OSError as e:
        # Don't abort the build — at worst the helium .S file remains
        # and the next assemble step fails with a clearer error.
        print(f"strip_lvgl_helium: warning: failed to remove "
              f"{helium_dir}: {e}")
