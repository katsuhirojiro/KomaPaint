"""In-memory state for design evaluation, not brush or artwork storage."""
from copy import deepcopy
from pathlib import Path
from time import monotonic

from PySide6.QtCore import Property, QObject, QUrl, Signal, Slot
from PySide6.QtQml import QJSValue

from recipes import BLENDS, CONTAINERS, GRAINS, MODES, PRESENTATION, RANGES, SERIAL, SOURCES, brush, group, mark, node, starters


def walk(root):
    stack = [(root, None, 0, 0, False)]
    while stack:
        item, parent, index, depth, inherited_hidden = stack.pop()
        yield item, parent, index, depth, inherited_hidden
        kids = item['children']
        stack.extend((kids[i], item, i, depth + 1, inherited_hidden or not item['visible'])
                     for i in range(len(kids) - 1, -1, -1))


def snapshot(root, fresh=False):
    """Keep placement seeds when copying, while assigning independent row identities."""
    result = None
    parents = {}
    for item, parent, *_ in walk(root):
        copy = deepcopy({k: v for k, v in item.items() if k != 'children'})
        if fresh:
            copy['uid'] = str(next(SERIAL))
        copy['children'] = []
        parents[item['uid']] = copy
        if parent:
            parents[parent['uid']]['children'].append(copy)
        else:
            result = copy
    return result


class Ink:
    def __init__(self):
        self.strokes, self.active = (), None
        self.undo, self.redo = [], []

    def begin(self, x, y, size, color):
        self.active = dict(seed=next(SERIAL), size=size, color=color, start=monotonic(), points=[])
        self.point(x, y)

    def point(self, x, y):
        if not self.active:
            return
        pt = min(1., max(0., x)), min(1., max(0., y)), .73, monotonic() - self.active['start'], .23
        pts = self.active['points']
        if not pts or pt[:2] != pts[-1][:2]:
            pts.append(pt)

    def replace(self, strokes):
        self.undo.append(self.strokes)
        self.undo = self.undo[-60:]
        self.strokes = strokes
        self.redo.clear()

    def end(self):
        if self.active and self.active['points']:
            self.replace((*self.strokes, self.active))
        self.active = None

    def action(self, action):
        self.active = None
        if action == 'clear' and self.strokes:
            self.replace(())
        if action == 'undo' and self.undo:
            self.redo.append(self.strokes)
            self.strokes = self.undo.pop()
        if action == 'redo' and self.redo:
            self.undo.append(self.strokes)
            self.strokes = self.redo.pop()


class Draft:
    def __init__(self, root, presentation):
        self.root = snapshot(root, True)
        self.original = snapshot(self.root)
        self.selected = self.root['uid']
        self.closed = {n['uid'] for n, p, *_ in walk(self.root)
                       if p and (n['kind'] == 'brush' or n['name'] == 'Wind traces')}
        self.undo, self.redo = [], []
        self.last = (None, 0.)
        self.pad = Ink()
        self.test_size, self.ink, self.sheet = presentation
        self.compare = False
        self.dirty = False


