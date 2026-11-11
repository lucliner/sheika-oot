"""The swap itself: an imported model replaces the geometry of one of the game's character models, as a mod."""
import os
import re

import numpy as np

from containers import Romfs
from bfres import Bfres, fmt_info
from bfres_write import patch_model, SlotMesh
from skeleton import world_matrices
import autoskin
import importers


# shapes whose materials need special shading (alpha, hair, face and eye effects) make poor hosts for an opaque body
AVOID = re.compile(r'eye|lash|brow|hair|teeth|tongue|lip|face|alpha|softedge|shadow|feather', re.I)


def pick_slots(f, count, max_matrix, need_float_uv=False):
    """Shapes of the target that can carry an imported mesh, best first: plain opaque materials with a full vertex layout,
    the most skin influences, bone indices wide enough for the bones auto-skinning uses, and float UVs when the mesh tiles."""
    ranked = []
    for s in f.shapes:
        if not s.vertex_count or s.skin_count < 4 or AVOID.search(s.name + ' ' + f.materials[s.material]):
            continue
        names = {a.name: a for a in s.attrs}
        if not all(n in names for n in ('_p0', '_n0', '_t0', '_u0', '_i0', '_w0')):
            continue
        if 8 * int(fmt_info(names['_i0'].fmt)[0][1]) == 8 and max_matrix > 255:
            continue
        cap = sum(fmt_info(a.fmt)[1] for a in s.attrs if a.name in ('_i0', '_i1'))
        # (a slot with float UVs is preferred when the mesh tiles; any other has its UV attribute redeclared as half-float)
        ranked.append((need_float_uv and fmt_info(names['_u0'].fmt)[2] != 'float', -cap, -s.vertex_count, s.index))
    ranked.sort()
    return [r[3] for r in ranked[:count]]


def build_slots(f, W, meshes, scale=None, rotate_y=0.0, offset=(0, 0, 0), slots=None, log=print):
    top = max(float(f.read_attr(s, '_p0')[:, 1].max()) for s in f.shapes if s.vertex_count and s.skin_count > 1)
    sc, R, t = autoskin.fit(meshes, f, W, scale, rotate_y, offset, target_height=top)
    log('fit: scale %.4f, rotate_y %.0f deg, offset %s' % (sc, rotate_y, np.round(t, 3)))
    max_matrix = max(b.smooth for b in autoskin.core_bones(f))
    need_float = any(m.uv is not None and (m.uv.min() < -1e-4 or m.uv.max() > 1 + 1e-4) for m in meshes)
    chosen = slots or pick_slots(f, len(meshes), max_matrix, need_float)
    # the biggest part takes the best slot
    meshes = sorted(meshes, key=lambda m: -len(m.pos))
    if len(chosen) < len(meshes):
        raise ValueError('the target has %d usable shape slots for %d imported parts; merge parts or pick another target'
                         % (len(chosen), len(meshes)))
    out = {}
    for m, si in zip(meshes, chosen):
        pos = (m.pos.astype(np.float64) @ R.T) * sc + t
        nrm = None if m.normal is None else m.normal.astype(np.float64) @ R.T
        uv = None if m.uv is None else np.c_[m.uv[:, 0], 1.0 - m.uv[:, 1]]
        j, w = autoskin.autoskin(f, W, pos, m.tris)
        out[si] = SlotMesh(pos.astype(np.float32), m.tris, j, w, nrm, uv, texture=m.texture, uv_float=need_float)
        log('  %-28s -> shape %2d %-40s (%d verts, %d tris)' % (m.name[:28], si, f.shapes[si].name[:40], len(pos), len(m.tris)))
    return out


def swap(romfs_dir, target_rel, input_path, mod_dir, scale=None, rotate_y=0.0, offset=(0, 0, 0), slots=None, textures=True, log=print):
    """Write mod_dir/romfs/<target_rel> with the imported model in place of the target's geometry. Returns the new
    model's Bfres (read back from the written file)."""
    rom = Romfs(romfs_dir)
    src = rom.read(target_rel)
    f = Bfres(src)
    W = world_matrices(f)
    meshes = importers.load_obj(input_path)
    sl = build_slots(f, W, meshes, scale, rotate_y, offset, slots, log)
    new = patch_model(src, sl)
    packed = rom.compress(new)
    dest = os.path.join(mod_dir, 'romfs', *target_rel.split('/'))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, 'wb') as fh:
        fh.write(packed)
    back = Bfres(rom.decompress(open(dest, 'rb').read()))
    assert len(back.shapes) == len(f.shapes) and back.bones and [b.name for b in back.bones] == [b.name for b in f.bones]
    log('wrote %s (%d bytes; the original is %d)' % (dest, len(packed), os.path.getsize(rom.path(target_rel))))
    if textures:
        import textures as tex
        for rel, data in tex.build(rom, f, {si: s.texture for si, s in sl.items()}, log).items():
            p = os.path.join(mod_dir, 'romfs', *rel.split('/'))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as fh:
                fh.write(data)
    return back
