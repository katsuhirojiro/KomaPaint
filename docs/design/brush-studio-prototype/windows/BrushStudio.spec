# Windows x64 only. Build the mockup; no CMake, KomaPaint executable, or SDK.
from importlib.util import find_spec
from pathlib import Path

windows = Path(SPECPATH).resolve()
project = windows.parent
qt_wheel = Path(find_spec("PySide6").origin).parent
crt_names = ("concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
             "msvcp140_codecvt_ids.dll", "vcruntime140.dll", "vcruntime140_1.dll")

analysis = Analysis(
    [str(project / "main.py")],
    pathex=[str(project)],
    binaries=[(str(qt_wheel / name), ".") for name in crt_names],
    datas=[(str(project / "qml"), "qml"), (str(project / "fonts"), "fonts"),
           (str(windows / "BrushStudio.ico"), "windows")],
    hiddenimports=["PySide6.QtQuickControls2", "PySide6.QtSvg"],
    hookspath=[str(windows / "hooks")],
    excludes=["remote", "aiohttp", "tkinter"],
    noarchive=False,
)
pyz = PYZ(analysis.pure)
exe = EXE(
    pyz, analysis.scripts, [], exclude_binaries=True,
    name="KomaPaint Brush Studio", icon=str(windows / "BrushStudio.ico"),
    console=False, debug=False, upx=False, strip=False,
)
app = COLLECT(
    exe, analysis.binaries, analysis.datas, upx=False, strip=False,
    name="KomaPaint Brush Studio",
)
