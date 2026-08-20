# pbr_renderer
PBR renderer in c++ with vulkan

## Development

```bash
make build-debug 
make build-release 
make run
make format
make lint
```

## Build types

- **Debug** — Vulkan validation layers enabled. Errors and warnings are printed to stderr at runtime.
- **Release** — Validation layers disabled. Optimized for performance.

## Linting

Requires `clang-tidy` and `clang-format`:

```bash
sudo apt install clang-tidy clang-format
```
