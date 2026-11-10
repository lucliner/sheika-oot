"""The game's GameData list (every flag, counter and string the save holds) from the user's own romfs, for the live editor.

    python -I nx2/gamedata_names.py <romfs dir> [--out work/gamedata.json]

Reads GameData/GameDataList.Product.*.byml.zs: the dictionary comes from Pack/ZsDic.pack.zs (a SARC of zstd
dictionaries, itself plain zstd), then the BYML is parsed. Writes {"entries": [{"name", "hash", "type", ...}]}
where hash is the MurMur3-32 of the name, as the game's getters take it. Game data: it stays in work/, never in git.
"""
import glob
import json
import os
import struct
import sys

import zstandard


def sarc_files(data):
    """{name: bytes} of a SARC archive."""
    if data[:4] != b"SARC":
        raise ValueError("not a SARC")
    bom = struct.unpack_from(">H", data, 6)[0]
    e = ">" if bom == 0xFEFF else "<"
    data_off = struct.unpack_from(e + "I", data, 0x0C)[0]
    assert data[0x14:0x18] == b"SFAT"
    count = struct.unpack_from(e + "H", data, 0x1A)[0]
    sfnt = 0x20 + 16 * count
    names_off = sfnt + 8
    out = {}
    for i in range(count):
        _, name_attr, beg, end = struct.unpack_from(e + "IIII", data, 0x20 + 16 * i)
        off = (name_attr & 0xFFFFFF) * 4 + names_off
        name = data[off:data.index(b"\0", off)].decode()
        out[name] = data[data_off + beg:data_off + end]
    return out


def zs_dicts(romfs):
    p = os.path.join(romfs, "Pack", "ZsDic.pack.zs")
    raw = zstandard.ZstdDecompressor().decompressobj().decompress(open(p, "rb").read())
    return {n: zstandard.ZstdCompressionDict(d) for n, d in sarc_files(raw).items()}


def murmur3_32(b, seed=0):
    c1, c2, h = 0xCC9E2D51, 0x1B873593, seed & 0xFFFFFFFF
    n = len(b) // 4 * 4
    for i in range(0, n, 4):
        k = struct.unpack_from("<I", b, i)[0]
        k = (k * c1) & 0xFFFFFFFF
        k = ((k << 15) | (k >> 17)) & 0xFFFFFFFF
        k = (k * c2) & 0xFFFFFFFF
        h ^= k
        h = ((h << 13) | (h >> 19)) & 0xFFFFFFFF
        h = (h * 5 + 0xE6546B64) & 0xFFFFFFFF
    k = 0
    tail = b[n:]
    if len(tail) >= 3: k ^= tail[2] << 16
    if len(tail) >= 2: k ^= tail[1] << 8
    if len(tail) >= 1:
        k ^= tail[0]
        k = (k * c1) & 0xFFFFFFFF
        k = ((k << 15) | (k >> 17)) & 0xFFFFFFFF
        k = (k * c2) & 0xFFFFFFFF
        h ^= k
    h ^= len(b)
    h ^= h >> 16
    h = (h * 0x85EBCA6B) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 0xC2B2AE35) & 0xFFFFFFFF
    h ^= h >> 16
    return h


def decompress_zs(path, dicts):
    raw = open(path, "rb").read()
    base = os.path.basename(path)
    order = ["bcett.byml.zsdic", "pack.zsdic", "zs.zsdic"] if base.endswith(".byml.zs") else ["zs.zsdic", "pack.zsdic", "bcett.byml.zsdic"]
    last = None
    for n in order:
        if n not in dicts:
            continue
        try:
            return zstandard.ZstdDecompressor(dict_data=dicts[n]).decompressobj().decompress(raw)
        except zstandard.ZstdError as err:
            last = err
    try:
        return zstandard.ZstdDecompressor().decompressobj().decompress(raw)
    except zstandard.ZstdError:
        raise last or ValueError("no dictionary fits " + path)


HARVEST_DIRS = ["Pack", "Logic", "Banc", "Sequence", "System", "Component", "Game", "GameParameter", "GameData", "Scene", "WorldMgr",
                "AI", "AISchedule", "AS", "AnimationEvent", "Life", "Reaction", "Perception", "BSA", "Chemical", "RSDB", "UI", "Mals", "Event"]
_STR = None


