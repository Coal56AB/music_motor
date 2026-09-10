@echo off
setlocal
set "PATH=%LOCALAPPDATA%\Programs\Python\Launcher;%PATH%"
cd /d "%~dp0"
set PYTHONDONTWRITEBYTECODE=1
py -3.7 -B "%~dp0desktop\run.py" %*
if errorlevel 1 pause
