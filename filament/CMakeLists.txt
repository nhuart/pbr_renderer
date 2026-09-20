cmake_minimum_required(VERSION 3.20)

# filament is built with clang/libc++; use the same compiler to avoid ABI mismatch.
# Must be set before the first project() call.
if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(CLANGPP clang++)
    if(CLANGPP)
        set(CMAKE_CXX_COMPILER "${CLANGPP}" CACHE STRING "C++ compiler" FORCE)
    endif()
endif()

project(filament_scene CXX)

set(CMAKE_CXX_STANDARD 20)

# ─── paths to the filament source + build trees ──────────────────────────────
# Adjust FILAMENT_SRC_DIR / FILAMENT_BUILD_DIR if your clone is elsewhere.
set(FILAMENT_SRC_DIR   "$ENV{HOME}/Documents/code/filament"
    CACHE PATH "Root of the cloned filament repository")
set(FILAMENT_BUILD_DIR "${FILAMENT_SRC_DIR}/out/cmake-release"
    CACHE PATH "Filament cmake-release build output directory")

# ─── compiler / linker flags matching the filament build ─────────────────────
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -fstrict-aliasing -stdlib=libc++ -fPIC -fvisibility=hidden")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic -lc++ -lc++abi -Wl,-Bdynamic -Wl,--gc-sections -Wl,-z,noexecstack")

# ─── include directories ─────────────────────────────────────────────────────
set(FILAMENT_INCLUDES
    # Public library headers (source tree)
    ${FILAMENT_SRC_DIR}/filament/include
    ${FILAMENT_SRC_DIR}/filament/backend/include
    ${FILAMENT_SRC_DIR}/libs/filamentapp/include
    ${FILAMENT_SRC_DIR}/libs/gltfio/include
    ${FILAMENT_SRC_DIR}/libs/camutils/include
    ${FILAMENT_SRC_DIR}/libs/math/include
    ${FILAMENT_SRC_DIR}/libs/utils/include
    ${FILAMENT_SRC_DIR}/libs/ktxreader/include
    ${FILAMENT_SRC_DIR}/libs/image/include
    ${FILAMENT_SRC_DIR}/libs/imageio-lite/include
    ${FILAMENT_SRC_DIR}/libs/iblprefilter/include
    ${FILAMENT_SRC_DIR}/libs/filamat/include
    ${FILAMENT_SRC_DIR}/libs/geometry/include
    ${FILAMENT_SRC_DIR}/libs/ibl/include
    ${FILAMENT_SRC_DIR}/libs/filabridge/include
    ${FILAMENT_SRC_DIR}/third_party/robin-map/include
    ${FILAMENT_SRC_DIR}/third_party/stb/tnt/..
    ${FILAMENT_SRC_DIR}/third_party/libsdl2/include
    # Generated headers (build tree)
    ${FILAMENT_BUILD_DIR}/libs/gltfio              # materials/uberarchive.h
    ${FILAMENT_BUILD_DIR}/include                  # out/cmake-release/include
)

# ─── static libraries from the build tree ────────────────────────────────────
# Order mirrors the link command used by filament's own bunny_ibl sample.
set(FILAMENT_LIBS_DIR ${FILAMENT_BUILD_DIR})

