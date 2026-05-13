cmake_minimum_required(VERSION 3.24)

project(KosmosRenderer
    VERSION 0.1.0
    DESCRIPTION "A Vulkan real-time renderer learning project"
    LANGUAGES CXX
)

option(KOSMOS_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_executable(KosmosRenderer
    src/main.cpp
    src/App/Application.cpp
    src/App/FlyCameraController.cpp
    src/Assets/GltfLoader.cpp
    src/Platform/Clock.cpp
    src/Platform/Window.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/Vulkan/VulkanRenderer.cpp
    src/Scene/Camera.cpp
    src/Scene/Scene.cpp
)

target_include_directories(KosmosRenderer
    PRIVATE
        src
)

target_link_libraries(KosmosRenderer
    PRIVATE
        Vulkan::Vulkan
        glfw
)

target_compile_definitions(KosmosRenderer
    PRIVATE
        GLFW_INCLUDE_NONE
        KOSMOS_ENABLE_VALIDATION=$<BOOL:${KOSMOS_ENABLE_VALIDATION}>
)

if(MSVC)
    target_compile_options(KosmosRenderer PRIVATE /W4 /permissive-)
else()
    target_compile_options(KosmosRenderer PRIVATE -Wall -Wextra -Wpedantic)
endif()
