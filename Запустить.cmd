@echo off
setlocal
cd /d "%~dp0"
set PYTHONDONTWRITEBYTECODE=1
"%~dp0_service\python37\Scripts\python.exe" -B "%~dp0desktop\run.py" %*
if errorlevel 1 pause
