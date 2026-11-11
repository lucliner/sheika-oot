"""tools/assetswap's readers on made-up data (no game files): the attribute format table, the 10_10_10_2 decoder, vertex
attribute decoding and the SARC reader. The reader itself was checked against the real models by hand (see
tools/assetswap); this keeps the pieces from drifting."""
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'assetswap'))
import bfres  # noqa: E402
import autoskin  # noqa: E402
import bfres_write  # noqa: E402
import importers  # noqa: E402
from containers import sarc_read  # noqa: E402

RUN_ALL_MODULES = ["numpy", "zstandard"]


def check(cond, what):
    if not cond:
        raise SystemExit('FAIL: ' + what)


def test_formats():
    # the formats the game's models use, with the component width and count they were measured to have
    want = {0x1805: ('f4', 3, 'float'), 0x0e02: ('p1010102', 4, 'snorm'), 0x0b01: ('u1', 4, 'unorm'),
            0x1205: ('f2', 2, 'float'), 0x1201: ('u2', 2, 'unorm'), 0x1503: ('u2', 4, 'uint'),
            0x0903: ('u1', 2, 'uint'), 0x0a03: ('u2', 1, 'uint'), 0x0201: ('u1', 1, 'unorm'),
            0x1505: ('f2', 4, 'float'), 0x0b02: ('i1', 4, 'snorm')}
    for fmt, w in want.items():
        check(bfres.fmt_info(fmt) == w, 'fmt_info(0x%04x) = %r, want %r' % (fmt, bfres.fmt_info(fmt), w))
    check(bfres.fmt_info(0x7f01) is None, 'unknown type must give None')


def make(attrs, rows, stride):
    """A Bfres shell holding one shape's vertex buffer, for read_attr."""
    f = bfres.Bfres.__new__(bfres.Bfres)
    f.b = b''.join(rows)
    s = bfres.Shape(0, 't', 0, 0, 0, 0, len(rows), attrs, stride, 0, 0, 0, 0, 0)
    return f, s


def test_decode():
    def pack(x, y, z, w):
        q = lambda v: int(round(v * 511)) & 0x3ff
        return struct.pack('<I', q(x) | q(y) << 10 | q(z) << 20 | (w & 3) << 30)
    rows = [struct.pack('<3f', 1, 2, 3) + pack(0, 1, -1, 1) + struct.pack('<4B', 255, 0, 51, 255) + struct.pack('<4H', 1, 2, 3, 280),
            struct.pack('<3f', -4, 5, 6) + pack(0.5, -0.5, 0, 0) + struct.pack('<4B', 0, 0, 0, 0) + struct.pack('<4H', 0, 0, 0, 0)]
    attrs = [bfres.Attr('_p0', 0x1805, 0, 0), bfres.Attr('_n0', 0x0e02, 12, 0),
             bfres.Attr('_w0', 0x0b01, 16, 0), bfres.Attr('_i0', 0x1503, 20, 0)]
    f, s = make(attrs, rows, 28)
    p = f.read_attr(s, '_p0')
    check(np.allclose(p, [[1, 2, 3], [-4, 5, 6]]), 'position decode')
    n = f.read_attr(s, '_n0')
    check(np.allclose(n[0, :3], [0, 1, -1], atol=1e-6) and n[0, 3] == 1, 'snorm 10_10_10_2 decode: %r' % n[0])
    check(np.allclose(n[1, :3], [0.5, -0.5, 0], atol=2e-3), '10_10_10_2 second vertex: %r' % n[1])
    w = f.read_attr(s, '_w0')
    check(np.allclose(w[0], [1, 0, 0.2, 1]), 'unorm8 weights: %r' % w[0])
    i = f.read_attr(s, '_i0')
    check(i.dtype.kind == 'u' and i[0].tolist() == [1, 2, 3, 280], '16-bit bone indices (a 280 does not fit in 8 bits): %r' % i[0])
    check(f.read_attr(s, '_zz') is None, 'a missing attribute reads as None')


