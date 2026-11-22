"""Memory-only drafts for design review. No KomaPaint formats or engine imports."""
from copy import deepcopy
from itertools import count
from pathlib import Path
from random import Random
from time import monotonic

from PySide6.QtCore import Property, QObject, QUrl, Signal, Slot
from PySide6.QtQml import QJSValue


MARKS = [
    ("graphite", "Graphite point", "Drawing"), ("ink", "Round nib", "Drawing"),
    ("bristle", "Dry bristles", "Drawing"), ("air", "Soft round", "Drawing"),
    ("round", "Solid round", "Drawing"),
    ("billow", "Billow", "Organic"), ("wisp", "Wind wisp", "Organic"),
    ("cumulus", "Cumulus edge", "Organic"), ("veil", "Mist veil", "Organic"),
    ("leaf", "Willow leaf", "Organic"), ("stone", "Shale chip", "Organic"),
    ("star", "Four-point star", "Effects"), ("spatter", "Speckle", "Effects"),
    ("hair", "Fine strands", "Effects"),
]
TEXTURES = [
    ("paper", "Warm paper", "Fine tooth"), ("cotton", "Cold press", "Coarse fibers"),
    ("linen", "Linen weave", "Directional"), ("mist", "Soft mist", "Soft breakup"),
    ("charcoal", "Charcoal tooth", "Dry and rough"), ("stipple", "Flecks", "Grainy"),
]
NAMES = {asset: label for asset, label, _ in MARKS + TEXTURES}
PRESETS = [
    ("pencil", "Pencil", "Soft grain for sketching", 16, "#44392f"),
    ("pen", "Pen", "Clean, expressive lines", 12, "#211f1c"),
    ("paint", "Paintbrush", "Textured, loaded strokes", 35, "#a74432"),
    ("airbrush", "Airbrush", "Soft edges and shading", 64, "#566e82"),
    ("cloud", "Cloud study", "Distinct marks, softened together", 72, "#f9f0e2"),
    ("scratch", "Scratch brush", "A simple round mark", 18, "#44392f"),
    ("dust", "Dust accents", "A fine scatter of flecks", 24, "#927451"),
]
PRESET_INFO = {item[0]: item for item in PRESETS}
MIX = ["Normal", "Multiply", "Screen", "Soft Light", "Subtract"]
SETTINGS = {"size": (10, 250), "opacity": (0, 100), "spacing": (4, 140), "scatter": (0, 160),
            "angle": (-180, 180), "flow": (0, 100), "sizeJitter": (0, 100),
            "opacityJitter": (0, 100), "angleJitter": (0, 180), "weight": (1, 12),
            "offset": (-100, 100), "width": (10, 200), "strength": (0, 100),
            "scale": (15, 300), "contribution": (0, 100), "wetness": (0, 100),
            "edge": (0, 100), "buildup": (0, 100), "colorDrift": (0, 100),
            "pressureSize": (0, 100), "pressureOpacity": (0, 100), "pressureFlow": (0, 100),
            "tiltAmount": (0, 100), "speedAmount": (0, 100), "taper": (0, 100)}
PERSONAL = [
    ("size", "Brush size", 10, 250, "%"), ("opacity", "Opacity", 0, 100, "%"),
    ("flow", "Flow", 0, 100, "%"), ("scatter", "Scatter", 0, 160, "%"),
    ("pressureSize", "Pressure → size", 0, 100, "%"),
]
SERIAL = count(1)


def make_entry(kind, asset="", name="", **kw):
    serial = next(SERIAL)
    entry = dict(uid=str(serial), seedOffset=serial, kind=kind, asset=asset, name=name or NAMES.get(asset, "Mark"),
                 visible=True, expanded=True, size=100, opacity=100, spacing=100, scatter=0, angle=0,
                 weight=3, flipX=False, flipY=False, sizeJitter=0, opacityJitter=0, angleJitter=0,
                 follow=False, offset=0, aspect=100, blend="Normal", contribution=100,
                 strength=35, scale=100, anchor="Canvas", textureMode="Whole stroke", invert=False,
                 order="In Order", children=[])
    entry.update(kw)
    return entry


