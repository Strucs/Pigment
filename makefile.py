import os

import powermake

from scripts import build_common, build_examples, build_lib, build_package, build_tests


def on_build(config: powermake.Config):

    build_common.configure_build(config)

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

    pigment_artifact = build_lib.build_pigment(config, shared_build)
    build_package.build_framework_package(config, shared_build)
    sdl_artifact = build_lib.build_sdl_integration(config)
    gltf_artifact = build_lib.build_gltf_tool(config)
    build_lib.build_shaderc_tool(config)

    needs_sdl = any(getattr(args_parsed, example) for example in example_list)
    if needs_sdl:
        config.add_shared_libs("SDL3")
        archives = [a for a in (sdl_artifact, gltf_artifact, pigment_artifact) if a]
        for example in example_list:
            if getattr(args_parsed, example):
                build_examples.build_example(config, example, shared_build, archives)


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

example_list = (
    [
        f
        for f in os.listdir(examples_dir)
        if not os.path.isfile(os.path.join(examples_dir, f))
    ]
    if os.path.isdir(examples_dir)
    else []
)

for example in example_list:
    parser.add_argument(
        f"--{example}", help=f"build {example} example", action="store_true"
    )

args_parsed = parser.parse_args()
shared_build = bool(getattr(args_parsed, "shared", False))


def on_test(config: powermake.Config, args: list[str]):
    build_tests.on_test(config, args, shared_build)


powermake.run(
    "pigment",
    build_callback=on_build,
    test_callback=on_test,
    args_parsed=args_parsed,
)
