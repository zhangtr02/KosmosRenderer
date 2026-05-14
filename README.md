# KosmosRenderer

KosmosRenderer 是一个基于 Vulkan 的实时渲染学习项目。当前版本先做成独立桌面应用，用来练习游戏引擎渲染模块的基础结构，后续计划再抽出稳定接口接入 KosmosEngine。

## 当前功能

- C++20 + CMake 项目结构。
- GLFW 窗口、输入处理和 Vulkan surface 创建。
- glm 数学库，当前用于相机、场景向量和 MVP 矩阵计算。
- HLSL shader 编译到 SPIR-V，并创建 Vulkan graphics pipeline。
- Vulkan instance、debug messenger、physical device、logical device 初始化。
- Swapchain、image view、depth image、dynamic rendering pipeline 创建与 resize 重建。
- Command buffer、semaphore、fence 基础同步流程。
- 使用 staging buffer 上传 vertex buffer / index buffer，并通过 push constant 绘制多个彩色 cube。
- 每个 cube 从 `Scene` 读取独立 transform 和材质颜色。
- 简单 `Renderer` 接口，预留未来接入 KosmosEngine 的边界。
- 基础 `Scene` / `Camera` 数据结构和 fly camera 控制。

## 环境要求

- Windows
- CMake 3.24+
- 支持 C++20 的 MSVC
- Vulkan SDK
- 支持 Vulkan 1.3 dynamic rendering 的 GPU 与驱动
- vcpkg

安装依赖：

```powershell
.\vcpkg.exe install glfw3:x64-windows
.\vcpkg.exe install glm:x64-windows
```

shader 编译依赖 Vulkan SDK 自带的 `dxc`。

## 构建与运行

在项目根目录执行：

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DKOSMOS_ENABLE_VALIDATION=ON

cmake --build build --config Debug
.\build\Debug\KosmosRenderer.exe
```

如果使用 Ninja 这类单配置生成器，运行路径通常是：

```powershell
.\build\KosmosRenderer.exe
```

快速启动并自动退出：

```powershell
.\build\Debug\KosmosRenderer.exe --frames 3
```

可选启动参数：

```text
--width 1280
--height 720
--frames 3
```

## 操作

- 按住鼠标右键：捕获鼠标并控制视角。
- `W/A/S/D`：前后左右移动。
- `Q/E`：下降 / 上升。
- `Esc`：退出程序。