def preset(key):
    data = dict(name=PRESET_INFO[key][1], preset=key, entries=[], size=100, opacity=92, flow=80,
                spacing=14, scatter=0, order="In Order", width=100, offset=0, sizeJitter=0,
                opacityJitter=0, angleJitter=0, follow=False, tiltAmount=12, speedAmount=0,
                pressureSize=60, pressureOpacity=32, pressureFlow=14, curves={}, wetness=0,
                edge=18, buildup=24, colorDrift=0, taper=0, blend="Normal")
    tip = {"pencil": "graphite", "pen": "ink", "paint": "bristle", "airbrush": "air",
           "scratch": "round", "dust": "spatter", "cloud": "billow"}[key]
    data["entries"].append(make_entry("mark", tip))
    if key == "pencil":
        data.update(name="Graphite Pencil", opacity=73, spacing=13, pressureSize=72,
                    pressureOpacity=43, tiltAmount=33, flow=67)
        data["entries"].append(make_entry("texture", "paper", strength=41))
    elif key == "pen":
        data.update(name="Inking Pen", opacity=100, flow=100, spacing=8, pressureSize=87,
                    pressureOpacity=0, pressureFlow=0, tiltAmount=0, edge=0)
    elif key == "paint":
        data.update(name="Dry Paintbrush", spacing=21, wetness=19, follow=True, flow=72,
                    pressureSize=52, pressureOpacity=25)
        data["entries"].append(make_entry("texture", "linen", strength=28))
    elif key == "airbrush":
        data.update(name="Soft Airbrush", opacity=57, spacing=15, pressureSize=31,
                    pressureOpacity=69, flow=59, edge=78)
    elif key == "dust":
        data.update(spacing=44, scatter=62, opacity=43, edge=0, pressureSize=35)
    elif key == "cloud":
        data.update(name="Cloud study", spacing=37, scatter=26, order="Random", opacity=88, flow=68,
                    pressureSize=32, pressureOpacity=39, edge=36, sizeJitter=18, buildup=22)
        data["entries"][0].update(weight=7, size=122, angleJitter=9)
        data["entries"] += [make_entry("mark", "wisp", weight=3, size=104, angle=-18, opacity=77),
                            make_entry("mark", "cumulus", weight=5, size=90, angle=11),
                            make_entry("texture", "mist", strength=25, scale=150, anchor="Stroke")]
    return data


def clone_entry(entry):
    output = deepcopy(entry)
    output["uid"] = str(next(SERIAL))
    output["children"] = [clone_entry(child) for child in entry["children"]]
    return output


def clone_draft(draft):
    return {**deepcopy(draft), "entries": [clone_entry(item) for item in draft["entries"]]}


def entries_in(items):
    for entry in items:
        yield entry
        yield from entries_in(entry["children"])


def q_value(value):
    return value.toVariant() if isinstance(value, QJSValue) else value


class Pad:
    def __init__(self):
        self.strokes = ()
        self.undo = []
        self.redo = []
        self.active = None
        self.serial = count(111)

    def begin(self, x, y, w, h, size, color):
        self.active = dict(seed=next(self.serial), points=[], size=size, color=color, start=monotonic())
        self.point(x, y, w, h)

    def point(self, x, y, w, h):
        if not self.active or w <= 0 or h <= 0:
            return
        points = self.active["points"]
        pt = (min(1, max(0, x / w)), min(1, max(0, y / h)), .72,
              monotonic() - self.active["start"], .28)
        if not points or (abs(pt[0] - points[-1][0]) + abs(pt[1] - points[-1][1])) > .0003:
            points.append(pt)

    def _set(self, strokes):
        self.undo.append(self.strokes)
        self.undo = self.undo[-100:]
        self.strokes = strokes
        self.redo.clear()

    def end(self):
        if self.active and self.active["points"]:
            self._set((*self.strokes, self.active))
        self.active = None

    def clear(self):
        self.active = None
        if self.strokes:
            self._set(())

    def step(self, action):
        if action == "Undo" and self.undo:
            self.redo.append(self.strokes)
            self.strokes = self.undo.pop()
        elif action == "Redo" and self.redo:
            self.undo.append(self.strokes)
            self.strokes = self.redo.pop()

    def all_strokes(self):
        return (*self.strokes, self.active) if self.active else self.strokes


