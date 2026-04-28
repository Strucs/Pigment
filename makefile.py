import powermake
import os
import shutil

def build_static_lib(config: powermake.Config):
    config.add_includedirs("src/core", "src/loader", "src/external", "src/vulkan")

    all_files = powermake.get_files("./src/**/*.c")
    external_files = {f for f in all_files if os.sep + "external" + os.sep in os.path.normpath(f)}
    project_files = set(all_files) - external_files

    headers = powermake.get_files("./src/**/*.h")
    shaders = powermake.get_files("./shaders/*")
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    shaders_dir = os.path.join(os.path.dirname(config.lib_build_directory), "shaders")

    powermake.utils.makedirs(shaders_dir)

    for file in headers:
        parts = os.path.normpath(file).split(os.sep)
        # parts = ['src', '<module>', ... , 'file.h']
        if len(parts) < 3:
            continue
        module = parts[1]
        if module == "external" and parts[-1] != "volk.h":
            continue
        if module == "core" and parts[-1] in ("structs.h", "internal.h", "log_internal.h"):
            continue
        rest_parts = parts[2:-1]
        if module == "core" or module == "external":
            new_dir = os.path.join(include_dir, *rest_parts)
        else:
            new_dir = os.path.join(include_dir, module, *rest_parts)
        powermake.utils.makedirs(new_dir)
        shutil.copy2(file, new_dir)

    for file in shaders:
        shutil.copy2(file, shaders_dir)

    ext_config = config.copy()
    ext_config.add_flags("-Wno-misleading-indentation")
    ext_objects = powermake.compile_files(ext_config, external_files)

    objects = powermake.compile_files(config, project_files)

    powermake.archive_files(config, list(objects) + list(ext_objects))

    config.remove_includedirs("src/core", "src/loader", "src/external", "src/vulkan")

def build_example(config: powermake.Config, example_name: str):
    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    config.add_includedirs(include_dir)

    example_files = powermake.get_files(f"./examples/{example_name}/**/*.c")

    objects = powermake.compile_files(config, example_files)

    lib_dir = os.path.join(os.path.dirname(config.lib_build_directory), "lib")

    archive = [os.path.join(lib_dir, "libpigment.a")]

    print(f"{example_name} :", powermake.link_files(config, objects, archive, executable_name=example_name))

def on_build(config: powermake.Config):

    config.target_name = "pigment"

    config.add_c_flags("-std=c23")
    config.add_flags("-Wsecurity", "-pedantic")
    config.remove_flags("-Wconversion", "-Wsign-conversion")
    # config.remove_flags("-fanalyzer") # uncomment for way faster compilation

    if not config.debug:
            config.add_flags("-flto=auto")

    if config.target_is_windows():
        config.add_shared_libs("SDL3", "shaderc_shared")
    elif config.target_is_macos():
        config.add_includedirs("/opt/homebrew/include")
        config.add_ld_flags("-L/opt/homebrew/lib")
        config.add_shared_libs("SDL3", "vulkan.1", "shaderc_shared")
    else:
        config.add_shared_libs("SDL3", "shaderc_shared")

    build_static_lib(config)

    for example in dir_list:
        if getattr(args_parsed, example):
            build_example(config, example)


parser = powermake.ArgumentParser()

examples_dir = "./examples"

dir_list = [f for f in os.listdir(examples_dir) if not os.path.isfile(os.path.join(examples_dir, f))]

for example in dir_list:
    parser.add_argument(f"--{example}", help=f"build {example} example", action="store_true")

args_parsed = parser.parse_args()

powermake.run("pigment", build_callback=on_build, args_parsed=args_parsed)
