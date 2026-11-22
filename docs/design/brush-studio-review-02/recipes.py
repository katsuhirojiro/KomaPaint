"""Ingredients for the *review* exercises. No subject-specific brush types."""
from itertools import count

SERIAL = count(1)
MODES = ['Paint Together', 'In Order', 'Random Selection', 'Weighted Selection']
BLENDS = ['Normal', 'Multiply', 'Screen', 'Soft Light', 'Cut Away']
CONTAINERS = {'brush', 'group'}
SOURCES = {
    'graphite': 'Graphite point', 'nib': 'Round nib', 'bristle': 'Dry bristles',
    'air': 'Soft round', 'round': 'Solid round', 'billow': 'Billow',
    'broken': 'Broken edge', 'wisp': 'Torn wisp', 'rake': 'Raked edge',
    'ribbon': 'Ribbon', 'veil': 'Frayed veil', 'spatter': 'Fine dots',
    'leaf': 'Willow leaf', 'chip': 'Shale chip', 'hatch': 'Fine strands',
}
GRAINS = {'paper': 'Paper tooth', 'cotton': 'Cotton tooth', 'mist': 'Fine mist',
          'flecks': 'Flecks', 'linen': 'Linen weave', 'breakup': 'Edge breakup'}
NAMES = SOURCES | GRAINS
RANGES = {
    'size': (5, 300), 'opacity': (0, 100), 'flow': (0, 100), 'spacing': (4, 300),
    'scatter': (0, 150), 'offset': (-100, 100), 'angle': (-180, 180), 'aspect': (10, 200),
    'sizeVar': (0, 100), 'opacityVar': (0, 100), 'angleVar': (0, 180), 'weight': (1, 20),
    'tone': (-65, 65), 'strength': (0, 100), 'scale': (12, 300), 'pressure': (0, 100),
}


def node(kind, asset='', name='', children=(), **settings):
    ident = next(SERIAL)
    return dict(uid=str(ident), seed=ident, kind=kind, asset=asset,
                name=name or NAMES.get(asset, 'Group of Marks'), children=list(children), visible=True,
                size=100, opacity=100, flow=100, spacing=100, scatter=0, offset=0, angle=0,
                aspect=100, follow=False, sizeVar=0, opacityVar=0, angleVar=0, weight=4,
                mode=MODES[0], blend='Normal', tone=0, strength=45, scale=100,
                anchor='Paper', invert=False, pressure=0, curves={}, **{}) | settings


def mark(asset, **settings):
    return node('mark', asset, **settings)


def grain(asset, **settings):
    return node('texture', asset, **settings)


def group(name, children=(), **settings):
    return node('group', name=name, children=children, **settings)


def brush(name, children=(), **settings):
    return node('brush', name=name, children=children, **settings)


def starters():
    air = brush('Soft Airbrush', [mark('air')], flow=34, spacing=14, pressure=55)
    spatter = brush('Fine Spatter', [mark('spatter', angleVar=170, sizeVar=29)], flow=72, spacing=62, scatter=44)
    bank = brush('Cloudbank', [
        group('Main masses', [
            mark('billow', weight=8, size=113, angleVar=13),
            mark('broken', weight=5, size=93, angle=-14, opacity=91),
            group('Edge fragments', [mark('wisp', angle=-22, aspect=74), mark('rake', angle=9, size=75)],
                  mode=MODES[2], weight=4, size=132, opacity=86, scatter=28, sizeVar=24, angleVar=16, tone=11),
            grain('cotton', strength=33, scale=70),
        ], mode=MODES[3], spacing=27, flow=90, scatter=15, sizeVar=25),
        group('Drift & light', [
            air | dict(size=164, flow=29, opacity=84, offset=15, tone=-16, blend='Multiply'),
            spatter | dict(size=61, spacing=68, opacity=70, tone=24, blend='Screen', offset=-18),
            grain('mist', strength=43, scale=145, anchor='Stroke'),
            node('mask', 'breakup', strength=55, scale=129, anchor='Stroke'),
        ], opacity=88, scatter=18, blend='Normal'),
        group('Wind traces', [mark('ribbon', angle=-8), mark('veil', angle=-23, aspect=78, sizeVar=18),
                              grain('flecks', strength=41, scale=54)],
              mode=MODES[1], opacity=50, spacing=77, size=72, offset=-32, follow=True, blend='Soft Light'),
    ], opacity=95, pressure=18, spacing=32)
    # An unrelated exercise arranges the very same kinds of ingredients.
    willow = brush('Understory', [
        group('Leaves', [mark('leaf', weight=8, follow=True), mark('rake', weight=3), mark('chip', size=66)],
              mode=MODES[3], spacing=47, scatter=62, sizeVar=36, angleVar=43),
        group('Twigs and dust', [mark('hatch', size=66, angle=20), mark('spatter', size=49, opacity=65)],
              mode=MODES[2], spacing=71, scatter=83, opacity=64, blend='Multiply'),
        grain('linen', strength=19, scale=77),
    ], pressure=27, spacing=45)
    return {
        'pencil': brush('Graphite Pencil', [mark('graphite'), grain('paper', strength=34)],
                        opacity=77, flow=57, spacing=12, pressure=72, follow=True),
        'pen': brush('Sumi Pen', [mark('nib')], spacing=9, pressure=90, flow=91),
        'bristle': brush('Dry Bristle', [mark('bristle', aspect=86), grain('linen', strength=26)],
                         spacing=18, follow=True, flow=76, pressure=45),
        'air': air, 'spatter': spatter, 'bank': bank, 'understory': willow,
    }


PRESENTATION = {
    'pencil': (16, '#2f2e2e', 'Graphite sheet'), 'pen': (15, '#272728', 'Stroke tests'),
    'bristle': (33, '#443839', 'Stroke tests'), 'air': (54, '#394754', 'Stroke tests'),
    'spatter': (39, '#3b3842', 'Stroke tests'), 'bank': (76, '#fff1dd', 'Sky study'),
    'understory': (49, '#4b5240', 'Stroke tests'),
}
