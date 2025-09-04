import powermake
import os
import shutil

def build_static_lib(config: powermake.Config):
    files = powermake.get_files(f"./src/**/*.c")

    headers = powermake.get_files(f"./src/**/*.h")

    shaders = powermake.get_files(f"./shaders/*")

    include_dir = os.path.join(os.path.dirname(config.lib_build_directory), "include")
    shaders_dir = os.path.join(os.path.dirname(config.lib_build_directory), "shaders")

    powermake.utils.makedirs(shaders_dir)

    for file in headers:
        dir = os.path.dirname(file)
        parts = os.path.normpath(dir).split(os.sep)
        if len(parts) >= 2:
            new_dir = os.path.join(*parts[1:])
        else:
            new_dir = ''
        new_dir = os.path.join(include_dir, new_dir)
        powermake.utils.makedirs(new_dir)
        shutil.copy2(file, new_dir)

    for file in shaders:
        shutil.copy2(file, shaders_dir)

    objects = powermake.compile_files(config, files)

    powermake.archive_files(config, objects)

def build_example(config: powermake.Config, example_name: str):
    example_files = powermake.get_files(f"./examples/{example_name}/**/*.c")

    objects = powermake.compile_files(config, example_files)

    lib_dir = os.path.join(os.path.dirname(config.lib_build_directory), "lib")

    archive = [os.path.join(lib_dir, "libpigment.a")]

    print(f"{example_name} :", powermake.link_files(config, objects, archive, executable_name=example_name))

def on_build(config: powermake.Config):

    config.target_name = "pigment"
    config.add_includedirs("./src")

    config.add_flags("-Wsecurity", "-pedantic")
    config.remove_flags("-Wconversion", "-Wsign-conversion")
    # config.remove_flags("-fanalyzer") # uncomment for way faster compilation

    if not config.debug:
            config.add_flags("-flto=auto")

    if config.target_is_windows():
        config.add_shared_libs("glfw3", "vulkan-1", "shaderc_shared")
    elif config.target_is_macos():
        config.add_includedirs("/opt/homebrew/include")
        config.add_ld_flags("-L/opt/homebrew/lib")
        config.add_shared_libs("glfw.3.4", "vulkan.1", "shaderc_shared")
    else:
        config.add_shared_libs("glfw", "vulkan", "shaderc_shared")

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