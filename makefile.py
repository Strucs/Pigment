import powermake
import os
import shutil
import subprocess

INTERNAL_HEADERS = {"structs.h", "internal.h", "log_internal.h", "internal_alloc.h", "std_internal.h"}

CONVENIENCE_HEADERS = {"pigment.h", "pigment_std.h", "pigment_sdl.h", "pigment_vk.h"}

def public_path_for_header(file: str) -> str | None:
    parts = os.path.normpath(file).split(os.sep)
    if len(parts) < 3:
        return None
    module   = parts[1]
    filename = parts[-1]
    if filename in INTERNAL_HEADERS:
        return None
    if filename in CONVENIENCE_HEADERS:
        return f"pigment/{filename}"
    if module == "external":
        return filename if filename == "volk.h" else None
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
        target_path  = public_path_for_header(file)
        if target_path is None:
            continue
        dst = os.path.join(include_dir, target_path )
        powermake.utils.makedirs(os.path.dirname(dst))
        shutil.copy2(file, dst)

def build_shaders(src_pattern: str, shaders_dst_dir: str, touch_files: list[str]) -> bool:
    all_files = list(powermake.get_files(src_pattern))
    shader_files = [f for f in all_files if os.path.splitext(f)[1] in (".vert", ".frag", ".comp", ".geom", ".tesc", ".tese")]
    if not shader_files:
        return False

    powermake.utils.makedirs(shaders_dst_dir)
    shader_includes = [f for f in all_files if os.path.splitext(f)[1] in (".glsl", ".h")]

    any_shader_rebuilt = False
    for file in shader_files:
        base = os.path.basename(file)
        name, ext = os.path.splitext(base)
        dst = os.path.join(shaders_dst_dir, f"{name}_{ext[1:]}.spv")
        if compile_shader_to_spv(file, dst, shader_includes):
            any_shader_rebuilt = True

    if any_shader_rebuilt:
        for c_file in touch_files:
            if os.path.exists(c_file):
                os.utime(c_file, None)

    return True

def build_pigment(config: powermake.Config):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    shaders_dir = os.path.join(os.path.dirname(config.lib_build_directory), "shaders")

    copy_public_headers(include_dir)
    build_shaders("./shaders/*", shaders_dir, ["src/std/pipeline_loader.c", "src/std/canvas/canvas.c"])

    config.add_includedirs(include_dir, "src/core", "src/std", "src/external", "src/vulkan")
    config.add_flags(f"--embed-dir={shaders_dir}")

    all_files = powermake.get_files("./src/**/*.c")
    external_files = {f for f in all_files if os.sep + "external" + os.sep in os.path.normpath(f)}
    integration_files = {f for f in all_files if os.sep + "integrations" + os.sep in os.path.normpath(f)}
    tools_files = {f for f in all_files if os.sep + "tools" + os.sep in os.path.normpath(f)}
    project_files = set(all_files) - external_files - integration_files - tools_files

    ext_config = config.copy()
    ext_config.add_flags("-Wno-misleading-indentation")
    ext_objects = powermake.compile_files(ext_config, external_files)

    config.target_name = "pigment"
    objects = powermake.compile_files(config, project_files)
    powermake.archive_files(config, list(objects) + list(ext_objects))

    config.remove_includedirs(include_dir, "src/core", "src/std", "src/external", "src/vulkan")
    config.remove_flags(f"--embed-dir={shaders_dir}")

def build_sdl_integration(config: powermake.Config):
    sdl_files = list(powermake.get_files("./src/integrations/sdl/**/*.c"))
    if not sdl_files:
        return

    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    sdl_config = config.copy()
    sdl_config.target_name = "pigment_sdl"
    sdl_config.add_includedirs(include_dir, "src/core", "src/std", "src/integrations/sdl")

    objects = powermake.compile_files(sdl_config, sdl_files)
    powermake.archive_files(sdl_config, objects)

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
    powermake.archive_files(shaderc_config, objects)

def build_example(config: powermake.Config, example_name: str):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    lib_dir     = os.path.join(os.path.dirname(config.lib_build_directory), "lib")
    example_shaders_dir = os.path.join(os.path.dirname(config.lib_build_directory), "example_shaders", example_name)
    config.add_includedirs(include_dir)

    example_c_files = list(powermake.get_files(f"./examples/{example_name}/**/*.c"))
    has_shaders     = build_shaders(f"./examples/{example_name}/**/*", example_shaders_dir, example_c_files)
    if has_shaders:
        config.add_flags(f"--embed-dir={example_shaders_dir}")

    example_files = powermake.get_files(f"./examples/{example_name}/**/*.c")

    objects = powermake.compile_files(config, example_files)

    archives = [
        os.path.join(lib_dir, "libpigment.a"),
        os.path.join(lib_dir, "libpigment_sdl.a"),
    ]

    print(f"{example_name} :", powermake.link_files(config, objects, archives, executable_name=example_name))

    if has_shaders:
        config.remove_flags(f"--embed-dir={example_shaders_dir}")

def build_test(config: powermake.Config, test_name: str):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    lib_dir     = os.path.join(os.path.dirname(config.lib_build_directory), "lib")
    config.add_includedirs(include_dir)

    test_files = powermake.get_files(f"./tests/{test_name}/**/*.c")

    objects = powermake.compile_files(config, test_files)

    archives = [
        os.path.join(lib_dir, "libpigment.a"),
        os.path.join(lib_dir, "libpigment_sdl.a"),
    ]

    print(f"{test_name} :", powermake.link_files(config, objects, archives, executable_name=test_name))

def on_build(config: powermake.Config):

    config.add_c_flags("-std=c23")
    config.add_flags("-Wsecurity", "-pedantic")
    config.remove_flags("-Wconversion", "-Wsign-conversion")
    # config.remove_flags("-fanalyzer") # uncomment for way faster compilation

    if not config.debug:
            config.add_flags("-flto=auto")

    if config.target_is_macos():
        config.add_includedirs("/opt/homebrew/include")
        config.add_ld_flags("-L/opt/homebrew/lib")
        config.add_shared_libs("vulkan.1")

    build_pigment(config)
    build_sdl_integration(config)
    build_shaderc_tool(config)

    needs_sdl = any(getattr(args_parsed, example) for example in dir_list) or any(getattr(args_parsed, test) for test in test_list)
    if needs_sdl:
        config.add_shared_libs("SDL3")
        for example in dir_list:
            if getattr(args_parsed, example):
                build_example(config, example)
        for test in test_list:
            if getattr(args_parsed, test):
                build_test(config, test)

parser = powermake.ArgumentParser()

examples_dir = "./examples"
tests_dir    = "./tests"

dir_list  = [f for f in os.listdir(examples_dir) if not os.path.isfile(os.path.join(examples_dir, f))]
test_list = [f for f in os.listdir(tests_dir) if not os.path.isfile(os.path.join(tests_dir, f))] if os.path.isdir(tests_dir) else []

for example in dir_list:
    parser.add_argument(f"--{example}", help=f"build {example} example", action="store_true")

for test in test_list:
    parser.add_argument(f"--{test}", help=f"build {test} test", action="store_true")

args_parsed = parser.parse_args()

powermake.run("pigment", build_callback=on_build, args_parsed=args_parsed)
