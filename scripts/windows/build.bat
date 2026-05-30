@echo off
REM Convenience wrapper for build_installers.py
REM This batch file can be run from x64 Native Tools Command Prompt

python "%~dp0build_installers.py" %*
