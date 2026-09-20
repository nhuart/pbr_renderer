# filament_scene

Filament-based renderer for scenes defined in `scenes/`. Each scene mirrors a corresponding JSON scene from the parent project and produces a PNG screenshot to `results/filament/`.

## Prerequisites

Clone and build [Filament](https://github.com/google/filament):

```sh
git clone https://github.com/google/filament
cd filament
cmake -S . -B out/cmake-release -DCMAKE_BUILD_TYPE=Release
cmake --build out/cmake-release -- -j$(nproc)
```

Then set `FILAMENT_SRC_DIR` and `FILAMENT_BUILD_DIR` in `CMakeLists.txt` to point at your clone and build output.

## Build

```sh
mkdir -p filament/build && cd filament/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j$(nproc)
```

## Run

Run from the repository root so that relative paths to models and IBL resolve correctly:

```sh
./filament/build/filament_scene
```
