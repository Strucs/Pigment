import os

import powermake

from scripts import build_common, build_lib, build_examples, build_package


def on_build(config: powermake.Config):

    config.add_flags("-Wsecurity", "-pedantic")
    config.remove_flags("-Wconversion", "-Wsign-conversion")
    # config.remove_flags("-fanalyzer") # uncomment for way faster compilation

    if build_common.is_msvc(config):
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
        config.add_c_flags("-std=c17")
        if not config.debug:
            config.add_c_flags("-flto=auto")

    if config.target_is_mingw():
        config.shared_linker.shared_lib_extension = ".dll"

    if config.target_is_macos():
        config.shared_linker.shared_lib_extension = ".dylib"
        config.add_includedirs("/opt/homebrew/include")
        config.add_ld_flags("-L/opt/homebrew/lib")

    if getattr(args_parsed, "xcframework", False):
        if config.debug:
            raise SystemExit(
                "--xcframework is a release artifact: build without -d/--debug"
            )
        if config.c_compiler is None or not config.c_compiler.type.startswith("clang"):
            raise SystemExit(
                "--xcframework requires a clang compiler (Apple toolchain)"
            )
        build_package.build_all_apple(config)
        return

    build_lib.build_pigment(config, shared_build)
    build_package.build_framework_package(config, shared_build)
    build_lib.build_sdl_integration(config)
    build_lib.build_gltf_tool(config)
    build_lib.build_shaderc_tool(config)

    needs_sdl = any(getattr(args_parsed, example) for example in dir_list) or any(
        getattr(args_parsed, test) for test in test_list
    )
    if needs_sdl:
        config.add_shared_libs("SDL3")
        for example in dir_list:
            if getattr(args_parsed, example):
                build_examples.build_example(config, example, shared_build)
        for test in test_list:
            if getattr(args_parsed, test):
                build_examples.build_test(config, test, shared_build)


parser = powermake.ArgumentParser()
parser.add_argument(
    "--shared",
    help="build pigment as a shared library instead of a static archive",
    action="store_true",
)
parser.add_argument(
    "--xcframework",
    help="build libpigment for every Apple slice and bundle a Pigment.xcframework",
    action="store_true",
)

examples_dir = "./examples"
tests_dir = "./tests"

dir_list = [
    f
    for f in os.listdir(examples_dir)
    if not os.path.isfile(os.path.join(examples_dir, f))
]
test_list = (
    [f for f in os.listdir(tests_dir) if not os.path.isfile(os.path.join(tests_dir, f))]
    if os.path.isdir(tests_dir)
    else []
)

for example in dir_list:
    parser.add_argument(
        f"--{example}", help=f"build {example} example", action="store_true"
    )

for test in test_list:
    parser.add_argument(f"--{test}", help=f"build {test} test", action="store_true")

args_parsed = parser.parse_args()
shared_build = bool(getattr(args_parsed, "shared", False))

powermake.run("pigment", build_callback=on_build, args_parsed=args_parsed)
