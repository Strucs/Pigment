import os

import powermake

from scripts import build_common


def build_pigment(config: powermake.Config, shared: bool):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    shaders_dir = os.path.join(os.path.dirname(config.lib_build_directory), "shaders")

    build_common.copy_public_headers(include_dir)
    build_common.build_shaders(
        "./shaders/*",
        shaders_dir,
        ["src/std/pipeline_loader.c", "src/std/canvas/canvas.c"],
    )

    config.add_includedirs(
        include_dir,
        "src/core",
        "src/std",
        "src/vulkan",
        shaders_dir,
        *build_common.EXTERNAL_INCLUDE_DIRS,
    )

    all_files = powermake.get_files("./src/**/*.c")
    integration_files = {
        f for f in all_files if os.sep + "integrations" + os.sep in os.path.normpath(f)
    }
    tools_files = {
        f for f in all_files if os.sep + "tools" + os.sep in os.path.normpath(f)
    }
    project_files = set(all_files) - integration_files - tools_files

    external_files = [src for src in ("external/volk/volk.c",) if os.path.exists(src)]

    ext_config = config.copy()
    if build_common.is_msvc(ext_config):
        ext_config.remove_flags("/W4", "/W3", "/W2", "/W1", "/GL")
    else:
        ext_config.add_flags("-w")
        ext_config.remove_flags("-flto=auto", "-flto")
        if shared:
            ext_config.add_c_flags("-fvisibility=hidden", "-fPIC")

    ext_objects = powermake.compile_files(ext_config, external_files)

    p_config = config.copy()
    p_config.target_name = "pigment"
    if shared:
        p_config.add_defines("PIGMENT_EXPORTS")
        if not build_common.is_msvc(p_config):
            p_config.add_c_flags("-fvisibility=hidden", "-fPIC")

    objects = powermake.compile_files(p_config, project_files)

    if shared:
        if p_config.target_is_macos():
            p_config.add_shared_linker_flags(
                "-Wl,-install_name,@rpath/libpigment.dylib",
                "-Wl,-compatibility_version,1.0.0",
                "-Wl,-current_version,1.0.0",
                # leave room for framework.py to rewrite the install name to @rpath/Pigment.framework/...
                "-Wl,-headerpad_max_install_names",
            )
        elif p_config.target_is_linux():
            p_config.add_shared_linker_flags("-Wl,-soname,libpigment.so")

        if p_config.target_is_mingw():
            implib = os.path.join(p_config.lib_build_directory, "libpigment.dll.a")
            p_config.add_shared_linker_flags(f"-Wl,--out-implib,{implib}")
        lib_name = "pigment" if p_config.target_is_windows() else None
        powermake.link_shared_lib(
            p_config, list(objects) + list(ext_objects), lib_name=lib_name
        )
    elif build_common.is_msvc(p_config):
        powermake.archive_files(
            p_config,
            list(objects) + list(ext_objects),
            archive_name=p_config.target_name,
        )
    else:
        powermake.archive_files(p_config, list(objects) + list(ext_objects))

    config.remove_includedirs(
        include_dir,
        "src/core",
        "src/std",
        "src/vulkan",
        shaders_dir,
        *build_common.EXTERNAL_INCLUDE_DIRS,
    )


def build_sdl_integration(config: powermake.Config):
    sdl_files = list(powermake.get_files("./src/integrations/sdl/**/*.c"))
    if not sdl_files:
        return

    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    sdl_config = config.copy()
    sdl_config.target_name = "pigment_sdl"
    sdl_config.add_includedirs(
        include_dir, "src/core", "src/std", "src/integrations/sdl"
    )

    objects = powermake.compile_files(sdl_config, sdl_files)

    if build_common.is_msvc(sdl_config):
        powermake.archive_files(config, objects, archive_name=sdl_config.target_name)
    else:
        powermake.archive_files(sdl_config, objects)


def build_gltf_tool(config: powermake.Config):
    gltf_files = list(powermake.get_files("./src/tools/gltf/**/*.c"))
    if not gltf_files:
        return

    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    gltf_config = config.copy()
    gltf_config.target_name = "pigment_gltf"
    gltf_config.add_includedirs(
        include_dir, "src/tools/gltf", "external/cgltf", "external/stb_image"
    )

    external_files = [
        src
        for src in ("external/cgltf/cgltf.c", "external/stb_image/stb_image.c")
        if os.path.exists(src)
    ]

    ext_config = gltf_config.copy()
    if build_common.is_msvc(ext_config):
        ext_config.remove_flags("/W4", "/W3", "/W2", "/W1", "/GL")
    else:
        ext_config.add_flags("-w")
        ext_config.remove_flags("-flto=auto", "-flto")

    ext_objects = powermake.compile_files(ext_config, external_files)
    objects = powermake.compile_files(gltf_config, gltf_files)

    if build_common.is_msvc(gltf_config):
        powermake.archive_files(
            gltf_config,
            list(objects) + list(ext_objects),
            archive_name=gltf_config.target_name,
        )
    else:
        powermake.archive_files(gltf_config, list(objects) + list(ext_objects))


def build_shaderc_tool(config: powermake.Config):
    shaderc_files = list(powermake.get_files("./src/tools/shaderc/**/*.c"))
    if not shaderc_files:
        return

    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    shaderc_config = config.copy()
    shaderc_config.target_name = "pigment_shaderc"
    shaderc_config.add_includedirs(include_dir, "src/tools/shaderc")
    shaderc_config.add_shared_libs("shaderc_shared")

    objects = powermake.compile_files(shaderc_config, shaderc_files)

    if build_common.is_msvc(shaderc_config):
        powermake.archive_files(
            shaderc_config, objects, archive_name=shaderc_config.target_name
        )
    else:
        powermake.archive_files(shaderc_config, objects)
