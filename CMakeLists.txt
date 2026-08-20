cmake_minimum_required(VERSION 3.20)
project(pbr_renderer)

set(CMAKE_CXX_STANDARD 20)

set(Vulkan_ROOT "$ENV{HOME}/vulkansdk/1.4.357.1/x86_64" CACHE PATH "Vulkan SDK path")

find_package(glfw3 REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(pbr_renderer main.cpp)

target_compile_definitions(pbr_renderer PRIVATE
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
)

target_include_directories(pbr_renderer PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(pbr_renderer PRIVATE Vulkan::Vulkan glfw)
