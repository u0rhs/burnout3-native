@echo off
rem run-all-tests.bat -- the scripted test sessions in a window, one after another (README.txt). Arguments: run ids / names, or "list".
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-all-tests.ps1" %*
pause
