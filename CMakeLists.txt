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

function(add_shader_output shader_source output_name)
    set(output_path "${CMAKE_BINARY_DIR}/shaders/${output_name}.spv")
    add_custom_command(
        OUTPUT "${output_path}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${GLSLC}" "${CMAKE_SOURCE_DIR}/${shader_source}" ${ARGN} -o "${output_path}"
        DEPENDS "${CMAKE_SOURCE_DIR}/${shader_source}"
        COMMENT "Compiling shader ${output_name}"
        VERBATIM
    )
    set(SHADER_SPIRV_OUTPUTS ${SHADER_SPIRV_OUTPUTS} "${output_path}" PARENT_SCOPE)
endfunction()

set(SHADER_SOURCES
    shaders/standard.vert
    shaders/lambertian.frag
    shaders/phong.frag
    shaders/blinn_phong.frag
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

set(SHADER_SPIRV_OUTPUTS)
foreach(shader IN LISTS SHADER_SOURCES)
    get_filename_component(shader_name "${shader}" NAME)
    add_shader_output("${shader}" "${shader_name}")
endforeach()

# Keep suffix order in sync with Renderer::resolveShaderVariants().
function(add_pbr_shader has_ibl has_normal_map shadow_mode has_ao)
    set(variant_name pbr)
    set(define_flags)
    if(has_ibl)
        string(APPEND variant_name _ibl)
        list(APPEND define_flags -DUSE_IBL)
    endif()
    if(has_normal_map)
        string(APPEND variant_name _normal)
        list(APPEND define_flags -DUSE_NORMAL_MAP)
    endif()
    if(NOT shadow_mode STREQUAL "none")
        string(APPEND variant_name "_${shadow_mode}")
        list(APPEND define_flags -DUSE_SHADOW)
        if(shadow_mode STREQUAL "pcf")
            list(APPEND define_flags -DUSE_PCF)
        endif()
    endif()
    if(has_ao)
        string(APPEND variant_name _sao)
        list(APPEND define_flags -DUSE_SAO)
    endif()

    add_shader_output("shaders/pbr.frag" "${variant_name}.frag" ${define_flags})
    set(SHADER_SPIRV_OUTPUTS "${SHADER_SPIRV_OUTPUTS}" PARENT_SCOPE)
endfunction()

foreach(ibl IN ITEMS OFF ON)
    foreach(normal_map IN ITEMS OFF ON)
        foreach(shadow IN ITEMS none shadow pcf)
            foreach(ao IN ITEMS OFF ON)
                add_pbr_shader(${ibl} ${normal_map} ${shadow} ${ao})
            endforeach()
        endforeach()
    endforeach()
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
    src/renderer/renderer_capture.cpp
    src/renderer/renderer_graph.cpp
    src/renderer/render_graph.cpp
    src/renderer/texture_atlas.cpp
    src/renderer/mesh_buffer.cpp
    src/renderer/light_buffer.cpp
    src/renderer/cubemap_atlas.cpp
    src/renderer/skybox_pipeline.cpp
    src/renderer/material.cpp
    src/renderer/material_instance.cpp
    src/renderer/shadow_pipeline.cpp
    src/renderer/ao_pipeline.cpp
    src/renderer/ao_pipeline_prepass.cpp
    src/renderer/ao_pipeline_effects.cpp
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

foreach(asset_dir IN ITEMS textures models scenes)
    add_custom_command(TARGET pbr_renderer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/${asset_dir}"
            "$<TARGET_FILE_DIR:pbr_renderer>/${asset_dir}"
        VERBATIM
    )
endforeach()
