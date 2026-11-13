"""Spawn-level swap: make the game place one actor where the map files say another one goes, without touching any model.

Map files (`Banc/<Area>/*.bcett.byml.zs`) name the actor of every placement in a string table. A mod copy of the file points the
entry for the old actor's name at the new actor's name, so every placement of the old actor spawns the new one. Nothing else in the
file moves: the new name is appended at the end and the table entry is re-pointed to it."""
import os
import struct


def _string_table(d):
    st = struct.unpack_from('<I', d, 8)[0]
    n = struct.unpack_from('<I', d, st)[0] >> 8
    offs = struct.unpack_from('<%dI' % (n + 1), d, st + 4)
    return st, n, [d[st + offs[i]:d.index(b'\0', st + offs[i])].decode('utf-8', 'replace') for i in range(n)]


def retarget(data, old, new):
    """The map file's bytes with every use of actor name `old` renamed to `new` (None when the file does not use `old`)."""
    if data[:2] not in (b'YB', b'BY'):
        return None
    st, n, names = _string_table(data)
    if old not in names:
        return None
    b = bytearray(data)
    at = len(b)
    b += new.encode() + b'\0'
    struct.pack_into('<I', b, st + 4 + 4 * names.index(old), at - st)
    return bytes(b)


def build(rom, old, new, log=print, areas=None):
    """{romfs path: compressed bytes} of every map file that places `old`, set to place `new` instead."""
    if not os.path.exists(rom.path('Pack/Actor/%s.pack.zs' % new)):
        raise ValueError('no actor named %s in the game (Pack/Actor/%s.pack.zs)' % (new, new))
    out = {}
    root = rom.path('Banc')
    for area in sorted(os.listdir(root)):
        if areas and area not in areas:
            continue
        for fn in sorted(os.listdir(os.path.join(root, area))):
            if not fn.endswith('.bcett.byml.zs'):
                continue
            rel = 'Banc/%s/%s' % (area, fn)
            new_data = retarget(rom.read(rel), old, new)
            if new_data is not None:
                out[rel] = rom.compress(new_data, dict_name=rom.dict_of(rel))
                log('  %s: %s -> %s' % (rel, old, new))
    return out


# ---------------------------------------------------------------------------------------------------------------------------------
# Keep the brain, swap the body
#
# Spawning another actor outright brings that actor's own brain too (an enemy attacks, a shopkeeper does not talk). An NPC's brain is
# its AI scripts, schedules, talk and event handling, perception and parameters; its body is the model, its animations and the
# animation state machine. `hybrid` builds a mod copy of the OLD actor's pack (`Pack/Actor/<old>.pack.zs`) in which only the body
# entries of the actor's component list point at the NEW actor's, and the files those need are added from the new pack. Every file
# of the old pack stays byte for byte as it was, so the old AI and behaviour are kept, and every placement of the actor in the game
# (and every spawn from a script) shows the new body.

import re

# component entries of an actor's parameter file that make up the body
BODY = ('ModelInfoRef', 'AnimationRef', 'ASRef', 'ASInfoRef', 'ASOptimize', 'ModelBindRef', 'ELinkRef', 'XLinkRef',
        'ActorPositionCalculatorRef', 'SoundFootStepRef')


def sarc_read(b):
    from containers import sarc_read as r
    return r(b)


def _sarc_hash(name):
    h = 0
    for c in name.encode():
        h = (h * 0x65 + c) & 0xffffffff
    return h


