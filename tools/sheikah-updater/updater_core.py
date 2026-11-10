"""Sheikah Updater, the engine behind sheikah-updater.exe (the window) and usable from a console.

    python -I updater_core.py [--fork DIR] [--cache DIR] <command> [options]

    status                       the version Sheikah is synced with, and the latest upstream release / commit
    detect-base --source DIR     find which upstream commit an untouched Hookshot copy is, and record it (docs/upstream-version.txt)
    merge [--dry] [--target release|main|<tag or sha>]
                                 3-way merge upstream's changes into this folder. --dry only reports. Nothing is written when
                                 any file conflicts: the conflicts are listed and the merged-with-markers files are left in the cache
    build --dump DIR --out DIR [--mingw DIR] [--cpm DIR] [--wgpu DIR] [--keep]
                                 runs nx2/make_game.py
    publish --remote URL [--branch main] [--message-file F] [--no-release]
                                 commit, tag and push this folder to the user's own GitHub repository, with a generated changelog

Lines starting with @@ are for the window: @@version, @@stage, @@progress, @@conflict, @@result. Everything else is plain log text.
Only git (and Python, which the build needs anyway) is required. Public upstream information only is asked for, anonymously.
The merge never touches game data: it works on text sources, and the work/ folder and everything .gitignore keeps out are left alone.
"""
import argparse, datetime, hashlib, json, os, re, shutil, subprocess, sys, tarfile, tempfile, urllib.request

UPSTREAM = "https://github.com/NotRexed/Hookshot.git"
API = "https://api.github.com/repos/NotRexed/Hookshot"
UA = "sheikah-updater"
VERSION_FILE = os.path.join("docs", "upstream-version.txt")
CHANGELOG = "SHEIKAH-CHANGELOG.md"
# upstream parts this fork leaves out on purpose (its own release machinery)
IGNORE = ("builder/", ".github/", "package.json", ".releaserc.json", "package-lock.json", ".git/")
SKIP_DIRS = {".git", "__pycache__", "work"}
HERE = os.path.dirname(os.path.abspath(__file__))


def say(*a):
    print(*a, flush=True)


def git(args, cwd=None, check=True, inp=None, env=None):
    r = subprocess.run(["git", "-c", "core.autocrlf=false", "-c", "core.safecrlf=false"] + args, cwd=cwd, input=inp,
                       capture_output=True, env=env)
    if check and r.returncode:
        raise SystemExit("git %s failed: %s" % (" ".join(args[:3]), r.stderr.decode("utf-8", "replace").strip()))
    return r


def out(args, cwd=None):
    return git(args, cwd).stdout.decode("utf-8", "replace").strip()


def read_version(fork):
    d = {}
    try:
        for line in open(os.path.join(fork, VERSION_FILE), encoding="utf-8"):
            if "=" in line and not line.startswith("#"):
                k, v = line.strip().split("=", 1)
                d[k] = v
    except OSError:
        pass
    return d


def write_version(fork, d):
    with open(os.path.join(fork, VERSION_FILE), "w", encoding="utf-8", newline="\n") as f:
        f.write("# The upstream Hookshot version this Sheikah Native is synced with. Written by tools/sheikah-updater; the build bakes it\n"
                "# into the game (nx2/version.py), which checks upstream for something newer. Do not edit by hand.\n")
        for k in ("commit", "describe", "date", "synced"):
            if d.get(k):
                f.write("%s=%s\n" % (k, d[k]))


def baked_string(fork):
    d = read_version(fork)
    return "%s/%s" % (d["commit"], d.get("describe", d["commit"][:7])) if d.get("commit") else ""


def http_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.load(r)


def latest_upstream():
    """(release tag, release commit, main commit) as far as the network says; '' for what could not be asked"""
    tag = rel = head = ""
    try:
        refs = out(["ls-remote", UPSTREAM, "HEAD", "refs/tags/v*"]).splitlines()
        peeled, tags = {}, {}
        for ln in refs:
            sha, ref = ln.split("\t")
            if ref == "HEAD":
                head = sha
            elif ref.endswith("^{}"):
                peeled[ref[10:-3]] = sha
            else:
                tags[ref[10:]] = sha
        tags.update(peeled)
    except (SystemExit, OSError) as e:
        say("could not ask upstream: %s" % e)
        return "", "", ""
    try:
        tag = http_json(API + "/releases/latest").get("tag_name", "")
    except Exception:
        stable = [t for t in tags if re.fullmatch(r"v\d+\.\d+\.\d+", t)]
        stable.sort(key=lambda t: tuple(int(x) for x in t[1:].split(".")))
        tag = stable[-1] if stable else ""
    return tag, tags.get(tag, ""), head


