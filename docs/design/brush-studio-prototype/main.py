#!/usr/bin/env python3
"""Launch the isolated KomaPaint Brush Studio design prototype."""
import argparse
from pathlib import Path
import os
import sys


def create_app(remote=False):
    # The Python image renderer and QML bindings share the GUI thread.
    os.environ.setdefault("QSG_RENDER_LOOP", "basic")
    os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")
    if getattr(sys, "frozen", False):
        # Use the same native Windows QPA with or without a GPU driver.
        os.environ["QT_QUICK_BACKEND"] = "software"
    if remote:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        os.environ.setdefault("QT_QUICK_BACKEND", "software")
    from PySide6.QtCore import QUrl
    from PySide6.QtGui import QFontDatabase, QGuiApplication, QIcon
    from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterType
    from art import ArtSheet, Assets
    from model import Studio
    import art

    app = QGuiApplication([sys.argv[0]])
    app.setApplicationName("KomaPaint Brush Workshop")
    root = Path(__file__).resolve().parent
    app.setWindowIcon(QIcon(str(root / "windows" / "BrushStudio.ico")))
    QFontDatabase.addApplicationFont(str(root / "fonts" / "InterVariable.ttf"))
    engine = QQmlApplicationEngine()
    assets = Assets()
    studio = Studio(assets, remote=remote)
    art.STUDIO = studio
    qmlRegisterType(ArtSheet, "StudioMock", 1, 0, "ArtSheet")
    engine.addImageProvider("assets", assets)
    engine.rootContext().setContextProperty("studio", studio)
    engine.load(QUrl.fromLocalFile(str(root / "qml" / "Main.qml")))
    if not engine.rootObjects():
        raise RuntimeError("Brush Studio QML did not load; see the Qt message above.")
    return app, engine, studio, engine.rootObjects()[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    packaged = getattr(sys, "frozen", False)
    parser.add_argument("--verify-package", type=Path, help=argparse.SUPPRESS)
    if not packaged:
        parser.add_argument("--review-port", type=int, help="mirror the actual Qt window to a browser on this port")
        parser.add_argument("--review-host", default="127.0.0.1", help="address for the optional review mirror")
    args = parser.parse_args()
    if args.verify_package is not None:
        if not packaged or sys.platform != "win32":
            parser.error("Package verification is only for the standalone Windows executable.")
        from verify import verify_package
        return verify_package(create_app, args.verify_package)
    remote = not packaged and args.review_port is not None
    app, engine, studio, window = create_app(remote=remote)
    if remote:
        from remote import Mirror
        mirror = Mirror(window, studio, args.review_host, args.review_port)
        app.aboutToQuit.connect(mirror.stop)
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
