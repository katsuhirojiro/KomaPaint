"""In-memory state for the third design review.

Three ownership layers are separate objects, not three visual modes over one
dictionary:

* ``settings`` / ``root`` / ``dynamics`` are the creator's definition.
* ``overrides`` and ``pins`` are the artist's personal layer.
* ``assist`` is application/device state and is never written back.

The painter is intentionally a preview compositor.  The review labels which
controls alter that preview and which demonstrate organisation only; it does
not repeat the production problem of claiming stored values are rendered.
"""
from copy import deepcopy
from pathlib import Path
from time import monotonic

import catalog
import recipes
import render
from assets import MARKS, SURFACES, Assets
from PySide6.QtCore import Property, QObject, Signal, Slot


def fmt_value(meta, value):
    if meta["kind"] == "toggle":
        return "On" if value else "Off"
    if meta["kind"] != "slider":
        return str(value)
    unit = meta["unit"]
    if isinstance(value, bool):
        value = int(value)
    if meta.get("step", 1) < 1:
        text = f"{float(value):.1f}"
    elif abs(float(value) - round(float(value))) < 1e-9:
        text = f"{int(round(float(value)))}"
    else:
        text = f"{float(value):.1f}"
    if unit == "°":
        return text + "°"
    if unit == "×":
        return text + "×"
    if unit == "px":
        return text + " px"
    if unit == "%":
        return text + "%"
    return text


def stable_hash(text):
    import hashlib
    return int.from_bytes(hashlib.sha256(str(text).encode()).digest()[:4], "little")


NODE_META = {
    "node.opacity": dict(key="node.opacity", label="Opacity", kind="slider", minimum=0,
                         maximum=100, unit="%", step=1, default=100,
                         desc="This part's contribution inside its parent."),
    "node.scale": dict(key="node.scale", label="Scale", kind="slider", minimum=5,
                       maximum=400, unit="%", step=1, default=100,
                       desc="Size relative to the brush tip."),
    "node.angle": dict(key="node.angle", label="Angle", kind="slider", minimum=-180,
                       maximum=180, unit="°", step=1, default=0,
                       desc="Fixed rotation for this part."),
    "node.aspect": dict(key="node.aspect", label="Aspect", kind="slider", minimum=25,
                        maximum=400, unit="%", step=1, default=100,
                        desc="Width against height for this part."),
    "node.weight": dict(key="node.weight", label="Weight", kind="slider", minimum=1,
                        maximum=20, unit="", step=1, default=4,
                        desc="Relative probability or contribution in a weighted group."),
    "node.members_per_stamp": dict(key="node.members_per_stamp", label="Parts per stamp",
                                   kind="slider", minimum=1, maximum=12, unit="×", step=1,
                                   default=1, desc="How many children this group contributes."),
    "node.amount": dict(key="node.amount", label="Contribution", kind="slider", minimum=0,
                        maximum=100, unit="%", step=1, default=100,
                        desc="How much this secondary brush adds to the parent."),
    "node.flipH": dict(key="node.flipH", label="Flip horizontally", kind="toggle",
                       minimum=0, maximum=1, unit="", default=0,
                       desc="Mirror this part."),
    "node.flipV": dict(key="node.flipV", label="Flip vertically", kind="toggle",
                       minimum=0, maximum=1, unit="", default=0,
                       desc="Mirror this part."),
    "node.mode": dict(key="node.mode", label="Selection", kind="choice", minimum=0,
                      maximum=0, unit="", choices=recipes.MODES, default="Paint together",
                      desc="How this group chooses among its children."),
    "node.blend": dict(key="node.blend", label="Blend", kind="choice", minimum=0,
                       maximum=0, unit="", choices=recipes.BLENDS, default="Normal",
                       desc="How this part meets the paint beneath it."),
    "node.texture_strength": dict(key="node.texture_strength", label="Strength", kind="slider",
                                  minimum=0, maximum=100, unit="%", step=1, default=40,
                                  desc="How much grain this component removes."),
    "node.texture_scale": dict(key="node.texture_scale", label="Scale", kind="slider",
                               minimum=5, maximum=400, unit="%", step=1, default=100,
                               desc="Size of this component's grain."),
    "node.texture_rotation": dict(key="node.texture_rotation", label="Rotation", kind="slider",
                                  minimum=-180, maximum=180, unit="°", step=1, default=0,
                                  desc="Rotation of this component's grain."),
    "node.texture_depth": dict(key="node.texture_depth", label="Depth", kind="slider",
                               minimum=0, maximum=100, unit="%", step=1, default=70,
                               desc="Cut through or tint the mark."),
    "node.texture_contrast": dict(key="node.texture_contrast", label="Contrast", kind="slider",
                                  minimum=0, maximum=200, unit="%", step=1, default=100,
                                  desc="Contrast of this grain."),
    "node.texture_anchor": dict(key="node.texture_anchor", label="Anchor", kind="choice",
                                minimum=0, maximum=0, unit="", choices=recipes.TEX_ANCHORS,
                                default="Canvas", desc="Stay with the canvas or travel with the stroke."),
    "node.texture_per_stamp": dict(key="node.texture_per_stamp", label="Apply per stamp",
                                   kind="toggle", minimum=0, maximum=1, unit="", default=0,
                                   desc="Evaluate the grain for each mark."),
    "node.texture_tiling": dict(key="node.texture_tiling", label="Repeat pattern", kind="toggle",
                                minimum=0, maximum=1, unit="", default=1,
                                desc="Tile the source when the mark is larger."),
}


