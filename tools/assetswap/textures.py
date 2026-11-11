"""Textures for a swap: the material of each shape that carries an imported part names its textures (`<Material>_<Model>_Alb`,
`_Nrm`, `_Rgh`, `_AO`, ...), one file each in the game's `Tex` folder. A mod replaces those files: the albedo with the
imported model's image, the other maps with neutral values (a flat normal map, mid roughness, no occlusion, no metal)."""
import os
import struct

import numpy as np
from PIL import Image

import bntx
import bntx_flat

# what each kind of map holds where the shader reads it (r, g, b, a): the neutral value of the map
NEUTRAL = {
    'Nrm': (0.5, 0.5, 1.0, 1.0),     # a flat normal map (the donors keep x and y in their first two channels)
    'Rgh': (0.75,) * 4,              # mostly matte
    'AO': (1.0,) * 4,                # nothing occluded
    'Mtl': (0.0,) * 4,               # not metal
    'Emm': (0.0,) * 4,               # not glowing
}


def material_textures(f, mat_index):
    """Texture names of a material of a Bfres (FMAT: name array at +0x20, texture count at +0xa2)."""
    import struct as st
    b = f.b
    mat_arr = st.unpack_from('<Q', b, f.model_off + 0x38)[0]
    o = mat_arr + mat_index * 0xb0
    arr = st.unpack_from('<Q', b, o + 0x20)[0]
    n = b[o + 0xa2]
    return [f._str(st.unpack_from('<Q', b, arr + i * 8)[0]) for i in range(n)]


def kind_of(name):
    suffix = name.rsplit('_', 1)[-1]
    return suffix if suffix in ('Alb', 'Nrm', 'Rgh', 'AO', 'Mtl', 'Emm') else None


def neutral_pixels(kind, channels, size=4):
    """RGBA8 pixels that read as the kind's neutral value through the donor's channel mapping (0 zero, 1 one, 2..5 R G B A)."""
    px = np.zeros(4)
    for shader_channel, src in enumerate(channels):
        if src >= 2:
            px[src - 2] = NEUTRAL[kind][shader_channel]
    return np.broadcast_to(np.rint(px * 255).astype(np.uint8), (size, size, 4)).copy()


def load_image(path):
    im = Image.open(path)
    if im.mode == 'P' and 'transparency' in im.info:
        im = im.convert('RGBA')
    return np.asarray(im.convert('RGBA'), np.uint8)


def build(rom, f, slot_textures, log=print):
    """{romfs path: compressed file bytes} for every texture of the materials of the given shapes.
    slot_textures: {shape index: image path or None}."""
    out = {}
    for si, img_path in slot_textures.items():
        mat = f.shapes[si].material
        for name in material_textures(f, mat):
            kind = kind_of(name)
            rel = 'Tex/%s.bntx.zs' % name
            if kind is None or not os.path.exists(rom.path(rel)):
                log('  texture %-40s left as it is (%s)' % (name, 'unknown kind' if kind is None else 'not in the romfs'))
                continue
            donor = rom.read(rel)
            d = bntx.read(donor)
            if kind == 'Alb':
                if not img_path:
                    continue
                pix = load_image(img_path)
                if max(pix.shape[:2]) > 128:                       # one block per pixel: keep the texture a sane size
                    im = Image.fromarray(pix).resize((min(128, pix.shape[1]), min(128, pix.shape[0])), Image.LANCZOS)
                    pix = np.asarray(im, np.uint8)
            else:
                pix = neutral_pixels(kind, d.channels, 1)
            new = bntx_flat.make_flat(donor, pix)
            if new is None:
                log('  texture %-40s: format 0x%04x cannot be written flat; stored as RGBA8' % (name, d.fmt))
                new = bntx.make(donor, pix, srgb=(d.fmt & 0xff) == 6)
            out[rel] = rom.compress(new, dict_name=rom.dict_of(rel))
            log('  texture %-40s <- %s' % (name, os.path.basename(img_path) if kind == 'Alb' else 'neutral %s' % kind))
    return out
