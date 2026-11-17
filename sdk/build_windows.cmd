@echo off
rem The code-mod build template for Windows with MinGW-w64 and nothing else (the mod kit's SDK, sdk\README.md): builds
rem one mod's library from its src\*.c (or src\*.c and src\*.cpp) with this folder's include\b3mod_api.h.
rem   sdk\build_windows.cmd <mod folder> [name]
rem gcc / g++ must be on PATH (MinGW-w64's bin folder). The library goes to <mod folder>\bin\windows\<name>.dll, the path
rem the mod.ini names under [code] (windows = bin/windows/<name>.dll); <name> defaults to the folder's name with '-' as '_'.
setlocal enabledelayedexpansion
if "%~1"=="" (
    echo usage: %~nx0 ^<mod folder^> [name]
    exit /b 2
)
set "MOD=%~f1"
set "SDK=%~dp0"
set "NAME=%~2"
if "%NAME%"=="" set "NAME=%~nx1"
set "NAME=%NAME:-=_%"
if not exist "%MOD%\src" (
    echo b3mod: %MOD%\src: no such folder
    exit /b 2
)
set "SRCS="
set "COMPILER=gcc"
for %%F in ("%MOD%\src\*.c") do set SRCS=!SRCS! "%%F"
for %%F in ("%MOD%\src\*.cpp") do (
    set SRCS=!SRCS! "%%F"
    set "COMPILER=g++ -static-libstdc++"
)
if "%SRCS%"=="" (
    echo b3mod: no .c or .cpp files in %MOD%\src
    exit /b 2
)
if not exist "%MOD%\bin\windows" mkdir "%MOD%\bin\windows"
%COMPILER% -O2 "-I%SDK%include" -fvisibility=hidden -fexceptions -Wall -Wextra -shared -static-libgcc -Wl,--no-undefined %SRCS% -o "%MOD%\bin\windows\%NAME%.dll"
if errorlevel 1 exit /b 1
echo b3mod: built %MOD%\bin\windows\%NAME%.dll
