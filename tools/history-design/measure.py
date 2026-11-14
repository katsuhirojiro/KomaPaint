#!/usr/bin/env python3
"""Offline history representation experiment; no production code is modified.

Builds the real CPU brush/store with g++, produces before/after tile buffers,
then measures candidate serialized payload sizes. Requires Python's zlib.
"""
import pathlib, struct, subprocess, tempfile, zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
TILE = 256 * 256 * 4

def u32(data):
    return struct.iter_unpack('<I', data)

def sparse_delta(before, after):
    # Header per run: uint32 start, uint32 count, followed by exact old/new
    # uint32 values per changed pixel. Includes every changed component/alpha.
    out = bytearray()
    b = [v[0] for v in u32(before)]
    a = [v[0] for v in u32(after)]
    changed = 0
    i = 0
    while i < len(b):
        if b[i] == a[i]:
            i += 1
            continue
        start = i
        while i < len(b) and b[i] != a[i]:
            i += 1
        count = i - start
        changed += count
        out += struct.pack('<II', start, count)
        for j in range(start, i):
            out += struct.pack('<II', b[j], a[j])
    # Verify exact forward and reverse reconstruction, including alpha.
    forward = b[:]
    reverse = a[:]
    pos = 0
    while pos < len(out):
        start, count = struct.unpack_from('<II', out, pos); pos += 8
        for j in range(start, start + count):
            old, new = struct.unpack_from('<II', out, pos); pos += 8
            forward[j] = new; reverse[j] = old
    assert forward == a and reverse == b
    return bytes(out), changed

def measure(data):
    count, = struct.unpack_from('<I', data)
    offset = 4
    totals = dict(changed=0, tiles=count, current=0, raw=0, spans=0, compressed=0, hybrid=0)
    for _ in range(count):
        flags = data[offset]; offset += 1
        before = data[offset:offset + TILE]; offset += TILE
        after = data[offset:offset + TILE]; offset += TILE
        assert len(before) == len(after) == TILE
        delta, changed = sparse_delta(before, after)
        # Count 16 bytes/tile framing per candidate for location, tags,
        # sizes and existence bits; excludes command indexes and allocator costs.
        compressed = [zlib.compress(image, 1) for image, present in ((before, flags & 1), (after, flags & 2)) if present]
        for image, encoded in zip([x for x, present in ((before, flags & 1), (after, flags & 2)) if present], compressed):
            assert zlib.decompress(encoded) == image
        sizes = [16 + len(delta), 16 + sum(map(len, compressed))]
        totals['changed'] += changed
        totals['current'] += 2 * TILE + 512
        totals['raw'] += 16 + TILE * (bool(flags & 1) + bool(flags & 2))
        totals['spans'] += sizes[0]
        totals['compressed'] += sizes[1]
        totals['hybrid'] += min(sizes)
    assert offset == len(data)
    return totals

def main():
    print('scenario,background,tiles,changed,current_charge,raw_payload,delta_runs,zlib1_snapshots,hybrid')
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / 'generate'
        subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'),
            str(ROOT / 'tools/history-design/generate_samples.cpp'),
            str(ROOT / 'src/drawing/BrushEngine.cpp'),
            *[str(ROOT / ('src/canvas/' + p)) for p in ('SparseTileStore.cpp', 'TileGeometry.cpp', 'DirtyTileTracker.cpp')],
            '-o', str(exe)], check=True)
        for scenario, name in enumerate(('small', 'medium', 'large')):
            for background, bg in enumerate(('empty', 'flat', 'noisy')):
                sample = tmp / 'sample.bin'
                subprocess.run([str(exe), str(scenario), str(background), str(sample)], check=True)
                m = measure(sample.read_bytes())
                print(','.join(map(str, [name, bg, *[m[x] for x in ('tiles', 'changed', 'current', 'raw', 'spans', 'compressed', 'hybrid')]])))
if __name__ == '__main__':
    main()
