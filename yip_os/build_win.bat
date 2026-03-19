@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
cd /d C:\git\williams_tube\yip_os
cmake -B build_win -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build_win
