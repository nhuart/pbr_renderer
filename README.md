# pbr_renderer
This project is a forward PBR renderer in C++ with Vulkan. It does not yet use clustered forward rendering.

The primary goal of this project was to learn Vulkan. After working through this Vulkan [tutorial](https://docs.vulkan.org/tutorial/latest/00_Introduction.html), my aim was not to reinvent the wheel in computer graphics, but to integrate graphics features into this Vulkan architecture and expand my knowledge. Therefore, the (surface) rendering features were heavily inspired by, and validated against, [Google Filament](https://github.com/google/filament).

See the [side-by-side comparisons with Filament](results/comparison/filament_validations.html) for the rendering results.

This project is still in progress and the end goal is to render a visually appealing scene that also includes volumetric effects, such as light beams, and particle effects, such as fire.

## Development

### Prerequisites

- CMake 3.20+
- Vulkan SDK 1.4+ at `~/vulkansdk/1.4.x/`
- `clang-format`, `clang-tidy` (`sudo apt install clang-format clang-tidy`)
- `glfw3`, `glm`, `libtinygltf-dev` (`sudo apt install libglfw3-dev libglm-dev libtinygltf-dev`)
- KTX-Software and nlohmann/json are fetched automatically by CMake via FetchContent

### First-time setup

```bash
make configure-debug
make configure-release
```

### Build

```bash
make build-debug    # debug build (Vulkan validation layers enabled)
make build-release  # optimized build
```

### Run

```bash
make run-debug   SCENE=<scene>
make run-release SCENE=<scene>
```

To capture a screenshot on the first rendered frame, pass `--screenshot` via `ARGS`:

```bash
make run-release SCENE=scenes/viking_room.json ARGS="--screenshot <output_path.png>"
```

Vulkan initialization and pipeline logs are disabled by default. Validation-layer warnings and errors remain visible in debug builds. Enable the informational logs when needed:

```bash
make run-debug SCENE=scenes/stanford_bunny_pbr_ibl_sao.json ARGS="--vulkan-logs"
```

### Code quality

```bash
make format
make lint
make lint-fix
```

## Scene format

Each scene is defined in a JSON file under `scenes/`. Keeping scene settings separate from the renderer makes it easy to change models, materials, lighting, and rendering effects without rebuilding the application.

A scene specifies a `camera` and a `meshInstances` array. Each mesh instance identifies a glTF model, the shaders used to render it, and optional material and transform settings. See [Rendering](#rendering) for the available lighting and shading features.

For example, a simplified PBR bunny scene could be written as:

```json
{
    "camera": {
        "target": [0.0, 0.0, 0.0],
        "azimuth": 45.0,
        "elevation": 30.0,
        "radius": 3.5,
        "fov": 45.0,
        "near": 0.1,
        "far": 100.0
    },
    "lights": [
        {
            "type": "directional",
            "direction": [1.0, 2.0, 1.0],
            "color": [1.0, 1.0, 1.0],
            "intensity": 10000.0
        }
    ],
    "meshInstances": [
        {
            "gltf": "models/stanford_bunny.glb",
            "vertexShader": "standard",
            "fragmentShader": "pbr",
            "baseColor": [0.8, 0.7, 0.6, 1.0],
            "metallic": 0.0,
            "roughness": 0.4,
            "position": [0.03, -1.19, -0.28],
            "rotation": [-90.0, 0.0, 45.0],
            "scale": [10.0, 10.0, 10.0]
        }
    ]
}
```


Multiple instances can reference the same `gltf` path. The mesh data, however, is uploaded to the GPU only once.

## Rendering

- Lambertian, Phong, and Blinn–Phong surface shading
- Metallic-roughness PBR with a Cook–Torrance microfacet specular BRDF (GGX)
- Base-color textures and glTF normal mapping
- Ambient, directional, point, and spot lights
- Physically based camera
- Image-based lighting and skybox
- Directional-light shadows with hard or percentage-closer filtered (PCF) shadow maps
- Screen-space ambient occlusion with Scalable ambient obscurance (SAO) and ground-truth ambient occlusion (GTAO)
- Reinhard tone mapping

The [Stanford bunny scenes](scenes/) demonstrate these features with different materials, lights, and effects.

## Code structure

```text
main.cpp        Application entry point and command-line arguments
src/
  core/         Vulkan context, swapchain, commands, memory, synchronization, and camera
  renderer/     Renderer orchestration, render graph, materials, GPU resources, and effect pipelines
  scene/        JSON scene loader and scene, light, and mesh data types
shaders/        GLSL shaders
scenes/         JSON scene configurations
models/         glTF models
ibl/            Image-based lighting assets
textures/       Texture assets
filament/       Filament-based reference renderer and matching scenes for comparison
results/        Rendered results and Filament comparisons
```

## TODOs

This section lists improvements I identified during development but deferred for later. It does not cover broader rendering features planned for the future, such as volumetric rendering and cascaded shadows.

### Vulkan and performance

- Suballocate buffer and image memory, for example with [Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).
- Fix swapchain presentation in `RenderGraph`: the present-layout barrier is currently added to the final pass's *pre*-barriers, before that pass resolves to the swapchain image. It needs to execute after rendering.
- Add topological sorting to `RenderGraph::buildBarriers()` so passes can be registered in any order. Currently passes must be declared in dependency order.
- Add frustum culling.

### Assets and image-based lighting

- Add mipmaps for embedded glTF albedo and normal textures, which are currently uploaded with only one level. The `.ktx2` loader already accepts precomputed mip levels.
- Replace dependency on Filament's `cmgen` with a self-contained IBL baking tool: equirectangular EXR → `*_irradiance.ktx2` (SH9), `*_prefilter.ktx2` (GGX importance sampling), `brdf_lut.ktx2` (512×512 RG32F).

### Materials and surface rendering

- Support glTF `metallicRoughnessTexture`.
- Support glTF `emissiveTexture` and `emissiveFactor`.
- Support glTF `occlusionTexture`.

### Ambient occlusion

- Add multi-bounce AO
- Evaluate bent normals for GTAO-based specular occlusion.
- Evaluate reconstructing SAO normals from depth instead of writing a normals attachment in the depth prepass.

### Shadows

- Consider a wider or rotated Poisson-disk PCF filter for softer directional shadows.
