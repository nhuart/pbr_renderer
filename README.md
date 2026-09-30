# pbr_renderer
PBR renderer in c++ with vulkan

## Development

### Prerequisites

- CMake 3.20+
- Vulkan SDK 1.4+ at `~/vulkansdk/1.4.x/`
- `clang-format`, `clang-tidy` (`sudo apt install clang-format clang-tidy`)
- `glfw3`, `glm`, `libtinygltf-dev` (`sudo apt install libglfw3-dev libglm-dev libtinygltf-dev`)
- KTX-Software and nlohmann/json are fetched automatically by CMake via FetchContent

### First-time setup

```bash
make configure-debug    # generate build/debug with compile_commands.json
make configure-release  # generate build/release
```

### Build

```bash
make build-debug    # debug build (Vulkan validation layers enabled)
make build-release  # optimized build
```

### Run

```bash
make run-debug   SCENE=scenes/viking_room.json
make run-release SCENE=scenes/viking_room.json
```

To capture a screenshot on the first rendered frame, pass `--screenshot` via `ARGS`:

```bash
make run-release SCENE=scenes/viking_room.json ARGS="--screenshot output.png"
```

### Code quality

```bash
make format     # clang-format all .cpp/.hpp/.h in-place
make lint       # clang-tidy static analysis (read-only)
make lint-fix   # clang-tidy with auto-fix
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
  core/      Raw Vulkan — device, swapchain, memory, sync primitives
  renderer/  GPU resources — camera, materials, meshes, textures, particles
  scene/     CPU data — scene graph, JSON loading, UBO types
```

## TODO

- [ ] Replace per-resource `allocateMemory` with [VulkanMemoryAllocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) — `maxMemoryAllocationCount` can be as low as 4096, one allocation per buffer doesn't scale.
- [ ] Pack vertex + index buffers into a single `vk::raii::Buffer` with offsets. Improves cache locality and enables memory aliasing between frames.
- [ ] Precompute mipmaps offline with `toktx` and store as `.ktx2` instead of generating at runtime with `vkCmdBlitImage`. Enables higher-quality filters and BC7 compressed formats.
- [ ] Add Vulkan 1.3 fallbacks: use traditional render passes instead of dynamic rendering, gate at startup with a version check.
- [ ] Add topological sorting to `RenderGraph::buildBarriers()` so passes can be registered in any order. Currently passes must be declared in dependency order.
- [ ] Add frustum culling: cull `RenderObject`s whose bounding sphere is outside the 6 frustum planes before recording draw calls.
- [ ] Use Vulkan [profiles](https://github.com/KhronosGroup/Vulkan-Profiles) (`VP_KHR_roadmap_2022`) to declare feature requirements at initialization and test fallback paths via the simulation layer.
- [ ] Replace dependency on Filament's `cmgen` with a self-contained IBL baking tool: equirectangular EXR → `*_irradiance.ktx2` (SH9), `*_prefilter.ktx2` (GGX importance sampling), `brdf_lut.ktx2` (512×512 RG32F).
- [ ] Upgrade PCF shadow rotation to Filament's stochastic approach: Poisson disk generated on CPU each frame, rotated per-pixel with a frame-counter seed. Current static hash produces the same grain every frame.
- [ ] Add glTF `metallicRoughnessTexture` support: G=roughness, B=metallic, sampled in `pbr.frag` via a new `ShaderFeatures::MetallicRoughnessMap` flag, falling back to scalar `pbrParams` when absent.
- [ ] Add glTF `emissiveTexture` + `emissiveFactor` support: bind under `ShaderFeatures::Emissive`, add `emissiveFactor * texture(emissiveSampler, uv).rgb` to final color before tone mapping.
- [ ] Add glTF `occlusionTexture` support: R channel of the metallic-roughness image, multiply ambient/IBL term by the AO value in `pbr.frag`.
- [ ] Add multi-bounce AO to `pbr.frag`: replace `ambient *= ao` with Filament's color-aware `gtaoMultiBounce` approximation for diffuse IBL, and handle specular IBL occlusion separately instead of applying the same AO factor to both.
- [ ] Enable bent normals in GTAO: output the bent-normal direction alongside AO visibility and use it for specular ambient occlusion, as in Filament's `bentNormals` mode.
- [ ] Switch SAO normals from the view-space normals prepass to depth-derived reconstruction (Yuwen Wu method, 8 samples like Filament's `computeViewSpaceNormalHighQ`). Blocker: requires reversed-Z depth (near=1, far=0) to avoid banding — flip depth compare op to `eGreater`, update `linearizeDepth`, set clear value to 0.0f.
- [ ] Add transparency: alpha blending (src-alpha / one-minus-src-alpha, depth write off) when `alphaMode == BLEND`, sort transparent objects back-to-front, add `ShaderFeatures::Blend`.
- [ ] Add screen-space refraction: mipmap opaque color buffer after the opaque pass, sample it in a second pass using a Snell's Law refracted UV offset, use roughness to select LOD.
- [ ] Replace the skybox pass-through check in `sao_blur.frag` (`center.g * center.b >= 0.9999`) with Filament's implicit approach: store raw view-space Z in GB channels so skybox pixels are naturally rejected by the bilateral weight without a special-case branch.
- [ ] Extend shadow mapping to spot lights (perspective projection, spot cone FOV) and point lights (cubemap: 6 depth passes, `vk::ImageViewType::eCube`, direction-vector sampling). Requires one `ShadowPipeline` per shadow-casting light and a shadow atlas or array binding.
