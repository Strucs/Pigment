import os

import powermake

from scripts import build_common, build_slang


def _shader_stages(
    source: str, *stages: str, include_dirs: list[str] | None = None
) -> list[build_slang.SlangShader]:
    basename = os.path.splitext(os.path.basename(source))[0]
    suffixes = {"vertex": "vert", "fragment": "frag", "compute": "comp"}
    include_dirs = include_dirs or []
    return [
        build_slang.SlangShader(
            source=source,
            entry_point=f"{stage}Main",
            stage=stage,
            name=f"{basename}_{suffixes.get(stage, stage)}",
            include_dirs=include_dirs,
            profile=(
                "spirv_1_6+spvMeshShadingEXT" if stage in ("task", "mesh") else None
            ),
        )
        for stage in stages
    ]


EXAMPLE_SHADERS = {
    "vertex_input": _shader_stages(
        "examples/vertex_input/test.slang", "vertex", "fragment"
    ),
    "primitives": _shader_stages(
        "examples/primitives/wave.slang", "compute", include_dirs=["shaders"]
    ),
    "mesh_shader": _shader_stages(
        "examples/mesh_shader/triangle.slang", "task", "mesh", "fragment"
    ),
}


def build_example(
    config: powermake.Config,
    example_name: str,
    shared: bool,
    archives: list[str],
):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    example_shaders_dir = os.path.join(
        os.path.dirname(config.lib_build_directory), "example_shaders", example_name
    )

    config.add_includedirs(include_dir, "external/stb_image")

    external_files = [
        src for src in ("external/stb_image/stb_image.c",) if os.path.exists(src)
    ]

    ext_config = config.copy()
    if build_common.is_msvc(ext_config):
        ext_config.remove_flags("/W4", "/W3", "/W2", "/W1", "/GL")
    else:
        ext_config.add_flags("-w")
        ext_config.remove_flags("-flto=auto", "-flto")

    has_shaders = build_slang.build_shaders(
        config, EXAMPLE_SHADERS.get(example_name, []), example_shaders_dir
    )
    if has_shaders:
        config.add_includedirs(example_shaders_dir)

    example_files = powermake.get_files(f"./examples/{example_name}/**/*.c")

    if shared:
        if config.target_is_macos():
            config.add_ld_flags("-Wl,-rpath,@executable_path/../lib")
        elif config.target_is_linux():
            config.add_ld_flags("-Wl,-rpath,$ORIGIN/../lib")

    ext_objects = powermake.compile_files(ext_config, external_files)
    objects = powermake.compile_files(config, example_files)

    print(
        f"{example_name} :",
        powermake.link_files(
            config,
            list(objects) + list(ext_objects),
            archives,
            executable_name=example_name,
        ),
    )

    if has_shaders:
        config.remove_includedirs(example_shaders_dir)
