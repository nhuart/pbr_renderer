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
    shaders/triangle.vert
    shaders/triangle.frag
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

add_custom_target(shaders ALL DEPENDS ${SHADER_SPIRV_OUTPUTS})

# Copy compiled shaders next to the binary so the app can find them at runtime
add_custom_command(TARGET shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders/compiled"
)

add_executable(pbr_renderer
    main.cpp
    src/core/instance.cpp
    src/core/device.cpp
    src/core/swapchain.cpp
    src/resources/buffer.cpp
    src/resources/image.cpp
    src/resources/texture.cpp
    src/renderer/graphics_pipeline.cpp
    src/renderer/descriptors.cpp
    src/renderer/command_buffer.cpp
    src/renderer/sync.cpp
    src/compute/compute_pipeline.cpp
    src/scene/model_loader.cpp
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
