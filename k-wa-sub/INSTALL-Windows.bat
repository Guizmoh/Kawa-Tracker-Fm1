@echo off
rem K-WA SUB: installs the firmware in install\ on the M-VAVE FM-1. Double-click it. Needs Python 3 (python.org).
cd /d "%~dp0"
echo.
echo  K-WA SUB - bass synthesizer for the M-VAVE FM-1
echo  1. Switch the FM-1 on, plug it in with a USB DATA cable, close every other MIDI program.
echo  2. Unplug nothing until the installer says it is done.
echo.
set "PKG="
for %%f in (install\k-wa-sub-*.fwsc) do set "PKG=%%f"
if not defined PKG (
  echo No firmware in the install folder.
  goto end
)
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY (
  where python >nul 2>nul && set "PY=python"
)
if not defined PY (
  echo Python 3 is missing: https://www.python.org/downloads/ - tick "Add python.exe to PATH" - then run this again.
  goto end
)
if not exist "install\.venv\Scripts\python.exe" (
  echo Getting the installer ready - only the first time...
  %PY% -m venv install\.venv
  if errorlevel 1 goto broken
  rem rtmidi2 has ready-made wheels for Python 3.10 to 3.14; older Pythons take mido with python-rtmidi.
  install\.venv\Scripts\python.exe -m pip install --quiet --disable-pip-version-check --only-binary=:all: rtmidi2
  if errorlevel 1 install\.venv\Scripts\python.exe -m pip install --quiet --disable-pip-version-check --only-binary=:all: mido python-rtmidi
  if errorlevel 1 goto broken
)
install\.venv\Scripts\python.exe install\fm1_install.py "%PKG%"
goto end
:broken
rmdir /s /q install\.venv 2>nul
echo The installer could not be made ready - see above.
:end
echo.
pause
