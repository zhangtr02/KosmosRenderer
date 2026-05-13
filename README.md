# KosmosRenderer

KosmosRenderer 是一个面向游戏引擎开发作品集的 Vulkan 实时渲染学习项目。第一版先作为独立桌面应用开发，后续再抽出稳定的渲染接口接入 KosmosEngine。

当前仓库已经从 Hello World 升级为一个可继续扩展的 Vulkan renderer 起点：

- C++20 + CMake 工程。
- GLFW 窗口、输入与 Vulkan surface。
- Vulkan instance、debug messenger、physical device、logical device。
- Swapchain、image view、render pass、framebuffer。
- Command buffer、semaphore、fence 与 resize 后重建 swapchain。
- `Renderer` 抽象接口，为未来接入 KosmosEngine 保留边界。
- `Scene`、`Camera`、`Material`、`MeshInstance` 数据结构。
- 右键捕获鼠标的 fly camera 控制器。

## Build

### Required

- Windows
- CMake 3.24+
- C++20 compiler
- Vulkan SDK

### Dependencies

CMake 不会自动下载第三方库。当前额外依赖使用 vcpkg 安装：

```powershell
vcpkg install glfw3:x64-windows
```

配置项目时把 vcpkg toolchain 传给 CMake：

```powershell
cmake -S . -B build `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
      -DKOSMOS_ENABLE_VALIDATION=ON
cmake --build build --config Debug
.\build\Debug\KosmosRenderer.exe
```

如果使用 Ninja 等单配置生成器，程序通常位于：

```powershell
.\build\KosmosRenderer.exe
```

Smoke test:

```powershell
.\build\Debug\KosmosRenderer.exe --frames 3
```

单配置生成器对应：

```powershell
.\build\KosmosRenderer.exe --frames 3
```

如果你使用 CLion，可以直接打开仓库根目录，并在 CMake profile 里添加：

```text
-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
```

## Controls

- 按住鼠标右键：捕获鼠标并控制视角
- `W/A/S/D`：水平移动
- `Q/E`：下降 / 上升
- `Esc`：退出

当前阶段还没有真正绘制 mesh；窗口会通过 Vulkan render pass 清屏，清屏颜色随时间轻微变化，用来验证 swapchain、同步和 resize 路径。

## Architecture

```text
src/
  App/          Application loop and camera controller
  Platform/     GLFW window, input, timing, Vulkan surface
  Renderer/     Engine-facing renderer interface
    Vulkan/     Vulkan backend implementation
  Scene/        Camera, transforms, light, material, scene data
  Assets/       Future glTF and texture loading
shaders/        Future GLSL shader sources
```

Engine-facing renderer interface:

```cpp
Renderer::Initialize(Window&, RendererConfig)
Renderer::Resize(width, height)
Renderer::BeginFrame()
Renderer::RenderScene(const Scene&)
Renderer::EndFrame()
Renderer::WaitIdle()
Renderer::Shutdown()
```

这个接口现在由 Vulkan 后端实现；未来接入 KosmosEngine 时，Engine 可以持有 `Scene` 或渲染提交数据，Renderer 负责把它转换为 GPU work。

## Roadmap

### Phase 1: Vulkan MVP

- 保持 validation layer 无严重错误。
- 在当前清屏 render pass 基础上加入 graphics pipeline。
- 添加 vertex buffer / index buffer。
- 绘制三角形，再绘制 cube。

### Phase 2: Resource System

- 封装 `Buffer`、`Image`、`ShaderModule`、`Pipeline`、`DescriptorSet`、`Sampler`。
- 接入 VulkanMemoryAllocator。
- 用 staging buffer 上传 mesh 和 texture。
- 加入 uniform buffer 或 push constant，支持 model / view / projection。

### Phase 3: Scene and Assets

- 接入 tinygltf 或 cgltf。
- 加载静态 glTF mesh、material、base color texture。
- 将当前 `Scene` 数据结构扩展为可提交的 scene graph。
- 加入 ImGui 面板显示场景、相机、灯光和 frame time。

### Phase 4: PBR Lighting

- 实现 metallic-roughness PBR。
- 支持 directional light、point light、normal map。
- 加入基础 IBL。
- README 增加 PBR shader 输入和材质数据流说明。

### Phase 5: Shadows and Debug Views

- 实现 directional light shadow map。
- 添加 tone mapping、gamma correction，可选 FXAA。
- ImGui 增加 debug view：Lit、Normal、Albedo、Roughness、Metallic、Shadow Map。
- 增加 CPU/GPU frame timing。

### Phase 6: Portfolio Delivery

- 整理轻量 pass 架构：`ShadowPass`、`ForwardPass`、`PostProcessPass`、`UIPass`。
- 准备 Demo 场景、截图和 1-2 分钟录屏。
- 补充架构图、技术难点和未来接入 KosmosEngine 的路线。

## Portfolio Narrative

这个项目的展示重点不是“我照着教程画出了一个模型”，而是：

- 我能从 Vulkan 底层搭建一条完整帧渲染路径。
- 我理解 swapchain、command buffer、descriptor、pipeline 和同步的生命周期。
- 我能把 renderer 按平台、场景、资源、后端拆分，让它未来可以进入自研引擎。
- 我能用 RenderDoc、validation layer 和 frame timing 调试渲染问题。
