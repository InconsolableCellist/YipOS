@echo off
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64
)
cd /d %~dp0\yip_os
cmake -B build_win -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build_win
