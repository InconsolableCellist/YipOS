@echo off
REM --- VS environment ---
if defined VSCMD_VER goto :skip_vcvars
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
:skip_vcvars

REM --- Vulkan SDK auto-detect ---
if defined VULKAN_SDK goto :found_vulkan
if not exist "C:\VulkanSDK" goto :found_vulkan
for /f "delims=" %%d in ('dir /b /ad /o-n "C:\VulkanSDK"') do (
    set "VULKAN_SDK=C:\VulkanSDK\%%d"
    goto :found_vulkan
)
:found_vulkan

cd /d %~dp0\yip_os

REM --- vcpkg integration (optional) ---
set "VCPKG_CMAKE="
if not defined VCPKG_ROOT goto :skip_vcpkg
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" goto :skip_vcpkg
set VCPKG_CMAKE=-DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
echo vcpkg found at %VCPKG_ROOT%
:skip_vcpkg

REM --- CTranslate2 local prefix (optional, alternative to vcpkg) ---
set "CT2_CMAKE="
if not defined CT2_PREFIX set "CT2_PREFIX=C:\ct2_install"
if not defined CT2_PREFIX goto :skip_ct2
if not exist "%CT2_PREFIX%\lib\cmake\ctranslate2" goto :skip_ct2
set "CT2_CMAKE=-DCMAKE_PREFIX_PATH=%CT2_PREFIX%"
echo CTranslate2 found at %CT2_PREFIX%
:skip_ct2

cmake -B build_win -G "Ninja" -DCMAKE_BUILD_TYPE=Release %VCPKG_CMAKE% %CT2_CMAKE%
cmake --build build_win

REM --- Copy translation DLLs to build output ---
if not defined CT2_PREFIX goto :skip_copy
copy /Y "%CT2_PREFIX%\bin\ctranslate2.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\sentencepiece.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\openblas.dll" build_win\ 2>nul
REM CUDA runtime (if present — redistributable per NVIDIA EULA)
copy /Y "%CT2_PREFIX%\bin\cublas64_12.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\cublasLt64_12.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\cudart64_12.dll" build_win\ 2>nul
:skip_copy