def sarc_write(files, align=0x10):
    """{name: bytes} -> SARC bytes (little endian, as the game's packs are)."""
    items = sorted(files.items(), key=lambda kv: _sarc_hash(kv[0]))
    names, offs = bytearray(), []
    for n, _ in items:
        offs.append(len(names) // 4)
        names += n.encode() + b'\0'
        while len(names) % 4:
            names += b'\0'
    head = 0x14 + 0x0c + 16 * len(items) + 8 + len(names)
    data_off = (head + 0xff) & ~0xff
    body, nodes = bytearray(), bytearray()
    for (n, d), o in zip(items, offs):
        while len(body) % align:
            body += b'\0'
        nodes += struct.pack('<IIII', _sarc_hash(n), 0x01000000 | o, len(body), len(body) + len(d))
        body += d
    out = bytearray(b'SARC' + struct.pack('<HHIIHH', 0x14, 0xfeff, data_off + len(body), data_off, 0x100, 0))
    out += b'SFAT' + struct.pack('<HHI', 0x0c, len(items), 0x65) + nodes
    out += b'SFNT' + struct.pack('<HH', 8, 0) + names
    out += b'\0' * (data_off - len(out)) + body
    return bytes(out)


def read_byml(d):
    """A small BYML (version 7) reader: dicts, arrays, strings and numbers; 64-bit numbers come back as (type, value)."""
    kt, st, root = struct.unpack_from('<III', d, 4)

    def table(o):
        n = struct.unpack_from('<I', d, o)[0] >> 8
        offs = struct.unpack_from('<%dI' % (n + 1), d, o + 4)
        return [d[o + offs[i]:d.index(b'\0', o + offs[i])].decode('utf-8', 'replace') for i in range(n)]
    keys, strs = table(kt), table(st)

    def node(t, v):
        if t == 0xa0:
            return strs[v]
        if t == 0xc0:
            n = struct.unpack_from('<I', d, v)[0] >> 8
            ts, base = d[v + 4:v + 4 + n], v + 4 + (n + 3) // 4 * 4
            return [node(ts[i], struct.unpack_from('<I', d, base + 4 * i)[0]) for i in range(n)]
        if t == 0xc1:
            n = struct.unpack_from('<I', d, v)[0] >> 8
            out = {}
            for i in range(n):
                e = struct.unpack_from('<I', d, v + 4 + i * 8)[0]
                out[keys[e & 0xffffff]] = node(e >> 24, struct.unpack_from('<I', d, v + 8 + i * 8)[0])
            return out
        if t == 0xd0:
            return v != 0
        if t in (0xd1, 0xd2):
            return v
        if t == 0xd3:
            return struct.unpack('<f', struct.pack('<I', v))[0]
        return None if t == 0xff else (t, v)
    return node(d[root], root)


def _key(path):
    """Where a path a parameter file names lives in an actor pack: no `?` (optional), no `Work/`, `.gyml` stored as `.bgyml`."""
    p = path.lstrip('?')
    if p.startswith('Work/'):
        p = p[5:]
    return p[:-5] + '.bgyml' if p.endswith('.gyml') else p


def _chain(files, name):
    """[(pack member, parsed parameter file)] of an actor's parameter file and its parents, child first."""
    out, member = [], 'Actor/%s.engine__actor__ActorParam.bgyml' % name
    while member in files and len(out) < 8:
        t = read_byml(files[member])
        out.append((member, t))
        parent = t.get('$parent')
        if not parent:
            break
        member = _key(parent)
    return out


def components(files, name):
    """The actor's resolved component list: parents first, the actor's own entries over them."""
    merged = {}
    for _, t in reversed(_chain(files, name)):
        merged.update(t.get('Components') or {})
    return merged


def _needs(data):
    pat = rb'[?]?(?:Work/)?[A-Za-z0-9_/]+\.[A-Za-z0-9_]+\.b?gyml'
    return {_key(m.group(0).decode()) for m in re.finditer(pat, data)}


def hybrid(rom, old, new, log=print):
    """(compressed pack bytes, report) of the old actor with the new actor's body. The report lists what was swapped, what was
    added, and what could not be done (`problems`)."""
    old_rel, new_rel = 'Pack/Actor/%s.pack.zs' % old, 'Pack/Actor/%s.pack.zs' % new
    for rel in (old_rel, new_rel):
        if not os.path.exists(rom.path(rel)):
            raise ValueError('no actor named %s in the game (%s)' % (os.path.basename(rel)[:-8], rel))
    ofiles = sarc_read(rom.read(old_rel))
    nfiles = sarc_read(rom.read(new_rel))
    ochain, ncomp = _chain(ofiles, old), components(nfiles, new)
    if not ochain or not ncomp:
        raise ValueError('%s or %s has no actor parameter file' % (old, new))
    problems, swapped = [], {}
    files = dict(ofiles)
    # 1. point the old actor's body entries at the new actor's
    for key in BODY:
        want = ncomp.get(key)
        holders = [(m, t) for m, t in ochain if key in (t.get('Components') or {})]
        if want is None:
            if holders:
                problems.append('%s: the new actor has no such entry; the old one stays' % key)
            continue
        if not holders:
            problems.append("%s: the old actor has no such entry to re-point, so the new actor's %s is not used" % (key, want))
            continue
        for member, t in holders:
            have = t['Components'][key]
            if have == want:
                continue
            if list(t['Components'].values()).count(have) > 1:
                problems.append('%s: shares its value with another entry in %s; left as it is' % (key, member))
                continue
            patched = retarget(files[member], have, want)
            if patched is None:
                problems.append('%s: could not be re-pointed in %s' % (key, member))
                continue
            files[member] = patched
            swapped[key] = want
    # 2. bring in the files the body entries need (and what those need); the old actor's own files are never replaced, except one
    #    that a body entry names itself
    named = {_key(v) for v in swapped.values()}
    todo, seen = list(swapped.values()), set()
    while todo:
        k = _key(todo.pop())
        if k in seen:
            continue
        seen.add(k)
        if k in nfiles and (k not in files or (k in named and files[k] != nfiles[k])):
            files[k] = nfiles[k]
            todo += list(_needs(nfiles[k]))
    # 3. check the result: the brain files are untouched, the body is the new one, every reference resolves
    built = sarc_read(sarc_write(files))
    brain_changed = [k for k in ofiles if k.startswith(('AI/', 'BSA/', 'Game/', 'GameParameter/')) and built.get(k) != ofiles[k]]
    now = components(built, old)
    wrong = [k for k, v in swapped.items() if now.get(k) != v]
    if brain_changed or wrong:
        raise RuntimeError('the built pack is not what was asked: brain changed %s, body wrong %s' % (brain_changed, wrong))
    # how alike the two animation state machines are: the old brain asks for animation states by name
    def names_in(asb):
        try:
            return set(re.findall(rb'[A-Za-z][A-Za-z0-9_]{2,}', rom.read(asb)))
        except Exception:
            return set()
    a_new, a_old = ncomp.get('ASRef'), components(ofiles, old).get('ASRef')
    overlap = None
    if a_new and a_old:
        so, sn = names_in(a_old.lstrip('?') + '.zs'), names_in(a_new.lstrip('?') + '.zs')
        overlap = len(so & sn) / max(1, len(so))
        if overlap < 0.5:
            problems.append('the two animation state machines share only %d%% of their names: the old AI may ask for animations '
                            'the new body does not have' % int(overlap * 100))
    report = {'swapped': swapped, 'added': sorted(k for k in files if k not in ofiles),
              'brain_files_kept': sum(1 for k in ofiles if built.get(k) == ofiles[k]), 'state_overlap': overlap, 'problems': problems}
    log('  %s keeps its brain (%d files untouched); body from %s: %s' % (old, report['brain_files_kept'], new, ', '.join(sorted(swapped)) or 'nothing'))
    for p in problems:
        log('  ! ' + p)
    return rom.compress(sarc_write(files), dict_name=rom.dict_of(old_rel)), report


def build_hybrid(rom, mod_dir, old, new, log=print):
    """Write the hybrid actor pack into the mod and grow its entry in the mod's resource size table."""
    import restbl
    data, report = hybrid(rom, old, new, log)
    rel = 'Pack/Actor/%s.pack.zs' % old
    path = os.path.join(mod_dir, 'romfs', *rel.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)
    restbl.patch(rom, mod_dir, {rel: len(rom.decompress(data))}, log)
    return report
