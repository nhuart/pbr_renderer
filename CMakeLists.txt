cmake_minimum_required(VERSION 3.20)
project(pbr_renderer)

set(CMAKE_CXX_STANDARD 20)

set(Vulkan_ROOT "$ENV{HOME}/vulkansdk/1.4.357.1/x86_64" CACHE PATH "Vulkan SDK path")

find_package(glfw3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)

# tinygltf is a header-only library installed via libtinygltf-dev
find_path(TINYGLTF_INCLUDE_DIR tiny_gltf.h REQUIRED)

# KTX-Software — provides libktx for loading KTX2 textures
include(FetchContent)
FetchContent_Declare(
    ktx
    GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
    GIT_TAG        v4.3.2
    GIT_SHALLOW    TRUE
    GIT_CONFIG     "filter.lfs.smudge=;filter.lfs.required=false;filter.lfs.clean=;lfs.fetchexclude=*"
)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS ON CACHE BOOL "" FORCE)
set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ktx)

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

find_program(GLSLC glslc HINTS "${Vulkan_ROOT}/bin" REQUIRED)

set(SHADER_SOURCES
    shaders/standard.vert
    shaders/lambertian.frag
    shaders/phong.frag
    shaders/blinn_phong.frag
    shaders/pbr.frag
    shaders/shadow.vert
    shaders/skybox.vert
    shaders/skybox.frag
    shaders/particle.vert
    shaders/particle.frag
    shaders/particle.comp
)

set(SHADER_SPIRV_OUTPUTS "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/${SHADER}" -o ${SPIRV_OUTPUT}
        DEPENDS "${CMAKE_SOURCE_DIR}/${SHADER}"
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_SPIRV_OUTPUTS ${SPIRV_OUTPUT})
endforeach()

