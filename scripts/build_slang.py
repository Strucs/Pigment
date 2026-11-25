import os
import re
import shutil
from collections.abc import Iterable
from dataclasses import dataclass

import powermake

from scripts import spv_to_header


@dataclass(frozen=True)
class SlangShader:
    source: str
    entry_point: str
    stage: str
    name: str  # Output basename, e.g. "crt_frag" -> crt_frag_spv.h.
    include_dirs: Iterable[str] = ()
    profile: str | None = None

    def __post_init__(self):
        object.__setattr__(self, "include_dirs", tuple(self.include_dirs))


SLANG_STAGES = {"vertex", "fragment", "compute", "mesh", "task"}


def compile_slang_to_spv(
    config: powermake.Config,
    shader: SlangShader,
    dst: str,
) -> str:
    if shader.stage not in SLANG_STAGES:
        raise SystemExit(f"Unsupported Slang stage: {shader.stage}")
    if not shader.entry_point:
        raise SystemExit(f"Missing Slang entry point for {shader.source}")

    compiler = shutil.which("slangc")
    if compiler is None:
        raise SystemExit("slangc not found: install Slang and add it to PATH.")

    profile = shader.profile or (
        "spirv_1_5" if shader.stage == "fragment" else "spirv_1_6"
    )

    command = [
        compiler,
        shader.source,
        "-entry",
        shader.entry_point,
        "-stage",
        shader.stage,
        "-target",
        "spirv",
        "-profile",
        profile,
        "-emit-spirv-directly",
        "-fvk-use-entrypoint-name",
        "-matrix-layout-column-major",
        "-o",
        dst,
    ]

    for directory in shader.include_dirs:
        command.extend(["-I", directory])

    shader_config = config.copy()
    shader_config.additional_includedirs = list(shader.include_dirs)
    return powermake.run_command_if_needed(
        shader_config, dst, (shader.source, __file__, compiler), command
    )


def build_shaders(
    config: powermake.Config,
    shaders: Iterable[SlangShader],
    shaders_dst_dir: str,
) -> bool:
    shaders = tuple(shaders)
    if not shaders:
        return False

    output_names = set()
    for shader in shaders:
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", shader.name):
            raise SystemExit(f"Invalid Slang output name: {shader.name}")
        if shader.name in output_names:
            raise SystemExit(f"Duplicate shader output name: {shader.name}")
        output_names.add(shader.name)

        os.makedirs(shaders_dst_dir, exist_ok=True)
    for shader in shaders:
        dst = os.path.join(shaders_dst_dir, shader.name + ".spv")
        spv = compile_slang_to_spv(config, shader, dst)
        spv_to_header.generate(spv, shaders_dst_dir)

    return True