def test_sarc():
    names = [b'a.bin', b'b.bin']
    datas = [b'1234', b'56']
    sfnt = b'SFNT' + struct.pack('<HH', 8, 0)
    offs = []
    for n in names:
        offs.append(len(sfnt) - 8)
        sfnt += n + b'\0' * (4 - len(n) % 4)
    sfat = b'SFAT' + struct.pack('<HHI', 0x0c, 2, 0x65)
    pos = 0
    for k, (n, d) in enumerate(zip(names, datas)):
        sfat += struct.pack('<IIII', k, 0x01000000 | (offs[k] // 4), pos, pos + len(d))
        pos += len(d)
    data_off = 0x14 + len(sfat) + len(sfnt)
    hdr = b'SARC' + struct.pack('<HHIIHH', 0x14, 0xfeff, data_off + pos, data_off, 0x100, 0)
    blob = hdr + sfat + sfnt + b''.join(datas)
    check(sarc_read(blob) == {'a.bin': b'1234', 'b.bin': b'56'}, 'sarc_read: %r' % sarc_read(blob))


def test_encode_roundtrip():
    rng = np.random.default_rng(1)
    n = 50
    cases = {0x1805: rng.normal(size=(n, 3)).astype(np.float32), 0x1205: rng.uniform(-3, 3, (n, 2)),
             0x1201: rng.uniform(0, 1, (n, 2)), 0x0b01: rng.uniform(0, 1, (n, 4)),
             0x1503: rng.integers(0, 300, (n, 4)), 0x0b03: rng.integers(0, 255, (n, 4)), 0x0a03: rng.integers(0, 300, (n, 1))}
    for fmt, v in cases.items():
        data, size = bfres_write.encode_attr(fmt, v)
        f, s = make([bfres.Attr('_p0', fmt, 0, 0)], [data[k * size:(k + 1) * size] for k in range(n)], size)
        got = f.read_attr(s, '_p0')
        tol = {0x1205: 2e-3, 0x1201: 1e-4, 0x0b01: 3e-3}.get(fmt, 1e-6)
        check(np.allclose(got, np.asarray(v).reshape(n, -1), atol=tol * max(1, np.abs(v).max())), 'encode/decode 0x%04x' % fmt)
    nrm = rng.normal(size=(n, 3)); nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
    data, size = bfres_write.encode_attr(0x0e02, np.c_[nrm, np.ones(n)])
    f, s = make([bfres.Attr('_n0', 0x0e02, 0, 0)], [data[k * 4:(k + 1) * 4] for k in range(n)], 4)
    g = f.read_attr(s, '_n0')
    check(np.abs(g[:, :3] - nrm).max() < 3e-3 and (g[:, 3] == 1).all(), '10_10_10_2 encode/decode')
    try:
        bfres_write.encode_attr(0x0b03, np.full((1, 4), 300))
        check(False, 'a bone index over 255 must not fit an 8-bit index attribute')
    except ValueError:
        pass
    w = bfres_write._quantize_weights(np.array([[0.333, 0.333, 0.334, 0.0]]), 4)
    check(abs(w.sum() - 1) < 1e-9 and np.allclose(w * 255, np.rint(w * 255)), 'weights quantise to exact bytes summing to 255')


def test_obj_and_autoskin():
    import tempfile
    nl = chr(10)
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, 'm.mtl'), 'w').write(nl.join(['newmtl a', 'map_Kd t.png', '']))
        open(os.path.join(d, 'm.obj'), 'w').write(nl.join([
            'mtllib m.mtl', 'o q', 'usemtl a', 'v 0 0 0', 'v 1 0 0', 'v 1 1 0', 'v 0 1 0',
            'vt 0 0', 'vt 1 0', 'vt 1 1', 'vt 0 1', 'f 1/1 2/2 3/3 4/4', 'f -4/1 -3/2 -2/3', '']))
        ms = importers.load_obj(os.path.join(d, 'm.obj'))
        check(len(ms) == 1 and len(ms[0].tris) == 3 and ms[0].material == 'a' and ms[0].texture.endswith('t.png'), 'OBJ: quad fan, negative index, mtl')
        check(ms[0].uv is not None and len(ms[0].pos) == 4, 'OBJ: corners shared by position/uv pairs stay merged')

    # a made-up skeleton: a thigh and a shin stacked on y; points near each take that bone
    class B:
        def __init__(s, i, n, p, sm): s.index, s.name, s.parent, s.smooth = i, n, p, sm
    class F:
        bones = [B(0, 'Waist', -1, 7), B(1, 'Leg_1_L', 0, 8), B(2, 'Leg_2_L', 1, 9), B(3, 'Ankle_L', 2, 10)]
    W = [np.eye(4) for _ in range(4)]
    for i, y in enumerate([1.0, 1.0, 0.5, 0.05]):
        W[i][1, 3] = y
    pts = np.array([[0, 0.9, 0], [0, 0.55, 0], [0, 0.45, 0], [0, 0.02, 0.1]])
    tris = np.array([[0, 1, 2], [1, 2, 3]])
    j, w = autoskin.autoskin(F, W, pts, tris, smooth=0)
    check(j.shape == (4, 4) and np.allclose(w.sum(1), 1), 'autoskin weights sum to 1')
    check(j[0, 0] in (7, 8) and j[3, 0] == 10, 'autoskin: the strongest bone is the nearest: %r' % j[:, 0])
    ms2 = [importers.Mesh('m', 'a', np.array([[0, 0, 0], [0, 2, 0], [1, 1, 0]], np.float32), np.array([[0, 1, 2]]))]
    sc, R, t = autoskin.fit(ms2, F, W, target_height=1.0)
    check(abs(sc - 0.5) < 1e-9 and abs(t[1]) < 1e-9, 'fit: scale to the target height, feet on the ground')


if __name__ == '__main__':
    for t in (test_formats, test_decode, test_sarc, test_encode_roundtrip, test_obj_and_autoskin):
        t()
    print('assetswap reader checks passed')