def meta_repo(cache):
    """A blobless bare clone of upstream (all commits and trees, no file contents): cheap to keep up to date"""
    path = os.path.join(cache, "upstream.git")
    if not os.path.isdir(path):
        say("fetching upstream's history (once)...")
        git(["clone", "--bare", "--filter=blob:none", "--quiet", UPSTREAM, path])
    else:
        git(["fetch", "--quiet", "--tags", "origin", "+refs/heads/*:refs/heads/*"], cwd=path)
    return path


def resolve(meta, target, tag, head):
    if target in ("", "release"):
        if not tag:
            raise SystemExit("the latest release could not be found (offline?)")
        return out(["rev-list", "-n1", tag], cwd=meta), tag
    if target == "main":
        return out(["rev-parse", "HEAD"], cwd=meta), "main"
    return out(["rev-parse", "--verify", target + "^{commit}"], cwd=meta), target


def describe(meta, sha):
    r = git(["describe", "--tags", "--match", "v[0-9]*", sha], cwd=meta, check=False)
    return r.stdout.decode().strip() or sha[:7]


def cmd_status(a):
    d = read_version(a.fork)
    say("@@version synced=%s" % (d.get("describe") or d.get("commit", "unknown")))
    say("@@version synced_commit=%s" % d.get("commit", ""))
    tag, rel, head = latest_upstream()
    say("@@version latest_release=%s" % (tag or "unknown"))
    say("@@version latest_commit=%s" % (head[:7] if head else "unknown"))
    behind = ""
    if d.get("commit") and (rel or head):
        try:
            meta = meta_repo(a.cache)
            behind = out(["rev-list", "--count", "--no-merges", "%s..%s" % (d["commit"], rel or head)], cwd=meta)
            say("@@version commits_behind=%s" % behind)
            say("@@version up_to_date=%s" % ("1" if behind == "0" else "0"))
        except SystemExit as e:
            say("could not compare: %s" % e)
    say("@@result ok")


