"""Assert the frozen app's payload, attach notices, and make the candidate ZIP."""
import argparse
from importlib.metadata import distribution
from pathlib import Path
from shutil import copyfile
import sys
from zipfile import ZIP_DEFLATED, ZipFile


ARCHIVE = "KomaPaint-Brush-Studio-Windows-x64.zip"


def find_license(package, filename):
    dist = distribution(package)
    for path in dist.files:
        if path.name.lower() == filename.lower() and not any(part.startswith("..") for part in path.parts):
            return Path(dist.locate_file(path))
    raise FileNotFoundError(f"{package}: {filename}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="PyInstaller onedir output folder")
    parser.add_argument("out", type=Path, help="candidate ZIP output folder")
    opts = parser.parse_args()
    app = opts.app.resolve()
    source = Path(__file__).resolve().parent
    runtime = app / "_internal"
    for path in [app / "KomaPaint Brush Studio.exe", runtime / "qml/Main.qml", runtime / "python312.dll",
                 runtime / "PySide6/plugins/platforms/qwindows.dll", runtime / "fonts/InterVariable.ttf",
                 runtime / "PySide6/qml/QtQuick/Controls/Basic/qmldir", runtime / "PySide6/qml/QtQuick/Dialogs/qmldir"]:
        if not path.is_file():
            raise FileNotFoundError(path)
    for name in ("Qt6Gui.dll", "Qt6Quick.dll", "Qt6Qml.dll", "msvcp140.dll", "vcruntime140.dll"):
        if not list(runtime.rglob(name)):
            raise FileNotFoundError(name)
    if list(runtime.rglob("Qt6WebEngine*")) or list(runtime.rglob("Qt6VirtualKeyboard*")):
        raise RuntimeError("Unrelated Qt modules made their way into the standalone package.")

    copyfile(source / "Start Here.txt", app / "Start Here.txt")
    licenses = app / "Licenses"
    licenses.mkdir(exist_ok=True)
    for name in ("Third-Party.txt", "GPL-3.0.txt", "LGPL-3.0.txt"):
        copyfile(source / name, licenses / name)
    copyfile(find_license("Pillow", "LICENSE"), licenses / "Pillow.txt")
    copyfile(find_license("PyInstaller", "COPYING.txt"), licenses / "PyInstaller.txt")
    for entry in distribution("Pillow").files:
        if entry.name.lower().startswith("license") and entry.name.lower() != "license":
            copyfile(distribution("Pillow").locate_file(entry), licenses / ("Pillow-" + entry.name))
    copyfile(Path(sys.base_prefix) / "LICENSE.txt", licenses / "Python.txt")

    opts.out.mkdir(parents=True, exist_ok=True)
    archive = opts.out / ARCHIVE
    with ZipFile(archive, "w", ZIP_DEFLATED, compresslevel=6) as zipfile:
        for path in sorted(app.rglob("*")):
            if path.is_file():
                zipfile.write(path, Path("KomaPaint Brush Studio") / path.relative_to(app))
    print(f"{archive}: {archive.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
