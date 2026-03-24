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

REM vcpkg integration for CTranslate2 + SentencePiece (optional)
REM Install with: vcpkg install ctranslate2:x64-windows sentencepiece:x64-windows
set "VCPKG_CMAKE="
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_CMAKE=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
        echo vcpkg found at %VCPKG_ROOT%
    )
)

cmake -B build_win -G "Ninja" -DCMAKE_BUILD_TYPE=Release %VCPKG_CMAKE%
cmake --build build_win
