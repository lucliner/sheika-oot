"""(sheikah) The game's own item icons, for the editor: UI/Tex/Icon/*.bntx.zs of the player's romfs decoded to one PNG each.

    python -I nx2/item_icons.py --romfs <romfs> --out <folder>      writes <folder>/<Item name>.png (about 130 files), and the
                                                                    editor's other art: the stones, medallions and songs by item name
                                                                    (from the shared UI textures), ui_*.png (hearts, rupee, magic, the
                                                                    world map)

The icons are the game's textures (BNTX: Tegra block-linear, mostly ASTC or BC), made on the player's PC from the player's
own dump and kept beside the game; nothing of them is in this repository. Needs numpy, Pillow, zstandard and
texture2ddecoder (python -m pip install texture2ddecoder pillow numpy zstandard).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamedata_names as gn  # noqa: E402

# NVN/Tegra format (the high byte of BRTI's format word) -> (block width, block height, bytes per block, decoder)
FORMATS = {
    0x01: (1, 1, 1, "r8"), 0x02: (1, 1, 1, "r8"), 0x07: (1, 1, 2, "rgb565"), 0x09: (1, 1, 2, "rg8"),
    0x0b: (1, 1, 4, "rgba8"), 0x0c: (1, 1, 4, "bgra8"),
    0x1a: (4, 4, 8, "bc1"), 0x1b: (4, 4, 16, "bc2"), 0x1c: (4, 4, 16, "bc3"), 0x1d: (4, 4, 8, "bc4"), 0x1e: (4, 4, 16, "bc5"),
    0x20: (4, 4, 16, "bc7"),
}
ASTC = {0x2d: (4, 4), 0x2e: (5, 4), 0x2f: (5, 5), 0x30: (6, 5), 0x31: (6, 6), 0x32: (8, 5), 0x33: (8, 6), 0x34: (8, 8),
        0x35: (10, 5), 0x36: (10, 6), 0x37: (10, 8), 0x38: (10, 10), 0x39: (12, 10), 0x3a: (12, 12)}


def block_linear(data, width, height, bw, bh, bpp, block_height_log2):
    """The image's blocks in order, out of a Tegra X1 block-linear surface (GOBs of 64 x 8 bytes, block_height GOBs high)."""
    import numpy as np
    w = -(-width // bw)
    h = -(-height // bh)
    block_height = 1 << block_height_log2
    gobs_per_row = -(-(w * bpp) // 64)
    out = bytearray(w * h * bpp)
    ys, xs = np.mgrid[0:h, 0:w]
    x = (xs * bpp).astype(np.int64)
    y = ys.astype(np.int64)
    addr = ((y // (8 * block_height)) * 512 * block_height * gobs_per_row + (x // 64) * 512 * block_height +
            ((y % (8 * block_height)) // 8) * 512 + ((x % 64) // 32) * 256 + ((y % 8) // 4) * 128 + ((x % 32) // 16) * 64 +
            ((y // 2) % 2) * 32 + (y % 2) * 16 + (x % 16))   # (the Switch 2's GOB: gpu/vk/vk_texture.cpp, ReadTexels)
    src = np.frombuffer(data, dtype=np.uint8)
    dst = np.frombuffer(out, dtype=np.uint8).copy().reshape(h, w, bpp)
    for b in range(bpp):
        a = addr + b if bpp == 1 else addr + b
        a = np.minimum(a, len(src) - 1)
        dst[:, :, b] = src[a]
    return dst.tobytes(), w, h


def parse_bntx(data):
    """[(name, width, height, format, block_height_log2, image bytes)] of a BNTX file."""
    if data[:4] != b"BNTX":
        raise ValueError("not a BNTX")
    nx = 0x20
    if data[nx:nx + 4] not in (b"NX  ", b"Ounc"):   # (Switch 2 files say "Ounce" where the first Switch's say "NX")
        raise ValueError("no NX header")
    count = struct.unpack_from("<I", data, nx + 4)[0]
    info_ptrs = struct.unpack_from("<Q", data, nx + 8)[0]
    out = []
    for i in range(count):
        brti = struct.unpack_from("<Q", data, info_ptrs + 8 * i)[0]
        if data[brti:brti + 4] != b"BRTI":
            raise ValueError("no BRTI")
        mips = struct.unpack_from("<H", data, brti + 0x16)[0]
        fmt = struct.unpack_from("<I", data, brti + 0x1c)[0]
        width, height = struct.unpack_from("<II", data, brti + 0x24)
        layout = struct.unpack_from("<I", data, brti + 0x34)[0]
        image_size = struct.unpack_from("<I", data, brti + 0x50)[0]
        name_off = struct.unpack_from("<Q", data, brti + 0x60)[0]
        ptrs = struct.unpack_from("<Q", data, brti + 0x70)[0]
        first = struct.unpack_from("<Q", data, ptrs)[0]
        name_len = struct.unpack_from("<H", data, name_off)[0]
        name = data[name_off + 2:name_off + 2 + name_len].decode("ascii", "replace")
        out.append((name, width, height, fmt, layout & 7, data[first:first + image_size], mips))
    return out


def decode(width, height, fmt, block_height_log2, image):
    """An RGBA PIL image of the first mip, or None for a format this does not know."""
    import numpy as np
    import texture2ddecoder as t2d
    from PIL import Image
    kind = (fmt >> 8) & 0xff
    if kind in ASTC:
        bw, bh = ASTC[kind]
        bpp, name = 16, "astc"
    elif kind in FORMATS:
        bw, bh, bpp, name = FORMATS[kind]
    else:
        return None
    blocks, w, h = block_linear(image, width, height, bw, bh, bpp, block_height_log2)
    if name == "astc":
        raw = t2d.decode_astc(blocks, w * bw, h * bh, bw, bh)
    elif name == "bc1":
        raw = t2d.decode_bc1(blocks, w * 4, h * 4)
    elif name == "bc3":
        raw = t2d.decode_bc3(blocks, w * 4, h * 4)
    elif name == "bc4":
        raw = t2d.decode_bc4(blocks, w * 4, h * 4)
    elif name == "bc5":
        raw = t2d.decode_bc5(blocks, w * 4, h * 4)
    elif name == "bc7":
        raw = t2d.decode_bc7(blocks, w * 4, h * 4)
    elif name in ("rgba8", "bgra8"):
        a = np.frombuffer(blocks, dtype=np.uint8).reshape(h, w, 4)
        img = Image.fromarray(a[:, :, [2, 1, 0, 3]] if name == "bgra8" else a, "RGBA")
        return img.crop((0, 0, width, height))
    else:
        return None
    # texture2ddecoder gives BGRA
    a = np.frombuffer(raw, dtype=np.uint8).reshape(h * bh, w * bw, 4)
    img = Image.fromarray(a[:, :, [2, 1, 0, 3]], "RGBA")
    return img.crop((0, 0, width, height))


# The shared UI textures (UI/Layout2Archive/Common.*.bntx.zs) the editor uses besides the item icons. The names are the game's own;
# the stones and medallions are DataFileImportantIcon_00..08 (the save file's icons), the songs OcarinaMelodyIcon_00..12 (white
# glyphs, tinted here)
STONES = {"Item_Stone_Forest": 0, "Item_Stone_Fire": 1, "Item_Stone_Water": 2, "Item_Medal_Light": 3, "Item_Medal_Forest": 4,
          "Item_Medal_Fire": 5, "Item_Medal_Water": 6, "Item_Medal_Spirit": 7, "Item_Medal_Shadow": 8}
SONGS = {"Item_Melody_ZeldaLullaby": (0, (240, 130, 175)), "Item_Melody_EponaSong": (1, (240, 150, 70)),
         "Item_Melody_SariaSong": (2, (80, 195, 100)), "Item_Melody_SunSong": (3, (250, 220, 80)),
         "Item_Melody_TimeSong": (4, (100, 150, 245)), "Item_Melody_StormSong": (5, (175, 182, 198)),
         "Item_Melody_ForestMinuet": (7, (80, 195, 100)), "Item_Melody_FireBolero": (8, (240, 90, 65)),
         "Item_Melody_WaterSerenade": (9, (90, 160, 240)), "Item_Melody_SpiritRequiem": (10, (240, 150, 70)),
         "Item_Melody_ShadowNocturne": (11, (180, 100, 220)), "Item_Melody_LightPrelude": (12, (252, 232, 120))}
UI_ART = {"ui_heart_full": "HeartW_00_ASTC4x4", "ui_heart_empty": "HeartFrame_00", "ui_rupee": "Nt_RupeeIconNew_00",
          "ui_mp_fill": "MpGaugeCenter_00", "ui_boss": "MapIconBoss_01", "ui_world_map": "MapTexWorldAll_00"}


def tint(img, rgb):
    """A white-ish glyph in a colour: its brightness times the colour, its alpha kept."""
    import numpy as np
    from PIL import Image
    a = np.asarray(img.convert("RGBA")).astype(np.float32)
    lum = a[:, :, :3].mean(axis=2, keepdims=True) / 255.0
    out = np.empty_like(a)
    out[:, :, :3] = np.clip(np.array(rgb, dtype=np.float32) * (0.45 + 0.7 * lum), 0, 255)
    out[:, :, 3] = a[:, :, 3]
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def ui_art(romfs, out, dicts):
    path = os.path.join(romfs, "UI", "Layout2Archive", "Common.Product.100.Nin_Ounce_NVN2.bntx.zs")
    if not os.path.isfile(path):
        print("no shared UI textures: the editor draws its own songs, medallions, hearts and map")
        return 0
    textures = {t[0]: t for t in parse_bntx(gn.decompress_zs(path, dicts))}
    done = 0

    def get(name):
        t = textures.get(name)
        return decode(t[1], t[2], t[3], t[4], t[5]) if t else None

    def save(img, name, width):
        if img.width != width:
            img = img.resize((width, max(1, img.height * width // img.width)))
        img.save(os.path.join(out, name + ".png"))

    for item, n in STONES.items():
        img = get("DataFileImportantIcon_%02d" % n)
        if img is not None:
            save(img, item, 128)
            done += 1
    for item, (n, rgb) in SONGS.items():
        img = get("OcarinaMelodyIcon_%02d" % n)
        if img is not None:
            save(tint(img, rgb), item, 128)
            done += 1
    for name, tex in UI_ART.items():
        img = get(tex)
        if img is not None:
            save(img, name, 1600 if name == "ui_world_map" else 128 if img.width <= 400 else 1024)
            done += 1
    print("%d interface images written" % done)
    return done



def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--romfs", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--size", type=int, default=0, help="scale to this many pixels wide (0: as stored)")
    a = p.parse_args()
    folder = os.path.join(a.romfs, "UI", "Tex", "Icon")
    os.makedirs(a.out, exist_ok=True)
    dicts = gn.zs_dicts(a.romfs)
    done = skipped = 0
    for fn in sorted(os.listdir(folder)):
        if not fn.endswith(".bntx.zs"):
            continue
        stem = fn[:-len(".bntx.zs")]
        try:
            tex = parse_bntx(gn.decompress_zs(os.path.join(folder, fn), dicts))
            name, w, h, fmt, bhl, image, _ = tex[0]
            img = decode(w, h, fmt, bhl, image)
        except Exception as err:   # one bad icon leaves that icon out, not the rest
            print("skip %s: %s" % (stem, err))
            skipped += 1
            continue
        if img is None:
            print("skip %s: format %#x" % (stem, fmt))
            skipped += 1
            continue
        if a.size and img.width != a.size:
            img = img.resize((a.size, max(1, img.height * a.size // img.width)))
        img.save(os.path.join(a.out, stem + ".png"))
        done += 1
    print("%d icons written to %s (%d skipped)" % (done, a.out, skipped))
    try:
        ui_art(a.romfs, a.out, dicts)
    except Exception as err:   # the interface art is optional: the editor draws its own without it
        print("interface art skipped: %s" % err)


if __name__ == "__main__":
    sys.exit(main())
