@echo off
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64
)
if not defined VULKAN_SDK (
    if exist "C:\VulkanSDK" (
        for /f "delims=" %%d in ('dir /b /ad /o-n "C:\VulkanSDK"') do (
            set "VULKAN_SDK=C:\VulkanSDK\%%d"
            goto :found_vulkan
        )
    )
)
:found_vulkan
cd /d %~dp0
cmake -B build_win -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build_win
