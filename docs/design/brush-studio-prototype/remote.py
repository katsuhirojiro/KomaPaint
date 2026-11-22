"""Optional, single-session review mirror. The browser never renders Brush Studio UI."""
import asyncio
import json
from pathlib import Path
from threading import Thread
from urllib.parse import urlsplit

from aiohttp import WSMsgType, web
from PySide6.QtCore import QByteArray, QBuffer, QCoreApplication, QEvent, QIODevice, QObject, QPoint, QPointF, Qt, QTimer, Signal, Slot
from PySide6.QtGui import QGuiApplication, QKeyEvent, QMouseEvent, QWheelEvent
from PySide6.QtTest import QTest

MAX_IMAGE = 12 * 1024 * 1024
KEYS = {"Escape": Qt.Key.Key_Escape, "Enter": Qt.Key.Key_Return, "Backspace": Qt.Key.Key_Backspace,
        "Delete": Qt.Key.Key_Delete, "Tab": Qt.Key.Key_Tab, "Home": Qt.Key.Key_Home, "End": Qt.Key.Key_End,
        "PageUp": Qt.Key.Key_PageUp, "PageDown": Qt.Key.Key_PageDown, "ArrowUp": Qt.Key.Key_Up,
        "ArrowDown": Qt.Key.Key_Down, "ArrowLeft": Qt.Key.Key_Left, "ArrowRight": Qt.Key.Key_Right}


