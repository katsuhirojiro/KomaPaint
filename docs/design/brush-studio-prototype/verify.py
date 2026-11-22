#!/usr/bin/env python3
"""Qt UI walkthrough for both the development app and the extracted Windows EXE."""
import argparse
from copy import deepcopy
import json
import os
from struct import calcsize
import sys
import traceback
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image, ImageDraw
from PySide6.QtCore import QCoreApplication, QEvent, QPointF, Qt, QtMsgType, QUrl, qInstallMessageHandler, qVersion
from PySide6.QtGui import QKeyEvent
from PySide6.QtTest import QTest
from shiboken6 import delete

from model import entries_in


def items(item):
    yield item
    for child in item.childItems():
        yield from items(child)


class UI:
    def __init__(self, window, captures):
        self.window = window
        self.captures = captures
        self.touch = QTest.createTouchDevice()

    def visible(self, name):
        return next((i for i in items(self.window.contentItem()) if i.objectName() == name and i.isVisible()), None)

    def get(self, name):
        found = self.visible(name)
        assert found is not None, f"No visible control called {name} at {self.window.width()}×{self.window.height()}"
        return found

    def dock(self, name):
        if self.visible(name + "Choice"):
            self.click(name + "Choice")

    def point(self, name, x=.5, y=.5):
        item = self.get(name)
        pt = item.mapToScene(QPointF(item.width() * x, item.height() * y)).toPoint()
        parent = item.parentItem()
        while parent:
            if parent.clip():
                local = parent.mapFromScene(QPointF(pt))
                if not parent.contains(local):
                    raise AssertionError(f"{name} is outside its visible region at {pt}; clipping item {parent.objectName()}")
            parent = parent.parentItem()
        return pt

    def click(self, name, x=.5, y=.5, touch=False):
        if not self.visible(name):
            if name.startswith(("entry_", "visibility_", "moveEntry")) or name in {
                    "wholeBrushRow", "addMarkButton", "addEntryButton", "entryActions"}:
                self.dock("Marks")
            elif name in ("DynamicsChoice", "BrushChoice", "SelectedChoice"):
                self.dock("Settings")
        pt = self.point(name, x, y)
        assert self.get(name).isEnabled(), name
        if touch:
            QTest.touchEvent(self.window, self.touch, False).press(0, pt, self.window).commit()
            QTest.qWait(20)
            QTest.touchEvent(self.window, self.touch, False).release(0, pt, self.window).commit()
        else:
            QTest.mouseClick(self.window, Qt.LeftButton, Qt.NoModifier, pt)
        QTest.qWait(56)

    def field(self, name, value, commit=True):
        self.click(name)
        QTest.keyClick(self.window, Qt.Key.Key_A, Qt.ControlModifier)
        for char in str(value):
            for kind in (QEvent.Type.KeyPress, QEvent.Type.KeyRelease):
                QCoreApplication.sendEvent(self.window, QKeyEvent(kind, ord(char.upper()), Qt.NoModifier, char))
        if commit:
            QTest.keyClick(self.window, Qt.Key.Key_Return)
        QTest.qWait(45)

    def stroke(self, surface, points):
        pos = [self.point(surface, x, y) for x, y in points]
        QTest.mousePress(self.window, Qt.LeftButton, Qt.NoModifier, pos[0])
        for point in pos[1:]:
            QTest.mouseMove(self.window, point, 7)
        QTest.mouseRelease(self.window, Qt.LeftButton, Qt.NoModifier, pos[-1])
        QTest.qWait(55)

    def close_popup(self):
        QTest.keyClick(self.window, Qt.Key.Key_Escape)
        QTest.qWait(45)

    def scroll(self, name, amount):
        view = self.get(name)
        flick = view.property("contentItem") if name in ("settingsScroll", "markList") else view
        flick.setProperty("contentY", int(amount))
        QTest.qWait(55)

    def reveal(self, name, in_view="settingsScroll"):
        child = self.get(name)
        view = self.get(in_view)
        parent = view.property("contentItem")
        relative = child.mapToItem(view, QPointF(0, 0)).y()
        if relative < 0 or relative + child.height() > view.height():
            target = parent.property("contentY") + relative - 14
            self.scroll(in_view, min(max(0, target), parent.property("contentHeight") - parent.height()))

    def draw_pad(self):
        self.stroke("padSurface", [(.44, .56), (.49, .53), (.53, .58), (.58, .63), (.62, .61), (.66, .59), (.72, .56)])

    def scene(self, filename=None):
        screenshot = self.window.grabWindow()
        assert not screenshot.isNull()
        if filename and self.captures:
            assert screenshot.save(str(self.captures / (filename + ".png")))
        return screenshot

    def pixels(self, name):
        item = self.get(name)
        corner = item.mapToScene(QPointF()).toPoint()
        copy = self.scene().copy(corner.x(), corner.y(), round(item.width()), round(item.height()))
        return bytes(copy.constBits())


