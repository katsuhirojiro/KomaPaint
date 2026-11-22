"""PyInstaller's QtQml hook, scoped to this app's QML import families."""
from pathlib import PurePath

from PyInstaller.utils.hooks.qt import add_qt6_dependencies, pyside6_library_info

hiddenimports, binaries, datas = add_qt6_dependencies(__file__)
qml_binaries, qml_datas = pyside6_library_info.collect_qtqml_files()


def used_resource(entry):
    src, dest = entry
    parts = PurePath(dest).parts
    imported = parts[parts.index("qml") + 1:] or (PurePath(src).name,)
    if imported[0] in {"QML", "QtQml", "QtCore"}:
        return True
    return imported[0] == "QtQuick" and (len(imported) == 1 or imported[1] in {
        "Controls", "Dialogs", "Effects", "Layouts", "NativeStyle", "Shapes", "Templates", "Window",
    })


binaries += [file for file in qml_binaries if used_resource(file)]
datas += [file for file in qml_datas if used_resource(file)]
