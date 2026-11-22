"""Local visual and interaction smoke harness for the isolated review build."""
import hashlib
import json
import sys
from pathlib import Path

from PySide6.QtCore import QObject, QRect, Qt, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtGui import QImage
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest

import preview
from recipes import MODES
from studio import walk


def settle(ms=350):
    QTest.qWait(ms)


def shot(win, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    image = win.grabWindow()
    if not image.save(str(path), 'PNG'):
        raise RuntimeError(f'Could not save {path}')
    print('captured', path)
    return image


def item(win, name):
    found = [x for x in win.findChildren(QObject, name) if isinstance(x, QQuickItem)]
    # Delegates created by GridView/ListView are visual children but their QObject
    # parent chain is owned by the view, so search the rendered item tree as well.
    stack = [win.contentItem()]
    while stack:
        candidate = stack.pop()
        if not isinstance(candidate, QQuickItem):
            continue
        if candidate.objectName() == name and candidate not in found:
            found.append(candidate)
        stack.extend(candidate.childItems())
    for candidate in found:
        if candidate.isVisible() and candidate.window() is not None:
            return candidate
    return found[0] if found else None


def popup(win, name):
    found = win.findChildren(QObject, name)
    return found[0] if found else None


def click(win, name):
    target = item(win, name)
    if not isinstance(target, QQuickItem):
        raise RuntimeError(f'No clickable QML item named {name}')
    point = target.mapToScene(target.boundingRect().center()).toPoint()
    QTest.mouseClick(target.window(), Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
    settle()


def draw_tip():
    studio = preview.STUDIO
    studio.newTip()
    for x0, y0, pts in ((.18, .72, ((.26, .57), (.34, .41), (.40, .28), (.47, .17))),
                        (.58, .72, ((.50, .58), (.46, .46), (.44, .34), (.45, .22))),
                        (.22, .80, ((.34, .72), (.44, .62), (.55, .53), (.64, .49)))):
        studio.beginLine('tip', x0, y0)
        for x, y in pts:
            studio.point('tip', x, y)
        studio.endLine('tip')
    settle(120)


def choose_part(name):
    s = preview.STUDIO
    for node, parent, _index, _depth, _hidden in walk(s.work.root):
        if node['name'] == name:
            s.work.closed.discard(node['uid'])
            if parent:
                s.work.closed.discard(parent['uid'])
            s.select(node['uid'])
            s.reveal(node['uid'])
            return node['uid']
    raise RuntimeError(f'No part named {name}')


def pad_image(win):
    s = preview.STUDIO
    for candidate in win.findChildren(QQuickItem):
        if candidate.property('kind') == 'pad' and candidate.isVisible():
            p = candidate.mapToScene(candidate.position())
            full = win.grabWindow()
            return full.copy(QRect(int(p.x()), int(p.y()), int(candidate.width()), int(candidate.height())))
    raise RuntimeError('The drawing pad was not visible')


def digest(image):
    angle = image.convertToFormat(QImage.Format.Format_RGBA8888)
    return hashlib.sha256(angle.bits().tobytes()).hexdigest()


def counts(s):
    c = s.counts
    return {key: int(c[key]) for key in ('brush', 'group', 'mark', 'texture', 'mask')}


def find_node(s, name):
    for node, parent, _i, _d, _h in walk(s.work.root):
        if node['name'] == name:
            return node, parent
    return None, None


def capture(win, folder, wide=True):
    out = Path(folder)
    s = preview.STUDIO
    win.resize(1400, 900) if wide else win.resize(1024, 720)
    settle(900)
    shot(win, out / '01-graphite-workspace.png')

    s.selectBrush('bank')
    settle(650)
    shot(win, out / '02-cloud-study.png')

    win.setProperty('advancedOpen', True)
    settle(500)
    shot(win, out / '03-advanced-anatomy.png')

    choose_part('Drift & light')
    settle(450)
    shot(win, out / '04-nested-group.png')

    choose_part('Broken edge')
    click(win, 'tabResponse')
    settle(300)
    shot(win, out / '05-mark-response.png')

    click(win, 'tabReuse')
    settle(300)
    shot(win, out / '06-reuse-groups.png')

    s.selectBrush('understory')
    settle(600)
    shot(win, out / '07-same-system-other-brush.png')

    win.setProperty('advancedOpen', False)
    s.selectBrush('bank')
    s.padSet('sheet', 'Sky study')
    settle(600)
    shot(win, out / '08-cloud-brush-on-paper.png')

    choose_part('Broken edge')
    settle(250)
    click(win, 'addMark')
    shot(win, out / '09-add-a-mark-picker.png')
    QTest.keyClick(win, Qt.Key.Key_Escape)
    settle(300)

    draw_tip()
    win.setProperty('advancedOpen', False)
    settle(400)
    popup(win, 'tipRoom').open()
    settle(450)
    shot(win, out / '10-draw-a-tip.png')
    QTest.keyClick(win, Qt.Key.Key_Escape)
    settle(300)

    win.setProperty('advancedOpen', False)
    settle(250)
    win.resize(1024, 720)
    settle(700)
    shot(win, out / '11-compact-workspace.png')
    win.setProperty('advancedOpen', True)
    settle(600)
    shot(win, out / '12-compact-advanced.png')
    click(win, 'tabPart')
    settle(350)
    shot(win, out / '13-compact-this-part.png')


def walkthrough(win, folder, warnings):
    """In-app checks used by the fresh Windows runner. Writes a JSON receipt."""
    from PIL import Image
    out = Path(folder)
    shots = out / 'screenshots'
    checks = []
    def check(name, condition, detail=''):
        checks.append({'name': name, 'passed': bool(condition), 'detail': str(detail)})
        if not condition:
            print(f'CHECK FAILED: {name} {detail}', file=sys.stderr)
        return bool(condition)

    try:
        capture(win, shots, wide=False)
        s = preview.STUDIO
        win.setProperty('advancedOpen', False)
        win.resize(1024, 720)
        s.selectBrush('bank')
        s.select(s.work.root['uid'])
        settle(500)

        before = counts(s)
        click(win, 'addMark')
        picker = popup(win, 'addPicker')
        grid = item(win, 'sourceGrid')
        check('mark picker opens with every mark',
              picker is not None and picker.property('visible') and grid is not None
              and int(grid.property('count')) == len(s.options('mark', '')))
        picker.close()
        settle(250)
        s.use('mark', 'billow', s.work.root['uid'], '')
        settle(250)
        after = counts(s)
        check('add a mark', after['mark'] == before['mark'] + 1, after)

        s.select(s.work.root['uid'])
        click(win, 'addTexture')
        picker = popup(win, 'addPicker')
        grid = item(win, 'sourceGrid')
        check('texture picker opens with every texture',
              picker is not None and picker.property('visible') and grid is not None
              and int(grid.property('count')) == len(s.options('texture', '')))
        picker.close()
        settle(250)
        s.use('texture', 'mist', s.work.root['uid'], '')
        settle(250)
        after = counts(s)
        check('add a texture', after['texture'] == before['texture'] + 1, after)

        s.select(s.work.root['uid'])
        click(win, 'addBrush')
        picker = popup(win, 'addPicker')
        rows = item(win, 'optionList')
        check('brush picker opens with the whole library',
              picker is not None and picker.property('visible') and rows is not None
              and int(rows.property('count')) == len(s.options('brush', '')))
        picker.close()
        settle(250)
        s.use('brush', 'air', s.work.root['uid'], '')
        settle(250)
        after = counts(s)
        check('add another brush', after['brush'] == before['brush'] + 1, after)

        s.select(s.work.root['uid'])
        click(win, 'addGroup')
        made_group = s.chosen
        check('group of marks', made_group['kind'] == 'group' and counts(s)['group'] == before['group'] + 1)
        s.keepGroup(made_group['uid'], 'Review reuse group')
        saved = s.options('group', '')
        check('save a group for reuse', any(g['name'] == 'Review reuse group' for g in saved), saved)

        original_pad = digest(pad_image(win))
        s.selectBrush('pencil')
        settle(400)
        s.select(s.work.root['uid'])
        target = s.addTo['uid']
        reuse = next(g for g in s.options('group', '') if g['name'] == 'Review reuse group')
        group_count = counts(s)['group']
        s.use('group', reuse['key'], target, '')
        settle(450)
        check('reuse a saved group in another brush', counts(s)['group'] == group_count + 1)

        s.selectBrush('bank')
        settle(350)
        main, _ = find_node(s, 'Main masses')
        s.select(main['uid'])
        s.edit(main['uid'], 'mode', MODES[2])
        picks = s.picks(main['uid'])
        check('random selection is applied per placement', len(picks) == 8 and len({p['name'] for p in picks}) > 1,
              [p['name'] for p in picks])
        s.edit(main['uid'], 'mode', MODES[3])
        check('weighted selection lists the same parts', len(s.picks(main['uid'])) == 8)
        s.edit(main['uid'], 'mode', MODES[0])
        settle(400)

        broken, _ = find_node(s, 'Broken edge')
        s.select(broken['uid'])
        before_pad = pad_image(win)
        s.edit(broken['uid'], 'size', 176)
        settle(500)
        changed_pad = pad_image(win)
        check('individual mark edit changes the pad', digest(before_pad) != digest(changed_pad))
        s.history('undo')
        settle(500)
        check('undo restores the pad', digest(pad_image(win)) == digest(before_pad))
        s.history('redo')
        settle(350)
        s.edit(broken['uid'], 'visible', False)
        settle(350)
        check('hiding a mark changes the pad', digest(pad_image(win)) != digest(changed_pad))
        s.edit(broken['uid'], 'visible', True)

        drift, _ = find_node(s, 'Drift & light')
        s.select(drift['uid'])
        blend_before = pad_image(win)
        s.edit(drift['uid'], 'blend', 'Multiply')
        settle(450)
        check('group blend changes the pad', digest(pad_image(win)) != digest(blend_before))
        s.edit(drift['uid'], 'blend', 'Normal')
        mask_before = counts(s)['mask']
        s.use('mask', 'flecks', s.work.root['uid'], '')
        settle(500)
        check('add a mask', counts(s)['mask'] == mask_before + 1)

        draw_tip()
        mark_before = counts(s)['mark']
        s.acceptTip('Review drawn tip', s.work.root['uid'], '')
        settle(450)
        check('draw and add a tip', counts(s)['mark'] == mark_before + 1)

        art = out / 'import-source.png'
        image = Image.new('RGBA', (96, 96), (0, 0, 0, 0))
        for y in range(12, 84):
            for x in range(12, 84):
                if (x - 48) ** 2 + (y - 48) ** 2 < 1200:
                    image.putpixel((x, y), (24, 24, 24, 245))
        image.save(art)
        mark_before = counts(s)['mark']
        imported = s.importImage(QUrl.fromLocalFile(str(art)), 'mark', s.work.root['uid'], '')
        settle(350)
        check('import an image mark', imported and counts(s)['mark'] == mark_before + 1)

        library_before = len(s.brushes)
        s.saveCopy('Review copy')
        check('save a brush copy', len(s.brushes) == library_before + 1)

        s.padSet('original', True)
        settle(400)
        original_view = digest(pad_image(win))
        s.padSet('original', False)
        settle(300)
        check('original comparison is available', original_view != digest(pad_image(win)))

        cap_before = counts(s)['mark']
        for _ in range(24):
            s.use('mark', 'spatter', s.work.root['uid'], '')
        settle(700)
        check('twenty-four more marks are accepted', counts(s)['mark'] == cap_before + 24, counts(s))

        full = win.grabWindow()
        full.save(str(out / 'final-state.png'))
        images = sorted((shots).glob('*.png'))
        sizes = []
        for path in images:
            with Image.open(path) as png:
                sizes.append([path.name, png.width, png.height])
        check('review screenshots captured', len(images) >= 12, sizes)
        check('screenshots are full size', all(w >= 960 and h >= 690 for _n, w, h in sizes), sizes)
        check('no Qt warnings', not warnings, warnings)
    except Exception as exc:
        import traceback
        traceback.print_exc()
        check('walkthrough completed without an exception', False, repr(exc))

    passed = all(c['passed'] for c in checks)
    report = {
        'passed': passed,
        'frozen': bool(getattr(sys, 'frozen', False)),
        'platform': sys.platform,
        'qtPlatform': QGuiApplication.platformName(),
        'architecture': 64 if sys.maxsize > 2 ** 32 else 32,
        'checks': checks,
        'qtWarnings': list(warnings),
        'screenshots': [p.name for p in sorted((out / 'screenshots').glob('*.png'))],
    }
    out.mkdir(parents=True, exist_ok=True)
    (out / 'qt-walkthrough.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'passed': passed, 'checks': len(checks)}, indent=2))
    return passed
