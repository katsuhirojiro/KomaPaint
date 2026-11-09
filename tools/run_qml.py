#!/usr/bin/env python3
"""Development helper: launch KomaPaint's QML shell with PySide6.

This is a developer convenience so the QML interface can be exercised without
having the full Qt 6 C++ development toolchain installed. The production
executable is still the C++ app built from apps/desktop/main.cpp.
"""
import sys
from pathlib import Path

from PySide6.QtCore import QSize, QUrl, Qt, QObject, Property, Signal, Slot
from PySide6.QtGui import QGuiApplication, QColor
from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterType
from PySide6.QtQuickControls2 import QQuickStyle

ROOT = Path(__file__).resolve().parent.parent
UI_DIR = ROOT / "ui"


class AppInfo(QObject):
    nameChanged = Signal()
    versionDisplayChanged = Signal()
    copyrightYearChanged = Signal()
    def __init__(self): super().__init__()
    @Property(str, constant=True)
    def name(self): return "KomaPaint"
    @Property(str, constant=True)
    def versionDisplay(self): return "KomaPaint 0.1.0 (Alpha)"
    @Property(str, constant=True)
    def copyrightYear(self): return "2026"
    @Property(bool, constant=True)
    def alphaBuild(self): return True


class Settings_(QObject):
    # Mirrors src/core/Settings: windowSize / windowMaximized are QML
    # properties (with notify signals), backed by the same defaults.
    valueChanged = Signal(str)
    windowSizeChanged = Signal()
    windowMaximizedChanged = Signal()
    def __init__(self): super().__init__(); self._size = QSize(1440,900); self._max=False
    @Slot(str, result="QVariant")
    def value(self, key, default=None): return default
    @Slot(str, "QVariant")
    def setValue(self, key, v): pass
    @Property(QSize, notify=windowSizeChanged)
    def windowSize(self): return self._size
    @windowSize.setter
    def windowSize(self, s):
        s = QSize(s)
        if s == self._size: return
        self._size = s; self.windowSizeChanged.emit()
    @Property(bool, notify=windowMaximizedChanged)
    def windowMaximized(self): return self._max
    @windowMaximized.setter
    def windowMaximized(self, m):
        if m == self._max: return
        self._max = m; self.windowMaximizedChanged.emit()


class Panels(QObject):
    visiblePanelsChanged = Signal()
    panelVisibilityChanged = Signal(str, bool)
    def __init__(self):
        super().__init__()
        self._state = { "layers": True, "properties": True, "color": True,
                        "timeline": False, "assets": False, "history": False,
                        "navigator": False }
    @Slot(str, result=bool)
    def isVisible(self, pid): return self._state.get(pid, False)
    @Slot(str, bool)
    def setVisible(self, pid, v):
        if self._state.get(pid) == v: return
        self._state[pid] = v
        self.panelVisibilityChanged.emit(pid, v)
        self.visiblePanelsChanged.emit()
    @Slot(str)
    def toggle(self, pid): self.setVisible(pid, not self.isVisible(pid))
    @Slot()
    def resetLayout(self):
        self._state = {"layers":True,"properties":True,"color":True,"timeline":False,"assets":False,"history":False,"navigator":False}
        self.visiblePanelsChanged.emit()
    @Property(list, notify=visiblePanelsChanged)
    def visiblePanels(self): return [k for k,v in self._state.items() if v]


