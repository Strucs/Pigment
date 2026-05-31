import os
import sys


def symbol_name(spv_path: str) -> str:
    base = os.path.splitext(os.path.basename(spv_path))[0]
    return f"{base}_spv"


def header_path(spv_path: str, header_dir: str) -> str:
    return os.path.join(header_dir, symbol_name(spv_path) + ".h")


def generate(spv_path: str, header_dir: str) -> str:
    dst = header_path(spv_path, header_dir)

    if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(spv_path):
        return dst

    with open(spv_path, "rb") as f:
        data = f.read()

    name = symbol_name(spv_path)
    bytes_per_line = 16

    guard = f"PIGMENT_{name.upper()}_H"

    os.makedirs(header_dir, exist_ok=True)
    with open(dst, "w", newline="\n") as f:
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")
        f.write("#include <stdalign.h>\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"alignas(uint32_t) static const unsigned char {name}[] = {{")
        for i, byte in enumerate(data):
            if i % bytes_per_line == 0:
                f.write("\n    ")
            f.write(f"0x{byte:02x},")
            if (i + 1) % bytes_per_line != 0 and i + 1 < len(data):
                f.write(" ")
        f.write("\n};\n\n")
        f.write(f"enum {{ {name}_size = {len(data)} }};\n\n")
        f.write("#endif\n")

    return dst


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: spv_to_header.py <input.spv> <header_dir>", file=sys.stderr)
        sys.exit(1)
    print(generate(sys.argv[1], sys.argv[2]))
