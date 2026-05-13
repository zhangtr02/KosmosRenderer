# KosmosRenderer Shaders

这个目录用于存放 GLSL shader 源文件。

当前 Phase 1 还没有真正绘制三角形或模型，只是通过 Vulkan render pass 清空 swapchain 图像，所以暂时不需要 shader module。

后续计划：

- `fullscreen.vert` / `tonemap.frag`：用于后处理，例如 tone mapping、gamma correction、FXAA。
- `pbr.vert` / `pbr.frag`：用于 metallic-roughness PBR 材质渲染。
- `shadow.vert`：用于 directional light shadow map。

文件名约定：

- `.vert` 表示 vertex shader，负责处理顶点位置、法线、UV 等输入。
- `.frag` 表示 fragment shader，负责计算像素颜色。
- shader 会在后续阶段通过 `glslc` 编译成 SPIR-V，再交给 Vulkan 创建 `VkShaderModule`。