class Doc(QObject):
    titleChanged = Signal(); sizeChanged = Signal(); dpiChanged = Signal(); backgroundChanged = Signal()
    def __init__(self): super().__init__(); self._title="Untitled"; self._w=1600; self._h=1200; self._dpi=300.0; self._trans=True
    @Property(str, notify=titleChanged)
    def title(self): return self._title
    @title.setter
    def title(self, v):
        if v==self._title: return; self._title=v; self.titleChanged.emit()
    @Property(int, notify=sizeChanged)
    def width(self): return self._w
    @width.setter
    def width(self, v):
        if v<=0 or v==self._w: return; self._w=v; self.sizeChanged.emit()
    @Property(int, notify=sizeChanged)
    def height(self): return self._h
    @height.setter
    def height(self, v):
        if v<=0 or v==self._h: return; self._h=v; self.sizeChanged.emit()
    @Property(float, notify=dpiChanged)
    def dpi(self): return self._dpi
    @dpi.setter
    def dpi(self, v):
        if v<=0 or abs(v-self._dpi)<1e-6: return; self._dpi=v; self.dpiChanged.emit()
    @Property(bool, notify=backgroundChanged)
    def hasTransparentBackground(self): return self._trans
    @Slot(bool)
    def setTransparentBackground(self, t):
        if t==self._trans: return; self._trans=t; self.backgroundChanged.emit()


class Colors(QObject):
    foregroundChanged = Signal(QColor); backgroundChanged = Signal(QColor)
    def __init__(self): super().__init__(); self._fg=QColor(Qt.black); self._bg=QColor(Qt.white)
    @Property(QColor, notify=foregroundChanged)
    def foreground(self): return self._fg
    @foreground.setter
    def foreground(self, c):
        if c==self._fg or not c.isValid(): return; self._fg=c; self.foregroundChanged.emit(c)
    @Property(QColor, notify=backgroundChanged)
    def background(self): return self._bg
    @background.setter
    def background(self, c):
        if c==self._bg or not c.isValid(): return; self._bg=c; self.backgroundChanged.emit(c)
    @Slot()
    def swap(self):
        a,b = self._fg, self._bg; self.foreground=b; self.background=a
    @Slot()
    def reset(self):
        self.foreground = QColor(Qt.black); self.background = QColor(Qt.white)


class History(QObject):
    canUndoChanged = Signal(); canRedoChanged = Signal(); stackChanged = Signal()
    def __init__(self): super().__init__(); self._cmds=[]; self._cur=0
    @Property(bool, notify=canUndoChanged)
    def canUndo(self): return self._cur>0
    @Property(bool, notify=canRedoChanged)
    def canRedo(self): return self._cur<len(self._cmds)
    @Slot(str)
    def push(self, d):
        self._cmds = self._cmds[:self._cur]
        self._cmds.append(d); self._cur += 1
        self.canUndoChanged.emit(); self.canRedoChanged.emit(); self.stackChanged.emit()
    @Slot()
    def undo(self):
        if not self.canUndo: return; self._cur-=1; self.canUndoChanged.emit(); self.canRedoChanged.emit(); self.stackChanged.emit()
    @Slot()
    def redo(self):
        if not self.canRedo: return; self._cur+=1; self.canUndoChanged.emit(); self.canRedoChanged.emit(); self.stackChanged.emit()
    @Slot()
    def clear(self):
        self._cmds.clear(); self._cur=0; self.canUndoChanged.emit(); self.canRedoChanged.emit(); self.stackChanged.emit()


class Layers(QObject):
    activeLayerIndexChanged = Signal(int)
    def __init__(self):
        super().__init__(); self._layers=[{"name":"Layer 1","visible":True,"locked":False,"opacity":1.0,"index":0}]; self._active=0
    @Slot()
    def addLayer(self):
        i = len(self._layers)
        self._layers.append({"name":f"Layer {i+1}","visible":True,"locked":False,"opacity":1.0,"index":i})
        self.modelReset.emit()
    @Slot(int)
    def removeLayer(self, row):
        if 0<=row<len(self._layers) and len(self._layers)>1:
            self._layers.pop(row)
            if self._active >= len(self._layers):
                self._active = len(self._layers)-1; self.activeLayerIndexChanged.emit(self._active)
            self.modelReset.emit()
    @Slot(int, int)
    def moveLayer(self, f, t):
        if 0<=f<len(self._layers) and 0<=t<len(self._layers) and f!=t:
            v = self._layers.pop(f); self._layers.insert(t, v); self.modelReset.emit()
    modelReset = Signal()
    @Slot(int, result="QVariant")
    def layerAt(self, i): return self._layers[i] if 0<=i<len(self._layers) else None
    @Property(int, notify=activeLayerIndexChanged)
    def activeLayerIndex(self): return self._active
    @Slot(int)
    def setActiveLayerIndex(self, i):
        if 0<=i<len(self._layers) and i!=self._active: self._active=i; self.activeLayerIndexChanged.emit(i)


