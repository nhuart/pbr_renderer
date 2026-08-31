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

## TODO

- [ ] Replace per-resource `allocateMemory` calls with a custom allocator or [VulkanMemoryAllocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator). The `maxMemoryAllocationCount` limit can be as low as 4096 on high-end hardware — one allocation per buffer doesn't scale.
- [ ] Pack multiple buffers (e.g. vertex + index) into a single `vk::raii::Buffer` using offsets in `bindVertexBuffers` etc. Improves cache locality. Also enables memory aliasing — reusing the same memory range for different resources that are never active in the same frame.
- [ ] Precompute mipmaps offline (e.g. with `toktx`) and store them in `.ktx2` or `.dds` files instead of generating at runtime with `vkCmdBlitImage`. Moves the cost to build time, allows higher-quality filters (e.g. Lanczos), and enables compressed formats like BC7 that halve VRAM usage.
- [ ] Add fallbacks for devices that don't support Vulkan 1.3 features: use traditional render passes (`vkCreateRenderPass` / `vkCmdBeginRenderPass`) instead of dynamic rendering (`VK_KHR_dynamic_rendering` / `vkCmdBeginRenderingKHR`), and similarly fall back for other 1.3-only features (e.g. synchronization2, extended dynamic state). Gate at startup with `vkGetPhysicalDeviceProperties2` version check.
- [ ] Use Vulkan [profiles](https://github.com/KhronosGroup/Vulkan-Profiles) (`VP_KHR_roadmap_2022`, or a custom profile) to declare and test the exact feature/extension requirements at initialization. Profiles provide a portable, machine-readable capability contract and a built-in simulation layer to test fallback paths on hardware that would otherwise satisfy the requirements.

## Linting

Requires `clang-tidy` and `clang-format`:

```bash
sudo apt install clang-tidy clang-format
```
