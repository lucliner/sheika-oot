"""Menu fitting must not shrink the FPS bitmap or its margins. No GDI, Vulkan or game."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = ("font2160", "closed2160", "margins2160", "font1440", "control900", "control720", "resize")


def block(text, marker):
    start = text.index(marker)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    p.add_argument("--source", type=Path, default=ROOT / "gpu/vk/vk_menu.cpp")
    p.add_argument("--case", action="append", choices=CASES)
    a = p.parse_args()
    cxx = shutil.which(a.cxx)
    if not cxx:
        raise SystemExit("compiler not found: " + a.cxx)
    source = a.source.read_text(encoding="utf-8")
    tick = source[source.index("void menu_tick()"):source.index("void menu_record(")]
    fps = block(tick, "    if (opts.show_fps) {")
    selector = next(line.strip() for line in tick.splitlines() if "const int scale =" in line)
    actual = "\n\n".join(block(source, marker) for marker in
                           ("int scale_of(", "void redraw_menu_at(", "int menu_scale_for(", "void redraw_menu(", "void menu_record("))
    actual += "\nint next_scale() {\n" + selector + "\nreturn scale;\n}\n"
    actual += "void draw_fps(int scale) { (void)scale;\n" + fps + "\n}\n"
    env = dict(os.environ)
    for name in ("INCLUDE", "LIB", "LIBPATH"):
        env.pop(name, None)
    env["PATH"] = str(Path(cxx).parent) + os.pathsep + env.get("PATH", "")
    flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name == "nt" else 0
    with tempfile.TemporaryDirectory(prefix="sheikah-menu-fps-scale-") as folder:
        tmp = Path(folder)
        (tmp / "actual_menu.inc").write_text(actual, encoding="utf-8")
        exe = tmp / ("fps.exe" if os.name == "nt" else "fps")
        command = [cxx, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-I" + str(tmp),
                   "-I" + str(ROOT / "gpu/vk"), str(ROOT / "tests/menu_fps_scale_test.cpp"), "-o", str(exe)]
        built = subprocess.run(command, cwd=tmp, env=env, capture_output=True, text=True, timeout=60, creationflags=flags)
        if built.returncode:
            print(built.stdout + built.stderr)
            return built.returncode
        failed = 0
        for case in a.case or CASES:
            result = subprocess.run([str(exe), case], cwd=tmp, env=env, capture_output=True, text=True, timeout=10, creationflags=flags)
            print((result.stdout + result.stderr).strip())
            failed += result.returncode != 0
        print("menu/FPS scale: %d cases, %d failed" % (len(a.case or CASES), failed))
        return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
