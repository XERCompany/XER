@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Users\LXXV\Documents\Developer\XER"
cmake --preset default
cmake --build --preset default --target XER_AsyncIO
