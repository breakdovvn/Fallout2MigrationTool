@echo off
setlocal

rem Build and run Fallout 2 -> FOnline Migration Tool.
rem Requires: CMake >= 3.24 and Visual Studio 2022 (C++20).

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto :error

cmake --build build --config RelWithDebInfo
if errorlevel 1 goto :error

build\RelWithDebInfo\f2mt_viewer.exe %*
goto :eof

:error
echo.
echo Build failed.
pause
exit /b 1