class Studio(QObject):
    changed = Signal()
    layoutChanged = Signal()
    treeChanged = Signal()
    selectionChanged = Signal()
    padChanged = Signal()
    tipChanged = Signal()
    noticeChanged = Signal()
    historyChanged = Signal()
    bindingChanged = Signal()
    deviceChanged = Signal()
    hintChanged = Signal()

    def __init__(self, assets: Assets):
        super().__init__()
        self.assets = assets
        self.docs = recipes.catalog_docs()
        self.key = "ferrous"
        self.doc = self.docs[self.key]
        self.layer = "personal"
        self._area = "stroke"
        self.selected_uid = self.doc["root"]["uid"]
        self.show_advanced = False
        self.version = 1
        self.pad_version = 1
        self._notice = "Creator definition intact. Personal changes are kept beside it."
        self._hint = "spacing"
        self._cancel = False
        self._undo, self._redo = [], []
        self._edit_depth = 0
        self._clipboard = None
        self._selected_binding = "d1"
        self._comparison = False
        self._retained = None
        self._last_stroke = None
        self._strokes = []
        self._pad_undo, self._pad_redo = [], []
        self._pad_active = None
        self._pad_before = None
        self._pad_last = (0.0, 0.0, 0.0)
        self._pad_pressure = 0.72
        self._test_mode = "stroke"
        self._tip_strokes, self._tip_undo = [], []
        self._tip_active = None
        self._tip_size = 14
        self._custom_tip = 0
        self._factory_key = 0
        self._reusable = []
        for ident, (label, builder, patch) in recipes.STARTERS.items():
            self._reusable.append(dict(id=f"r{ident}", name=label, key=ident))

    # ------------------------------------------------------------------ plumbing
    def note(self, text):
        self._notice = text
        self.noticeChanged.emit()

    def touch(self, layout=False):
        self.version += 1
        self.changed.emit()
        if layout:
            self.layoutChanged.emit()

    def snapshot(self):
        return dict(doc=deepcopy(self.doc), selected=self.selected_uid,
                    layer=self.layer, binding=self._selected_binding)

    def push_undo(self):
        self._undo.append(self.snapshot())
        if len(self._undo) > 80:
            self._undo.pop(0)
        self._redo.clear()
        self.historyChanged.emit()

    def restore(self, state):
        self.doc = state["doc"]
        self.selected_uid = state["selected"] if recipes.find(self.doc["root"], state["selected"]) else self.doc["root"]["uid"]
        self.layer = state["layer"]
        self._selected_binding = state["binding"]
        self.version += 1
        self.changed.emit()
        self.layoutChanged.emit()
        self.treeChanged.emit()
        self.selectionChanged.emit()
        self.bindingChanged.emit()
        self.historyChanged.emit()

    @Slot()
    def beginEdit(self):
        if self._edit_depth == 0:
            self.push_undo()
        self._edit_depth += 1

    @Slot()
    def endEdit(self):
        self._edit_depth = max(0, self._edit_depth - 1)
        if self._edit_depth == 0:
            self.layoutChanged.emit()

    @Property(bool, notify=historyChanged)
    def editCanUndo(self):
        return bool(self._undo)

    @Property(bool, notify=historyChanged)
    def editCanRedo(self):
        return bool(self._redo)

    @Slot()
    def undoEdit(self):
        if not self._undo:
            return
        self._redo.append(self.snapshot())
        self.restore(self._undo.pop())
        self.note("Brush edit undone. Test strokes are untouched.")

    @Slot()
    def redoEdit(self):
        if not self._redo:
            return
        self._undo.append(self.snapshot())
        self.restore(self._redo.pop())
        self.note("Brush edit restored. Test strokes are untouched.")

    @Property(str, notify=noticeChanged)
    def notice(self):
        return self._notice

    @Property(int, notify=changed)
    def revision(self):
        return self.version

    @Property(int, notify=padChanged)
    def padRevision(self):
        return self.pad_version

    @Property(bool, notify=changed)
    def modified(self):
        return bool(self.doc.get("overrides") or self.doc.get("pins"))

    # --------------------------------------------------------------------- areas
    @Property("QVariantList", notify=layoutChanged)
    def areas(self):
        result = []
        for key, label, description in catalog.AREAS:
            count = 0
            if key == "quick":
                count = len(self.doc.get("pins", []))
            elif key == "construction":
                count = sum(1 for _ in recipes.walk(self.doc["root"]))
            elif key == "assist":
                count = len([1 for item in catalog.SETTINGS.values() if item.get("area") == "assist"])
            else:
                count = len([1 for item in catalog.SETTINGS.values() if item.get("area") == key])
            result.append(dict(key=key, label=label, description=description, count=count))
        return result

    @Property(str, notify=layoutChanged)
    def area(self):
        return self._area

    @Property(str, notify=layoutChanged)
    def areaTitle(self):
        return next((label for key, label, _ in catalog.AREAS if key == self._area), "")

    @Property(str, notify=layoutChanged)
    def areaDescription(self):
        return next((desc for key, _, desc in catalog.AREAS if key == self._area), "")

    @Property(bool, notify=layoutChanged)
    def showAdvanced(self):
        return self.show_advanced

    @Slot(str)
    def setArea(self, key):
        if key == self._area:
            return
        if key == "secondary":
            node = self._selected_node()
            if node["kind"] != "brush" or node is self.doc["root"]:
                children = [n for n in self.doc["root"]["children"] if n["kind"] == "brush"]
                if children:
                    self.selected_uid = children[0]["uid"]
        self._area = key
        self._hint = self._first_hint(key)
        self.layoutChanged.emit()
        self.hintChanged.emit()

    @Slot()
    def toggleAdvanced(self):
        self.show_advanced = not self.show_advanced
        self.layoutChanged.emit()

    def _first_hint(self, area):
        for card in self._cards_for(area):
            if card["settings"]:
                return card["settings"][0]["key"]
        return "spacing"

    @Property(str, notify=layoutChanged)
    def currentLayer(self):
        return self.layer

    @Slot(str)
    def setLayer(self, layer):
        if layer not in ("creator", "personal"):
            return
        self.layer = layer
        self.note("Editing the creator definition. Personal overrides are preserved."
                  if layer == "creator" else
                  "Editing personal overrides. The creator definition stays intact.")
        self.layoutChanged.emit()

    @Property(str, notify=layoutChanged)
    def layerDescription(self):
        if self._area == "assist":
            return "Application & device · not saved into the brush"
        if self.layer == "creator":
            return "Creator definition · saved with the brush"
        return "Personal overrides & pins · never rewrite the original"

    # ------------------------------------------------------------------ settings
    def _base_value(self, key):
        if key.startswith("assist_"):
            return self.doc.get("assist", {}).get(key, catalog.SETTINGS[key]["default"])
        return self.doc["settings"].get(key, catalog.SETTINGS[key]["default"])

    def _effective_value(self, key):
        if key.startswith("assist_"):
            return self._base_value(key)
        return self.doc.get("overrides", {}).get(key, self._base_value(key))

    @Slot(str, result="QVariantMap")
    def parameterInfo(self, key):
        meta = catalog.SETTINGS.get(key) or NODE_META.get(key)
        return dict(meta or {})

    def _binding_summary(self, key):
        bindings = [b for b in self.doc["dynamics"] if b.get("target") == key]
        if not bindings:
            return "", ""
        b = bindings[0]
        device = self._device_key()
        available = self._signal_available(b["signal"])
        suffix = "" if available else f" · fallback {self._fallback()}"
        return b["id"], f"{b['signal']} · {round(b['amount'] * 100)}%{suffix}"

    def _setting(self, key, meta):
        if key.startswith("node."):
            node = self._selected_node()
            value = node.get(key.split(".", 1)[1], meta["default"]) if node else meta["default"]
            overridden = False
            pinned = False
        else:
            value = self._effective_value(key)
            overridden = key in self.doc.get("overrides", {})
            pinned = key in self.doc.get("pins", [])
        binding_id, binding_label = self._binding_summary(key)
        numeric = meta["kind"] == "slider"
        try:
            fraction = ((float(value) - meta["minimum"]) /
                        max(1e-9, meta["maximum"] - meta["minimum"])) if numeric else 0
        except (TypeError, ValueError):
            fraction = 0
        return dict(
            key=key, label=meta["label"], kind=meta["kind"], value=value,
            valueText=fmt_value(meta, value), fraction=max(0.0, min(1.0, fraction)),
            minimum=meta["minimum"], maximum=meta["maximum"], step=meta.get("step", 1),
            unit=meta["unit"], choices=meta.get("choices", []), desc=meta["desc"],
            advanced=bool(meta.get("advanced", False)), overridden=overridden, pinned=pinned,
            bindingId=binding_id, bindingLabel=binding_label,
            rendered=self._is_rendered(key), scope=self._scope(key),
            badge=("DEVICE ONLY" if key.startswith("assist_") else
                   ("AFFECTS PREVIEW" if self._is_rendered(key) else "ORGANISATION ONLY")),
            source="Creator" if not overridden else "Personal",
        )

    def _cards_for(self, area):
        if area == "quick":
            return [self._quick_card()]
        if area == "construction":
            return self._construction_cards()
        if area == "secondary":
            return self._secondary_cards()
        if area == "assist":
            rows = [self._setting(item["key"], item) for item in catalog.ASSIST]
            cards = []
            by_title = {}
            for row in rows:
                title = catalog.SECTION_OF[row["key"]]
                by_title.setdefault(title, []).append(row)
            for section_area, title, note, _keys in catalog.SECTIONS:
                if section_area == "assist" and title in by_title:
                    cards.append(dict(title=title, note=note, settings=by_title[title]))
            return cards
        cards = []
        for section_area, title, note, keys in catalog.SECTIONS:
            if section_area != area:
                continue
            rows = []
            for key in keys:
                meta = catalog.SETTINGS[key]
                if meta.get("advanced") and not self.show_advanced:
                    continue
                rows.append(self._setting(key, meta))
            if rows:
                cards.append(dict(title=title, note=note, settings=rows))
        if area == "dynamics":
            cards.insert(0, dict(title="Bindings", note="One signal-to-setting system. Edit the curve for any binding.",
                                 settings=[], bindings=self.binding_rows()))
        return cards

    @Property("QVariantList", notify=layoutChanged)
    def sections(self):
        return self._cards_for(self._area)

    def _scope(self, key):
        if key.startswith("node."):
            return "Selected part"
        if key.startswith("assist_"):
            return "This device only"
        if key in ("surface", "surface_scale", "surface_tooth", "surface_absorbency", "surface_grain"):
            return "Canvas-wide"
        if key in ("property_overflow", "property_reference", "property_vector_erase"):
            return "Application"
        return "Whole brush"

    def _is_rendered(self, key):
        if key.startswith("node."):
            return True
        if key.startswith("assist_"):
            return False
        return key in catalog.RENDERED.get(self._area, set())

    @Slot(str, result="QVariantMap")
    def settingByKey(self, key):
        meta = catalog.SETTINGS.get(key) or NODE_META.get(key)
        if not meta:
            return {}
        return self._setting(key, meta)

    @Slot(str, float)
    def setSetting(self, key, value):
        meta = catalog.SETTINGS.get(key) or NODE_META.get(key)
        if not meta:
            return
        if key.startswith("node."):
            self._set_node_value(key, value)
            return
        old = self._effective_value(key)
        if meta["kind"] == "toggle":
            value = bool(round(value))
        if meta["kind"] == "slider":
            value = max(float(meta["minimum"]), min(float(meta["maximum"]), float(value)))
        if old == value:
            return
        if self._edit_depth == 0:
            self.push_undo()
        if key.startswith("assist_"):
            self.doc.setdefault("assist", {})[key] = value
            if key == "assist_device":
                self.deviceChanged.emit()
        elif self.layer == "creator":
            self.doc["settings"][key] = value
        else:
            self.doc.setdefault("overrides", {})[key] = value
        self.version += 1
        self.changed.emit()
        if self._edit_depth == 0:
            self.layoutChanged.emit()
        if self._area == "dynamics" and key.startswith("dynamics_"):
            self.bindingChanged.emit()

    @Slot(str, str)
    def setSettingText(self, key, text):
        meta = catalog.SETTINGS.get(key) or NODE_META.get(key)
        if not meta:
            return
        if meta["kind"] == "choice":
            if text in meta.get("choices", []):
                self.setChoice(key, text)
            return
        try:
            value = float(text.replace("%", "").replace("°", "").replace("px", "").replace("×", "").strip())
        except ValueError:
            self.note("That is not a number.")
            return
        self.setSetting(key, value)

    @Slot(str, str)
    def setChoice(self, key, value):
        meta = catalog.SETTINGS.get(key) or NODE_META.get(key)
        if not meta or value not in meta.get("choices", []):
            return
        if key.startswith("node."):
            self._set_node_value(key, value)
            return
        if self._edit_depth == 0:
            self.push_undo()
        if key.startswith("assist_"):
            self.doc.setdefault("assist", {})[key] = value
        elif self.layer == "creator":
            self.doc["settings"][key] = value
        else:
            self.doc.setdefault("overrides", {})[key] = value
        if key == "assist_device":
            self.deviceChanged.emit()
        self.version += 1
        self.changed.emit()
        self.layoutChanged.emit()

    @Slot(str)
    def resetSetting(self, key):
        if key in self.doc.get("overrides", {}):
            self.push_undo()
            del self.doc["overrides"][key]
            self.note("Personal override removed. The creator's value is back.")
            self.touch(True)
        elif key.startswith("assist_"):
            self.push_undo()
            self.doc["assist"][key] = catalog.SETTINGS[key]["default"]
            self.touch(True)
        elif self.layer == "creator" and key in catalog.SETTINGS:
            self.push_undo()
            self.doc["settings"][key] = self.doc.get("initial", {}).get(key, catalog.SETTINGS[key]["default"])
            self.note("Creator setting reset to the saved initial value.")
            self.touch(True)
        elif key.startswith("node."):
            self.push_undo()
            node = self._selected_node()
            field = key.split(".", 1)[1]
            if node:
                node[field] = NODE_META[key]["default"]
            self.touch(True)
        else:
            self.note("This value already matches its owner layer.")

    @Slot(str)
    def togglePin(self, key):
        pins = self.doc.setdefault("pins", [])
        if key in pins:
            pins.remove(key)
            self.note("Removed from Quick Access. The setting itself is unchanged.")
        else:
            pins.append(key)
            self.note("Pinned to Quick Access for this artist.")
        self.touch(True)

    @Slot()
    def resetPersonal(self):
        self.push_undo()
        self.doc["overrides"] = {}
        self.doc["pins"] = []
        self.note("Personal overrides and pins cleared. Creator values were never touched.")
        self.touch(True)

    @Slot()
    def resetCreator(self):
        self.push_undo()
        self.doc["settings"] = deepcopy(self.doc.get("initial", {}))
        self.note("Creator settings restored to the saved initial settings.")
        self.touch(True)

    @Slot()
    def saveInitial(self):
        self.push_undo()
        self.doc["initial"] = deepcopy(self.doc["settings"])
        self.note("Current creator settings saved as this brush's initial reset point.")
        self.touch(True)

    @Slot()
    def copySettings(self):
        self._clipboard = dict(settings=deepcopy(self.doc["settings"]),
                               dynamics=deepcopy(self.doc["dynamics"]))
        self.note("Copied the creator settings and dynamics. Parts and textures are not copied.")

    @Slot()
    def pasteSettings(self):
        if not self._clipboard:
            self.note("Nothing has been copied yet.")
            return
        self.push_undo()
        for key, value in self._clipboard["settings"].items():
            if key in catalog.SETTINGS and not key.startswith("assist_"):
                self.doc["overrides"][key] = value
        self.note("Pasted as personal overrides. The creator's brush is unchanged.")
        self.touch(True)

    def _quick_card(self):
        rows = []
        for key in self.doc.get("pins", []):
            if key.startswith("node."):
                continue
            meta = catalog.SETTINGS.get(key)
            if meta:
                rows.append(self._setting(key, meta))
        return dict(title="Pinned by this artist", note="A pin is personal. Removing it leaves the brush unchanged.",
                    settings=rows)

    @Slot(str)
    def focusSetting(self, key):
        if key == self._hint:
            return
        self._hint = key
        self.hintChanged.emit()

    @Property("QVariantMap", notify=hintChanged)
    def currentHint(self):
        if self._area == "quick":
            cards = self.sections
            if cards and not cards[0]["settings"]:
                return dict(label="Quick Access", scope="Personal layer",
                            desc="Pins are personal. They choose what appears here and never change the creator's value.",
                            rendered=False, badge="PERSONAL ONLY", effect="", source="")
        return self.settingByKey(self._hint) if self._hint else {}

    # ------------------------------------------------------------------ dynamics
    def _device_key(self):
        device = self._base_value("assist_device")
        if device.startswith("Pen"):
            return "pen"
        if device.startswith("Touch"):
            return "touch"
        return "mouse"

    def _signal_available(self, signal):
        device = self._device_key()
        if device == "pen":
            return True
        if signal in ("Pressure", "Tilt", "Rotation", "Hover"):
            return False
        return True

    def _fallback(self):
        return self._base_value("dynamics_fallback")

    @Slot(str)
    def setDevice(self, device):
        self.setChoice("assist_device", device)

    @Property(str, notify=deviceChanged)
    def deviceKey(self):
        return self._device_key()

    @Property(str, notify=deviceChanged)
    def deviceNotice(self):
        device = self._device_key()
        if device == "mouse":
            return "Mouse · pressure, tilt, barrel roll and hover are unavailable; the declared fallback is used."
        if device == "touch":
            return "Touch · pressure, tilt, barrel roll and hover are unavailable on this design's touch profile."
        return "Pen (simulated) · pressure, tilt and barrel roll are offered."

    @Property("QVariantList", notify=deviceChanged)
    def signals(self):
        result = []
        for name, requirement, _key in catalog.SIGNALS:
            count = len([b for b in self.doc["dynamics"] if b["signal"] == name])
            result.append(dict(name=name, requirement=requirement,
                               available=self._signal_available(name),
                               fallback=self._fallback(), count=count))
        return result

    @Property("QVariantList", notify=bindingChanged)
    def bindings(self):
        return self.binding_rows()

    def binding_rows(self):
        result = []
        for binding in self.doc["dynamics"]:
            meta = catalog.SETTINGS.get(binding["target"])
            result.append(dict(
                id=binding["id"], name=binding["name"], signal=binding["signal"],
                target=binding["target"], targetLabel=meta["label"] if meta else binding["target"],
                amount=round(binding["amount"] * 100),
                scope=binding.get("scope", "Per stamp"),
                enabled=binding.get("enabled", True),
                available=self._signal_available(binding["signal"]),
                selected=binding["id"] == self._selected_binding,
                points=deepcopy(binding["curve"]),
            ))
        return result

    @Slot(str, result="QVariantMap")
    def bindingFor(self, key):
        for binding in self.doc["dynamics"]:
            if binding["target"] == key:
                return self._binding_map(binding)
        return {}

    @Slot(str, result="QVariantMap")
    def bindingById(self, ident):
        for binding in self.doc["dynamics"]:
            if binding["id"] == ident:
                return self._binding_map(binding)
        return {}

    def _binding_map(self, binding):
        meta = catalog.SETTINGS.get(binding["target"])
        choices = [name for name, _req, _key in catalog.SIGNALS if self._signal_available(name)]
        return dict(
            id=binding["id"], name=binding["name"], signal=binding["signal"],
            target=binding["target"], targetLabel=meta["label"] if meta else binding["target"],
            amount=round(binding["amount"] * 100),
            scope=binding.get("scope", "Per stamp"),
            enabled=binding.get("enabled", True),
            available=self._signal_available(binding["signal"]),
            fallback=self._fallback(), signalChoices=choices,
            points=deepcopy(binding["curve"]),
        )

    @Property("QVariantMap", notify=bindingChanged)
    def selectedBinding(self):
        return self.bindingById(self._selected_binding)

    @Slot(str)
    def selectBinding(self, ident):
        if self._selected_binding != ident:
            self._selected_binding = ident
            self.bindingChanged.emit()

    @Slot(str, result=str)
    def ensureBinding(self, key):
        for binding in self.doc["dynamics"]:
            if binding["target"] == key:
                self._selected_binding = binding["id"]
                self.bindingChanged.emit()
                return binding["id"]
        signal = "Pressure" if self._device_key() == "pen" else self._fallback()
        if signal not in [name for name, _r, _k in catalog.SIGNALS if self._signal_available(name)]:
            signal = "Distance"
        ident = f"d{len(self.doc['dynamics']) + 1}"
        while any(b["id"] == ident for b in self.doc["dynamics"]):
            ident += "x"
        meta = catalog.SETTINGS.get(key)
        self.push_undo()
        self.doc["dynamics"].append(dict(
            id=ident, name=f"{signal} → {meta['label'] if meta else key}",
            signal=signal, target=key, amount=0.45,
            curve=[dict(x=0.0, y=0.0), dict(x=0.5, y=0.5), dict(x=1.0, y=1.0)],
            scope="Per stamp", enabled=True))
        self._selected_binding = ident
        self.version += 1
        self.changed.emit()
        self.layoutChanged.emit()
        self.bindingChanged.emit()
        self.note("Added one shared dynamic. Drag its curve; no second pressure system was created.")
        return ident

    @Slot(str, str)
    def setBindingSignal(self, ident, signal):
        if not self._signal_available(signal):
            self.note(f"{signal} is not available on this device. Choose a signal the device reports.")
            return
        for binding in self.doc["dynamics"]:
            if binding["id"] == ident:
                self.push_undo()
                binding["signal"] = signal
                self.version += 1
                self.changed.emit()
                self.bindingChanged.emit()
                return

    @Slot(str, float)
    def setBindingAmount(self, ident, value):
        for binding in self.doc["dynamics"]:
            if binding["id"] == ident:
                binding["amount"] = max(0.0, min(1.0, value / 100.0))
                self.version += 1
                self.changed.emit()
                self.bindingChanged.emit()
                return

    @Slot(str, str)
    def setBindingScope(self, ident, scope):
        if scope not in ("Per stamp", "Per stroke"):
            return
        for binding in self.doc["dynamics"]:
            if binding["id"] == ident:
                binding["scope"] = scope
                self.version += 1
                self.changed.emit()
                self.bindingChanged.emit()
                return

    @Slot(str, int, float, float)
    def setCurvePoint(self, ident, index, x, y):
        for binding in self.doc["dynamics"]:
            if binding["id"] != ident:
                continue
            points = binding["curve"]
            if index <= 0 or index >= len(points) - 1:
                return
            low = points[index - 1]["x"] + 0.005
            high = points[index + 1]["x"] - 0.005
            points[index]["x"] = max(low, min(high, x))
            points[index]["y"] = max(0.0, min(1.0, y))
            self.version += 1
            self.changed.emit()
            self.bindingChanged.emit()
            return

    @Slot(str, float, float, result=int)
    def addCurvePoint(self, ident, x, y):
        for binding in self.doc["dynamics"]:
            if binding["id"] == ident:
                x = max(0.03, min(0.97, x))
                binding["curve"].append(dict(x=x, y=max(0.0, min(1.0, y))))
                binding["curve"].sort(key=lambda p: p["x"])
                self.version += 1
                self.changed.emit()
                self.bindingChanged.emit()
                return binding["curve"].index(next(p for p in binding["curve"] if abs(p["x"] - x) < 1e-9))
        return -1

    @Slot(str, int)
    def removeCurvePoint(self, ident, index):
        for binding in self.doc["dynamics"]:
            if binding["id"] == ident and len(binding["curve"]) > 2 and 0 < index < len(binding["curve"]) - 1:
                del binding["curve"][index]
                self.version += 1
                self.changed.emit()
                self.bindingChanged.emit()
                return

    @Slot(str)
    def resetCurve(self, ident):
        for binding in self.doc["dynamics"]:
            if binding["id"] == ident:
                binding["curve"] = [dict(x=0.0, y=0.0), dict(x=0.5, y=0.5), dict(x=1.0, y=1.0)]
                self.version += 1
                self.changed.emit()
                self.bindingChanged.emit()
                return

    @Slot(str)
    def removeBinding(self, ident):
        for index, binding in enumerate(self.doc["dynamics"]):
            if binding["id"] == ident:
                self.push_undo()
                del self.doc["dynamics"][index]
                self._selected_binding = self.doc["dynamics"][0]["id"] if self.doc["dynamics"] else ""
                self.version += 1
                self.changed.emit()
                self.layoutChanged.emit()
                self.bindingChanged.emit()
                self.note("Dynamic removed from this brush.")
                return

    @Property("QVariantList", notify=bindingChanged)
    def presets(self):
        return deepcopy(self.doc.get("presets", []))

    @Slot(str)
    def applyPreset(self, ident):
        preset = next((p for p in self.doc.get("presets", []) if p["id"] == ident), None)
        target = next((b for b in self.doc["dynamics"] if b["id"] == self._selected_binding), None)
        if not preset or not target:
            self.note("Select a binding first, then apply a reusable dynamic.")
            return
        if not self._signal_available(preset["signal"]):
            self.note(f"{preset['signal']} is unavailable on this device. The preset was not applied.")
            return
        self.push_undo()
        target["signal"] = preset["signal"]
        target["curve"] = deepcopy(preset["curve"])
        target["scope"] = preset.get("scope", "Per stamp")
        self.version += 1
        self.changed.emit()
        self.bindingChanged.emit()
        self.note(f"Applied “{preset['name']}” to {target['name']}. The curve remains editable.")

    @Slot()
    def savePreset(self):
        binding = next((b for b in self.doc["dynamics"] if b["id"] == self._selected_binding), None)
        if not binding:
            self.note("Select a binding to save as a reusable dynamic.")
            return
        ident = f"p{len(self.doc['presets']) + 1}"
        self.doc["presets"].append(dict(id=ident, name=f"{binding['signal']} shape {len(self.doc['presets']) + 1}",
                                        signal=binding["signal"], curve=deepcopy(binding["curve"]),
                                        scope=binding.get("scope", "Per stamp")))
        self.touch(True)
        self.bindingChanged.emit()
        self.note("Dynamic saved to this brush's reusable set.")

    # -------------------------------------------------------------- construction
    def _selected_node(self):
        row = recipes.find(self.doc["root"], self.selected_uid)
        return row[0] if row else self.doc["root"]

    @Property("QVariantList", notify=treeChanged)
    def tree(self):
        rows = []
        for node, parent, depth in recipes.walk(self.doc["root"]):
            rows.append(dict(
                uid=node["uid"], name=node["name"], kind=node["kind"], depth=depth,
                selected=node["uid"] == self.selected_uid,
                visible=node.get("visible", True), asset=node.get("asset", ""),
                detail=self._node_detail(node), parent=parent["name"] if parent else "",
                children=len(node.get("children", [])),
            ))
        return rows

    def _node_detail(self, node):
        kind = node["kind"]
        if kind == "mark":
            return MARKS.get(node.get("asset"), node.get("asset", "Mark"))
        if kind == "texture":
            return f"Texture · {node.get('texture_anchor', 'Canvas')}"
        if kind == "mask":
            return "Mask"
        if kind == "brush":
            return f"Secondary · {round(node.get('amount', 100))}% · {node.get('blend')}"
        if kind == "group":
            return node.get("mode", "Paint together")
        return "Whole brush"

    @Property(str, notify=selectionChanged)
    def selectedNodeId(self):
        return self.selected_uid

    @Property("QVariantMap", notify=selectionChanged)
    def selectedNode(self):
        node = self._selected_node()
        return dict(uid=node["uid"], name=node["name"], kind=node["kind"],
                    detail=self._node_detail(node), asset=node.get("asset", ""))

    @Slot(str)
    def selectNode(self, uid):
        if recipes.find(self.doc["root"], uid):
            self.selected_uid = uid
            self.selectionChanged.emit()
            self.treeChanged.emit()
            self.layoutChanged.emit()
            self.note(f"{self._selected_node()['name']} selected. Its settings are scoped to this part.")

    def _set_node_value(self, key, value):
        node = self._selected_node()
        field = key.split(".", 1)[1]
        meta = NODE_META[key]
        if meta["kind"] == "toggle":
            value = bool(round(float(value)))
        elif meta["kind"] == "slider":
            value = max(meta["minimum"], min(meta["maximum"], float(value)))
            if field in ("members_per_stamp", "weight"):
                value = int(round(value))
        if node.get(field) == value:
            return
        if self._edit_depth == 0:
            self.push_undo()
        node[field] = value
        self.version += 1
        self.changed.emit()
        if self._edit_depth == 0:
            self.treeChanged.emit()
            self.layoutChanged.emit()

    def _secondary_cards(self):
        brushes = [node for node, _parent, _depth in recipes.walk(self.doc["root"])
                   if node["kind"] == "brush" and node is not self.doc["root"]]
        if not brushes:
            return [dict(title="No secondary brush yet",
                         note="Use Construction, select a group or the brush, then add a secondary brush.",
                         settings=[])]
        node = self._selected_node()
        if node["kind"] != "brush" or node is self.doc["root"]:
            node = brushes[0]
        keys = ["node.amount", "node.blend", "node.opacity", "node.scale", "node.angle",
                "node.weight", "node.flipH", "node.flipV"]
        cards = [dict(title=node["name"],
                      note="An independent subtree. Its amount is how much it contributes to the parent.",
                      settings=[self._setting(key, NODE_META[key]) for key in keys])]
        cards.append(dict(title="Brush-level settings",
                          note="These belong to the whole brush, not to the selected secondary.",
                          settings=[self._setting(k, catalog.SETTINGS[k]) for k in
                                    ("opacity", "flow", "spacing", "tip_size")]))
        return cards

    def _construction_cards(self):
        node = self._selected_node()
        kind = node["kind"]
        keys = []
        if kind == "mark":
            keys = ["node.opacity", "node.scale", "node.angle", "node.aspect", "node.weight",
                    "node.flipH", "node.flipV"]
        elif kind == "group":
            keys = ["node.mode", "node.members_per_stamp", "node.opacity", "node.scale",
                    "node.angle", "node.weight"]
        elif kind == "texture":
            keys = ["node.texture_strength", "node.texture_scale", "node.texture_rotation",
                    "node.texture_depth", "node.texture_contrast", "node.texture_anchor",
                    "node.texture_per_stamp", "node.texture_tiling"]
        elif kind == "brush":
            keys = ["node.amount", "node.blend", "node.opacity", "node.scale"]
        rows = [self._setting(key, NODE_META[key]) for key in keys]
        cards = []
        if rows:
            cards.append(dict(title=node["name"], note=f"{kind.title()} · settings apply to this part only.",
                              settings=rows))
        cards.append(dict(title="Brush-level settings", note="These belong to the whole brush, not to the selected part.",
                          settings=[self._setting(k, catalog.SETTINGS[k]) for k in
                                    ("opacity", "flow", "spacing", "tip_size")]))
        return cards

    @Slot(str, result=str)
    def addNode(self, kind):
        parent = self._selected_node()
        if parent["kind"] not in ("group", "brush"):
            found = recipes.find(self.doc["root"], self.selected_uid)
            parent = found[1] if found and found[1] else self.doc["root"]
        if kind == "mark":
            node = recipes.mark("New mark", "graphite", scale=72, opacity=80)
        elif kind == "group":
            node = recipes.group("New group", [], mode="Random", members_per_stamp=1)
        elif kind == "texture":
            node = recipes.texture("New texture", "paper", texture_strength=34)
        elif kind == "mask":
            node = recipes.mask("New mask", "breakup", texture_strength=36)
        elif kind == "brush":
            return self.addSecondary()
        else:
            return ""
        self.push_undo()
        parent["children"].append(node)
        self.selected_uid = node["uid"]
        self.version += 1
        self.changed.emit()
        self.treeChanged.emit()
        self.layoutChanged.emit()
        self.selectionChanged.emit()
        self.note(f"Added {node['name']} inside {parent['name']}. There is no part cap.")
        return node["uid"]

    @Slot()
    def addSecondary(self):
        template = recipes.build_graphite()
        children = recipes.clone(dict(uid="tmp", kind="brush", name="", children=template["children"],
                                      visible=True, opacity=100, blend="Normal", mode="Paint together",
                                      weight=4, amount=100, scale=100, angle=0, aspect=100,
                                      flipH=False, flipV=False, asset="graphite",
                                      texture_scale=100, texture_rotation=0, texture_strength=40,
                                      texture_depth=70, texture_contrast=100, texture_anchor="Canvas",
                                      texture_offset_x=0, texture_offset_y=0,
                                      texture_source="Cold press", texture_tiling=True,
                                      texture_per_stamp=False, texture_filter="Smooth",
                                      members_per_stamp=1, ref=""))
        node = recipes.secondary(
            f"Secondary brush {len([1 for n, _, _ in recipes.walk(self.doc['root']) if n['kind'] == 'brush'])}",
            [], amount=24, blend="Normal")
        # A secondary brush is an ordinary editable subtree. Its children are
        # independent copies so editing it can never mutate another brush.
        node["children"] = [self._reidentify(child) for child in children["children"]
                            if child["kind"] != "brush"]
        parent = self._selected_node()
        if parent["kind"] not in ("group", "brush"):
            found = recipes.find(self.doc["root"], self.selected_uid)
            parent = found[1] if found and found[1] else self.doc["root"]
        self.push_undo()
        parent["children"].append(node)
        self.selected_uid = node["uid"]
        self.version += 1
        self.changed.emit()
        self.treeChanged.emit()
        self.layoutChanged.emit()
        self.selectionChanged.emit()
        self.note("Added another independent secondary brush. No two-brush limit is implied.")
        return node["uid"]

    def _reidentify(self, node):
        node = deepcopy(node)
        node["uid"] = f"n{recipes.next_serial()}"
        node["children"] = [self._reidentify(child) for child in node.get("children", [])]
        return node

    @Slot()
    def duplicateNode(self):
        found = recipes.find(self.doc["root"], self.selected_uid)
        if not found or found[1] is None:
            self.note("The whole brush cannot be duplicated into itself.")
            return
        node, parent, _depth = found
        copy = recipes.clone(node)
        copy["name"] = node["name"] + " copy"
        index = parent["children"].index(node)
        self.push_undo()
        parent["children"].insert(index + 1, copy)
        self.selected_uid = copy["uid"]
        self.version += 1
        self.changed.emit()
        self.treeChanged.emit()
        self.layoutChanged.emit()
        self.selectionChanged.emit()
        self.note("Part duplicated. It has its own pixels and settings.")

    @Slot()
    def removeNode(self):
        found = recipes.find(self.doc["root"], self.selected_uid)
        if not found or found[1] is None:
            self.note("The whole brush is the brush. Remove a part inside it.")
            return
        node, parent, _depth = found
        self.push_undo()
        parent["children"].remove(node)
        self.selected_uid = parent["uid"]
        self.version += 1
        self.changed.emit()
        self.treeChanged.emit()
        self.layoutChanged.emit()
        self.selectionChanged.emit()
        self.note("Part removed. Brush undo restores it; test strokes are untouched.")

    @Slot(int)
    def moveNode(self, delta):
        found = recipes.find(self.doc["root"], self.selected_uid)
        if not found or found[1] is None:
            return
        node, parent, _depth = found
        index = parent["children"].index(node)
        target = max(0, min(len(parent["children"]) - 1, index + int(delta)))
        if target == index:
            return
        self.push_undo()
        parent["children"].insert(target, parent["children"].pop(index))
        self.version += 1
        self.changed.emit()
        self.treeChanged.emit()

    @Slot(str)
    def toggleNodeVisible(self, uid):
        found = recipes.find(self.doc["root"], uid)
        if not found:
            return
        self.push_undo()
        found[0]["visible"] = not found[0].get("visible", True)
        self.version += 1
        self.changed.emit()
        self.treeChanged.emit()

    @Slot(str)
    def setNodeTip(self, asset):
        node = self._selected_node()
        if node["kind"] != "mark":
            self.note("Select a mark to change its tip image.")
            return
        self.push_undo()
        node["asset"] = asset
        self.version += 1
        self.changed.emit()
        self.treeChanged.emit()
        self.layoutChanged.emit()
        self.note(f"Tip changed to {MARKS.get(asset, asset)}. Every mark sharing this part changes; other parts do not.")

    @Property("QVariantList", notify=changed)
    def tipChoices(self):
        return [dict(id=key, name=MARKS.get(key, key), source=f"image://brushes/pale/{key}")
                for key in self.doc.get("tips", [])]

    @Slot(str, str, result=str)
    def importImage(self, url, kind):
        path = Path(url.replace("file:///", "").replace("file://", ""))
        key = f"import{self._custom_tip}"
        self._custom_tip += 1
        try:
            self.assets.add_image(key, path, kind)
        except (OSError, ValueError) as exc:
            self.note(str(exc))
            return ""
        if kind == "mark":
            self.doc["tips"].append(key)
            if self._selected_node()["kind"] == "mark":
                self.setNodeTip(key)
            else:
                self.note("Imported mark added. Select a mark to use it.")
                self.touch(True)
        else:
            meta = catalog.SETTINGS["texture_source"]
            if key not in meta["choices"]:
                meta["choices"].append(key)
            self.setChoice("texture_source", key)
            self.note("Imported texture added as an independent source.")
        return key

    @Property("QVariantList", notify=changed)
    def reusableBrushes(self):
        for extra in self._reusable:
            if extra["id"].startswith("c"):
                extra["source"] = "This session"
            else:
                extra["source"] = "Starter"
        return deepcopy(self._reusable)

    @Slot(str)
    def insertReusable(self, ident):
        item = next((entry for entry in self._reusable if entry["id"] == ident), None)
        if not item:
            return
        if item["id"].startswith("c"):
            node = self._reidentify(item["node"])
        else:
            root = recipes.build_ferrous() if item["key"] == "ferrous" else getattr(recipes, f"build_{item['key']}")()
            node = recipes.secondary(item["name"], root["children"], amount=22, blend="Normal")
            node = self._reidentify(node)
        parent = self._selected_node()
        if parent["kind"] not in ("group", "brush"):
            found = recipes.find(self.doc["root"], self.selected_uid)
            parent = found[1] if found and found[1] else self.doc["root"]
        self.push_undo()
        parent["children"].append(node)
        self.selected_uid = node["uid"]
        self.version += 1
        self.changed.emit()
        self.treeChanged.emit()
        self.layoutChanged.emit()
        self.selectionChanged.emit()
        self.note(f"Inserted “{item['name']}” as an independent copy. Editing it does not alter the original.")

    @Slot()
    def makeReusable(self):
        node = self._selected_node()
        if node["kind"] != "brush":
            self.note("Select a secondary brush to make it reusable.")
            return
        ident = f"c{len(self._reusable) + 1}"
        item = dict(id=ident, name=node["name"] + " (saved)", key="", source="This session",
                    node=deepcopy(node))
        self._reusable.append(item)
        self.changed.emit()
        self.note("Saved this construction as a reusable brush. It stays independent when inserted.")

    @Property("QVariantMap", notify=changed)
    def counts(self):
        return recipes.counts(self.doc["root"])

    # ----------------------------------------------------------------------- pad
    @Property("QVariantList", notify=padChanged)
    def strokes(self):
        return deepcopy(self._strokes)

    @Property("QVariantList", notify=padChanged)
    def retainedStrokes(self):
        return deepcopy(self._retained["strokes"]) if self._retained else []

    @Property("QVariantMap", notify=changed)
    def retainedDocument(self):
        return deepcopy(self._retained["doc"]) if self._retained else {}

    @Property(bool, notify=padChanged)
    def compare(self):
        return self._comparison

    @Slot(bool)
    def setCompare(self, value):
        self._comparison = bool(value)
        self.padChanged.emit()

    @Property(bool, notify=padChanged)
    def hasRetained(self):
        return self._retained is not None

    @Slot()
    def retainInput(self):
        if not self._strokes:
            self.note("Draw a stroke first; retentions replay real input, not a sample.")
            return
        source = self._strokes[-1]
        self._retained = dict(strokes=[deepcopy(source)], doc=deepcopy(self.doc),
                              label="Retained input")
        self._comparison = True
        self.padChanged.emit()
        self.note("Input retained with the settings used to draw it. Compare replays the same path.")

    @Slot()
    def clearRetained(self):
        self._retained = None
        self._comparison = False
        self.padChanged.emit()

    @Slot(float, float, float)
    def beginStroke(self, x, y, pressure=-1.0):
        self._pad_before = deepcopy(self._strokes)
        p = self._pad_pressure if pressure <= 0 else pressure
        self._pad_last = (x, y, monotonic())
        self._pad_active = dict(seed=stable_hash((x, y, self.pad_version)), points=[(x, y, p, 0.0)],
                                size=self._effective_value("tip_size"))
        self.pad_version += 1
        self.padChanged.emit()

    @Slot(float, float, float)
    def moveStroke(self, x, y, pressure=-1.0):
        if not self._pad_active:
            return
        lx, ly, _lt = self._pad_last
        dist = ((x - lx) ** 2 + (y - ly) ** 2) ** 0.5
        if dist < 0.001:
            return
        p = self._pad_pressure if pressure <= 0 else pressure
        speed = min(1.0, dist * 6.0)
        self._pad_active["points"].append((x, y, p, speed))
        self._pad_last = (x, y, monotonic())
        self.pad_version += 1
        self.padChanged.emit()

    @Slot()
    def endStroke(self):
        if self._pad_active and self._pad_active["points"]:
            self._strokes.append(self._pad_active)
            self._pad_undo.append(self._pad_before or [])
            self._pad_undo = self._pad_undo[-60:]
            self._pad_redo.clear()
            self._last_stroke = self._pad_active
        self._pad_active = None
        self._pad_before = None
        self.pad_version += 1
        self.padChanged.emit()
        self.historyChanged.emit()

    @Slot()
    def cancelStroke(self):
        self._pad_active = None
        self.pad_version += 1
        self.padChanged.emit()

    @Slot()
    def undoPad(self):
        if not self._pad_undo:
            return
        self._pad_redo.append(deepcopy(self._strokes))
        self._strokes = self._pad_undo.pop()
        self.pad_version += 1
        self.padChanged.emit()
        self.historyChanged.emit()
        self.note("Test-pad undo. Brush edits are a separate history.")

    @Slot()
    def redoPad(self):
        if not self._pad_redo:
            return
        self._pad_undo.append(deepcopy(self._strokes))
        self._strokes = self._pad_redo.pop()
        self.pad_version += 1
        self.padChanged.emit()
        self.historyChanged.emit()
        self.note("Test-pad redo. Brush edits are a separate history.")

    @Slot()
    def clearPad(self):
        if not self._strokes:
            return
        self._pad_undo.append(deepcopy(self._strokes))
        self._pad_redo.clear()
        self._strokes = []
        self.pad_version += 1
        self.padChanged.emit()
        self.historyChanged.emit()
        self.note("Test pad cleared. The brush draft and its history are untouched.")

    @Property(bool, notify=historyChanged)
    def padCanUndo(self):
        return bool(self._pad_undo)

    @Property(bool, notify=historyChanged)
    def padCanRedo(self):
        return bool(self._pad_redo)

    @Property("QVariantList", notify=padChanged)
    def activeStroke(self):
        return [deepcopy(self._pad_active)] if self._pad_active else []

    @Property("QVariantList", notify=padChanged)
    def testModes(self):
        return ["Mark", "Stroke", "Repeat", "Pressure", "Texture", "Mixing", "Surface"]

    @Property(str, notify=padChanged)
    def testMode(self):
        return self._test_mode.capitalize()

    @Slot(str)
    def setTestMode(self, mode):
        mode = mode.lower()
        if mode not in [m.lower() for m in self.testModes]:
            return
        self._test_mode = mode
        self.pad_version += 1
        self.padChanged.emit()

    @Slot()
    def nextTestMode(self):
        modes = self.testModes
        index = [m.lower() for m in modes].index(self._test_mode)
        self.setTestMode(modes[(index + 1) % len(modes)])

    @Property(float, notify=padChanged)
    def padPressure(self):
        return self._pad_pressure

    @Slot(float)
    def setPadPressure(self, value):
        self._pad_pressure = max(0.02, min(1.0, value))
        self.padChanged.emit()

    @Slot(str)
    def setColour(self, value):
        self.doc["colour"] = value
        self.version += 1
        self.changed.emit()
        self.padChanged.emit()

    @Slot(str)
    def setSecondaryColour(self, value):
        self.doc["secondary_colour"] = value
        self.version += 1
        self.changed.emit()

    @Property(str, notify=changed)
    def colour(self):
        return self.doc.get("colour", "#2b2927")

    @Property(str, notify=changed)
    def secondaryColour(self):
        return self.doc.get("secondary_colour", "#7a756c")

    # ---------------------------------------------------------------------- tips
    @Property("QVariantList", notify=tipChanged)
    def tipStrokes(self):
        return deepcopy(self._tip_strokes + ([self._tip_active] if self._tip_active else []))

    @Slot(float, float)
    def beginTip(self, x, y):
        self._tip_active = dict(points=[(x, y, 1.0, 0.0)], size=self._tip_size, seed=999)
        self.tipChanged.emit()

    @Slot(float, float)
    def moveTip(self, x, y):
        if self._tip_active:
            self._tip_active["points"].append((x, y, 1.0, 0.0))
            self.tipChanged.emit()

    @Slot()
    def endTip(self):
        if self._tip_active:
            self._tip_strokes.append(self._tip_active)
            self._tip_active = None
            self.tipChanged.emit()

    @Slot()
    def clearTip(self):
        self._tip_undo.append(deepcopy(self._tip_strokes))
        self._tip_strokes = []
        self.tipChanged.emit()

    @Slot()
    def undoTip(self):
        if self._tip_undo:
            self._tip_strokes = self._tip_undo.pop()
            self.tipChanged.emit()

    @Slot(result=str)
    def makeDrawnTip(self):
        if not self._tip_strokes:
            self.note("Draw a mark in the tip room first.")
            return ""
        key = f"drawn{self._custom_tip}"
        self._custom_tip += 1
        self.assets.add_drawn(key, self._tip_strokes)
        self.doc["tips"].append(key)
        self.tipChanged.emit()
        if self._selected_node()["kind"] == "mark":
            self.setNodeTip(key)
        else:
            self.touch(True)
            self.note("Drawn tip added. Select a mark to use it.")
        self._tip_strokes = []
        self.tipChanged.emit()
        return key

    @Slot(float)
    def setTipSize(self, value):
        self._tip_size = max(2, min(80, value))
        self.tipChanged.emit()

    @Property(float, notify=tipChanged)
    def tipSize(self):
        return self._tip_size

    # ------------------------------------------------------------------- brushes
    @Property(str, notify=changed)
    def brushName(self):
        return self.doc["name"]

    @Slot(str)
    def setBrushName(self, value):
        if value and value != self.doc["name"]:
            self.doc["name"] = value
            self.touch(True)

    @Property(str, notify=changed)
    def engine(self):
        return self.doc.get("engine", "Digital")

    @Slot(str)
    def setEngine(self, value):
        if value in ("Digital", "Realistic"):
            self.doc["engine"] = value
            self.touch(True)

    @Property("QVariantList", notify=changed)
    def brushChoices(self):
        return [dict(id=key, name=label) for key, (label, _b, _p) in recipes.STARTERS.items()]

    @Slot(str)
    def selectBrush(self, key):
        if key not in self.docs:
            return
        self.docs[self.key] = self.doc
        self.key = key
        self.doc = self.docs[key]
        self.selected_uid = self.doc["root"]["uid"]
        self._strokes = []
        self._pad_undo.clear()
        self._pad_redo.clear()
        self._comparison = False
        self._retained = None
        self._selected_binding = self.doc["dynamics"][0]["id"] if self.doc["dynamics"] else ""
        self.version += 1
        self.pad_version += 1
        self.changed.emit()
        self.padChanged.emit()
        self.layoutChanged.emit()
        self.treeChanged.emit()
        self.selectionChanged.emit()
        self.bindingChanged.emit()
        self.note(f"{self.doc['name']} loaded. Its creator definition is editable; personal layers are empty.")

    @Slot()
    def newTestStroke(self):
        self._strokes = []
        self._pad_undo.append(deepcopy(self._strokes))
        self.pad_version += 1
        self.padChanged.emit()

    @Property("QVariantList", notify=changed)
    def areaIndex(self):
        return [key for key, _label, _desc in catalog.AREAS]

    @Slot(result="QVariantList")
    def sampleStrokes(self):
        return render.sample_strokes(self._test_mode, self.doc, 1000, 700)

    @Property("QVariantList", notify=changed)
    def auditSummary(self):
        return [
            dict(label="Settings catalogued", value=str(len(catalog.SETTINGS))),
            dict(label="Construction parts", value=str(sum(recipes.counts(self.doc["root"]).values()))),
            dict(label="Shared dynamics", value=str(len(self.doc["dynamics"]))),
            dict(label="Secondary brushes", value=str(sum(1 for n, _p, _d in recipes.walk(self.doc["root"]) if n["kind"] == "brush"))),
        ]
