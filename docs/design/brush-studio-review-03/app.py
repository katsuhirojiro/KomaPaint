"""Third design review. Standalone mockup; does not import production code."""
import argparse
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")
if getattr(sys, "frozen", False):
    os.environ.setdefault("QSG_RENDER_LOOP", "basic")
    os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QUrl, QtMsgType, qInstallMessageHandler
from PySide6.QtGui import QFontDatabase, QIcon
from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterType
from PySide6.QtWidgets import QApplication

MESSAGES = []
_LEVELS = {QtMsgType.QtWarningMsg: "warning", QtMsgType.QtCriticalMsg: "critical",
           QtMsgType.QtFatalMsg: "fatal"}


def _message(mode, context, text):
    level = _LEVELS.get(mode)
    if level:
        MESSAGES.append(f"{level}: {text}")
        if sys.stderr:
            print(f"{level}: {text}", file=sys.stderr)


qInstallMessageHandler(_message)

TRACE = os.environ.get("KOMAPAINT_STUDIO_TRACE")


def note(text):
    if not TRACE:
        return
    try:
        with Path(TRACE).open("a", encoding="utf-8") as stream:
            stream.write(text + "\n")
    except OSError:
        pass


note("Starting Brush Studio Review 03")

import render
from artwork import Artwork
from assets import Assets
from model import Studio


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--captures", type=Path, help="Save review PNGs to this folder and exit.")
    parser.add_argument("--verify-package", type=Path, help="Run the packaged interaction walkthrough.")
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--advanced", action="store_true")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setApplicationName("KomaPaint Brush Studio Review 03")
    app.setOrganizationName("KomaPaint")
    icon = HERE / "windows" / "BrushStudio.ico"
    if icon.is_file():
        app.setWindowIcon(QIcon(str(icon)))
    fonts = HERE / "fonts"
    if not fonts.is_dir():
        fonts = HERE.parent / "brush-studio-prototype" / "fonts"
    for font in sorted(fonts.glob("*.ttf")):
        QFontDatabase.addApplicationFont(str(font))
    note(f"Qt platform: {app.platformName()}")

    qmlRegisterType(Artwork, "ReviewArt", 1, 0, "Artwork")
    engine = QQmlApplicationEngine()
    assets = Assets(HERE / "assets" / "icons")
    studio = Studio(assets)
    render.STUDIO = studio
    engine.addImageProvider("brushes", assets)
    engine.rootContext().setContextProperty("studio", studio)
    try:
        engine.load(QUrl.fromLocalFile(str(HERE / "qml/Main.qml")))
    except Exception:
        import traceback
        note(traceback.format_exc())
        raise
    if not engine.rootObjects():
        note("QML did not produce a window")
        return 2
    note("QML loaded")
    win = engine.rootObjects()[0]
    screen = app.primaryScreen().availableGeometry()
    width = args.width or min(1440, max(1000, screen.width() - 40))
    height = args.height or min(920, max(700, screen.height() - 70))
    if app.platformName() == "windows":
        width = min(width, 1000)
        height = min(height, 700)
    win.resize(width, height)
    win.show()
    note(f"Window: {win.width()} x {win.height()}")
    if args.advanced:
        studio.toggleAdvanced()

    if args.captures:
        from verify import capture
        capture(win, args.captures)
        app.quit()
        return 0
    if args.verify_package:
        from verify import walkthrough
        win.resize(1280, 800)
        passed = walkthrough(win, args.verify_package, MESSAGES)
        app.quit()
        return 0 if passed else 1
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
