# Shaders

这个目录用于存放 KosmosRenderer 后续的 GLSL shader 源文件。

当前版本还没有真正绘制三角形或模型，只通过 Vulkan render pass 清屏来验证 swapchain、同步和 resize 流程，因此暂时不需要 shader 文件。

后续会逐步加入：

- `pbr.vert` / `pbr.frag`：PBR 材质渲染。
- `shadow.vert`：方向光阴影贴图。
- `fullscreen.vert` / `tonemap.frag`：后处理、tone mapping 和 gamma correction。

shader 之后会通过 Vulkan SDK 自带的 `glslc` 编译成 SPIR-V，再交给 Vulkan 创建 `VkShaderModule`。
