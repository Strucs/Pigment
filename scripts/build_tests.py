import os
import subprocess

import powermake

from scripts import build_common, build_lib


def build_test(
    config: powermake.Config,
    test_name: str,
    shared: bool,
    archives: list[str],
) -> str:
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    test_shaders_dir = os.path.join(
        os.path.dirname(config.lib_build_directory), "test_shaders", test_name
    )
    config.add_includedirs(include_dir)

    test_files = list(powermake.get_files(f"./tests/{test_name}/**/*.c"))
    if build_common.build_shaders(
        f"./tests/{test_name}/**/*", test_shaders_dir, test_files
    ):
        config.add_includedirs(test_shaders_dir)

    if shared:
        if config.target_is_macos():
            config.add_ld_flags("-Wl,-rpath,@executable_path/../lib")
        elif config.target_is_linux():
            config.add_ld_flags("-Wl,-rpath,$ORIGIN/../lib")

    objects = powermake.compile_files(config, test_files)

    return powermake.link_files(config, objects, archives, executable_name=test_name)


def on_test(config: powermake.Config, args: list[str], shared: bool):
    test_names = sorted(
        name
        for name in os.listdir("tests")
        if os.path.isdir(os.path.join("tests", name))
    )
    selected = args[0] if args else "all"
    if len(args) > 1:
        raise SystemExit("Usage: makefile.py test [test | all]")
    if selected != "all" and selected not in test_names:
        raise SystemExit(
            f"Unknown test '{selected}'. Choose: all, {', '.join(test_names)}"
        )

    build_common.configure_build(config)
    config.add_shared_libs("SDL3")
    pigment_artifact = build_lib.build_pigment(config, shared)
    sdl_artifact = build_lib.build_sdl_integration(config)
    archives = [artifact for artifact in (sdl_artifact, pigment_artifact) if artifact]

    for name in test_names if selected == "all" else [selected]:
        test_config = config.copy()
        test_config.target_name = name
        test_config.obj_build_directory = os.path.join(
            config.obj_build_directory, "tests", name
        )
        test_config.add_includedirs("tests")
        test_config.remove_defines("NDEBUG")
        test_config.add_c_flags(
            "/UNDEBUG" if build_common.is_msvc(test_config) else "-UNDEBUG"
        )
        executable = build_test(test_config, name, shared, archives)
        print(f"Running {name} tests...", flush=True)
        result = subprocess.run([os.path.abspath(executable)], check=False)
        if result.returncode != 0:
            raise SystemExit(f"Test '{name}' failed (exit code {result.returncode}).")
