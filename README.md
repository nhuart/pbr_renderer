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

Dependency flow: `scene/` ← `core/` ← `renderer/`. Nothing in `core/` knows about pipelines; nothing in `scene/` touches Vulkan.

## TODO

- [ ] Replace per-resource `allocateMemory` calls with a custom allocator or [VulkanMemoryAllocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator). The `maxMemoryAllocationCount` limit can be as low as 4096 on high-end hardware — one allocation per buffer doesn't scale.
- [ ] Pack multiple buffers (e.g. vertex + index) into a single `vk::raii::Buffer` using offsets in `bindVertexBuffers` etc. Improves cache locality. Also enables memory aliasing — reusing the same memory range for different resources that are never active in the same frame.
- [ ] Precompute mipmaps offline (e.g. with `toktx`) and store them in `.ktx2` or `.dds` files instead of generating at runtime with `vkCmdBlitImage`. Moves the cost to build time, allows higher-quality filters (e.g. Lanczos), and enables compressed formats like BC7 that halve VRAM usage.
- [ ] Add fallbacks for devices that don't support Vulkan 1.3 features: use traditional render passes (`vkCreateRenderPass` / `vkCmdBeginRenderPass`) instead of dynamic rendering (`VK_KHR_dynamic_rendering` / `vkCmdBeginRenderingKHR`), and similarly fall back for other 1.3-only features (e.g. synchronization2, extended dynamic state). Gate at startup with `vkGetPhysicalDeviceProperties2` version check.
- [ ] Add topological sorting to `RenderGraph::buildBarriers()` so passes can be registered in any order and the graph resolves execution sequence automatically from read/write dependencies. Currently passes execute in declaration order, which requires the caller to register them in dependency order — fine for one pass but fragile as more passes are added (shadow maps, deferred lighting, post-process).
- [ ] Split `geometrySchlickGGX` into two variants. Keep `k = (r+1)²/8` for direct lights — this remapping prevents a hotspot artifact at grazing angles on smooth surfaces by ensuring `k > 0` even at `roughness = 0`. Use `k = r²/2` for the IBL specular path — this is the form the split-sum derivation assumes; applying the direct-light remapping to the precomputed integral produces over-darkened rough surfaces.
- [ ] Add frustum culling: compute the 6 frustum planes from the view-projection matrix each frame and cull `RenderObject`s whose bounding sphere lies fully outside any plane before recording draw calls. Eliminates GPU work for off-screen objects at near-zero CPU cost.
- [ ] Add energy compensation to the IBL specular path (Karis 2017): multiply the specular term by a `energyCompensation` factor that corrects for multi-scattering energy loss at high roughness. Without it, rough metals go unrealistically dark because the single-scattering BRDF doesn't account for inter-microsurface bounces. Filament implements this as `Fr *= energyCompensation` where the factor is derived from the DFG LUT.
- [ ] Use Vulkan [profiles](https://github.com/KhronosGroup/Vulkan-Profiles) (`VP_KHR_roadmap_2022`, or a custom profile) to declare and test the exact feature/extension requirements at initialization. Profiles provide a portable, machine-readable capability contract and a built-in simulation layer to test fallback paths on hardware that would otherwise satisfy the requirements.
- [ ] Replace the dependency on Filament's `cmgen` for IBL asset generation with a self-contained baking tool in this repo. The tool should take an equirectangular EXR as input and produce the three KTX2 files (`*_irradiance.ktx2`, `*_prefilter.ktx2`, `brdf_lut.ktx2`) that `IblEnvironment` expects. Quality targets: irradiance via SH9 projection (equivalent to infinite samples, no banding), prefilter via GGX importance sampling at ≥1024 samples per texel, BRDF LUT at 512×512 RG32F.
- [ ] Replace the dummy-bind fallback for absent optional textures (e.g. normal map) with a structured pipeline permutation system. Currently, missing textures are covered by binding a placeholder descriptor (e.g. the albedo texture reused for the normal map slot) to satisfy Vulkan's requirement that all declared descriptor slots are bound, and variant selection is done ad-hoc in `resolveFragShader` via nested if-chains that don't scale as feature axes are added. Options in increasing architectural cleanliness: (a) encode variant axes (IBL, normal map, …) as bitmask flags and look up the pipeline in a flat table; (b) define a `ShaderVariant` struct with a hash and use it as the map key, replacing all branching with a single lookup; (c) bake the resolved pipeline key into the material at load time so the renderer does no variant logic at all — just `inst.resolvedShader`. Option (c) is architecturally correct: the renderer should only draw, not reason about feature combinations.
- [ ] Extend shadow mapping to spot and point lights. **Spot lights** are straightforward: replace `glm::ortho` with `glm::perspective` using the spot cone angle as FOV, and place the light at the spot position looking along its direction. The fragment shader projection code is identical to the directional case. Requires `updateLightSpaceMatrix` to accept a light-type union or overload, and `ShadowMap` construction to be driven by light type rather than being hardcoded to `DirectionalLight`. **Point lights** require a cubemap shadow map: render 6 depth-only passes (one per cube face) using a 90° perspective projection, store results in a `vk::ImageViewType::eCube` image, and sample in the fragment shader with `texture(shadowCubemap, fragPos - lightPos)` using a direction vector instead of projected UV coordinates. The render graph gains 5 extra passes per point light (6 total), and per-object UBOs need 6 light-space matrices. Both light types also require generalizing `ShadowMap` away from the current single-instance design — one `ShadowMap` per shadow-casting light, with the material binding updated to support an array of shadow maps or a shadow atlas.

