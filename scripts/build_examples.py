import os

import powermake

from scripts import build_common


def build_example(config: powermake.Config, example_name: str, shared: bool):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    lib_dir = os.path.join(os.path.dirname(config.lib_build_directory), "lib")
    bin_dir = os.path.join(os.path.dirname(config.lib_build_directory), "bin")
    example_shaders_dir = os.path.join(
        os.path.dirname(config.lib_build_directory), "example_shaders", example_name
    )
    config.add_includedirs(include_dir)

    example_c_files = list(powermake.get_files(f"./examples/{example_name}/**/*.c"))
    has_shaders = build_common.build_shaders(
        f"./examples/{example_name}/**/*", example_shaders_dir, example_c_files
    )
    if has_shaders:
        config.add_includedirs(example_shaders_dir)

    example_files = powermake.get_files(f"./examples/{example_name}/**/*.c")

    if shared:
        if config.target_is_macos():
            config.add_ld_flags("-Wl,-rpath,@executable_path/../lib")
        elif config.target_is_linux():
            config.add_ld_flags("-Wl,-rpath,$ORIGIN/../lib")

    objects = powermake.compile_files(config, example_files)

    archives = [
        build_common.link_target_path(config, lib_dir, "pigment_sdl", shared),
        build_common.link_target_path(config, lib_dir, "pigment_gltf", shared),
        build_common.link_target_path(config, lib_dir, "pigment", shared),
    ]

    print(
        f"{example_name} :",
        powermake.link_files(config, objects, archives, executable_name=example_name),
    )
    build_common.copy_shared_runtime(config, lib_dir, bin_dir, shared)

    if has_shaders:
        config.remove_includedirs(example_shaders_dir)


def build_test(config: powermake.Config, test_name: str, shared: bool):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    lib_dir = os.path.join(os.path.dirname(config.lib_build_directory), "lib")
    bin_dir = os.path.join(os.path.dirname(config.lib_build_directory), "bin")
    config.add_includedirs(include_dir)

    test_files = powermake.get_files(f"./tests/{test_name}/**/*.c")

    if shared:
        if config.target_is_macos():
            config.add_ld_flags("-Wl,-rpath,@executable_path/../lib")
        elif config.target_is_linux():
            config.add_ld_flags("-Wl,-rpath,$ORIGIN/../lib")

    objects = powermake.compile_files(config, test_files)

    archives = [
        build_common.link_target_path(config, lib_dir, "pigment_sdl", shared),
        build_common.link_target_path(config, lib_dir, "pigment_gltf", shared),
        build_common.link_target_path(config, lib_dir, "pigment", shared),
    ]

    print(
        f"{test_name} :",
        powermake.link_files(config, objects, archives, executable_name=test_name),
    )
    build_common.copy_shared_runtime(config, lib_dir, bin_dir, shared)
