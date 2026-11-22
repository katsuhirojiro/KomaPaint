# Windows x64 only. Freeze the isolated design review; no KomaPaint build or SDK.
from importlib.util import find_spec
from pathlib import Path

windows = Path(SPECPATH).resolve()
project = windows.parent
repo = project.parents[2]
qt_wheel = Path(find_spec("PySide6").origin).parent
crt_names = ("concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
             "msvcp140_codecvt_ids.dll", "vcruntime140.dll", "vcruntime140_1.dll")
APP = "KomaPaint Brush Studio Review 03"

analysis = Analysis(
    [str(project / "app.py")],
    pathex=[str(project)],
    binaries=[(str(qt_wheel / name), ".") for name in crt_names],
    datas=[(str(project / "qml"), "qml"),
           (str(project / "fonts"), "fonts"),
           (str(windows / "BrushStudio.ico"), "windows")],
    hiddenimports=["PySide6.QtQuick", "PySide6.QtQuickControls2", "PySide6.QtSvg", "PySide6.QtTest",
                   "PySide6.QtWidgets", "PySide6.QtGui", "verify",
                   "artwork", "assets", "catalog", "model", "recipes", "render"],
    excludes=["remote", "aiohttp", "tkinter",
              "PySide6.QtWebEngineCore", "PySide6.QtWebEngineWidgets",
              "PySide6.QtWebEngineQuick", "PySide6.QtVirtualKeyboard"],
    noarchive=False,
)
pyz = PYZ(analysis.pure)
exe = EXE(
    pyz, analysis.scripts, [], exclude_binaries=True,
    name=APP, icon=str(windows / "BrushStudio.ico"),
    console=False, debug=False, upx=False, strip=False,
)
app = COLLECT(
    exe, analysis.binaries, analysis.datas, upx=False, strip=False,
    name=APP,
)
