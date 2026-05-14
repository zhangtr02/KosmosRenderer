# Shaders

这个目录用于存放 KosmosRenderer 的 HLSL shader 源文件。

当前已有：

- `triangle.vert.hlsl`：使用 `SV_VertexID` 在 shader 内生成三角形顶点和颜色。
- `triangle.frag.hlsl`：输出插值后的顶点颜色。

CMake 配置时会查找 Vulkan SDK 自带的 `dxc`，构建前自动把 HLSL 编译成面向 Vulkan 1.3 的 SPIR-V：

```text
build/shaders/triangle.vert.spv
build/shaders/triangle.frag.spv
```

也可以手动运行：

```powershell
.\scripts\compile_shaders.ps1
```