def marks(studio):
    return [e for e in entries_in(studio._draft["entries"]) if e["kind"] == "mark"]


STATES = {
    "01-starters": "01-first-opening.png",
    "02-pencil": "02-basic-pencil.png",
    "04-cloud": "03-multi-mark-cloud.png",
    "05-advanced": "04-advanced-curves.png",
    "08-draw-tip": "05-draw-a-tip.png",
}


def run(create_app, captures=None, evidence=None):
    if captures:
        captures.mkdir(parents=True, exist_ok=True)
    warnings = []
    if evidence is not None:
        evidence["qtWarnings"] = warnings

    def report(mode, _context, message):
        if mode in (QtMsgType.QtWarningMsg, QtMsgType.QtCriticalMsg):
            warnings.append(message)
            if sys.stdout is not None:
                print("Qt:", message, flush=True)

    old = qInstallMessageHandler(report)
    engine = None
    try:
        app, engine, s, w = create_app()
        if evidence is not None:
            evidence.update(executable=sys.executable, frozen=bool(getattr(sys, "frozen", False)),
                            qtPlatform=app.platformName(), qtVersion=qVersion(), architecture=calcsize("P") * 8,
                            workingDirectory=os.getcwd())
            assert evidence["qtPlatform"] == "windows" and evidence["architecture"] == 64
        w.requestActivate()
        QTest.qWait(140)
        # Some native desktops constrain the first shown size. Request the wide
        # review layout after the top-level window exists, and support either dock.
        w.resize(1440, 900)
        QTest.qWait(180)
        if evidence is not None:
            evidence["reviewWindow"] = [w.width(), w.height()]
            evidence["display"] = [w.screen().geometry().width(), w.screen().geometry().height()]
        ui = UI(w, captures)
        assert s.route == "home"
        for starter in ("pencil", "pen", "paint", "airbrush", "scratch", "duplicate", "tip", "import", "cloud"):
            ui.point(starter + "Starter")
        ui.scene("01-starters")
        ui.click("pencilStarter")
        assert s.route == "editor" and s.brush["name"] == "Graphite Pencil"
        original = deepcopy(s._draft)
        dark = ui.pixels("padSurface")
        ui.field("opacityValue", 24)
        assert s.brush["opacity"] == 24 and s.brush["size"] == original["size"]
        assert s.catalog["pencil"]["opacity"] == original["opacity"]
        assert dark != ui.pixels("padSurface")
        ui.scene("02-pencil")

        ui.draw_pad()
        assert s.strokeCount == 1 and s.canUndo
        ui.click("padUndo")
        assert s.strokeCount == 0 and s.brush["opacity"] == 24 and s.draftEdits == 1
        ui.click("padRedo")
        assert s.strokeCount == 1
        ui.click("padClear")
        assert s.strokeCount == 0
        ui.click("padUndo")
        assert s.strokeCount == 1
        ui.field("opacityValue", 51)
        ui.click("draftHistoryButton")
        ui.click("draftUndo")
        assert s.brush["opacity"] == 24 and s.strokeCount == 1
        ui.click("draftRedo")
        assert s.brush["opacity"] == 51 and s.strokeCount == 1
        ui.close_popup()

        draft = deepcopy(s._draft)
        ui.click("padColor")
        ui.click("SiennaInk")
        ui.click("padSize")
        ui.field("testSizeValue", 32)
        ui.close_popup()
        ui.draw_pad()
        assert [a["size"] for a in s.pad.strokes] == [16, 32]
        assert s.pad.strokes[-1]["color"] == "#ad573f" and s._draft == draft
        ui.click("comparePad")
        before = ui.pixels("padSurface")
        ui.click("comparePad")
        assert before != ui.pixels("padSurface")

        ui.click("entry_paper")
        ui.click("chooseTexture")
        ui.scene("03-texture-shelf")
        ui.click("linenTile")
        ui.click("acceptAsset")
        assert s.entry["asset"] == "linen"
        ui.field("texture_strengthValue", 92)
        for name in ("CanvasChoice", "StrokeChoice", "PermarkChoice"):
            ui.reveal(name)
            ui.click(name)
        moving = ui.pixels("padSurface")
        ui.reveal("CanvasChoice")
        ui.click("CanvasChoice")
        assert s.entry["textureMode"] == "Per mark" and s.entry["anchor"] == "Canvas"
        assert moving != ui.pixels("padSurface")

        ui.click("addMarkButton")
        ui.click("leafTile")
        ui.click("acceptAsset")
        assert [e["asset"] for e in marks(s)] == ["graphite", "leaf"] and s.entry["asset"] == "leaf"
        ui.click("backToStarters")
        ui.click("cloudStarter")
        assert [e["asset"] for e in marks(s)] == ["billow", "wisp", "cumulus"]
        signatures, drawings = [], []
        for mode, name in [("In Order", "InOrderChoice"), ("Random", "RandomChoice"), ("Weighted", "WeightedChoice")]:
            ui.click(name)
            assert s.brush["order"] == mode
            signatures.append([e["number"] for e in s.picks])
            drawings.append(ui.pixels("padSurface"))
        assert signatures[0] == [1, 2, 3, 1, 2, 3, 1, 2]
        assert len({tuple(i) for i in signatures}) == 3 and len(set(drawings)) == 3
        ui.scene("04-cloud")
        ui.click("entry_wisp")
        others = [deepcopy(e) for e in marks(s) if e["uid"] != s.selected]
        ui.field("mark_sizeValue", 140)
        ui.field("mark_angleValue", 43)
        assert s.entry["size"] == 140 and s.entry["angle"] == 43
        assert others == [e for e in marks(s) if e["uid"] != s.selected]
        ui.click("moveEntryUp")
        assert marks(s)[0]["asset"] == "wisp"
        ui.click("visibility_wisp")
        assert not s.entry["visible"] and all(e["asset"] != "wisp" for e in s.picks)
        ui.click("visibility_wisp")
        ui.click("entryActions")
        ui.click("duplicateAction")
        assert len(marks(s)) == 4 and s.entry["name"].endswith(" copy")
        ui.click("entryActions")
        ui.click("removeAction")
        assert len(marks(s)) == 3
        ui.click("addEntryButton")
        ui.click("textureAction")
        ui.click("charcoalTile")
        ui.click("acceptAsset")
        assert s.textureCount == 2 and s.entry["asset"] == "charcoal"

        ui.draw_pad()
        selected = s.selected
        retained = deepcopy(s.pad.strokes)
        ui.click("DynamicsChoice")
        ui.click("advancedButton")
        fresh = ui.pixels("padSurface")
        ui.click("curveGraph", .5, .76)
        assert "Pressure:Size" in s.brush["curves"] and fresh != ui.pixels("padSurface")
        ui.click("curveInput")
        ui.click("TiltAction")
        ui.click("curveOutput")
        ui.click("OpacityAction")
        ui.click("curveGraph", .45, .22)
        assert "Tilt:Opacity" in s.brush["curves"]
        ui.click("OriginalChoice")
        ui.scene("05-advanced")
        ui.click("advancedButton")
        assert s.selected == selected and s.pad.strokes == retained
        assert ui.get("OriginalChoice").property("picked")
        ui.click("ModifiedChoice")

        ui.click("personalButton")
        ui.click("pin_opacity")
        authored = s.brush["opacity"]
        ui.field("my_opacityValue", 18)
        assert s.working["opacity"] == 18 and s.brush["opacity"] == authored and s.pad.strokes == retained
        ui.click("closeSheet")
        ui.click("keepCopy")
        saved = next(iter(s.saved.values()))
        assert saved["opacity"] == authored and saved is not s._draft

        ui.click("wholeBrushRow")
        ui.click("addEntryButton")
        ui.click("groupAction")
        ui.click("newGroup")
        outer = s.selected
        ui.click("addEntryButton")
        ui.click("groupAction")
        ui.click("newGroup")
        assert s.get(s.selected)[3]["uid"] == outer
        for key in ("pen", "airbrush", "dust"):
            ui.click("addEntryButton")
            ui.click("brushAction")
            ui.click(key + "Tile")
            ui.click("acceptAsset")
        added = [e for e in entries_in(s._draft["entries"]) if e["kind"] == "brush"]
        assert len(added) == 3 and s.entry["name"] == "Dust accents"
        ui.field("part_contributionValue", 46)
        assert s.entry["contribution"] == 46 and s.catalog["dust"].get("contribution") is None
        s.act(outer, "reuse")
        s.act(outer, "expand")
        assert len(s.rows) < len(list(entries_in(s._draft["entries"])))
        s.act(outer, "expand")
        ui.click("wholeBrushRow")
        ui.click("addEntryButton")
        ui.click("groupAction")
        ui.click(s.savedGroups[0]["key"] + "Tile")
        ui.click("acceptAsset")
        assert len([e for e in entries_in(s._draft["entries"]) if e["kind"] == "brush"]) == 6

        ui.click("wholeBrushRow")
        ui.click("advancedButton")
        ui.click("addEntryButton")
        ui.click("maskAction")
        ui.click("leafTile")
        ui.click("acceptAsset")
        assert s.entry["kind"] == "mask"
        ui.click("advancedButton")

        current = s.selected
        w.resize(1024, 768)
        QTest.qWait(90)
        ui.click("MarksChoice", touch=True)
        assert ui.get("addMarkButton").isVisible()
        ui.click("SettingsChoice", touch=True)
        assert s.selected == current and s.pad.strokes == retained
        w.resize(768, 960)
        QTest.qWait(90)
        ui.scene("06-tablet-portrait")
        w.resize(430, 540)
        QTest.qWait(90)
        ui.click("MarksChoice", touch=True)
        ui.click("addMarkButton", touch=True)
        assert ui.get("acceptAsset").isVisible()
        ui.close_popup()
        ui.scene("07-small-window-tools")
        ui.click("PadChoice", touch=True)
        ui.point("padSurface")
        ui.scene("07-small-window-pad")

        w.resize(1440, 900)
        QTest.qWait(130)
        ui.click("backToStarters")
        ui.click("tipStarter")
        assert s.route == "tip" and not ui.get("useTip").isEnabled()
        ui.stroke("tipSurface", [(.31, .58), (.49, .24), (.69, .64), (.29, .44), (.68, .40), (.31, .58)])
        assert s.tipCount == 1
        ui.field("tipName", "Star on paper", commit=False)
        ui.scene("08-draw-tip")
        ui.click("useTip")
        assert s.route == "editor" and s.entry["name"] == "Star on paper"
        assert s.entry["asset"].startswith("drawn") and s.assets.has(s.entry["asset"])
        ui.draw_pad()
        assert s.strokeCount == 1

        with TemporaryDirectory() as directory:
            file = Path(directory) / "Ink test.png"
            im = Image.new("RGBA", (60, 60), (255, 255, 255, 0))
            ImageDraw.Draw(im).ellipse((7, 18, 29, 44), fill="black")
            im.save(file)
            s.importFile(QUrl.fromLocalFile(str(file)), "new")
            assert s.route == "editor" and s.entry["name"] == "Ink test"
            assert s.assets.has(s.entry["asset"]) and s.strokeCount == 0
            s.importFile(QUrl.fromLocalFile(str(file)), "texture")
            assert s.textureCount == 1 and s.entry["kind"] == "texture"
        ui.scene("09-imported")

        ui.click("backToStarters")
        ui.click("duplicateStarter")
        ui.click("pencilTile")
        ui.click("acceptAsset")
        assert s.brush["name"] == "Graphite Pencil copy"
        assert s.brush["opacity"] == s.catalog["pencil"]["opacity"]
        ui.click("backToStarters")
        ui.click("scratchStarter")
        assert s.markCount == 1
        blank = ui.pixels("padSurface")
        ui.draw_pad()
        assert s.strokeCount == 1 and blank != ui.pixels("padSurface")
        assert not warnings, warnings
        if sys.stdout is not None:
            print("PASS: Qt starters, pencil, independent histories, texture, multiple marks, curves, pins, nested groups, mask, tablet, drawn tip and import.")
    finally:
        if engine is not None:
            delete(engine)
        qInstallMessageHandler(old)


def verify_package(create_app, output):
    output.mkdir(parents=True, exist_ok=True)
    evidence = {"passed": False}
    try:
        run(create_app, output / "captures", evidence)
        review = output / "screenshots"
        review.mkdir(exist_ok=True)
        for capture, filename in STATES.items():
            (output / "captures" / (capture + ".png")).replace(review / filename)
        evidence.update(passed=True, reviewStates=list(STATES.values()),
                        checked=["starters", "pencil", "independent pad and draft history", "textures",
                                 "multi-mark selection", "advanced curves and comparisons", "personal controls",
                                 "nested groups and masks", "responsive and touch controls", "drawn tip", "image import"])
    except Exception:
        evidence["error"] = traceback.format_exc()
    (output / "qt-walkthrough.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    return 0 if evidence["passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", type=Path, help="optional folder for screenshots of the actual Qt states")
    args = parser.parse_args()
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    os.environ.setdefault("QT_QUICK_BACKEND", "software")
    from main import create_app
    run(create_app, args.captures)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
