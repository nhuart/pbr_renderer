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

## Running a scene

A scene file must be passed as the first argument:

```bash
make run-debug SCENE=scenes/viking_room.json
make run-release SCENE=scenes/three_viking_rooms_particles.json
```

## Scene format

Scenes are defined in JSON. The binary must be run from the repository root so that asset paths resolve correctly.

```json
{
    "meshInstances": [
        {
            "gltf": "models/viking_room.glb",
            "texture": "textures/viking_room.ktx2",
            "position": [0.0, 0.0, 0.0],
            "rotation": [0.0, 0.0, 45.0],
            "scale": [0.75, 0.75, 0.75]
        }
    ],
    "particles": {
        "count": 8192
    }
}
```

**`meshInstances`** (required) — array of mesh objects to render.

| Field | Type | Default | Description |
|---|---|---|---|
| `gltf` | string | — | Path to a `.glb` / `.gltf` model |
| `texture` | string | — | Path to a `.ktx2` texture |
| `position` | `[x, y, z]` | `[0,0,0]` | World-space position |
| `rotation` | `[x, y, z]` | `[0,0,0]` | Euler angles in degrees, applied X→Y→Z |
| `scale` | `[x, y, z]` | `[1,1,1]` | Per-axis scale |

Multiple instances can reference the same `gltf` path — mesh data is uploaded to the GPU only once.

**`particles`** (optional) — when present, enables the GPU particle system.

| Field | Type | Default | Description |
|---|---|---|---|
| `count` | integer | `8192` | Number of particles |

Omitting the `particles` key disables all compute and particle rendering infrastructure entirely.

## Code structure

```
src/
  core/        Raw Vulkan: device, swapchain, memory, sync
    context.hpp/.cpp          VulkanContext — instance, physical/logical device, queues, surface
    swapchain.hpp/.cpp        Swapchain — images, image views, MSAA color/depth attachments
    command_service.hpp/.cpp  CommandService — command pool, per-frame command buffers
    resource_allocator.hpp/.cpp  vkutil:: free functions — createBuffer, createImage, etc.
    sync.hpp/.cpp             SyncObjects — semaphores and fences

  renderer/    Pipelines and GPU resources
    renderer.hpp/.cpp         Renderer — thin orchestrator, owns all subsystems, frame loop
    mesh_pipeline.hpp/.cpp    MeshPipeline — descriptor layout, graphics pipeline, descriptor sets
    mesh_buffer.hpp/.cpp      MeshBuffer — glTF loading, vertex/index GPU buffers
    texture_atlas.hpp/.cpp    TextureAtlas — texture image, view, sampler
    particle_pipeline.hpp/.cpp  ParticlePipeline — SSBOs, compute pipeline, particle graphics

  scene/       CPU-side data
    types.hpp                 Vertex, GameObject, Scene, UBO structs
    scene_loader.hpp/.cpp     loadScene() — parses JSON into Scene

scenes/        Scene definition files
shaders/       GLSL source + compiled SPIR-V
models/        glTF assets
textures/      KTX2 textures
```

Dependency flow: `scene/` ← `core/` ← `renderer/`. Nothing in `core/` knows about pipelines; nothing in `scene/` touches Vulkan.

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