class Studio(QObject):
    changed = Signal()
    treeChanged = Signal()
    selectionChanged = Signal()
    libraryChanged = Signal()
    padChanged = Signal()
    tipChanged = Signal()
    noticeChanged = Signal()

    def __init__(self, assets):
        super().__init__()
        self.assets = assets
        self.catalog, self.groups = starters(), {}
        # Two ordinary groups saved from the study brush. They are ingredients,
        # not brush types: any brush or group can borrow them.
        for name, label, key in (('Edge fragments', 'Soft edge fragments', 'saved-edge'),
                                 ('Wind traces', 'Wind traces', 'saved-wind')):
            for entry, _parent, _i, _d, _h in walk(self.catalog['bank']):
                if entry['name'] == name:
                    copy = snapshot(entry, True)
                    copy['name'] = label
                    self.groups[key] = copy
                    break
        self.sessions = {}
        self.names = SOURCES | GRAINS
        self.keys = {'mark': list(SOURCES), 'texture': list(GRAINS), 'mask': list(reversed(GRAINS))}
        self.key = 'pencil'
        self.work = Draft(self.catalog[self.key], PRESENTATION[self.key])
        self.sessions[self.key] = self.work
        self.tip = Ink()
        self.tip_size = 23
        self.version = 0
        self._notice = ''

    def tell(self, text):
        self._notice = text
        self.noticeChanged.emit()

    @Property(str, notify=noticeChanged)
    def notice(self):
        return self._notice

    def locate(self, uid):
        return next((row for row in walk(self.work.root) if row[0]['uid'] == uid), None)

    @staticmethod
    def brief(n):
        return {k: v for k, v in n.items() if k != 'children'} | {'count': len(n['children'])}

    @Property('QVariantMap', notify=changed)
    def whole(self):
        return self.brief(self.work.root)

    @Property('QVariantMap', notify=selectionChanged)
    def chosen(self):
        row = self.locate(self.work.selected) or self.locate(self.work.root['uid'])
        n, parent, pos, _depth, _hidden = row
        parent_info = parent or self.work.root
        return self.brief(n) | dict(parent=parent_info['name'], parentMode=parent_info['mode'],
                                   root=parent is None, up=bool(parent and pos),
                                   down=bool(parent and pos + 1 < len(parent['children'])))

    @Property('QVariantMap', notify=selectionChanged)
    def addTo(self):
        row = self.locate(self.work.selected)
        target = row[0] if row and row[0]['kind'] in CONTAINERS else ((row and row[1]) or self.work.root)
        return dict(uid=target['uid'], name=target['name'], root=target is self.work.root)

    @Property('QVariantList', notify=treeChanged)
    def rows(self):
        rows, collapsed_at = [], None
        for entry, _parent, _i, depth, hidden in walk(self.work.root):
            if not depth:
                continue
            if collapsed_at is not None and depth > collapsed_at:
                continue
            closed = entry['uid'] in self.work.closed
            collapsed_at = depth if closed else None
            rows.append({k: entry[k] for k in ('uid', 'kind', 'name', 'asset', 'mode', 'visible')} |
                        dict(depth=depth - 1, closed=closed, inheritedHidden=hidden, count=len(entry['children'])))
        return rows

    @Property('QVariantList', notify=selectionChanged)
    def childrenOf(self):
        row = self.locate(self.work.selected)
        return [self.brief(n) for n in row[0]['children']] if row else []

    @Property('QVariantMap', notify=treeChanged)
    def counts(self):
        result = dict(mark=0, brush=0, group=0, mask=0, texture=0)
        for n, parent, *_ in walk(self.work.root):
            if parent:
                result[n['kind']] += 1
        return result

    @Property('QVariantList', notify=libraryChanged)
    def brushes(self):
        return [dict(key=k, name=n['name'], section=('Study Brushes' if k in ('bank', 'understory') else
                                                    'Brushes in This Session' if k.startswith('user') else 'Starters'))
                for k, n in self.catalog.items()]

    @Property(str, notify=changed)
    def currentKey(self):
        return self.key

    @Property(bool, notify=changed)
    def canUndo(self):
        return bool(self.work.undo)

    @Property(bool, notify=changed)
    def canRedo(self):
        return bool(self.work.redo)

    @Property(bool, notify=changed)
    def dirty(self):
        return self.work.dirty

    def update(self, tree=False):
        self.version += 1
        self.changed.emit()
        self.selectionChanged.emit()
        if tree:
            self.treeChanged.emit()

    def record(self, label=None):
        prior, at = self.work.last
        now = monotonic()
        if label is None or label != prior or now - at > .7:
            self.work.undo.append((snapshot(self.work.root), self.work.selected, self.work.dirty))
            self.work.undo = self.work.undo[-72:]
        self.work.redo.clear()
        self.work.last = label, now
        self.work.dirty = True

    @Slot(str)
    def history(self, action):
        w = self.work
        source, dest = (w.undo, w.redo) if action == 'undo' else (w.redo, w.undo)
        if source:
            dest.append((w.root, w.selected, w.dirty))
            w.root, w.selected, w.dirty = source.pop()
            self.reveal(w.selected)
            w.last = (None, 0.)
            self.update(True)
            self.tell('Brush edit undone.' if action == 'undo' else 'Brush edit redone.')

    def _switch(self, key):
        if key not in self.catalog:
            return
        if key not in self.sessions:
            self.sessions[key] = Draft(self.catalog[key], PRESENTATION.get(key, (28, '#302f30', 'Blank paper')))
        self.key, self.work = key, self.sessions[key]
        self.tell('')
        self.update(True)
        self.padChanged.emit()

    @Slot(str)
    def selectBrush(self, key):
        if key != self.key:
            self._switch(key)

    def keep(self, name, open_copy=False, source=None):
        key = f'user{next(SERIAL)}'
        self.catalog[key] = snapshot(source or self.work.root, True)
        self.catalog[key]['name'] = self.clean_name(name, 'Untitled Brush')
        self.libraryChanged.emit()
        if open_copy:
            self._switch(key)
        self.tell(f'“{self.catalog[key]["name"]}” is in Sub Tools for this session.')

    @Slot(str)
    def saveCopy(self, name):
        self.keep(name)

    @Slot()
    def duplicateBrush(self):
        self.keep(self.work.root['name'] + ' copy', True)

    @Slot()
    def newBrush(self):
        self.keep('Untitled Brush', True, brush('Untitled Brush', [mark('round')], spacing=18, pressure=25))

    def reveal(self, uid):
        row = self.locate(uid)
        while row and row[1]:
            self.work.closed.discard(row[1]['uid'])
            row = self.locate(row[1]['uid'])

    @Slot(str)
    def select(self, uid):
        row = self.locate(uid)
        if row:
            self.work.selected = uid
            self.selectionChanged.emit()

    @Slot(str)
    def expand(self, uid):
        row = self.locate(uid)
        if row and row[0]['kind'] in CONTAINERS:
            if uid in self.work.closed:
                self.work.closed.remove(uid)
            else:
                self.work.closed.add(uid)
                if any(n['uid'] == self.work.selected for n, *_ in walk(row[0])):
                    self.select(uid)
            self.treeChanged.emit()

    @staticmethod
    def clean_name(value, default):
        return ' '.join(str(value).split())[:82] or default

    @Slot(str, str, 'QVariant')
    def edit(self, uid, key, value):
        row = self.locate(uid)
        if not row:
            return
        n = row[0]
        value = value.toVariant() if isinstance(value, QJSValue) else value
        if key in RANGES:
            lo, hi = RANGES[key]
            try:
                value = max(lo, min(hi, round(float(value))))
            except (ValueError, TypeError, OverflowError):
                return
        elif key == 'name':
            value = self.clean_name(value, n['name'])
        elif key in ('follow', 'invert', 'visible'):
            value = bool(value)
        elif value not in {'mode': MODES, 'blend': BLENDS, 'anchor': ('Paper', 'Stroke')}.get(key, []):
            return
        if n[key] != value:
            self.record((uid, key))
            n[key] = value
            self.update(key in ('name', 'mode', 'visible'))

    @Slot(str, str, 'QVariantList')
    def curve(self, uid, key, values):
        row = self.locate(uid)
        if not row or (values and len(values) != 5):
            return
        curves = row[0]['curves']
        self.record((uid, key))
        if values:
            curves[key] = [max(0., min(1., float(x))) for x in values]
        else:
            curves.pop(key, None)
        self.update()

    @Slot(str, result='QVariantList')
    def picks(self, uid):
        # Same choice routine and deterministic example seed as the renderer.
        from preview import selector
        row = self.locate(uid)
        if not row:
            return []
        n = row[0]
        live = [e for e in n['children'] if e['visible'] and e['kind'] in ('mark', 'brush', 'group')]
        if not live or n['mode'] == MODES[0]:
            return []
        choose = selector(n, live, 77)
        return [self.brief(choose(i)) for i in range(8)]

    def add(self, item, target_id):
        row = self.locate(target_id)
        if not row or row[0]['kind'] not in CONTAINERS:
            return
        self.record()
        target = row[0]
        target['children'].append(item)
        self.work.selected = item['uid']
        self.reveal(item['uid'])
        self.update(True)
        self.tell(f'Added {item["name"]} to {target["name"]}.')

    @Slot(str, str, result='QVariantList')
    def options(self, kind, uid=''):
        if kind in ('brush', 'group'):
            source = self.catalog if kind == 'brush' else self.groups
            return [dict(key=k, name=n['name'], count=len(n['children'])) for k, n in source.items()]
        if kind == 'move':
            row = self.locate(uid)
            excluded = {n['uid'] for n, *_ in walk(row[0])} if row else set()
            return [dict(key=n['uid'], name=('  ' * d + n['name']) if parent else 'Whole Brush',
                         count=len(n['children'])) for n, parent, _i, d, _h in walk(self.work.root)
                    if n['kind'] in CONTAINERS and n['uid'] not in excluded and not (row and n is row[1])]
        return [dict(key=k, name=self.names[k], count=0) for k in self.keys.get(kind, [])]

    @Slot(str, str, str, str)
    def use(self, kind, key, target, replace):
        if kind == 'move':
            self.move(replace, key, 'inside')
        elif kind in ('brush', 'group'):
            n = (self.catalog if kind == 'brush' else self.groups).get(key)
            if n:
                self.add(snapshot(n, True), target)
        elif key in self.keys.get(kind, []):
            row = self.locate(replace)
            if row and row[0]['kind'] == kind:
                self.record()
                row[0]['asset'] = key
                row[0]['name'] = self.names[key]
                self.update(True)
                self.tell(f'Changed source to {self.names[key]}.')
            else:
                self.add(node(kind, key, self.names[key]), target)

    @Slot(str, str)
    def newGroup(self, name, target):
        self.add(group(self.clean_name(name, 'Group of Marks')), target)

    @Slot(str, str)
    def keepGroup(self, uid, name):
        row = self.locate(uid)
        if row and row[1] and row[0]['kind'] == 'group':
            n = snapshot(row[0], True)
            n['name'] = self.clean_name(name, n['name'])
            self.groups[f'group{next(SERIAL)}'] = n
            self.libraryChanged.emit()
            self.tell(f'“{n["name"]}” is ready in Reuse a Group.')

    @Slot(str, str)
    def act(self, uid, action):
        row = self.locate(uid)
        if not row or not row[1]:
            return
        n, parent, index, *_ = row
        peers = parent['children']
        if action in ('up', 'down'):
            dest = index + (-1 if action == 'up' else 1)
            if not 0 <= dest < len(peers):
                return
            self.record()
            peers[index], peers[dest] = peers[dest], peers[index]
        elif action == 'duplicate':
            self.record()
            n = snapshot(n, True)
            n['name'] = self.clean_name(n['name'] + ' copy', n['name'])
            peers.insert(index + 1, n)
        elif action == 'wrap':
            self.record()
            n = group('Group of Marks', [n])
            peers[index] = n
        elif action == 'remove':
            self.record()
            peers.pop(index)
            n = peers[min(index, len(peers) - 1)] if peers else parent
        else:
            return
        self.work.selected = n['uid']
        self.update(True)

    @Slot(str, str, str, result=bool)
    def move(self, uid, to, where):
        src, dest = self.locate(uid), self.locate(to)
        if not src or not dest or not src[1] or any(x['uid'] == to for x, *_ in walk(src[0])):
            return False
        parent = dest[0] if where == 'inside' else dest[1]
        if not parent or parent['kind'] not in CONTAINERS:
            return False
        if where == 'inside' and parent is src[1] and src[2] + 1 == len(parent['children']):
            return False
        self.record()
        item = src[1]['children'].pop(src[2])
        peers = parent['children']
        pos = len(peers) if where == 'inside' else peers.index(dest[0]) + int(where == 'after')
        peers.insert(pos, item)
        self.work.selected = uid
        self.reveal(uid)
        self.update(True)
        self.tell(f'Moved {item["name"]} into {parent["name"]}.')
        return True

    @Property('QVariantMap', notify=padChanged)
    def padOptions(self):
        w, p = self.work, self.work.pad
        return dict(size=w.test_size, color=w.ink, sheet=w.sheet, original=w.compare,
                    count=len(p.strokes), undo=bool(p.undo), redo=bool(p.redo))

    @Property('QVariantMap', notify=tipChanged)
    def tipOptions(self):
        t = self.tip
        return dict(size=self.tip_size, ink=bool(t.strokes or t.active), ready=bool(t.strokes),
                    undo=bool(t.undo), redo=bool(t.redo))

    @Slot(str, 'QVariant')
    def padSet(self, key, value):
        value = value.toVariant() if isinstance(value, QJSValue) else value
        if key == 'size':
            self.work.test_size = max(1, min(180, int(value)))
        elif key == 'color':
            self.work.ink = str(value)
        elif key == 'sheet':
            self.work.sheet = str(value)
        elif key == 'original':
            self.work.compare = bool(value)
        self.padChanged.emit()

    @Slot(int)
    def tipSize(self, value):
        self.tip_size = max(1, min(84, value))
        self.tipChanged.emit()

    def ink(self, which):
        return self.tip if which == 'tip' else self.work.pad

    def ink_changed(self, which):
        (self.tipChanged if which == 'tip' else self.padChanged).emit()

    @Slot(str, float, float)
    def beginLine(self, which, x, y):
        self.ink(which).begin(x, y, self.tip_size if which == 'tip' else self.work.test_size,
                              '#292929' if which == 'tip' else self.work.ink)
        self.ink_changed(which)

    @Slot(str, float, float)
    def point(self, which, x, y):
        self.ink(which).point(x, y)
        self.ink_changed(which)

    @Slot(str)
    def endLine(self, which):
        self.ink(which).end()
        self.ink_changed(which)

    @Slot(str, str)
    def inkAction(self, which, action):
        self.ink(which).action(action)
        self.ink_changed(which)

    @Slot()
    def newTip(self):
        self.tip = Ink()
        self.tipChanged.emit()

    def register_asset(self, key, name, kind, target, replace):
        self.names[key] = name
        self.keys[kind].append(key)
        self.libraryChanged.emit()
        self.use(kind, key, target, replace)

    @Slot(str, str, str)
    def acceptTip(self, name, target, replace):
        if self.tip.strokes:
            key = f'drawn{next(SERIAL)}'
            self.assets.add_drawn(key, self.tip.strokes)
            self.register_asset(key, self.clean_name(name, 'Hand-drawn mark'), 'mark', target, replace)

    @Slot(QUrl, str, str, str, result=bool)
    def importImage(self, url, kind, target, replace):
        try:
            path = Path(url.toLocalFile())
            if not path.is_file():
                raise ValueError('Choose an image file on this computer.')
            name = self.clean_name(path.stem, 'Imported image')
            key = f'image{next(SERIAL)}'
            self.assets.add_file(key, path, kind)
            self.register_asset(key, name, kind, target, replace)
            return True
        except (OSError, ValueError) as exc:
            self.tell(f'Could not import that image: {exc}')
            return False
