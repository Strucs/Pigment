import os
import shutil
import subprocess
import plistlib

# Declares the framework as a Clang module so consumers can do
# `import <Name>` (Swift) / `@import <Name>;` (Obj-C).
MODULEMAP = """framework module {name} {{
    {umbrella}
    export *
    module * {{ export * }}
}}
"""


class FrameworkInfo:
    """
    Parameters for build_framework().
    """

    headers = ()
    headers_base_dir = None
    identifier = None
    version = "1"
    short_version = "1.0"
    min_os = "11.0"
    resources = ()
    flat = None
    platform = "MacOSX"
    region = "en"
    module = False
    umbrella_header = (
        None  # str, or list: [0] = umbrella header, rest = explicit headers
    )
    extra = None

    def __init__(self, output_dir, name, library_path, **options):
        self.output_dir = output_dir
        self.name = name
        self.library_path = library_path
        for key, value in options.items():
            if not hasattr(FrameworkInfo, key):
                raise TypeError(f"FrameworkSpec: unknown option {key!r}")
            setattr(self, key, value)


def _relink(link: str, target: str):
    """
    Create the symlink ``link`` -> ``target``.
    """
    if os.path.islink(link) or os.path.exists(link):
        os.remove(link)
    os.symlink(target, link)


def _install_name_tool(*args: str):
    """
    Run ``install_name_tool``, raising a clear error when the headerpad is too small.
    """
    r = subprocess.run(["install_name_tool", *args], capture_output=True, text=True)
    if r.returncode != 0:
        if "larger updated load commands do not fit" in r.stderr:
            raise RuntimeError(
                "install_name_tool: The binary must be linked with -Wl,-headerpad_max_install_names."
            )
        raise RuntimeError(f"install_name_tool {' '.join(args)}:\n{r.stderr}")


def build_framework(spec: FrameworkInfo) -> str:
    """
    Build an Apple ``.framework`` bundle from a prebuilt library.

    Parameters
    ----------
    spec : FrameworkSpec
        Everything describing the framework to build.

    Returns
    -------
    str
        Path to the created ``.framework`` bundle.
    """

    flat = spec.flat if spec.flat is not None else (spec.platform != "MacOSX")

    fw_root = os.path.join(spec.output_dir, f"{spec.name}.framework")
    if os.path.exists(fw_root):
        shutil.rmtree(fw_root)

    if flat:
        headers_d = os.path.join(fw_root, "Headers")
        modules_d = os.path.join(fw_root, "Modules")
        binary = os.path.join(fw_root, spec.name)
        plist_path = os.path.join(fw_root, "Info.plist")
        os.makedirs(headers_d, exist_ok=True)
    else:
        versions = os.path.join(fw_root, "Versions")
        current = os.path.join(versions, "A")
        headers_d = os.path.join(current, "Headers")
        modules_d = os.path.join(current, "Modules")
        res_d = os.path.join(current, "Resources")
        binary = os.path.join(current, spec.name)
        plist_path = os.path.join(res_d, "Info.plist")
        os.makedirs(headers_d, exist_ok=True)
        os.makedirs(res_d, exist_ok=True)

    rpath_prefix = f"@rpath/{spec.name}.framework" + ("" if flat else "/Versions/A")

    shutil.copy2(spec.library_path, binary)
    is_static = spec.library_path.endswith(".a")

    for header in spec.headers:
        rel = (
            os.path.relpath(header, spec.headers_base_dir)
            if spec.headers_base_dir
            else os.path.basename(header)
        )
        dst = os.path.join(headers_d, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(header, dst)

    if not flat:
        for resource in spec.resources:
            shutil.copy2(resource, os.path.join(res_d, os.path.basename(resource)))

    plist = {
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleDevelopmentRegion": spec.region,
        "CFBundleExecutable": spec.name,
        "CFBundleIdentifier": spec.identifier or f"com.unknown.{spec.name.lower()}",
        "CFBundleName": spec.name,
        "CFBundlePackageType": "FMWK",
        "CFBundleShortVersionString": spec.short_version,
        "CFBundleVersion": spec.version,
        "CFBundleSupportedPlatforms": [spec.platform],
        (
            "LSMinimumSystemVersion"
            if spec.platform == "MacOSX"
            else "MinimumOSVersion"
        ): spec.min_os,
    }
    plist.update(spec.extra or {})
    with open(plist_path, "wb") as f:
        plistlib.dump(plist, f)

    if spec.module:
        os.makedirs(modules_d, exist_ok=True)

        def _href(h):
            return (
                os.path.relpath(h, spec.headers_base_dir)
                if spec.headers_base_dir
                else os.path.basename(h)
            )

        primary = spec.umbrella_header
        if isinstance(primary, str):
            primary = [primary]
        if primary:
            lines = [f'umbrella header "{_href(primary[0])}"']
            lines += [f'header "{_href(h)}"' for h in primary[1:]]
            umbrella = "\n    ".join(lines)
        elif os.path.exists(os.path.join(headers_d, f"{spec.name}.h")):
            umbrella = f'umbrella header "{spec.name}.h"'
        else:
            umbrella = 'umbrella "."'
        with open(os.path.join(modules_d, "module.modulemap"), "w") as f:
            f.write(MODULEMAP.format(name=spec.name, umbrella=umbrella))

    if not flat:
        _relink(os.path.join(versions, "Current"), "A")
        _relink(os.path.join(fw_root, spec.name), f"Versions/Current/{spec.name}")
        _relink(os.path.join(fw_root, "Headers"), "Versions/Current/Headers")
        _relink(os.path.join(fw_root, "Resources"), "Versions/Current/Resources")
        if spec.module:
            _relink(os.path.join(fw_root, "Modules"), "Versions/Current/Modules")

    if not is_static:
        _install_name_tool("-id", f"{rpath_prefix}/{spec.name}", binary)

    print(f"[framework] {fw_root}{' (static)' if is_static else ''}")
    return fw_root


def build_xcframework(output_path: str, frameworks: list[str]) -> str:
    """
    Bundle several ``.framework`` into a single ``.xcframework``.
    """
    if os.path.exists(output_path):
        shutil.rmtree(output_path)
    args = ["xcodebuild", "-create-xcframework"]
    for fw in frameworks:
        args += ["-framework", fw]
    args += ["-output", output_path]
    subprocess.run(args, check=True)
    print(f"[xcframework] {output_path}")
    return output_path
