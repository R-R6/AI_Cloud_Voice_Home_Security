@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo µ±Ç°Ä¿Â¼: %CD%
python md_to_word.py
echo.
pause
