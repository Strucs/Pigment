import os
import shutil

import powermake

INTERNAL_HEADERS = {"structs.h", "internal.h", "std_internal.h"}

CONVENIENCE_HEADERS = {"pigment.h", "pigment_std.h", "pigment_sdl.h", "pigment_vk.h"}

EXTERNAL_INCLUDE_DIRS = (
    "external/volk",
    "external/cgltf",
    "external/stb_image",
    "external/Vulkan-Headers/include",
)


def public_path_for_header(file: str) -> str | None:
    parts = os.path.normpath(file).split(os.sep)
    if len(parts) < 3:
        return None
    module = parts[1]
    filename = parts[-1]
    if filename in INTERNAL_HEADERS:
        return None
    if filename in CONVENIENCE_HEADERS:
        return f"pigment/{filename}"
    if module == "core":
        return f"pigment/{filename}"
    if module == "std":
        rest = parts[2:-1]
        if rest:
            return f"pigment/std/{'/'.join(rest)}/{filename}"
        return f"pigment/std/{filename}"
    if module == "vulkan":
        return f"pigment/vulkan/{filename}"
    if module == "integrations":
        return f"pigment/{filename}"
    if module == "tools":
        return f"pigment/{filename}"
    return None


def copy_public_headers(include_dir: str):
    for file in powermake.get_files("./src/**/*.h"):
        target_path = public_path_for_header(file)
        if target_path is None:
            continue
        dst = os.path.join(include_dir, target_path)
        powermake.utils.makedirs(os.path.dirname(dst))
        shutil.copy2(file, dst)

    volk_header = os.path.join("external", "volk", "volk.h")
    if os.path.exists(volk_header):
        powermake.utils.makedirs(include_dir)
        shutil.copy2(volk_header, os.path.join(include_dir, "volk.h"))


def is_msvc(config: powermake.Config) -> bool:
    return config.c_compiler is not None and config.c_compiler.type in (
        "msvc",
        "clang-cl",
    )


def copy_shared_runtime(config: powermake.Config, dll_path: str, bin_dir: str):
    if not config.target_is_windows() or not os.path.exists(dll_path):
        return
    powermake.utils.makedirs(bin_dir)
    shutil.copy2(dll_path, os.path.join(bin_dir, os.path.basename(dll_path)))


def configure_build(config: powermake.Config):
    config.add_flags("-Wsecurity", "-pedantic")
    config.remove_flags("-Wconversion", "-Wsign-conversion")
    # config.remove_flags("-fanalyzer") # uncomment for way faster compilation

    if is_msvc(config):
        config.add_c_flags(
            "/std:c17",
            "/W4",
            "/wd4820",
            "/wd4201",
            "/wd4100",
            "/wd4996",
            "/wd5045",
            "/wd4324",
            "/wd4061",
            "/wd4191",
        )
        config.remove_flags("/Wall")
        if config.c_compiler.type == "clang-cl":
            config.add_c_flags("-Wno-unused-command-line-argument")
        else:
            config.add_c_flags("/experimental:c11atomics", "/Zc:preprocessor")
            if not config.debug:
                config.add_c_flags("/GL")
                config.add_ld_flags("/LTCG")
                config.add_shared_linker_flags("/LTCG")
    else:
        if not config.debug:
            config.add_c_flags("-flto=auto")

    if config.target_is_macos():
        config.shared_linker.shared_lib_extension = ".dylib"
        config.add_includedirs("/opt/homebrew/include")
        config.add_ld_flags("-L/opt/homebrew/lib")
