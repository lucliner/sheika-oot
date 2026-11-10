"""Rebrand the Hookshot fork to Sheikah Native. Idempotent: running it twice changes nothing more.

    python -I rebrand.py <fork folder> [--dry]

Renames: hookshot -> sheikah (file names, exe/dll names, data files), Hookshot -> Sheikah Native (display text).
Keeps: HOOKSHOT_* build/environment switch names (a stable interface, documented in docs/development.md),
upstream attribution (NotRexed/Hookshot URLs and the Credits/PROVENANCE text), third_party/, LICENSE, work/ contents.
"""
import os, sys, re

SKIP_DIRS = {".git", "third_party", "work", "builder", ".github", "__pycache__", "node_modules"}
SKIP_FILES = {"LICENSE"}
TEXT_EXT = {".c", ".h", ".cpp", ".hpp", ".py", ".md", ".txt", ".ps1", ".sh", ".bat", ".json", ".rc", ".manifest", ".ini", ".yml", ".yaml", ".cmake"}
KEEP = ["NotRexed/Hookshot", "Hookshot's maintainer", "upstream Hookshot", "Hookshot by NotRexed"]   # attribution, put back after


def rewrite(s):
    holes = {}
    for i, k in enumerate(KEEP + ["HOOKSHOT_", "HOOKSHOT"]):
        tag = "\x00K%d\x00" % i
        holes[tag] = k
        s = s.replace(k, tag)
    s = s.replace("HookshotBuilder", "SheikahBuilder")
    s = s.replace("hookshot", "sheikah")
    s = s.replace("Hookshot", "Sheikah Native")
    for tag, k in holes.items():
        s = s.replace(tag, k)
    return s


def main(root, dry):
    changed = 0
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
        for f in files:
            p = os.path.join(d, f)
            if f in SKIP_FILES or os.path.splitext(f)[1].lower() not in TEXT_EXT:
                continue
            try:
                raw = open(p, "rb").read()
                s = raw.decode("utf-8")
            except UnicodeDecodeError:
                continue
            t = rewrite(s)
            if t != s:
                changed += 1
                if not dry:
                    open(p, "wb").write(t.encode("utf-8"))
    renamed = 0
    for d, dirs, files in os.walk(root, topdown=False):
        parts = set(os.path.relpath(d, root).split(os.sep))
        if parts & (SKIP_DIRS - {"work"}):
            continue
        for f in files + dirs:
            if "hookshot" in f.lower() and f != "LICENSE":
                new = f.replace("Hookshot", "Sheikah").replace("hookshot", "sheikah")
                renamed += 1
                if not dry:
                    os.replace(os.path.join(d, f), os.path.join(d, new))
    print("%d files rewritten, %d paths renamed%s" % (changed, renamed, " (dry run)" if dry else ""))


if __name__ == "__main__":
    main(sys.argv[1], "--dry" in sys.argv)
