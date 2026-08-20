# pbr_renderer
PBR renderer in c++ with vulkan

## Development

```bash
make build-debug    # debug build (validation layers on)
make build-release  # release build (optimized)
make run            # build and run debug
make format         # format all source files
make lint           # run clang-tidy on all source files
```

## Build types

- **Debug** — Vulkan validation layers enabled. Errors and warnings are printed to stderr at runtime.
- **Release** — Validation layers disabled. Optimized for performance.

## Linting

Requires `clang-tidy` and `clang-format`:

```bash
sudo apt install clang-tidy clang-format
```
