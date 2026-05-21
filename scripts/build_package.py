import os
import subprocess

import powermake

from scripts import framework, build_lib

ARCH = "arm64"

MIN_MACOS = "13.0"
MIN_IOS = "16.0"
MIN_TVOS = "16.0"
MIN_VISIONOS = "1.0"

APPLE_SLICES = [
    ("macos", "macosx", "macos", MIN_MACOS),
    ("ios", "iphoneos", "ios", MIN_IOS),
    ("tvos", "appletvos", "tvos", MIN_TVOS),
    ("visionos", "xros", "xros", MIN_VISIONOS),
]

APPLE_COMPONENTS = [
    ("Pigment", "libpigment.dylib", ["pigment.h", "pigment_std.h"]),
    ("PigmentGLTF", "libpigment_gltf.a", ["pigment_gltf.h"]),
    ("PigmentSDL", "libpigment_sdl.a", ["pigment_sdl.h"]),
]

_COMPONENT_HEADERS = {"pigment_gltf.h", "pigment_sdl.h", "pigment_shaderc.h"}


def _sdk_path(sdk: str) -> str:
    return subprocess.run(
        ["xcrun", "--sdk", sdk, "--show-sdk-path"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()


def _sdk_platform(sdk: str) -> str:
    path = subprocess.run(
        ["xcrun", "--sdk", sdk, "--show-sdk-platform-path"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    return os.path.basename(path).removesuffix(".platform")


def _package(
    out_dir: str,
    name: str,
    library: str,
    include_dir: str,
    primary: list[str],
    platform: str,
    min_os: str,
) -> str:
    if name == "Pigment":
        headers = [
            h
            for h in powermake.get_files(f"{include_dir}/*.h", f"{include_dir}/**/*.h")
            if os.path.basename(h) not in _COMPONENT_HEADERS
        ]
    else:
        headers = [os.path.join(include_dir, primary[0])]

    return framework.build_framework(
        framework.FrameworkInfo(
            out_dir,
            name,
            library,
            headers=headers,
            headers_base_dir=os.path.dirname(include_dir),
            identifier=f"org.libpigment.{name}",
            umbrella_header=[os.path.join(include_dir, h) for h in primary],
            platform=platform,
            min_os=min_os,
            module=True,
        )
    )


def build_framework_package(
    config: powermake.Config, shared: bool, platform: str = "MacOSX"
) -> str | None:
    if not config.host_is_macos():
        return None

    base = os.path.dirname(config.lib_build_directory)
    include_dir = os.path.join(base, "include", "pigment")
    library = os.path.join(
        config.lib_build_directory, "libpigment.dylib" if shared else "libpigment.a"
    )

    if not os.path.exists(library):
        return None

    return _package(
        os.path.join(base, "framework"),
        "Pigment",
        library,
        include_dir,
        ["pigment.h", "pigment_std.h"],
        platform,
        MIN_MACOS,
    )


def build_all_apple(config: powermake.Config):
    if not config.host_is_macos():
        return

    collected: dict[str, list[str]] = {name: [] for name, _, _ in APPLE_COMPONENTS}

    for sname, sdk, os_name, min_os in APPLE_SLICES:
        triple = f"{ARCH}-apple-{os_name}{min_os}" + (
            "-simulator" if "simulator" in sdk else ""
        )
        platform = _sdk_platform(sdk)

        target_flags = [
            powermake.EnforcedFlag(f)
            for f in ("-target", triple, "-isysroot", _sdk_path(sdk))
        ]

        apple_config = config.copy()
        apple_config.add_c_flags(*target_flags)
        apple_config.add_ld_flags(*target_flags)
        apple_config.add_shared_linker_flags(*target_flags)

        slice_root = os.path.join("build", "apple", sname)
        apple_config.obj_build_directory = os.path.join(slice_root, "objs")
        apple_config.lib_build_directory = os.path.join(slice_root, "lib")
        apple_config.exe_build_directory = os.path.join(slice_root, "bin")

        print(f"[xcframework] slice {sname} ({triple})")
        build_lib.build_pigment(apple_config, shared=True)

        c_static = apple_config.copy()
        c_static.remove_flags("-flto=auto", "-flto")
        build_lib.build_gltf_tool(c_static)
        build_lib.build_sdl_integration(c_static)

        base = os.path.dirname(apple_config.lib_build_directory)
        include_dir = os.path.join(base, "include", "pigment")
        out_dir = os.path.join(base, "framework")

        for name, lib_file, primary in APPLE_COMPONENTS:
            library = os.path.join(apple_config.lib_build_directory, lib_file)
            if not os.path.exists(library):
                continue
            collected[name].append(
                _package(out_dir, name, library, include_dir, primary, platform, min_os)
            )

    for name, _, _ in APPLE_COMPONENTS:
        if collected[name]:
            framework.build_xcframework(
                os.path.join("build", "apple", f"{name}.xcframework"), collected[name]
            )