set(FILAMENT_STATIC_LIBS
    # sample / app framework
    ${FILAMENT_LIBS_DIR}/libs/gltfio/libuberarchive.a
    ${FILAMENT_LIBS_DIR}/libs/gltfio/libgltfio.a
    ${FILAMENT_LIBS_DIR}/libs/filamentapp/libfilamentapp.a
    ${FILAMENT_LIBS_DIR}/third_party/libassimp/tnt/libassimp.a
    ${FILAMENT_LIBS_DIR}/libs/camutils/libcamutils.a
    ${FILAMENT_LIBS_DIR}/libs/iblprefilter/libfilament-iblprefilter.a
    ${FILAMENT_LIBS_DIR}/libs/imageio-lite/libimageio-lite.a
    ${FILAMENT_LIBS_DIR}/third_party/civetweb/tnt/libcivetweb.a
    ${FILAMENT_LIBS_DIR}/libs/filagui/libfilagui.a
    ${FILAMENT_LIBS_DIR}/third_party/imgui/tnt/libimgui.a
    ${FILAMENT_LIBS_DIR}/libs/imageio/libimageio.a
    ${FILAMENT_LIBS_DIR}/third_party/libpng/tnt/libpng.a
    ${FILAMENT_LIBS_DIR}/third_party/tinyexr/tnt/libtinyexr.a
    ${FILAMENT_LIBS_DIR}/third_party/libz/tnt/libz.a
    ${FILAMENT_LIBS_DIR}/third_party/basisu/tnt/libbasis_encoder.a
    ${FILAMENT_LIBS_DIR}/third_party/libsdl2/tnt/libsdl2.a
    ${FILAMENT_LIBS_DIR}/libs/filamentapp/libfilamentapp-resources.a
    ${FILAMENT_LIBS_DIR}/libs/filamentapp/libfilamentapp-web-resources.a
    ${FILAMENT_LIBS_DIR}/libs/filamat/libfilamat.a
    ${FILAMENT_LIBS_DIR}/shaders/libshaders.a
    # SPIRV / GLSL tools (whole-archive group)
    ${FILAMENT_LIBS_DIR}/third_party/glslang/tnt/SPIRV/libSPIRV.a
    ${FILAMENT_LIBS_DIR}/third_party/glslang/tnt/glslang/libglslang.a
    ${FILAMENT_LIBS_DIR}/third_party/glslang/tnt/glslang/OSDependent/Unix/libOSDependent.a
    ${FILAMENT_LIBS_DIR}/third_party/spirv-tools/source/opt/libSPIRV-Tools-opt.a
    ${FILAMENT_LIBS_DIR}/third_party/spirv-tools/source/libSPIRV-Tools.a
    ${FILAMENT_LIBS_DIR}/third_party/spirv-cross/tnt/libspirv-cross-glsl.a
    ${FILAMENT_LIBS_DIR}/third_party/spirv-cross/tnt/libspirv-cross-msl.a
    ${FILAMENT_LIBS_DIR}/third_party/spirv-cross/tnt/libspirv-cross-core.a
    # core filament
    ${FILAMENT_LIBS_DIR}/libs/gltfio/libgltfio_core.a
    ${FILAMENT_LIBS_DIR}/third_party/stb/tnt/libstb.a
    ${FILAMENT_LIBS_DIR}/libs/ktxreader/libktxreader.a
    ${FILAMENT_LIBS_DIR}/filament/libfilament.a
    ${FILAMENT_LIBS_DIR}/filament/backend/libbackend.a
    ${FILAMENT_LIBS_DIR}/libs/bluegl/libbluegl.a
    ${FILAMENT_LIBS_DIR}/libs/bluevk/libbluevk.a
    ${FILAMENT_LIBS_DIR}/libs/filaflat/libfilaflat.a
    ${FILAMENT_LIBS_DIR}/third_party/smol-v/tnt/libsmol-v.a
    ${FILAMENT_LIBS_DIR}/libs/image/libimage.a
    ${FILAMENT_LIBS_DIR}/third_party/basisu/tnt/libbasis_transcoder.a
    ${FILAMENT_LIBS_DIR}/libs/geometry/libgeometry.a
    ${FILAMENT_LIBS_DIR}/third_party/mikktspace/libmikktspace.a
    ${FILAMENT_LIBS_DIR}/libs/uberz/libuberzlib.a
    ${FILAMENT_LIBS_DIR}/libs/filabridge/libfilabridge.a
    ${FILAMENT_LIBS_DIR}/libs/math/libmath.a
    ${FILAMENT_LIBS_DIR}/libs/utils/libutils.a
    ${FILAMENT_LIBS_DIR}/third_party/zstd/tnt/libzstd.a
    ${FILAMENT_LIBS_DIR}/third_party/meshoptimizer/tnt/libmeshoptimizer.a
    ${FILAMENT_LIBS_DIR}/third_party/draco/tnt/libdracodec.a
)

# ─── target ──────────────────────────────────────────────────────────────────
add_executable(filament_scene
    main.cpp
    mesh_loader.cpp
    screenshot.cpp
)

target_include_directories(filament_scene PRIVATE ${FILAMENT_INCLUDES})

target_compile_definitions(filament_scene PRIVATE
    FILAMENTAPP_HAS_SDL
    FILAMENTAPP_HAS_IMGUI
    FILAMENTAPP_HAS_WEB_UI
    FILAMENT_DRIVER_SUPPORTS_VULKAN
    FILAMENT_SUPPORTS_OPENGL
    FILAMENT_SUPPORTS_X11
    FILAMENT_SUPPORTS_XCB
    FILAMENT_SUPPORTS_XLIB
    NDEBUG
)

# The SPIRV/GLSL tool archives use --start-group/--end-group to resolve circular deps.
target_link_libraries(filament_scene PRIVATE
    -Wl,-Bstatic
    ${FILAMENT_STATIC_LIBS}
    -Wl,-Bdynamic
    # bluegl contains private symbols that must not be re-exported
    -Wl,--exclude-libs,bluegl
    dl rt
)
