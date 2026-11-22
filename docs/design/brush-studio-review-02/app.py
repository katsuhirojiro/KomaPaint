"""Second design review. Standalone mockup; does not import production code."""
import argparse
import os
import sys
from math import sin
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

os.environ.setdefault('QT_QUICK_CONTROLS_STYLE', 'Basic')
if getattr(sys, 'frozen', False):
    os.environ.setdefault('QSG_RENDER_LOOP', 'basic')
    os.environ.setdefault('QT_QUICK_BACKEND', 'software')

from PySide6.QtCore import QUrl, QtMsgType, qInstallMessageHandler
from PySide6.QtGui import QFontDatabase, QIcon
from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterType
from PySide6.QtWidgets import QApplication

MESSAGES = []
_LEVELS = {QtMsgType.QtWarningMsg: 'warning', QtMsgType.QtCriticalMsg: 'critical',
           QtMsgType.QtFatalMsg: 'fatal'}


def _message(mode, context, text):
    level = _LEVELS.get(mode)
    if level:
        MESSAGES.append(f'{level}: {text}')
        if sys.stderr:
            print(f'{level}: {text}', file=sys.stderr)


qInstallMessageHandler(_message)

TRACE = os.environ.get('KOMAPAINT_STUDIO_TRACE')


def note(text):
    if not TRACE:
        return
    try:
        with Path(TRACE).open('a', encoding='utf-8') as stream:
            stream.write(text + '\n')
    except OSError:
        pass


note('Starting Brush Studio Review 02')

import preview
from preview import Artwork, Assets, motion
from recipes import PRESENTATION, starters
from studio import Draft, Studio


def seeded_cloud():
    strokes = []
    bands = [
        (.045, .386, .675, .040, 222, '#a29aaa', '#746f86', 11),
        (.245, .322, .690, -.022, 196, '#d8c8cc', '#a89bab', 12),
        (.080, .292, .615, .030, 163, '#f0e0d2', '#c5b6b6', 13),
        (.315, .241, .445, -.028, 133, '#fdf1dd', '#dcbea9', 14),
        (.060, .540, .395, .020, 105, '#e7cdc4', '#b7a3a9', 15),
        (.505, .504, .405, -.014, 89, '#d6c0bf', '#9b93a2', 16),
    ]
    for index, (x, y, length, rise, size, ink, blend, seed) in enumerate(bands):
        points = []
        for i in range(36):
            t = i / 35
            points.append((x + length*t, y + rise*t + .021*preview.sin(t*5.4 + index*.9),
                           .94, .9*t, .18))
        strokes.append(dict(points=points, seed=seed, size=size, color=ink))
    return tuple(strokes)


def seeded_pencil():
    strokes = []
    strokes.append(motion(lambda t: (.565 + .285*t, .205 + .052*sin(t*3.7+.4) + .016*t),
                          19, '#403d3d', seed=31, firm=True))
    strokes.append(motion(lambda t: (.575 + .225*t, .315 - .036*sin(t*4.9) + .024*t),
                          9, '#514d4b', seed=32))
    for i in range(7):
        x = .615 + i*.022
        strokes.append(motion(lambda t: (x + .034*t, .605 + .050*t + .008*sin(t*5.1+i)),
                              7 + (i % 2)*2, '#55514f', seed=40+i, firm=True))
    strokes.append(motion(lambda t: (.505 + .150*t, .472 + .034*sin(t*4.4) - .018*t),
                          12, '#4a4645', seed=51))
    return tuple(strokes)


def seeded_understory():
    strokes = []
    for index, (x, y, length, size, ink, seed) in enumerate((
            (.14, .46, .58, 84, '#59634c', 21),
            (.26, .42, .48, 68, '#7b7a52', 22),
            (.21, .55, .50, 58, '#8a7358', 23))):
        points = []
        for i in range(28):
            t = i / 27
            points.append((x + length*t, y + .018*preview.sin(t*4.1 + index), .82, .9*t, .28))
        strokes.append(dict(points=points, seed=seed, size=size, color=ink))
    return tuple(strokes)


def seed_examples(studio):
    for key, strokes in (('pencil', seeded_pencil()), ('bank', seeded_cloud()), ('understory', seeded_understory())):
        if key not in studio.catalog:
            continue
        draft = studio.sessions.setdefault(key, Draft(studio.catalog[key], PRESENTATION[key]))
        draft.pad.strokes = strokes
        draft.pad.undo.clear()
        draft.pad.redo.clear()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--captures', type=Path, help='Save wide review PNGs to this folder and exit.')
    parser.add_argument('--verify-package', type=Path, help='Run the packaged interaction walkthrough.')
    parser.add_argument('--width', type=int)
    parser.add_argument('--height', type=int)
    parser.add_argument('--advanced', action='store_true')
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setApplicationName('KomaPaint Brush Studio Review 02')
    app.setOrganizationName('KomaPaint')
    icon = HERE / 'windows' / 'BrushStudio.ico'
    if icon.is_file():
        app.setWindowIcon(QIcon(str(icon)))
    fonts = HERE / 'fonts'
    if not fonts.is_dir():
        fonts = HERE.parent / 'brush-studio-prototype' / 'fonts'
    for font in sorted(fonts.glob('*.ttf')):
        QFontDatabase.addApplicationFont(str(font))
    note(f'Qt platform: {app.platformName()}')

    qmlRegisterType(Artwork, 'ReviewArt', 1, 0, 'Artwork')
    engine = QQmlApplicationEngine()
    icons = HERE / 'assets' / 'icons'
    if not icons.is_dir():
        icons = ROOT / 'ui' / 'assets' / 'icons'
    assets = Assets(icons)
    studio = Studio(assets)
    preview.STUDIO = studio
    seed_examples(studio)
    engine.addImageProvider('brushes', assets)
    engine.rootContext().setContextProperty('studio', studio)
    try:
        engine.load(QUrl.fromLocalFile(str(HERE / 'qml/Main.qml')))
    except Exception:
        import traceback
        note(traceback.format_exc())
        raise
    if not engine.rootObjects():
        note('QML did not produce a window')
        return 2
    note('QML loaded')
    win = engine.rootObjects()[0]
    screen = app.primaryScreen().availableGeometry()
    width = args.width or min(1400, max(1000, screen.width() - 40))
    height = args.height or min(900, max(700, screen.height() - 70))
    win.resize(width, height)
    win.show()
    note(f'Window: {win.width()} x {win.height()}')
    if args.advanced:
        win.setProperty('advancedOpen', True)

    if args.captures:
        from verify import capture
        capture(win, args.captures, wide=True)
        app.quit()
        return 0
    if args.verify_package:
        from verify import walkthrough
        win.resize(1024, 720)
        passed = walkthrough(win, args.verify_package, MESSAGES)
        app.quit()
        return 0 if passed else 1
    return app.exec()


if __name__ == '__main__':
    sys.exit(main())
