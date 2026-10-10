@echo off
title Load games to the Zune
rem Drag a game folder, a disc image (.iso/.bin/.cue) or a .zip onto this file - or double-click it and follow the prompts.
where python >nul 2>nul
if errorlevel 1 (
  echo Python 3 is needed: https://www.python.org/downloads/  ^(tick "Add python.exe to PATH"^)
  pause
  exit /b 1
)
python "%~dp0tools\load_games.py" %*
echo.
pause
