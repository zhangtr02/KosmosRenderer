# Shaders

这个目录用于存放 KosmosRenderer 的 HLSL shader 源文件。

当前已有：

- `mesh.vert.hlsl`：读取 vertex buffer 中的位置、法线和 UV，通过 push constant 中的 MVP、材质颜色和光照方向输出裁剪空间位置与基础光照。
- `mesh.frag.hlsl`：通过 descriptor set 采样 base color texture，并输出贴图、材质色和光照相乘后的颜色。

CMake 配置时会查找 Vulkan SDK 自带的 `dxc`，构建前自动把 HLSL 编译成面向 Vulkan 1.3 的 SPIR-V：

```text
build/shaders/mesh.vert.spv
build/shaders/mesh.frag.spv
```

也可以手动运行：

```powershell
.\scripts\compile_shaders.ps1
```
