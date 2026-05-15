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
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)

find_program(DXC_EXECUTABLE
    NAMES dxc dxc.exe
    HINTS "$ENV{VULKAN_SDK}/Bin"
)

if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "dxc was not found. Install the Vulkan SDK and make sure VULKAN_SDK is set.")
endif()

set(KOSMOS_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(KOSMOS_SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

function(kosmos_compile_shader shader_source shader_profile shader_output_name)
    set(shader_output "${KOSMOS_SHADER_BINARY_DIR}/${shader_output_name}")
    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${KOSMOS_SHADER_BINARY_DIR}"
        COMMAND "${DXC_EXECUTABLE}"
                -spirv
                -T ${shader_profile}
                -E main
                -fspv-target-env=vulkan1.3
                -Fo "${shader_output}"
                "${shader_source}"
        DEPENDS "${shader_source}"
        VERBATIM
    )
    set(KOSMOS_SHADER_OUTPUTS "${KOSMOS_SHADER_OUTPUTS};${shader_output}" PARENT_SCOPE)
endfunction()

kosmos_compile_shader("${KOSMOS_SHADER_SOURCE_DIR}/mesh.vert.hlsl" vs_6_0 "mesh.vert.spv")
kosmos_compile_shader("${KOSMOS_SHADER_SOURCE_DIR}/mesh.frag.hlsl" ps_6_0 "mesh.frag.spv")
kosmos_compile_shader("${KOSMOS_SHADER_SOURCE_DIR}/shadow.vert.hlsl" vs_6_0 "shadow.vert.spv")
kosmos_compile_shader("${KOSMOS_SHADER_SOURCE_DIR}/fullscreen.vert.hlsl" vs_6_0 "fullscreen.vert.spv")
kosmos_compile_shader("${KOSMOS_SHADER_SOURCE_DIR}/post.frag.hlsl" ps_6_0 "post.frag.spv")

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

add_custom_target(KosmosRendererShaders DEPENDS ${KOSMOS_SHADER_OUTPUTS})
add_dependencies(KosmosRenderer KosmosRendererShaders)

target_include_directories(KosmosRenderer
    PRIVATE
        src
        ${Stb_INCLUDE_DIR}
        ${TINYGLTF_INCLUDE_DIRS}
)

target_link_libraries(KosmosRenderer
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
        imgui::imgui
)

target_compile_definitions(KosmosRenderer
    PRIVATE
        GLFW_INCLUDE_NONE
        KOSMOS_ENABLE_VALIDATION=$<BOOL:${KOSMOS_ENABLE_VALIDATION}>
        KOSMOS_SHADER_DIRECTORY="${KOSMOS_SHADER_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(KosmosRenderer PRIVATE /W4 /permissive-)
else()
    target_compile_options(KosmosRenderer PRIVATE -Wall -Wextra -Wpedantic)
endif()
