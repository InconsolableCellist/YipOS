@echo off
echo Building YipOS Release + Installer...

REM Build the Release version first
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64
)
cd /d %~dp0

REM vcpkg integration (optional, for CTranslate2 + SentencePiece)
set "VCPKG_CMAKE="
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_CMAKE=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
        echo vcpkg found at %VCPKG_ROOT%
    )
)

cmake -B build_win -G "Ninja" -DCMAKE_BUILD_TYPE=Release %VCPKG_CMAKE%
cmake --build build_win

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo Build successful! Building installer...

REM Try to find NSIS
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

"%MAKENSIS%" "installer\app_installer.nsi"

if %ERRORLEVEL% neq 0 (
    echo Installer build failed!
    pause
    exit /b 1
)

echo.
echo Installer built: YipOS v1.0 Setup.exe
pause
