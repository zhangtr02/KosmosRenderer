param(
    [string]$OutputDirectory = "$PSScriptRoot\..\build\shaders"
)

$ErrorActionPreference = "Stop"

$ShaderDirectory = Join-Path $PSScriptRoot "..\shaders"
$Dxc = if ($env:VULKAN_SDK) {
    Join-Path $env:VULKAN_SDK "Bin\dxc.exe"
} else {
    "dxc"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

& $Dxc -spirv -T vs_6_0 -E main -fspv-target-env=vulkan1.3 -Fo (Join-Path $OutputDirectory "mesh.vert.spv") (Join-Path $ShaderDirectory "mesh.vert.hlsl")
& $Dxc -spirv -T ps_6_0 -E main -fspv-target-env=vulkan1.3 -Fo (Join-Path $OutputDirectory "mesh.frag.spv") (Join-Path $ShaderDirectory "mesh.frag.hlsl")
