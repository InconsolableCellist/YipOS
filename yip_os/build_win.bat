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

cd /d %~dp0

REM --- vcpkg integration (optional) ---
set "VCPKG_CMAKE="
if not defined VCPKG_ROOT goto :skip_vcpkg
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" goto :skip_vcpkg
set VCPKG_CMAKE=-DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
echo vcpkg found at %VCPKG_ROOT%
:skip_vcpkg

REM --- CTranslate2 auto-detect ---
REM Override: set CT2_PREFIX=C:\path\to\ct2 before running.
REM Priority: user-set CT2_PREFIX > CUDA build > CPU-only build > none
set "CT2_PREFIX_FOUND="
if defined CT2_PREFIX goto :check_ct2
REM Try CUDA build first
if exist "C:\ct2_install\lib\cmake\ctranslate2" (
    set "CT2_PREFIX=C:\ct2_install"
    goto :check_ct2
)
REM Fall back to CPU-only build
if exist "C:\ct2_install_cpu\lib\cmake\ctranslate2" (
    set "CT2_PREFIX=C:\ct2_install_cpu"
    goto :check_ct2
)
echo CTranslate2 not found — translation will be disabled
goto :skip_ct2

:check_ct2
if not exist "%CT2_PREFIX%\lib\cmake\ctranslate2" (
    echo CTranslate2 not found at %CT2_PREFIX% — translation will be disabled
    goto :skip_ct2
)
REM Detect if this is a CUDA or CPU-only build
if exist "%CT2_PREFIX%\bin\cublas64_12.dll" echo CTranslate2 found at %CT2_PREFIX% [CUDA]
if not exist "%CT2_PREFIX%\bin\cublas64_12.dll" echo CTranslate2 found at %CT2_PREFIX% [CPU-only]
set "CT2_PREFIX_FOUND=%CT2_PREFIX%"
:skip_ct2

REM --- MeCab auto-detect ---
REM Override: set MECAB_PREFIX=C:\path\to\mecab before running.
set "MECAB_PREFIX_FOUND="
if not defined MECAB_PREFIX set "MECAB_PREFIX=C:\mecab_install"
if not exist "%MECAB_PREFIX%\lib\libmecab.lib" (
    echo MeCab not found at %MECAB_PREFIX% — Japanese kanji will display as '?'
    goto :skip_mecab
)
echo MeCab found at %MECAB_PREFIX%
set "MECAB_PREFIX_FOUND=%MECAB_PREFIX%"
:skip_mecab

REM --- Build CMAKE_PREFIX_PATH from detected prefixes ---
set "PREFIX_PATH="
if defined CT2_PREFIX_FOUND set "PREFIX_PATH=%CT2_PREFIX_FOUND%"
if defined MECAB_PREFIX_FOUND (
    if defined PREFIX_PATH (
        set "PREFIX_PATH=%PREFIX_PATH%;%MECAB_PREFIX_FOUND%"
    ) else (
        set "PREFIX_PATH=%MECAB_PREFIX_FOUND%"
    )
)
set "PREFIX_CMAKE="
if defined PREFIX_PATH set "PREFIX_CMAKE=-DCMAKE_PREFIX_PATH=%PREFIX_PATH%"

cmake -B build_win -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DYIPOS_HAS_OPENVR=ON %VCPKG_CMAKE% %PREFIX_CMAKE%
if errorlevel 1 (
    echo.
    echo *** CMake configure FAILED — aborting before build. ***
    exit /b 1
)
cmake --build build_win
if errorlevel 1 (
    echo.
    echo *** Build FAILED. ***
    exit /b 1
)

REM --- Copy CT2 runtime DLLs to build output ---
if not defined CT2_PREFIX_FOUND goto :skip_ct2_copy
copy /Y "%CT2_PREFIX_FOUND%\bin\ctranslate2.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX_FOUND%\bin\sentencepiece.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX_FOUND%\bin\openblas.dll" build_win\ 2>nul
REM CUDA DLLs — only present in CUDA builds, silently skipped for CPU-only
copy /Y "%CT2_PREFIX_FOUND%\bin\cublas64_12.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX_FOUND%\bin\cublasLt64_12.dll" build_win\ 2>nul
copy /Y "%CT2_PREFIX_FOUND%\bin\cudart64_12.dll" build_win\ 2>nul
:skip_ct2_copy

REM --- Copy MeCab DLL + dictionary + mecabrc to build output ---
if not defined MECAB_PREFIX_FOUND goto :skip_mecab_copy
copy /Y "%MECAB_PREFIX_FOUND%\bin\libmecab.dll" build_win\ 2>nul
if exist "%MECAB_PREFIX_FOUND%\dic\ipadic" (
    if not exist "build_win\mecab-dic\ipadic" mkdir "build_win\mecab-dic\ipadic"
    xcopy /Y /Q "%MECAB_PREFIX_FOUND%\dic\ipadic\*.*" "build_win\mecab-dic\ipadic\" >nul
    echo MeCab dictionary copied to build_win\mecab-dic\ipadic
)
REM Generate mecabrc next to exe (MeCab needs explicit -r to find it)
echo dicdir = mecab-dic\ipadic> build_win\mecabrc
:skip_mecab_copy
