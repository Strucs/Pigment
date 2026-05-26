import os

import powermake

from scripts import build_common


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

    print(
        f"{example_name} :",
        powermake.link_files(config, objects, archives, executable_name=example_name),
    )

    if has_shaders:
        config.remove_includedirs(example_shaders_dir)


def build_test(
    config: powermake.Config,
    test_name: str,
    shared: bool,
    archives: list[str],
):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    config.add_includedirs(include_dir)

    test_files = powermake.get_files(f"./tests/{test_name}/**/*.c")

    if shared:
        if config.target_is_macos():
            config.add_ld_flags("-Wl,-rpath,@executable_path/../lib")
        elif config.target_is_linux():
            config.add_ld_flags("-Wl,-rpath,$ORIGIN/../lib")

    objects = powermake.compile_files(config, test_files)

    print(
        f"{test_name} :",
        powermake.link_files(config, objects, archives, executable_name=test_name),
    )