class Studio(QObject):
    draftChanged = Signal()
    draftHistoryChanged = Signal()
    rowsChanged = Signal()
    selectionChanged = Signal()
    routeChanged = Signal()
    padChanged = Signal()
    padOptionsChanged = Signal()
    tipChanged = Signal()
    libraryChanged = Signal()
    personalChanged = Signal()
    noteChanged = Signal()
    uploadRequested = Signal(str)
    uploadFinished = Signal(bool)

    def __init__(self, assets, remote=False):
        super().__init__()
        self.assets = assets
        self.remote = remote
        self.catalog = {key: preset(key) for key in PRESET_INFO}
        self.saved = {}
        self.reusable = {}
        self.user_marks = []
        self.user_textures = []
        self._draft = clone_draft(self.catalog["scratch"])
        self.original = deepcopy(self._draft)
        self._selected = self._draft["entries"][0]["uid"]
        self._undo_draft = []
        self._redo_draft = []
        self._last_edit = None, 0.
        self._route = "home"
        self._has_draft = False
        self._tip_return = "home"
        self._note = ""
        self.pending_image = None
        self.pinned = []
        self.overrides = {}
        self.pad = Pad()
        self.tip = Pad()
        self._pad_size = 18
        self._pad_color = PRESET_INFO["scratch"][4]
        self._study = "Pressure study"
        self._tip_width = 26
        self._epochs = {"pad": 0, "tip": 0, "brush": 0, "personal": 0}

    def get(self, uid, items=None, parent=None, depth=0):
        if items is None:
            items = self._draft["entries"]
        for i, entry in enumerate(items):
            if entry["uid"] == uid:
                return entry, items, i, parent, depth
            found = self.get(uid, entry["children"], entry, depth + 1)
            if found:
                return found
        return None

    def refresh_brush(self, rows=False):
        self._epochs["brush"] += 1
        self.draftChanged.emit()
        if rows:
            self.rowsChanged.emit()

    def _record(self, label=None):
        now = monotonic()
        prior, when = self._last_edit
        if label is None or label != prior or now - when > .5:
            self._undo_draft.append((deepcopy(self._draft), self._selected))
            self._undo_draft = self._undo_draft[-80:]
        self._redo_draft.clear()
        self._last_edit = label, now
        self.draftHistoryChanged.emit()

    @Property(int, notify=draftHistoryChanged)
    def draftEdits(self):
        return len(self._undo_draft)

    @Property(bool, notify=draftHistoryChanged)
    def draftRedo(self):
        return bool(self._redo_draft)

    @Slot(str)
    def stepDraft(self, action):
        previous, future = (self._undo_draft, self._redo_draft) if action == "Undo" else (self._redo_draft, self._undo_draft)
        if previous:
            future.append((self._draft, self._selected))
            self._draft, self._selected = previous.pop()
            self._last_edit = None, 0.
            self.refresh_brush(True)
            self.selectionChanged.emit()
            self.draftHistoryChanged.emit()

    @Property(str, notify=routeChanged)
    def route(self):
        return self._route

    @Property(bool, notify=routeChanged)
    def hasDraft(self):
        return self._has_draft

    @Property(bool, constant=True)
    def remoteReview(self):
        return self.remote

    @Property(str, notify=noteChanged)
    def note(self):
        return self._note

    def message(self, message):
        self._note = message
        self.noteChanged.emit()

    @Property("QVariantMap", notify=draftChanged)
    def brush(self):
        return {k: v for k, v in self._draft.items() if k != "entries"}

    @Property(str, notify=selectionChanged)
    def selected(self):
        return self._selected

    @Property("QVariantMap", notify=draftChanged)
    def entry(self):
        row = self.get(self._selected)
        return {**{k: v for k, v in row[0].items() if k != "children"},
                "childCount": len(row[0]["children"]), "parent": row[3]["name"] if row[3] else "",
                "parentOrder": (row[3] or self._draft)["order"]} if row else {}

    @Property("QVariantList", notify=rowsChanged)
    def rows(self):
        def flat(items, depth=0, hidden=False):
            for i, e in enumerate(items):
                yield {k: e[k] for k in ("uid", "kind", "asset", "name", "visible", "expanded")} | {
                    "depth": depth, "fromAbove": hidden, "childrenCount": len(e["children"]),
                    "canUp": i > 0, "canDown": i + 1 < len(items)}
                if e["expanded"]:
                    yield from flat(e["children"], depth + 1, hidden or not e["visible"])
        return list(flat(self._draft["entries"]))

    @Property(int, notify=rowsChanged)
    def markCount(self):
        return sum(row["kind"] == "mark" for row in entries_in(self._draft["entries"]))

    @Property(int, notify=rowsChanged)
    def textureCount(self):
        return sum(row["kind"] == "texture" for row in entries_in(self._draft["entries"]))

    @Property("QVariantList", notify=rowsChanged)
    def groups(self):
        return [dict(uid=e["uid"], name=e["name"], kind=e["kind"]) for e in entries_in(self._draft["entries"])
                if e["kind"] in ("group", "brush")]

    @Property("QVariantList", notify=libraryChanged)
    def marks(self):
        return [dict(key=key, title=label, sub=category) for key, label, category in MARKS + self.user_marks]

    @Property("QVariantList", notify=libraryChanged)
    def textures(self):
        return [dict(key=key, title=label, sub=category) for key, label, category in TEXTURES + self.user_textures]

    @Property("QVariantList", notify=libraryChanged)
    def brushes(self):
        return [dict(key=item[0], title=item[1], sub=item[2]) for item in PRESETS if item[0] != "scratch"] + [
            dict(key=key, title=item["name"], sub="A copy from this session") for key, item in self.saved.items()]

    @Property("QVariantList", notify=libraryChanged)
    def savedGroups(self):
        return [dict(key=key, title=item["name"], sub=f'{len(item["children"])} entries')
                for key, item in self.reusable.items()]

    @Slot(str)
    def navigate(self, route):
        if route not in ("home", "editor", "tip"):
            return
        self._route = route
        self.routeChanged.emit()

    def _begin(self, data):
        self._draft = clone_draft(data)
        self.original = deepcopy(self._draft)
        self._has_draft = True
        self._selected = self._draft["entries"][0]["uid"] if self._draft["entries"] else ""
        self.pad = Pad()
        self._undo_draft.clear()
        self._redo_draft.clear()
        self._last_edit = None, 0.
        self.draftHistoryChanged.emit()
        self.overrides = {}
        self._pad_size, self._pad_color = PRESET_INFO.get(self._draft["preset"], PRESET_INFO["scratch"])[3:]
        self._study = "Sky study" if self._draft["preset"] == "cloud" else "Pressure study"
        self.refresh_brush(True)
        self.selectionChanged.emit()
        self.padChanged.emit()
        self.padOptionsChanged.emit()
        self.personalChanged.emit()
        self.navigate("editor")

    @Slot(str, bool)
    def start(self, key, duplicate=False):
        data = self.lookup(key)
        if data:
            if duplicate:
                data = deepcopy(data)
                data["name"] += " copy"
            self._begin(data)

    def lookup(self, key):
        return self.saved.get(key) or self.catalog.get(key)

    @Slot()
    def saveCopy(self):
        self.saved[f"saved{next(SERIAL)}"] = deepcopy(self._draft)
        self.message(f'Kept “{self._draft["name"]}” in this session’s brush picker.')
        self.libraryChanged.emit()

    @Slot(str)
    def choose(self, uid):
        if not uid or self.get(uid):
            self._selected = uid
            self.selectionChanged.emit()
            self.draftChanged.emit()

    @Slot(str, "QVariant")
    def edit(self, key, value):
        self._edit(self._draft, key, q_value(value))

    @Slot(str, "QVariant")
    def editEntry(self, key, value):
        path = self.get(self._selected)
        if path:
            self._edit(path[0], key, q_value(value))

    def _edit(self, target, key, value):
        if key in SETTINGS:
            lo, hi = SETTINGS[key]
            value = max(lo, min(hi, int(round(float(value)))))
        elif key == "name":
            value = str(value).strip()[:72] or target["name"]
        elif key not in ("order", "anchor", "textureMode", "blend", "flipX", "flipY", "follow", "invert"):
            return
        if target.get(key) == value:
            return
        self._record((target.get("uid", "whole"), key))
        target[key] = value
        self.refresh_brush(key == "name" and target is not self._draft)

    @Slot(str, "QVariantList", bool)
    def editCurve(self, key, levels, selected_brush):
        row = self.get(self._selected)
        target = row[0] if selected_brush and row and row[0]["kind"] == "brush" else self._draft
        curves = target["curves"]
        if len(levels) == 5:
            points = [min(1., max(0., float(x))) for x in levels]
            if curves.get(key) != points:
                self._record((target.get("uid", "whole"), key))
                curves[key] = points
                self.refresh_brush()
        elif not levels and key in curves:
            self._record()
            del curves[key]
            self.refresh_brush()

    def sequence(self, items, how):
        marks = [e for e in items if e["kind"] == "mark" and e["visible"]]
        if not marks:
            return []
        rng = Random(38)
        if how == "Weighted":
            picks = [rng.choices(marks, weights=[e["weight"] for e in marks], k=1)[0] for _ in range(8)]
        elif how == "Random":
            picks = [rng.choice(marks) for _ in range(8)]
        else:
            picks = [marks[i % len(marks)] for i in range(8)]
        return [dict(asset=e["asset"], name=e["name"], number=marks.index(e) + 1) for e in picks]

    @Property("QVariantList", notify=draftChanged)
    def picks(self):
        return self.sequence(self._draft["entries"], self._draft["order"])

    @Property("QVariantList", notify=draftChanged)
    def entryPicks(self):
        found = self.get(self._selected)
        return self.sequence(found[0]["children"], found[0]["order"]) if found else []

    def _add(self, item):
        self._record()
        path = self.get(self._selected)
        if path and path[0]["kind"] in ("group", "brush"):
            path[0]["children"].append(item)
            path[0]["expanded"] = True
        elif path and path[3]:
            path[1].append(item)
        else:
            self._draft["entries"].append(item)
        self._selected = item["uid"]
        self.refresh_brush(True)
        self.selectionChanged.emit()

    @Slot(str, str)
    def addAsset(self, kind, asset):
        if kind in ("mark", "texture", "mask") and self.assets.has(asset):
            self._add(make_entry(kind, asset, name=self.asset_name(asset)))

    def asset_name(self, asset):
        return NAMES.get(asset) or next((x[1] for x in self.user_marks + self.user_textures if x[0] == asset), "Drawn mark")

    @Slot(str)
    def replaceAsset(self, asset):
        path = self.get(self._selected)
        if path and self.assets.has(asset) and path[0]["asset"] != asset:
            self._record()
            path[0].update(asset=asset, name=self.asset_name(asset))
            self.refresh_brush(True)

    @Slot(str)
    def addBrush(self, key):
        data = self.lookup(key)
        if data:
            params = {k: deepcopy(v) for k, v in data.items() if k not in ("entries", "name", "preset", "spacing")}
            self._add(make_entry("brush", key, name=data["name"], children=clone_draft(data)["entries"],
                                 spacing=data["spacing"], contribution=75, **params))

    @Slot(str)
    def addGroup(self, key=""):
        group = self.reusable.get(key)
        self._add(clone_entry(group) if group else make_entry("group", name="Group of Marks", spacing=self._draft["spacing"]))

    @Slot(str, str)
    def act(self, uid, action):
        path = self.get(uid)
        if not path:
            return
        entry, siblings, pos, parent, _ = path
        selected_removed = (self._selected == uid or any(e["uid"] == self._selected for e in entries_in(entry["children"])))
        if action == "up" and pos:
            self._record()
            siblings[pos - 1], siblings[pos] = siblings[pos], siblings[pos - 1]
        elif action == "down" and pos + 1 < len(siblings):
            self._record()
            siblings[pos + 1], siblings[pos] = siblings[pos], siblings[pos + 1]
        elif action == "duplicate":
            self._record()
            copy = clone_entry(entry)
            copy["name"] += " copy"
            siblings.insert(pos + 1, copy)
            self._selected = copy["uid"]
        elif action == "hide":
            self._record()
            entry["visible"] = not entry["visible"]
        elif action == "remove":
            self._record()
            siblings.pop(pos)
            if selected_removed:
                self._selected = (siblings[min(pos, len(siblings) - 1)]["uid"] if siblings else
                                  parent["uid"] if parent else "")
        elif action == "expand" and entry["kind"] in ("group", "brush"):
            entry["expanded"] = not entry["expanded"]
            if not entry["expanded"] and selected_removed:
                self._selected = uid
            self.rowsChanged.emit()
            self.selectionChanged.emit()
            self.draftChanged.emit()
            return
        elif action == "out" and parent:
            container = self.get(parent["uid"])
            self._record()
            siblings.pop(pos)
            container[1].insert(container[2] + 1, entry)
        elif action == "group":
            self._record()
            group = make_entry("group", name="Group of Marks", children=[entry], spacing=self._draft["spacing"])
            siblings[pos] = group
            self._selected = group["uid"]
        elif action == "reuse" and entry["kind"] in ("group", "brush"):
            self.reusable[f"group{next(SERIAL)}"] = deepcopy(entry)
            self.libraryChanged.emit()
            self.message(f'Kept “{entry["name"]}” for reuse this session.')
            return
        else:
            return
        self.refresh_brush(True)
        self.selectionChanged.emit()

    @Slot(str, str, bool)
    def drop(self, source, target, into):
        old = self.get(source)
        new = self.get(target)
        if not old or not new or source == target or any(e["uid"] == target for e in entries_in(old[0]["children"])):
            return
        if into and new[0]["kind"] not in ("group", "brush"):
            return
        self._record()
        moved = old[1].pop(old[2])
        if into:
            new[0]["children"].append(moved)
            new[0]["expanded"] = True
        else:
            index = new[1].index(new[0])
            new[1].insert(index + (1 if new[1] is old[1] and old[2] <= index else 0), moved)
        self._selected = moved["uid"]
        self.refresh_brush(True)
        self.selectionChanged.emit()

    @Property(int, notify=padOptionsChanged)
    def padSize(self):
        return self._pad_size

    @Property(str, notify=padOptionsChanged)
    def padColor(self):
        return self._pad_color

    @Property(str, notify=padOptionsChanged)
    def study(self):
        return self._study

    @Slot(int)
    def testSize(self, size):
        self._pad_size = min(160, max(1, size))
        self.padOptionsChanged.emit()

    @Slot(str)
    def testColor(self, color):
        self._pad_color = color
        self.padOptionsChanged.emit()

    @Slot(str)
    def setStudy(self, study):
        self._study = study
        self.padOptionsChanged.emit()

    @Property(int, notify=padChanged)
    def strokeCount(self):
        return len(self.pad.strokes)

    @Property(bool, notify=padChanged)
    def canUndo(self):
        return bool(self.pad.undo)

    @Property(bool, notify=padChanged)
    def canRedo(self):
        return bool(self.pad.redo)

    @Property(int, notify=tipChanged)
    def tipCount(self):
        return len(self.tip.strokes)

    @Property(bool, notify=tipChanged)
    def tipHasInk(self):
        return bool(self.tip.all_strokes())

    @Property(bool, notify=tipChanged)
    def tipUndo(self):
        return bool(self.tip.undo)

    @Property(bool, notify=tipChanged)
    def tipRedo(self):
        return bool(self.tip.redo)

    @Property(int, notify=tipChanged)
    def tipWidth(self):
        return self._tip_width

    @Slot(int)
    def setTipWidth(self, size):
        self._tip_width = size
        self.tipChanged.emit()

    @Slot(bool)
    def beginTip(self, new_draft):
        self.tip = Pad()
        self._tip_width = 26
        self._tip_return = "home" if new_draft else "editor"
        self.tipChanged.emit()
        self.navigate("tip")

    @Slot()
    def cancelTip(self):
        self.navigate(self._tip_return)

    @Slot(str)
    def acceptTip(self, name):
        if not self.tip.strokes:
            return
        name = name.strip()[:72] or "Hand-drawn mark"
        key = f"drawn{next(SERIAL)}"
        self.assets.add_drawn(key, self.tip.strokes)
        self.user_marks.append((key, name, "My marks"))
        self.libraryChanged.emit()
        if self._tip_return == "home":
            self._with_mark(key, name)
        else:
            self._add(make_entry("mark", key, name=name))
            self.navigate("editor")
        self.message(f'Added “{name}” to Brush Marks.')

    def _with_mark(self, key, name):
        data = preset("scratch")
        data.update(name=name, entries=[make_entry("mark", key, name=name)], spacing=56)
        self._begin(data)

    def _pad_for(self, which):
        return self.tip if which == "tip" else self.pad

    def _pad_update(self, which):
        key = "tip" if which == "tip" else "pad"
        self._epochs[key] += 1
        (self.tipChanged if which == "tip" else self.padChanged).emit()

    @Slot(str, float, float, float, float)
    def beginStroke(self, which, x, y, w, h):
        self._pad_for(which).begin(x, y, w, h, self._tip_width if which == "tip" else self._pad_size,
                                  "#292523" if which == "tip" else self._pad_color)
        self._pad_update(which)

    @Slot(str, float, float, float, float)
    def appendPoint(self, which, x, y, w, h):
        self._pad_for(which).point(x, y, w, h)
        self._pad_update(which)

    @Slot(str)
    def endStroke(self, which):
        self._pad_for(which).end()
        self._pad_update(which)

    @Slot(str, str)
    def padAction(self, which, action):
        p = self._pad_for(which)
        p.clear() if action == "Clear" else p.step(action)
        self._pad_update(which)

    @Property("QVariantList", notify=personalChanged)
    def personal(self):
        return [dict(key=key, title=name, low=lo, high=hi, suffix=suffix, picked=key in self.pinned)
                for key, name, lo, hi, suffix in PERSONAL]

    @Property("QVariantMap", notify=personalChanged)
    def working(self):
        return dict(self.overrides)

    @Slot(str)
    def pin(self, key):
        if key in self.pinned:
            self.pinned.remove(key)
            self.overrides.pop(key, None)
        elif any(row[0] == key for row in PERSONAL):
            self.pinned.append(key)
        self._epochs["personal"] += 1
        self.personalChanged.emit()

    @Slot(str, int)
    def editWorking(self, key, value):
        if key in self.pinned:
            lo, hi = SETTINGS[key]
            self.overrides[key] = max(lo, min(hi, value))
            self._epochs["personal"] += 1
            self.personalChanged.emit()

    @Slot()
    def resetWorking(self):
        self.overrides.clear()
        self._epochs["personal"] += 1
        self.personalChanged.emit()

    @Slot()
    def cancelUpload(self):
        self.pending_image = None

    @Slot(str)
    def requestUpload(self, mode):
        self.pending_image = mode
        self.uploadRequested.emit(mode)

    @Slot(QUrl, str)
    def importFile(self, url, mode):
        try:
            path = Path(url.toLocalFile())
            if path.stat().st_size > 12 * 1024 * 1024:
                raise ValueError("Choose an image under 12 MB.")
            self.import_image(path.read_bytes(), path.name, mode)
        except (ValueError, OSError) as exc:
            self.message(f"Could not open image: {exc}")

    def import_image(self, content, filename, mode):
        try:
            key = f"import{next(SERIAL)}"
            name = Path(filename).stem[:64].strip() or "Imported image"
            is_texture = mode in ("texture", "replaceTexture")
            self.assets.add_import(key, content, is_texture)
            if is_texture:
                self.user_textures.append((key, name, "Imported"))
            else:
                self.user_marks.append((key, name, "Imported"))
            self.libraryChanged.emit()
            if mode == "new":
                self._with_mark(key, name)
            elif mode in ("replaceMark", "replaceTexture"):
                self.replaceAsset(key)
            else:
                self._add(make_entry("texture" if is_texture else "mark", key, name=name))
            self.message(f'Imported “{name}” as a {"texture" if is_texture else "brush tip"}.')
            self.uploadFinished.emit(True)
        except (ValueError, OSError) as exc:
            self.message(f"Could not open image: {exc}")
            self.uploadFinished.emit(False)
        finally:
            self.pending_image = None
