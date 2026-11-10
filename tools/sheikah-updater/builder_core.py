"""Sheikah Builder's engine (the window, Sheikah-Builder.exe, runs it): builds the game from the player's own dump.

    python -I builder_core.py [--fork DIR] build --dump DIR --out DIR [--mingw DIR] [--cpm DIR] [--wgpu DIR] [--keep]

Runs nx2/make_game.py. Lines starting with @@ are for the window (@@stage, @@progress, @@result). The dump is only read from the
player's own disk; nothing is uploaded anywhere. This file does nothing else.
"""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def say(*a):
    print(*a, flush=True)


def cmd_build(a):
    env = dict(os.environ)
    for k, v in (("HOOKSHOT_MINGW", a.mingw), ("RX_CPM", a.cpm), ("WGPU_NATIVE", a.wgpu)):
        if v:
            env[k] = v
    if a.keep:
        env["HOOKSHOT_KEEP_BUILD_FILES"] = "1"
    for label in ("romfs", "exefs"):
        if not os.path.isdir(os.path.join(a.dump, label)):
            raise SystemExit("the game dump has no %s folder: %s" % (label, os.path.join(a.dump, label)))
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fork", default=os.path.abspath(os.path.join(HERE, "..", "..")))
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("build")
    for k in ("dump", "out"):
        p.add_argument("--" + k, required=True)
    for k in ("mingw", "cpm", "wgpu"):
        p.add_argument("--" + k, default="")
    p.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    a.fork = os.path.abspath(a.fork)
    try:
        cmd_build(a)
    except SystemExit as e:
        if e.code not in (None, 0):
            say("ERROR: %s" % e.code)
            say("@@result error")
            sys.exit(1)


if __name__ == "__main__":
    main()
