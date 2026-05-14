@echo off
echo Building YipOS Release + Installers...

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

cd /d %~dp0

REM --- vcpkg integration (optional) ---
set "VCPKG_CMAKE="
if not defined VCPKG_ROOT goto :skip_vcpkg
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" goto :skip_vcpkg
set VCPKG_CMAKE=-DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
echo vcpkg found at %VCPKG_ROOT%
:skip_vcpkg

REM --- CTranslate2 prefixes ---
REM CT2_PREFIX       = CUDA-enabled build   (default C:\ct2_install)
REM CT2_CPU_PREFIX   = CPU-only build       (default C:\ct2_install_cpu)
if not defined CT2_PREFIX set "CT2_PREFIX=C:\ct2_install"
if not defined CT2_CPU_PREFIX set "CT2_CPU_PREFIX=C:\ct2_install_cpu"

REM --- MeCab prefix ---
if not defined MECAB_PREFIX set "MECAB_PREFIX=C:\mecab_install"

REM --- Build using CUDA CT2 (import lib is ABI-compatible with CPU DLL) ---
REM Combine CT2 + MeCab into CMAKE_PREFIX_PATH
set "PREFIX_PATH=%CT2_PREFIX%"
if not exist "%MECAB_PREFIX%\lib\libmecab.lib" goto :no_mecab_installer
set "PREFIX_PATH=%PREFIX_PATH%;%MECAB_PREFIX%"
echo MeCab found at %MECAB_PREFIX%
goto :done_mecab_installer
:no_mecab_installer
echo MeCab not found -- Japanese kanji will display as '?'
:done_mecab_installer

set "PREFIX_CMAKE="
if not exist "%CT2_PREFIX%\lib\cmake\ctranslate2" goto :no_ct2_installer
echo CTranslate2 found at %CT2_PREFIX% [CUDA]
set "PREFIX_CMAKE=-DCMAKE_PREFIX_PATH=%PREFIX_PATH%"
goto :done_ct2_installer
:no_ct2_installer
echo CTranslate2 not found -- translation will be disabled
if exist "%MECAB_PREFIX%\lib\libmecab.lib" set "PREFIX_CMAKE=-DCMAKE_PREFIX_PATH=%MECAB_PREFIX%"
:done_ct2_installer

cmake -B build_win -G "Ninja" -DCMAKE_BUILD_TYPE=Release %VCPKG_CMAKE% %PREFIX_CMAKE%
cmake --build build_win

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

REM --- Copy CUDA DLLs to build output ---
copy /Y "%CT2_PREFIX%\bin\ctranslate2.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\sentencepiece.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\openblas.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\cublas64_12.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\cublasLt64_12.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX%\bin\cudart64_12.dll" build_win\ 2>nul

REM --- Copy MeCab DLL + dictionary to build output ---
if exist "%MECAB_PREFIX%\bin\libmecab.dll" (
    copy /Y "%MECAB_PREFIX%\bin\libmecab.dll" build_win\ 2>nul
)
if exist "%MECAB_PREFIX%\dic\ipadic" (
    if not exist "build_win\mecab-dic\ipadic" mkdir "build_win\mecab-dic\ipadic"
    xcopy /Y /Q "%MECAB_PREFIX%\dic\ipadic\*.*" "build_win\mecab-dic\ipadic\" >nul
    echo MeCab dictionary copied to build_win\mecab-dic\ipadic
)

echo.
echo Build successful! Building installers...

REM --- Find NSIS ---
set "MAKENSIS="
if exist "%ProgramFiles%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"
if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if exist "installer\NSIS\makensis.exe" set "MAKENSIS=installer\NSIS\makensis.exe"

if "%MAKENSIS%"=="" (
    echo NSIS not found! Install from https://nsis.sourceforge.io/
    echo The built exe is at: build_win\yip_os.exe
    pause
    exit /b 1
)

REM ==========================================================================
REM  1. Standard installer — CPU-only CT2
REM ==========================================================================
echo.
echo --- [1/3] Standard installer (CPU) ---
if not exist "%CT2_CPU_PREFIX%\bin\ctranslate2.dll" (
    echo WARNING: CPU-only CT2 not found at %CT2_CPU_PREFIX%
    echo          Build CT2 with -DWITH_CUDA=OFF and install to %CT2_CPU_PREFIX%
    echo          Skipping standard installer.
    goto :skip_standard
)
copy /Y "%CT2_CPU_PREFIX%\bin\ctranslate2.dll" build_win\ 2>nul
"%MAKENSIS%" "installer\app_installer.nsi"
if %ERRORLEVEL% neq 0 (
    echo Standard installer failed!
    pause
    exit /b 1
)
:skip_standard

REM ==========================================================================
REM  2. CUDA Lite installer — CUDA CT2, no cuBLAS bundled
REM ==========================================================================
echo.
echo --- [2/3] CUDA Lite installer ---
copy /Y "%CT2_PREFIX%\bin\ctranslate2.dll" build_win\ 2>nul
"%MAKENSIS%" /DCUDA_LITE "installer\app_installer.nsi"
if %ERRORLEVEL% neq 0 (
    echo CUDA Lite installer failed!
    pause
    exit /b 1
)

REM ==========================================================================
REM  3. CUDA Full installer — CUDA CT2 + cuBLAS bundled
REM ==========================================================================
echo.
echo --- [3/3] CUDA Full installer ---
"%MAKENSIS%" /DCUDA_FULL "installer\app_installer.nsi"
if %ERRORLEVEL% neq 0 (
    echo CUDA Full installer failed!
    pause
    exit /b 1
)

echo.
echo ===================================
echo  All installers built successfully
echo ===================================
echo   YipOS v1.2.0 Setup.exe
echo   YipOS v1.2.0 Setup (CUDA Lite).exe
echo   YipOS v1.2.0 Setup (CUDA).exe
pause
