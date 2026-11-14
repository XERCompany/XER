@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\Developers\XER3\out\build\default
"C:\Program Files\CMake\bin\ninja.exe" HubCivitAIBrowser
