import os
import shutil
import subprocess

import powermake

from scripts import spv_to_header

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


def compile_shader_to_spv(src: str, dst: str, deps: list[str]) -> bool:
    if os.path.exists(dst):
        dst_mtime = os.path.getmtime(dst)
        sources_mtime = max(os.path.getmtime(p) for p in [src] + deps)
        if dst_mtime >= sources_mtime:
            return False
    result = subprocess.run(["glslc", src, "-o", dst], capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise SystemExit(f"glslc failed for {src}")
    print(f"[shader] {src} -> {dst}")
    return True


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


def build_shaders(
    src_pattern: str, shaders_dst_dir: str, touch_files: list[str]
) -> bool:
    all_files = list(powermake.get_files(src_pattern))
    shader_files = [
        f
        for f in all_files
        if os.path.splitext(f)[1]
        in (".vert", ".frag", ".comp", ".geom", ".tesc", ".tese")
    ]
    if not shader_files:
        return False

    powermake.utils.makedirs(shaders_dst_dir)
    shader_includes = [
        f for f in all_files if os.path.splitext(f)[1] in (".glsl", ".h")
    ]

    any_shader_rebuilt = False
    for file in shader_files:
        base = os.path.basename(file)
        name, ext = os.path.splitext(base)
        dst = os.path.join(shaders_dst_dir, f"{name}_{ext[1:]}.spv")
        if compile_shader_to_spv(file, dst, shader_includes):
            any_shader_rebuilt = True
        spv_to_header.generate(dst, shaders_dst_dir)

    if any_shader_rebuilt:
        for c_file in touch_files:
            if os.path.exists(c_file):
                os.utime(c_file, None)

    return True


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
