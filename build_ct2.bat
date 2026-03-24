@echo off
REM Build CTranslate2 + SentencePiece with CUDA into C:\ct2_install
REM Requires: VS 2026 BuildTools, micromamba ct2build env with cuda-toolkit
REM Usage: build_ct2.bat

set "CT2_INSTALL=C:\ct2_install"
set "CUDA_ENV=C:\micromamba\envs\ct2build\Library"

REM --- VS 2026 environment ---
if defined VSCMD_VER goto :skip_vcvars
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
:skip_vcvars

REM --- Point CMake at CUDA toolkit without overriding system cmake ---
REM Only add nvcc to PATH, append AFTER system PATH so VS cmake wins
REM Use forward slashes for CMake env vars (backslashes break FindCUDA string parsing)
set "CUDA_TOOLKIT_ROOT_DIR=C:/micromamba/envs/ct2build/Library"
set "CUDAToolkit_ROOT=C:/micromamba/envs/ct2build/Library"
set "CUDA_PATH=C:/micromamba/envs/ct2build/Library"
set "PATH=%PATH%;%CUDA_ENV%\bin"

echo.
echo === Build environment ===
echo CUDA: %CUDA_ENV%
echo Install prefix: %CT2_INSTALL%
where nvcc
nvcc --version
where cmake
echo.

REM --- Clone/update repos ---
if not exist "C:\ct2_build" mkdir C:\ct2_build
cd /d C:\ct2_build

if not exist sentencepiece (
    echo === Cloning SentencePiece ===
    git clone --depth 1 https://github.com/google/sentencepiece.git
)
if not exist CTranslate2 (
    echo === Cloning CTranslate2 ===
    git clone --depth 1 --branch v4.5.0 --recurse-submodules https://github.com/OpenNMT/CTranslate2.git
)
REM Init submodules if clone already existed without them
cd /d C:\ct2_build\CTranslate2
git submodule update --init --recursive
cd /d C:\ct2_build

REM --- Build SentencePiece ---
echo.
echo === Building SentencePiece ===
cd /d C:\ct2_build\sentencepiece
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%CT2_INSTALL%" -DSPM_BUILD_TEST=OFF -DSPM_USE_BUILTIN_PROTOBUF=ON
if errorlevel 1 goto :error
cmake --build build
if errorlevel 1 goto :error
cmake --install build
if errorlevel 1 goto :error
echo SentencePiece installed to %CT2_INSTALL%

REM --- Build CTranslate2 ---
echo.
echo === Building CTranslate2 with CUDA ===
cd /d C:\ct2_build\CTranslate2
cmake -B build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX="%CT2_INSTALL%" ^
    -DCMAKE_PREFIX_PATH="%CT2_INSTALL%" ^
    -DWITH_CUDA=ON ^
    -DWITH_MKL=OFF ^
    -DWITH_DNNL=OFF ^
    -DOPENMP_RUNTIME=NONE ^
    -DCUDA_TOOLKIT_ROOT_DIR="C:/micromamba/envs/ct2build/Library" ^
    -DCUDAToolkit_ROOT="C:/micromamba/envs/ct2build/Library" ^
    -DCMAKE_CUDA_COMPILER="C:/micromamba/envs/ct2build/Library/bin/nvcc.exe" ^
    -DCMAKE_CUDA_FLAGS="--allow-unsupported-compiler" ^
    -DCUDA_NVCC_FLAGS="--allow-unsupported-compiler" ^
    -DCMAKE_CUDA_ARCHITECTURES="60;70;75;80;86;89" ^
    -DCMAKE_POLICY_DEFAULT_CMP0146=OLD ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
    -DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES="C:/micromamba/envs/ct2build/Library/include/targets/x64"
if errorlevel 1 goto :error
cmake --build build
if errorlevel 1 goto :error
cmake --install build
if errorlevel 1 goto :error
echo CTranslate2 installed to %CT2_INSTALL%

REM --- Copy CUDA runtime DLLs to install prefix ---
echo.
echo === Copying CUDA runtime DLLs ===
if not exist "%CT2_INSTALL%\bin" mkdir "%CT2_INSTALL%\bin"
copy /Y "%CUDA_ENV%\bin\cublas64_12.dll" "%CT2_INSTALL%\bin\" 2>nul
copy /Y "%CUDA_ENV%\bin\cublasLt64_12.dll" "%CT2_INSTALL%\bin\" 2>nul
copy /Y "%CUDA_ENV%\bin\cudart64_12.dll" "%CT2_INSTALL%\bin\" 2>nul

echo.
echo === Done! ===
echo To build YipOS with translation:
echo   set CT2_PREFIX=%CT2_INSTALL%
echo   build_win.bat
goto :eof

:error
echo.
echo === BUILD FAILED ===
exit /b 1
