# Shaders

这个目录用于存放 KosmosRenderer 的 HLSL shader 源文件。

当前已有：

- `mesh.vert.hlsl`：读取 vertex buffer 中的位置、法线、UV 和 tangent，通过 push constant 中的 MVP / model 矩阵输出裁剪空间位置、世界坐标、世界法线和 TBN 所需数据。
- `mesh.frag.hlsl`：通过材质 descriptor set 采样 base color、metallic-roughness 和 normal texture，使用简化 Cook-Torrance BRDF 计算 directional light 下的 metallic-roughness PBR 结果。

当前材质贴图绑定约定：

- `t0`：base color texture，按 sRGB 格式上传。
- `t1`：metallic-roughness texture，按线性 UNORM 格式上传，当前使用 B 通道作为 metallic，G 通道作为 roughness。
- `t2`：normal texture，按线性 UNORM 格式上传。
- `s3`：材质 sampler。

push constant 会同时给 vertex shader 和 fragment shader 使用，包含 MVP、model、base color、光照方向与强度、相机位置、metallic、roughness 和光照颜色。

CMake 配置时会查找 Vulkan SDK 自带的 `dxc`，构建前自动把 HLSL 编译成面向 Vulkan 1.3 的 SPIR-V：

```text
build/shaders/mesh.vert.spv
build/shaders/mesh.frag.spv
```

也可以手动运行：

```powershell
.\scripts\compile_shaders.ps1
```
