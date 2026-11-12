#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The one-file installer: KAWA's web installer (../../install/webinstall.js) and the newest install/k-wa-sub-*.fwsc
# put into web/installer.html -> ../installer.html (works offline, from a file or a web page).
set -e
cd "$(dirname "$0")/.."
python3 - <<'PY'
import base64, glob, re
from pathlib import Path
pkg = sorted(glob.glob("../install/k-wa-sub-*.fwsc"), key=lambda p: [int(x) for x in re.findall(r"\d+", p)])[-1]
raw = Path(pkg).read_bytes()
js = Path("../../install/webinstall.js").read_text()
name = Path(pkg).name
ver = re.search(r"k-wa-sub-([\d.]+)(?:-\w+)?\.fwsc", name).group(1)
page = (Path("web/installer.html").read_text().replace("__WEBINSTALL_JS__", js).replace("__PKG_B64__", base64.b64encode(raw).decode())
        .replace("__PKG_NAME__", name).replace("__PKG_ID__", "FM-1_97" + ver.split(".")[1]))
Path("../installer.html").write_text(page)
print(name, len(page), "B")
PY