# pbr.frag compiled a second time with USE_IBL defined -> pbr_ibl.frag.spv
set(PBR_IBL_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_ibl.frag.spv")
add_custom_command(
    OUTPUT ${PBR_IBL_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_IBL -o ${PBR_IBL_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_ibl.frag (USE_IBL)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_IBL_SPIRV})

# pbr.frag compiled with USE_NORMAL_MAP -> pbr_normal.frag.spv
set(PBR_NORMAL_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_normal.frag.spv")
add_custom_command(
    OUTPUT ${PBR_NORMAL_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_NORMAL_MAP -o ${PBR_NORMAL_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_normal.frag (USE_NORMAL_MAP)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_NORMAL_SPIRV})

# pbr.frag compiled with USE_IBL + USE_NORMAL_MAP -> pbr_ibl_normal.frag.spv
set(PBR_IBL_NORMAL_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_ibl_normal.frag.spv")
add_custom_command(
    OUTPUT ${PBR_IBL_NORMAL_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_IBL -DUSE_NORMAL_MAP -o ${PBR_IBL_NORMAL_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_ibl_normal.frag (USE_IBL + USE_NORMAL_MAP)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_IBL_NORMAL_SPIRV})

# pbr.frag compiled with USE_SHADOW -> pbr_shadow.frag.spv
set(PBR_SHADOW_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_shadow.frag.spv")
add_custom_command(
    OUTPUT ${PBR_SHADOW_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_SHADOW -o ${PBR_SHADOW_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_shadow.frag (USE_SHADOW)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_SHADOW_SPIRV})

# pbr.frag compiled with USE_SHADOW + USE_NORMAL_MAP -> pbr_normal_shadow.frag.spv
set(PBR_NORMAL_SHADOW_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_normal_shadow.frag.spv")
add_custom_command(
    OUTPUT ${PBR_NORMAL_SHADOW_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_NORMAL_MAP -DUSE_SHADOW -o ${PBR_NORMAL_SHADOW_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_normal_shadow.frag (USE_NORMAL_MAP + USE_SHADOW)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_NORMAL_SHADOW_SPIRV})

# pbr.frag compiled with USE_IBL + USE_SHADOW -> pbr_ibl_shadow.frag.spv
set(PBR_IBL_SHADOW_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_ibl_shadow.frag.spv")
add_custom_command(
    OUTPUT ${PBR_IBL_SHADOW_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_IBL -DUSE_SHADOW -o ${PBR_IBL_SHADOW_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_ibl_shadow.frag (USE_IBL + USE_SHADOW)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_IBL_SHADOW_SPIRV})

# pbr.frag compiled with USE_IBL + USE_NORMAL_MAP + USE_SHADOW -> pbr_ibl_normal_shadow.frag.spv
set(PBR_IBL_NORMAL_SHADOW_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_ibl_normal_shadow.frag.spv")
add_custom_command(
    OUTPUT ${PBR_IBL_NORMAL_SHADOW_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_IBL -DUSE_NORMAL_MAP -DUSE_SHADOW -o ${PBR_IBL_NORMAL_SHADOW_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_ibl_normal_shadow.frag (USE_IBL + USE_NORMAL_MAP + USE_SHADOW)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_IBL_NORMAL_SHADOW_SPIRV})

# PCF variants — USE_SHADOW + USE_PCF
set(PBR_PCF_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_pcf.frag.spv")
add_custom_command(
    OUTPUT ${PBR_PCF_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_SHADOW -DUSE_PCF -o ${PBR_PCF_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_pcf.frag (USE_SHADOW + USE_PCF)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_PCF_SPIRV})

set(PBR_NORMAL_PCF_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_normal_pcf.frag.spv")
add_custom_command(
    OUTPUT ${PBR_NORMAL_PCF_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_NORMAL_MAP -DUSE_SHADOW -DUSE_PCF -o ${PBR_NORMAL_PCF_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_normal_pcf.frag (USE_NORMAL_MAP + USE_SHADOW + USE_PCF)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_NORMAL_PCF_SPIRV})

set(PBR_IBL_PCF_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_ibl_pcf.frag.spv")
add_custom_command(
    OUTPUT ${PBR_IBL_PCF_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_IBL -DUSE_SHADOW -DUSE_PCF -o ${PBR_IBL_PCF_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_ibl_pcf.frag (USE_IBL + USE_SHADOW + USE_PCF)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_IBL_PCF_SPIRV})

set(PBR_IBL_NORMAL_PCF_SPIRV "${CMAKE_BINARY_DIR}/shaders/pbr_ibl_normal_pcf.frag.spv")
add_custom_command(
    OUTPUT ${PBR_IBL_NORMAL_PCF_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" -DUSE_IBL -DUSE_NORMAL_MAP -DUSE_SHADOW -DUSE_PCF -o ${PBR_IBL_NORMAL_PCF_SPIRV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
    COMMENT "Compiling shader pbr_ibl_normal_pcf.frag (USE_IBL + USE_NORMAL_MAP + USE_SHADOW + USE_PCF)"
)
list(APPEND SHADER_SPIRV_OUTPUTS ${PBR_IBL_NORMAL_PCF_SPIRV})

add_custom_target(shaders ALL DEPENDS ${SHADER_SPIRV_OUTPUTS})

# Copy compiled shaders next to the binary so the app can find them at runtime
add_custom_command(TARGET shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders/compiled"
)

add_executable(pbr_renderer
    main.cpp
    src/core/context.cpp
    src/core/swapchain.cpp
    src/core/command_service.cpp
    src/core/resource_allocator.cpp
    src/core/sync.cpp
    src/renderer/renderer.cpp
    src/renderer/render_graph.cpp
    src/renderer/texture_atlas.cpp
    src/renderer/mesh_buffer.cpp
    src/renderer/light_buffer.cpp
    src/renderer/cubemap_atlas.cpp
    src/renderer/skybox_pipeline.cpp
    src/renderer/material.cpp
    src/renderer/shadow_map.cpp
    src/renderer/particle_pipeline.cpp
    src/scene/scene_loader.cpp
)
add_dependencies(pbr_renderer shaders)
target_precompile_headers(pbr_renderer PRIVATE
    <vulkan/vulkan_raii.hpp>
    <GLFW/glfw3.h>
)

target_compile_definitions(pbr_renderer PRIVATE
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

# stb headers are under <stb/stb_image.h> on this system; tinygltf expects <stb_image.h>
find_path(STB_INCLUDE_DIR stb/stb_image.h REQUIRED)

target_include_directories(pbr_renderer PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${TINYGLTF_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}/stb
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(pbr_renderer PRIVATE Vulkan::Vulkan glfw glm::glm ktx nlohmann_json::nlohmann_json)

# Copy textures next to the binary so the app can find them at runtime
add_custom_command(TARGET pbr_renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/textures"
        "$<TARGET_FILE_DIR:pbr_renderer>/textures"
)

# Copy models next to the binary so the app can find them at runtime
add_custom_command(TARGET pbr_renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:pbr_renderer>/models"
)

# Copy scene definitions next to the binary
add_custom_command(TARGET pbr_renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scenes"
        "$<TARGET_FILE_DIR:pbr_renderer>/scenes"
)
