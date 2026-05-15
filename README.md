# Pigment
Pigment is a graphic library written in C using Vulkan API.

## Requirements

- C compiler
- [Vulkan SDK](https://vulkan.lunarg.com/)
- [SDL3](https://www.libsdl.org/)
- [Shaderc](https://github.com/google/shaderc)
- [Powermake](https://github.com/mactul/powermake) for compilation

## Compilation

To compile Pigment in release mode, run :
```sh
python makefile.py -rv
```

To compile Pigment in debug mode, run
```sh
python makefile.py -rvd
```

The compiled library will be available in the `build` directory.

## Usage

The [examples](examples/) directory contains sample code demonstrating how to use Pigment.

You can compile those examples with :
```sh
python makefile.py -rvd --"example_name"
```

For instance, to compile [suzanne_and_cube](examples/suzanne_and_cube) example, run :

```sh
python makefile.py -rvd --suzanne_and_cube
```
