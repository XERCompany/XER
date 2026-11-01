@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d "C:\Users\LXXV\Documents\Developer\XER"
echo === CMAKE CONFIGURE (default + ENABLE_AI) ===
cmake --preset default -DENABLE_AI=ON
if errorlevel 1 (echo CONFIGURE_FAILED & exit /b 1)
echo === CMAKE BUILD (default, keep-going) ===
cmake --build --preset default -- -k 0
set RC=%ERRORLEVEL%
echo === BUILD EXIT CODE: %RC% ===
exit /b %RC%
