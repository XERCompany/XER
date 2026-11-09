@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d "C:\Users\LXXV\Documents\Developer\XER"
cmake --preset default
if errorlevel 1 exit /b 1
cmake --build out\build\default --target XER_SecurityFlash SecurityFlashService ControllAccountsWindows XER_ContentPasswordGen XER_ContentApps_Smoke XER_UI_Core
exit /b %ERRORLEVEL%
