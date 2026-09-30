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
    shaders/fullscreen.vert
    shaders/view_space_normals.frag
    shaders/sao.frag
    shaders/sao_depth_mip.frag
    shaders/gtao.frag
    shaders/sao_blur.frag
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

# pbr.frag variants: each entry is "output_suffix|DEFINE1 DEFINE2 ..."
set(PBR_VARIANTS
    "pbr_ibl|USE_IBL"
    "pbr_normal|USE_NORMAL_MAP"
    "pbr_ibl_normal|USE_IBL USE_NORMAL_MAP"
    "pbr_shadow|USE_SHADOW"
    "pbr_normal_shadow|USE_NORMAL_MAP USE_SHADOW"
    "pbr_ibl_shadow|USE_IBL USE_SHADOW"
    "pbr_ibl_normal_shadow|USE_IBL USE_NORMAL_MAP USE_SHADOW"
    "pbr_pcf|USE_SHADOW USE_PCF"
    "pbr_normal_pcf|USE_NORMAL_MAP USE_SHADOW USE_PCF"
    "pbr_ibl_pcf|USE_IBL USE_SHADOW USE_PCF"
    "pbr_ibl_normal_pcf|USE_IBL USE_NORMAL_MAP USE_SHADOW USE_PCF"
    "pbr_sao|USE_SAO"
    "pbr_ibl_sao|USE_IBL USE_SAO"
    "pbr_normal_sao|USE_NORMAL_MAP USE_SAO"
    "pbr_ibl_normal_sao|USE_IBL USE_NORMAL_MAP USE_SAO"
    "pbr_shadow_sao|USE_SHADOW USE_SAO"
    "pbr_ibl_shadow_sao|USE_IBL USE_SHADOW USE_SAO"
    "pbr_ibl_normal_shadow_sao|USE_IBL USE_NORMAL_MAP USE_SHADOW USE_SAO"
    "pbr_pcf_sao|USE_SHADOW USE_PCF USE_SAO"
    "pbr_ibl_pcf_sao|USE_IBL USE_SHADOW USE_PCF USE_SAO"
    "pbr_ibl_normal_pcf_sao|USE_IBL USE_NORMAL_MAP USE_SHADOW USE_PCF USE_SAO"
)
foreach(VARIANT ${PBR_VARIANTS})
    string(REPLACE "|" ";" VARIANT_PARTS "${VARIANT}")
    list(GET VARIANT_PARTS 0 SUFFIX)
    list(GET VARIANT_PARTS 1 DEFINES_STR)
    string(REPLACE " " ";" DEFINES_LIST "${DEFINES_STR}")
    set(SPIRV_OUT "${CMAKE_BINARY_DIR}/shaders/${SUFFIX}.frag.spv")
    set(DEFINE_FLAGS "")
    foreach(DEF ${DEFINES_LIST})
        list(APPEND DEFINE_FLAGS "-D${DEF}")
    endforeach()
    add_custom_command(
        OUTPUT ${SPIRV_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND ${GLSLC} "${CMAKE_SOURCE_DIR}/shaders/pbr.frag" ${DEFINE_FLAGS} -o ${SPIRV_OUT}
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/pbr.frag"
        COMMENT "Compiling shader ${SUFFIX}.frag"
    )
    list(APPEND SHADER_SPIRV_OUTPUTS ${SPIRV_OUT})
endforeach()

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
    src/renderer/shadow_pipeline.cpp
    src/renderer/ao_pipeline.cpp
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
