#!/bin/sh
# KAWA: installs the firmware in install/ on the M-VAVE FM-1. On a Mac, double-click it (the first time: right
# click, then Open). Needs Python 3 (https://www.python.org/downloads/).
cd "$(dirname "$0")" || exit 1
done_() { printf "\nPress Enter to close. "; read x; exit "$1"; }
echo
echo " KAWA - Pocket Tracker for the M-VAVE FM-1"
echo " 1. Switch the FM-1 on, plug it in with a USB DATA cable, close every other MIDI program."
echo " 2. Unplug nothing until the installer says it is done."
echo
PKG=$(ls install/kawa-*.fwsc 2>/dev/null | tail -n 1)
[ -n "$PKG" ] || { echo "No firmware in the install folder."; done_ 1; }
command -v python3 >/dev/null 2>&1 || { echo "Python 3 is missing: https://www.python.org/downloads/ , then run this again."; done_ 1; }
# rtmidi2 has ready-made wheels for Python 3.10 to 3.14; older Pythons take mido with python-rtmidi.
pipi() { install/.venv/bin/python -m pip install --quiet --disable-pip-version-check --only-binary=:all: "$@"; }
if [ ! -x install/.venv/bin/python ]; then
    echo "Getting the installer ready (only the first time)..."
    if ! { python3 -m venv install/.venv && { pipi rtmidi2 || pipi mido python-rtmidi; }; }; then
        rm -rf install/.venv
        echo "The installer could not be made ready (see above)."
        done_ 1
    fi
fi
install/.venv/bin/python install/fm1_install.py "$PKG"
done_ $?