def hash_blob(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def cmd_detect_base(a):
    meta = meta_repo(a.cache)
    src = os.path.abspath(a.source)
    local = {}
    for d, dirs, files in os.walk(src):
        dirs[:] = [x for x in dirs if x not in (".git", "__pycache__")]
        for f in files:
            p = os.path.join(d, f)
            rel = os.path.relpath(p, src).replace("\\", "/")
            if rel.startswith("third_party/") or os.path.getsize(p) > 2_000_000:
                continue
            data = open(p, "rb").read()
            local[rel] = {hash_blob(data), hash_blob(data.replace(b"\r\n", b"\n"))}
    say("comparing %d files with upstream's last 1500 commits..." % len(local))
    best = (0, None)
    for i, sha in enumerate(out(["rev-list", "-n", "1500", "HEAD"], cwd=meta).split()):
        tree = git(["ls-tree", "-r", sha], cwd=meta).stdout.decode().splitlines()
        score = 0
        for ln in tree:
            meta_, path = ln.split("\t", 1)
            blob = meta_.split()[2]
            if path in local and blob in local[path]:
                score += 1
        if score > best[0]:
            best = (score, sha)
        if score >= len(local):
            break
        if best[0] and score < best[0] * 0.5 and i > 40 and score < best[0] - 200:
            break   # (older than the best: it only gets worse)
    if not best[1]:
        raise SystemExit("no upstream commit looks like that folder")
    sha = best[1]
    say("best match: %s (%d of %d files identical)" % (sha[:10], best[0], len(local)))
    d = {"commit": sha, "describe": describe(meta, sha), "date": out(["log", "-1", "--format=%cs", sha], cwd=meta),
         "synced": datetime.date.today().isoformat()}
    write_version(a.fork, d)
    say("recorded %s in %s" % (d["describe"], VERSION_FILE))
    say("@@result ok")


def snapshot(meta, sha, dest, rebrand_mod):
    """upstream at sha, as files, with the rebrand applied (the same one the fork was made with)"""
    if os.path.isdir(dest):
        return
    tmp = dest + ".part"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    r = subprocess.Popen(["git", "-c", "core.autocrlf=false", "archive", "--format=tar", sha], cwd=meta, stdout=subprocess.PIPE)
    with tarfile.open(fileobj=r.stdout, mode="r|") as tf:
        for m in tf:
            if m.isfile() and not m.name.startswith(IGNORE):
                tf.extract(m, tmp, filter="data")
    r.wait()
    if r.returncode:
        raise SystemExit("git archive failed for %s" % sha)
    rebrand_mod.main(tmp, False)
    os.replace(tmp, dest)


def load_rebrand(fork):
    import importlib.util
    p = os.path.join(fork, "tools", "rebrand.py")
    if not os.path.isfile(p):
        raise SystemExit("tools/rebrand.py is missing from the Sheikah folder")
    spec = importlib.util.spec_from_file_location("rebrand", p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def files_of(root):
    res = {}
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x not in (".git", "__pycache__")]
        for f in files:
            p = os.path.join(d, f)
            res[os.path.relpath(p, root).replace("\\", "/")] = p
    return res


def rd(p):
    if not p:
        return None
    with open(p, "rb") as f:
        return f.read()


def same(a, b):
    if a is None or b is None:
        return a is b
    return a == b or a.replace(b"\r\n", b"\n") == b.replace(b"\r\n", b"\n")


def merge3(fork_b, base_b, new_b, rel, cache):
    """(merged bytes or None, conflict count)"""
    if b"\0" in fork_b + base_b + new_b:
        return None, 1
    tmp = tempfile.mkdtemp(dir=cache)
    try:
        paths = []
        for name, data in (("fork", fork_b), ("base", base_b), ("up", new_b)):
            p = os.path.join(tmp, name)
            open(p, "wb").write(data.replace(b"\r\n", b"\n"))
            paths.append(p)
        r = git(["merge-file", "-p", "-L", "Sheikah (yours)", "-L", "Hookshot (old base)", "-L", "Hookshot (new)"] + paths, check=False)
        if r.returncode < 0 or r.returncode > 127:
            return None, 1
        return r.stdout, r.returncode
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def plan_merge(a, sha_base, sha_new, meta):
    rb = load_rebrand(a.fork)
    snaps = os.path.join(a.cache, "snap")
    os.makedirs(snaps, exist_ok=True)
    say("@@stage snapshot Preparing the old and the new Hookshot")
    bdir, ndir = (os.path.join(snaps, s[:12]) for s in (sha_base, sha_new))
    snapshot(meta, sha_base, bdir, rb)
    snapshot(meta, sha_new, ndir, rb)
    base, new, fork = files_of(bdir), files_of(ndir), files_of(a.fork)
    plan = {"take": [], "add": [], "delete": [], "merge": [], "conflict": [], "skip": []}
    names = sorted(set(base) | set(new))
    for i, rel in enumerate(names):
        if i % 200 == 0:
            say("@@progress %.3f" % (i / max(1, len(names))))
        b, n, f = rd(base.get(rel)), rd(new.get(rel)), rd(fork.get(rel))
        if same(n, b):
            continue
        if f is not None and same(f, n):
            continue   # (the fork already has it)
        if b is not None and same(f, b):
            plan["delete" if n is None else "take"].append(rel)
        elif b is None and f is None:
            plan["add"].append(rel)
        elif b is not None and f is None:
            plan["skip"].append(rel)   # (the fork removed it on purpose)
        elif n is None:
            plan["conflict"].append((rel, "deleted upstream, changed in Sheikah"))
        elif b is None:
            plan["conflict"].append((rel, "added upstream, and a different file of that name is in Sheikah"))
        else:
            merged, c = merge3(f, b, n, rel, a.cache)
            if merged is not None and c == 0:
                plan["merge"].append((rel, merged))
            else:
                plan["conflict"].append((rel, "both changed the same lines" if merged is not None else "a binary file both changed"))
                if merged is not None:
                    cp = os.path.join(a.cache, "conflicts", rel)
                    os.makedirs(os.path.dirname(cp), exist_ok=True)
                    open(cp, "wb").write(merged)
    return plan, ndir


def cmd_merge(a):
    d = read_version(a.fork)
    if not d.get("commit"):
        raise SystemExit("this folder has no %s yet: run detect-base --source <an untouched Hookshot copy>" % VERSION_FILE)
    tag, rel, head = latest_upstream()
    meta = meta_repo(a.cache)
    sha_new, label = resolve(meta, a.target, tag, head)
    if sha_new == d["commit"]:
        say("Sheikah is already synced with %s." % (d.get("describe") or sha_new[:7]))
        say("@@result uptodate")
        return
    ahead = out(["rev-list", "--count", "%s..%s" % (d["commit"], sha_new)], cwd=meta)
    if ahead == "0":
        raise SystemExit("%s is older than what Sheikah is synced with" % label)
    say("merging Hookshot %s (%s commits) into Sheikah (based on %s)" % (describe(meta, sha_new), ahead, d.get("describe")))
    plan, ndir = plan_merge(a, d["commit"], sha_new, meta)
    if getattr(a, "resolved", None):   # a developer's resolutions: files (same relative paths) with no conflict markers left in them
        still = []
        for rel_, why in plan["conflict"]:
            p_ = os.path.join(a.resolved, rel_)
            data = open(p_, "rb").read() if os.path.isfile(p_) else None
            if data is not None and not re.search(rb"^(<<<<<<<|=======$|>>>>>>>)", data, re.M):
                plan["merge"].append((rel_, data))
                say("  resolved %s (by hand, from %s)" % (rel_, a.resolved))
            else:
                still.append((rel_, why))
        plan["conflict"] = still
    for k in ("take", "add", "delete", "merge", "skip"):
        for it in plan[k]:
            say("  %-7s %s" % (k, it if isinstance(it, str) else it[0]))
    for rel_, why in plan["conflict"]:
        say("@@conflict %s | %s" % (rel_, why))
    say("summary: %d updated, %d added, %d removed, %d merged with your changes, %d left (removed in Sheikah), %d CONFLICTS" % (
        len(plan["take"]), len(plan["add"]), len(plan["delete"]), len(plan["merge"]), len(plan["skip"]), len(plan["conflict"])))
    if plan["conflict"]:
        say("Nothing was changed. Each conflict needs a decision about which side wins; the files with markers are in %s" %
            os.path.join(a.cache, "conflicts"))
        say("@@result conflicts")
        return
    if a.dry:
        say("dry run: nothing was changed")
        say("@@result dryok")
        return
    # apply, with a backup of every file about to change
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    bk = os.path.join(a.cache, "backups", stamp)
    def backup(rel_):
        p = os.path.join(a.fork, rel_)
        if os.path.isfile(p):
            os.makedirs(os.path.dirname(os.path.join(bk, rel_)), exist_ok=True)
            shutil.copy2(p, os.path.join(bk, rel_))
    for rel_ in plan["take"] + plan["add"] + plan["delete"] + [m[0] for m in plan["merge"]]:
        backup(rel_)
    backup(VERSION_FILE)
    for rel_ in plan["take"] + plan["add"]:
        dest = os.path.join(a.fork, rel_)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(os.path.join(ndir, rel_), dest)
    for rel_, data in plan["merge"]:
        open(os.path.join(a.fork, rel_), "wb").write(data)
    for rel_ in plan["delete"]:
        os.remove(os.path.join(a.fork, rel_))
    log = out(["log", "--no-merges", "--format=%s", "%s..%s" % (d["commit"], sha_new)], cwd=meta).splitlines()
    notes = changelog_entry(describe(meta, sha_new), d.get("describe", d["commit"][:7]), log, plan)
    open(os.path.join(a.cache, "pending-changelog.md"), "w", encoding="utf-8", newline="\n").write(notes)
    write_version(a.fork, {"commit": sha_new, "describe": describe(meta, sha_new),
                           "date": out(["log", "-1", "--format=%cs", sha_new], cwd=meta),
                           "synced": datetime.date.today().isoformat()})
    say("Done. Backup of the changed files: %s" % bk)
    say("@@result merged")


TYPES = [("feat", "New features"), ("fix", "Fixes"), ("perf", "Performance"), ("revert", "Reverts"), ("build", "Build"),
         ("refactor", "Internal changes"), ("docs", "Documentation"), ("test", "Tests"), ("chore", "Maintenance"), ("ci", "Maintenance")]


def changelog_entry(new_name, old_name, subjects, plan):
    groups, other = {}, []
    for s in subjects:
        m = re.match(r"(\w+)(\([^)]*\))?!?:\s*(.+)", s)
        if m and m.group(1) in dict(TYPES):
            groups.setdefault(dict(TYPES)[m.group(1)], []).append(m.group(3))
        else:
            other.append(s)
    lines = ["## Synced with Hookshot %s (%s)" % (new_name, datetime.date.today().isoformat()), "",
             "Sheikah Native now includes the upstream Hookshot changes from %s up to %s (%d commits)." % (old_name, new_name, len(subjects)),
             "Sheikah's own additions (live editor, flag names, item icons, cheats on by default, no update channel) were kept.", ""]
    for _, title in dict.fromkeys((t, n) for t, n in TYPES):
        items = groups.get(title)
        if items:
            lines.append("### " + title)
            lines += ["- " + x for x in items[:40]] + (["- ...and %d more" % (len(items) - 40)] if len(items) > 40 else [])
            lines.append("")
    if other:
        lines += ["### Other changes"] + ["- " + x for x in other[:20]] + [""]
    lines += ["### In this fork", "- %d files updated, %d added, %d removed, %d merged with Sheikah's changes" % (
        len(plan["take"]), len(plan["add"]), len(plan["delete"]), len(plan["merge"])), ""]
    return "\n".join(lines) + "\n"


def cmd_build(a):
    env = dict(os.environ)
    for k, v in (("HOOKSHOT_MINGW", a.mingw), ("RX_CPM", a.cpm), ("WGPU_NATIVE", a.wgpu)):
        if v:
            env[k] = v
    if a.keep:
        env["HOOKSHOT_KEEP_BUILD_FILES"] = "1"
    for label, p in (("romfs", os.path.join(a.dump, "romfs")), ("exefs", os.path.join(a.dump, "exefs"))):
        if not os.path.isdir(p):
            raise SystemExit("the game dump has no %s folder: %s" % (label, p))
    cmd = [sys.executable, "-I", os.path.join(a.fork, "nx2", "make_game.py"), "--romfs", os.path.join(a.dump, "romfs"),
           "--exefs", os.path.join(a.dump, "exefs"), "--out", a.out]
    say("@@stage build Building the game (this takes a while)")
    say(" ".join(cmd))
    p = subprocess.Popen(cmd, cwd=a.fork, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for line in p.stdout:
        say(line.decode("utf-8", "replace").rstrip())
    p.wait()
    if p.returncode:
        say("@@result builderror")
        raise SystemExit("the build failed (exit %d); the log above says where" % p.returncode)
    say("The game is in %s" % a.out)
    say("@@result built")


def forbidden(path):
    return re.search(r"(^|/)(romfs|exefs|image|gen)/|\.(nca|nsp|xci|nso|nsz|xcz|keys|bin|nup)$", path) is not None


def cmd_publish(a):
    """Commit and push to the user's repository. Uses whatever credentials git already has (Git Credential Manager);
    asks for none and stores none. Refuses anything that looks like game data or a big file."""
    fork = a.fork
    if not os.path.isdir(os.path.join(fork, ".git")):
        git(["init", "-b", a.branch], cwd=fork)
    r = git(["remote"], cwd=fork).stdout.decode().split()
    if "origin" in r:
        git(["remote", "set-url", "origin", a.remote], cwd=fork)
    else:
        git(["remote", "add", "origin", a.remote], cwd=fork)
    # (a repository that already has history, even just a starter readme: build on it, so the push is a plain fast-forward and
    # nothing of its history is thrown away)
    if git(["rev-parse", "--verify", "-q", "HEAD"], cwd=fork, check=False).returncode:
        if git(["fetch", "-q", "origin", a.branch], cwd=fork, check=False).returncode == 0:
            git(["reset", "-q", "--mixed", "FETCH_HEAD"], cwd=fork)
            say("the repository already has history on %s: building on it" % a.branch)
    d = read_version(fork)
    pend = os.path.join(a.cache, "pending-changelog.md")
    notes = open(pend, encoding="utf-8").read() if os.path.isfile(pend) else \
        "## Synced with Hookshot %s (%s)\n" % (d.get("describe", "?"), datetime.date.today().isoformat())
    if os.path.isfile(pend):
        cl = os.path.join(fork, CHANGELOG)
        old = open(cl, encoding="utf-8").read() if os.path.isfile(cl) else "# Sheikah Native changelog\n\n"
        head, _, rest = old.partition("\n\n")
        open(cl, "w", encoding="utf-8", newline="\n").write(head + "\n\n" + notes + "\n" + rest)
    git(["add", "-A"], cwd=fork)
    names = out(["diff", "--cached", "--name-only"], cwd=fork).splitlines()
    bad = [n for n in names if forbidden(n) or os.path.getsize(os.path.join(fork, n)) > 20_000_000]
    if bad:
        git(["reset", "--quiet"], cwd=fork)
        raise SystemExit("REFUSING to publish: these look like game data or are huge:\n  " + "\n  ".join(bad[:20]))
    title = "chore(upstream): sync with Hookshot %s" % d.get("describe", "")
    msg = title + "\n\n" + notes + "\nCo-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>\n"
    if names:
        git(["commit", "-q", "-m", msg], cwd=fork)
        say("committed %d files" % len(names))
    else:
        say("nothing new to commit")
    tag = "sheikah-%s-%s" % (datetime.date.today().strftime("%Y.%m.%d"), (d.get("commit") or "x")[:7])
    git(["tag", "-f", tag], cwd=fork)
    say("pushing to %s (%s) ..." % (a.remote, a.branch))
    p = git(["push", "-u", "origin", "HEAD:" + a.branch, "--quiet"], cwd=fork, check=False)
    if p.returncode:
        raise SystemExit("push failed: %s\nIf the repository already has other history, make it empty or pull it first." %
                         p.stderr.decode("utf-8", "replace").strip())
    git(["push", "origin", tag], cwd=fork, check=False)
    say("pushed branch %s and tag %s" % (a.branch, tag))
    gh = shutil.which("gh")
    if gh and not a.no_release:
        nf = os.path.join(a.cache, "release-notes.md")
        open(nf, "w", encoding="utf-8").write(notes)
        repo = re.sub(r"^https://github.com/|\.git$", "", a.remote)
        rr = subprocess.run([gh, "release", "create", tag, "--repo", repo, "--title",
                             "Sheikah Native - Hookshot %s" % d.get("describe", ""), "--notes-file", nf], capture_output=True, text=True)
        say("GitHub release created" if rr.returncode == 0 else "release not created: " + (rr.stderr or "").strip())
    elif not gh:
        say("(the GitHub command line, gh, is not installed: the changelog is in %s and the tag is pushed; install gh to also create releases)" % CHANGELOG)
    try:
        os.remove(pend)
    except OSError:
        pass
    say("@@result published")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fork", default=os.path.abspath(os.path.join(HERE, "..", "..")))
    ap.add_argument("--cache", default=os.path.join(os.environ.get("LOCALAPPDATA", tempfile.gettempdir()), "SheikahUpdater"))
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    p = sub.add_parser("detect-base"); p.add_argument("--source", required=True)
    p = sub.add_parser("merge"); p.add_argument("--dry", action="store_true"); p.add_argument("--target", default="release"); p.add_argument("--resolved", help="a folder of hand-resolved conflicted files (same relative paths, no markers)")
    p = sub.add_parser("build")
    for k in ("dump", "out"):
        p.add_argument("--" + k, required=True)
    for k in ("mingw", "cpm", "wgpu"):
        p.add_argument("--" + k, default="")
    p.add_argument("--keep", action="store_true")
    p = sub.add_parser("publish"); p.add_argument("--remote", required=True); p.add_argument("--branch", default="main")
    p.add_argument("--no-release", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.cache, exist_ok=True)
    a.fork = os.path.abspath(a.fork)
    try:
        {"status": cmd_status, "detect-base": cmd_detect_base, "merge": cmd_merge, "build": cmd_build, "publish": cmd_publish}[a.cmd](a)
    except SystemExit as e:
        if e.code not in (None, 0):
            say("ERROR: %s" % e.code)
            say("@@result error")
            sys.exit(1)


if __name__ == "__main__":
    main()