def harvest(romfs, wanted, dirs=HARVEST_DIRS, log=print, max_mb=300):
    """{hash: name} of every identifier-like string in the data folders (zstd and SARC unwrapped) whose MurMur3 is wanted."""
    import re
    global _STR
    _STR = _STR or re.compile(rb"[A-Za-z_][A-Za-z0-9_.:\-]{2,119}")
    dicts = zs_dicts(romfs)
    found, seen = {}, set()

    def scan(buf):
        for m in _STR.finditer(buf):
            t = m.group()
            if t in seen:
                continue
            seen.add(t)
            h = murmur3_32(t)
            if h in wanted:
                found[h] = t.decode()

    def unwrap(buf, name, depth=0):
        if buf[:4] == b"\x28\xb5\x2f\xfd" and depth < 4:   # (the zstd frame magic)
            for order in (["zs.zsdic", "pack.zsdic", "bcett.byml.zsdic"], ["pack.zsdic", "zs.zsdic", "bcett.byml.zsdic"], [None]):
                for dn in order:
                    try:
                        z = zstandard.ZstdDecompressor(dict_data=dicts[dn]) if dn else zstandard.ZstdDecompressor()
                        return unwrap(z.decompressobj().decompress(buf), name, depth + 1)
                    except (zstandard.ZstdError, KeyError):
                        pass
            return
        if buf[:4] == b"SARC" and depth < 6:
            try:
                for n, b in sarc_files(buf).items():
                    unwrap(b, n, depth + 1)
            except Exception:
                scan(buf)
            return
        scan(buf)

    n = 0
    for top in dirs:
        base = os.path.join(romfs, top)
        for d, _, files in os.walk(base):
            for f in files:
                p = os.path.join(d, f)
                try:
                    if os.path.getsize(p) > max_mb << 20:
                        continue
                    unwrap(open(p, "rb").read(), f)
                except OSError:
                    pass
                n += 1
        log("%s: %d files, %d names so far" % (top, n, len(found)))
    return found


def kind_of(e):
    """0 bool, 1 int (an enum's value is one too), 2 float, 3 string, 4 anything else (arrays, structs, vectors)."""
    d = e.get("DefaultValue", None)
    return 0 if isinstance(d, bool) else 1 if isinstance(d, int) else 2 if isinstance(d, float) else 3 if isinstance(d, str) else 4


def write_tsv(entries, path):
    """sheikah_gamedata.tsv, one entry a line: hash in hex, kind, default, name ('-' when the name is not known).
    Named entries first, as the editor lists them. Read by host/oot_live_editor.inc."""
    rows = sorted(entries, key=lambda e: (e.get("name") is None, (e.get("name") or "").lower(), int(e["Hash"])))
    tab, nl = chr(9), chr(10)
    with open(path, "w", encoding="utf-8", newline=nl) as f:
        for e in rows:
            d = e.get("DefaultValue", "")
            d = ("1" if d else "0") if isinstance(d, bool) else str(d) if isinstance(d, (int, float, str)) else ""
            d = d.replace(tab, " ").replace(nl, " ")[:40]
            f.write(tab.join(["%08x" % int(e["Hash"]), str(kind_of(e)), d, e.get("name") or "-"]) + nl)


def main(romfs, out):
    dicts = zs_dicts(romfs)
    print("dictionaries:", ", ".join(sorted(dicts)))
    gd = os.path.join(romfs, "GameData")   # (listdir, not glob: a dump folder name with [brackets] is a glob pattern)
    files = sorted(os.path.join(gd, f) for f in os.listdir(gd) if f.startswith("GameDataList.Product.") and f.endswith(".byml.zs"))
    if not files:
        raise SystemExit("no GameDataList in " + romfs)
    data = decompress_zs(files[-1], dicts)
    import byml
    doc = byml.Byml(data).parse()
    entries = []

    def walk(node, path):
        if isinstance(node, dict):
            if "Hash" in node or "DataName" in node or "Name" in node:
                entries.append(node)
                return
            for k, v in node.items():
                walk(v, path + [k])
        elif isinstance(node, list):
            for v in node:
                walk(v, path)

    walk(doc, [])
    print("top-level keys:", list(doc)[:20] if isinstance(doc, dict) else type(doc))
    wanted = {int(e["Hash"]) for e in entries}
    names = harvest(romfs, wanted, log=lambda m: print(m, flush=True))
    for e in entries:
        e["name"] = names.get(int(e["Hash"]))
    print("named %d of %d" % (len(names), len(wanted)))
    if "--tsv" in sys.argv:
        write_tsv(entries, sys.argv[sys.argv.index("--tsv") + 1])
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    json.dump({"source": os.path.basename(files[-1]), "doc_keys": list(doc)[:50] if isinstance(doc, dict) else [], "count": len(entries),
               "entries": entries[:100000]}, open(out, "w"), default=str, indent=1)
    print(len(entries), "entries ->", out)


if __name__ == "__main__":
    if "--from-json" in sys.argv:   # (a harvest's json again as a tsv, without scanning the romfs)
        write_tsv(json.load(open(sys.argv[sys.argv.index("--from-json") + 1]))["entries"], sys.argv[sys.argv.index("--tsv") + 1])
        raise SystemExit(0)
    out = "work/gamedata.json"
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
    main(sys.argv[1], out)