class Timeline(QObject):
    frameCountChanged=Signal(); currentFrameChanged=Signal(); fpsChanged=Signal(); playingChanged=Signal()
    def __init__(self): super().__init__(); self._frames=24; self._cur=0; self._fps=24; self._playing=False
    def _p(self): return self._frames
    @Property(int, notify=frameCountChanged)
    def frameCount(self): return self._frames
    @frameCount.setter
    def frameCount(self, n):
        n=max(1,n);
        if n==self._frames: return
        self._frames=n
        if self._cur>=n: self.currentFrame=n-1
        self.frameCountChanged.emit()
    @Property(int, notify=currentFrameChanged)
    def currentFrame(self): return self._cur
    @currentFrame.setter
    def currentFrame(self, f):
        f=max(0,min(f,self._frames-1))
        if f==self._cur: return
        self._cur=f; self.currentFrameChanged.emit()
    @Property(int, notify=fpsChanged)
    def fps(self): return self._fps
    @fps.setter
    def fps(self, v):
        v=max(1,min(120,v))
        if v==self._fps: return
        self._fps=v; self.fpsChanged.emit()
    @Property(bool, notify=playingChanged)
    def playing(self): return self._playing
    @playing.setter
    def playing(self, p):
        if p==self._playing: return; self._playing=p; self.playingChanged.emit()
    @Slot()
    def play(self): self.playing=True
    @Slot()
    def pause(self): self.playing=False
    @Slot()
    def stop(self): self.playing=False; self.currentFrame=0


class Workspace(QObject):
    activeToolChanged = Signal(str); zoomChanged = Signal(float)
    def __init__(self):
        super().__init__()
        self.document=Doc(); self.layers=Layers(); self.timeline=Timeline()
        self.colors=Colors(); self.history=History()
        self._tool="brush"; self._zoom=100.0
        # forward reset signals where needed
    @Property(str, notify=activeToolChanged)
    def activeTool(self): return self._tool
    @activeTool.setter
    def activeTool(self, t):
        if t==self._tool: return; self._tool=t; self.activeToolChanged.emit(t)
    @Property(float, notify=zoomChanged)
    def zoom(self): return self._zoom
    @zoom.setter
    def zoom(self, z):
        z=max(6.25,min(6400.0,z))
        if abs(z-self._zoom)<1e-3: return; self._zoom=z; self.zoomChanged.emit(z)
    @Slot()
    def zoomIn(self): self.zoom=self._zoom*1.25
    @Slot()
    def zoomOut(self): self.zoom=self._zoom/1.25
    @Slot()
    def resetZoom(self): self.zoom=100.0
    @Slot()
    def zoomFit(self): self.zoom=100.0
    fileAssociationCompleted = Signal(str, bool)
    @Slot()
    def associateWithKomaFiles(self): self.fileAssociationCompleted.emit(
        "File association is only available in the real Windows build.", False)


def main():
    app = QGuiApplication(sys.argv)
    app.setApplicationName("KomaPaint")
    app.setOrganizationName("KomaPaint")
    QQuickStyle.setStyle("Basic")

    engine = QQmlApplicationEngine()
    engine.addImportPath(str(UI_DIR))

    ctx = engine.rootContext()
    ctx.setContextProperty("App", AppInfo())
    ctx.setContextProperty("Settings", Settings_())
    ctx.setContextProperty("Panels", Panels())
    ctx.setContextProperty("Workspace", Workspace())

    engine.load(QUrl.fromLocalFile(str(UI_DIR / "workspaces" / "MainWindow.qml")))
    if not engine.rootObjects():
        return -1
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