class Mirror(QObject):
    command = Signal(object)
    upload = Signal(bytes, str)

    def __init__(self, window, studio, host, port):
        super().__init__()
        self.window = window
        self.studio = studio
        self.host, self.port = host, port
        self.loop = None
        self.clients = {}
        self.frame = None
        self.image = None
        self.touch = QTest.createTouchDevice()
        self.cursor = "default"
        self.command.connect(self.send_to_qt)
        self.upload.connect(self.load_upload)
        studio.uploadRequested.connect(self.ask_for_image)
        self.thread = Thread(target=self.serve, name="Qt review mirror", daemon=True)
        self.thread.start()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.capture)
        self.timer.start(42)

    def serve(self):
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)
        server = web.Application(client_max_size=MAX_IMAGE + 65536)
        base = Path(__file__).resolve().parent

        async def index(request):
            return web.Response(text=(base / "review.html").read_text(encoding="utf-8"), content_type="text/html",
                                headers={"Cache-Control": "no-store"})

        server.router.add_get("/", index)
        server.router.add_get("/qt", self.socket)
        web.run_app(server, host=self.host, port=self.port, handle_signals=False, loop=self.loop, access_log=None,
                    print=lambda message: print(f"Qt review mirror: {message}", flush=True))

    async def socket(self, request):
        origin = urlsplit(request.headers.get("Origin", "")).hostname
        forwarded = request.headers.get("X-Forwarded-Host", request.host).split(",")[0].strip()
        expected = urlsplit("//" + forwarded).hostname
        if origin != expected and not (origin and origin.endswith(".e2b.app") and self.host == "0.0.0.0"):
            raise web.HTTPForbidden(text="Open the Qt review from its own preview page.")
        socket = web.WebSocketResponse(heartbeat=24, compress=False, max_msg_size=MAX_IMAGE + 65536)
        await socket.prepare(request)
        frames = asyncio.Queue(maxsize=1)
        self.clients[socket] = frames
        if self.frame:
            frames.put_nowait(self.frame)
        await socket.send_json({"t": "ready"})
        await socket.send_json({"t": "cursor", "value": self.cursor})

        async def stream():
            while not socket.closed:
                await socket.send_bytes(await frames.get())

        task = asyncio.create_task(stream())
        filename = None
        try:
            async for m in socket:
                if m.type == WSMsgType.TEXT and len(m.data) < 40000:
                    try:
                        event = json.loads(m.data)
                        if not isinstance(event, dict):
                            continue
                        kind = event.get("t")
                        if kind == "file":
                            filename = str(event.get("name", "image.png"))[:250].replace("\\", "/").split("/")[-1]
                        elif kind in ("size", "pointer", "wheel", "key", "paste", "cancelFile"):
                            self.command.emit(event)
                    except (ValueError, TypeError):
                        continue
                elif m.type == WSMsgType.BINARY and filename and len(m.data) <= MAX_IMAGE:
                    self.upload.emit(bytes(m.data), filename)
                    filename = None
        finally:
            self.clients.pop(socket, None)
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
        return socket

    def ask_for_image(self, _mode):
        if self.loop:
            self.loop.call_soon_threadsafe(lambda: asyncio.create_task(self.broadcast({"t": "chooseFile"})))

    async def broadcast(self, message):
        for client in tuple(self.clients):
            if not client.closed:
                try:
                    await client.send_json(message)
                except ConnectionError:
                    continue

    @Slot(bytes, str)
    def load_upload(self, payload, filename):
        if self.studio.pending_image:
            self.studio.import_image(payload, filename, self.studio.pending_image)

    @Slot()
    def capture(self):
        if not self.clients:
            return
        cursor = {Qt.CrossCursor: "crosshair", Qt.IBeamCursor: "text", Qt.SizeVerCursor: "ns-resize",
                  Qt.PointingHandCursor: "pointer", Qt.ClosedHandCursor: "grabbing"}.get(self.window.cursor().shape(), "default")
        if cursor != self.cursor:
            self.cursor = cursor
            self.loop.call_soon_threadsafe(lambda: asyncio.create_task(self.broadcast({"t": "cursor", "value": cursor})))
        image = self.window.grabWindow()
        if image.isNull() or (self.image is not None and image == self.image):
            return
        self.image = image
        raw = QByteArray()
        buf = QBuffer(raw)
        buf.open(QIODevice.OpenModeFlag.WriteOnly)
        if image.save(buf, "JPEG", 94):
            self.frame = bytes(raw)
            self.loop.call_soon_threadsafe(self.enqueue_frame, self.frame)

    def enqueue_frame(self, frame):
        for queue in self.clients.values():
            if queue.full():
                queue.get_nowait()
            queue.put_nowait(frame)

    def stop(self):
        self.timer.stop()

    @Slot(object)
    def send_to_qt(self, e):
        def number(key, fallback=0, low=-10000, high=10000):
            x = float(e.get(key, fallback))
            return max(low, min(high, x))

        def modifiers():
            mods = Qt.KeyboardModifier.NoModifier
            for field, flag in (("shift", Qt.ShiftModifier), ("ctrl", Qt.ControlModifier),
                                ("alt", Qt.AltModifier), ("meta", Qt.MetaModifier)):
                if e.get(field):
                    mods |= flag
            return mods

        try:
            kind = e["t"]
            w = self.window
            if kind == "size":
                w.resize(int(number("w", 1440, 430, 2048)), int(number("h", 900, 540, 1440)))
            elif kind == "cancelFile":
                self.studio.cancelUpload()
            elif kind == "paste":
                QGuiApplication.clipboard().setText(str(e.get("text", ""))[:8000])
                QCoreApplication.sendEvent(w, QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_V, Qt.ControlModifier, "v"))
                QCoreApplication.sendEvent(w, QKeyEvent(QEvent.Type.KeyRelease, Qt.Key.Key_V, Qt.ControlModifier, "v"))
            elif kind == "key":
                key = str(e.get("key", ""))
                qt_key = KEYS.get(key)
                if qt_key is None and len(key) == 1:
                    qt_key = ord(key.upper()) if key.isascii() else 0
                if qt_key is not None:
                    text = key if len(key) == 1 else ""
                    event_type = QEvent.Type.KeyPress if e.get("down") else QEvent.Type.KeyRelease
                    QCoreApplication.sendEvent(w, QKeyEvent(event_type, qt_key, modifiers(), text, bool(e.get("repeat"))))
            elif kind in ("pointer", "wheel"):
                pt = QPointF(w.width() * number("x", low=-1, high=2), w.height() * number("y", low=-1, high=2))
                if kind == "wheel":
                    dx = int(number("dx", low=-420, high=420))
                    dy = int(number("dy", low=-420, high=420))
                    delta = QPoint(-dx, -dy)
                    event = QWheelEvent(pt, pt, delta, delta, Qt.NoButton, modifiers(), Qt.ScrollPhase.NoScrollPhase, False)
                else:
                    phase = e.get("phase")
                    if e.get("device") == "touch":
                        method = {"down": "press", "move": "move", "up": "release"}.get(phase)
                        if method:
                            touch = QTest.touchEvent(w, self.touch, False)
                            getattr(touch, method)(0, pt.toPoint(), w).commit()
                        return
                    button = {0: Qt.LeftButton, 1: Qt.MiddleButton, 2: Qt.RightButton}.get(e.get("button"), Qt.NoButton)
                    qt_buttons = Qt.NoButton
                    buttons = int(number("buttons", low=0, high=7))
                    for mask, flag in ((1, Qt.LeftButton), (2, Qt.RightButton), (4, Qt.MiddleButton)):
                        if buttons & mask:
                            qt_buttons |= flag
                    phases = {"down": QEvent.Type.MouseButtonPress, "up": QEvent.Type.MouseButtonRelease,
                              "move": QEvent.Type.MouseMove, "double": QEvent.Type.MouseButtonDblClick}
                    event = QMouseEvent(phases[phase], pt, pt, pt, Qt.NoButton if phase == "move" else button,
                                        qt_buttons, modifiers())
                QCoreApplication.sendEvent(w, event)
        except (ValueError, TypeError, KeyError, OverflowError):
            pass
