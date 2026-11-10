# Sheikah Native

*The Legend of Zelda: Ocarina of Time* (Nintendo Switch 2) running natively on Windows, built on your PC from your own
copy of the game. No emulator.

Sheikah Native is a fork of [Hookshot](https://github.com/NotRexed/Hookshot) by NotRexed, a static recompilation: the
game's code is translated to C and compiled into a Windows program, the console's system software is reimplemented, and
the Switch 2 graphics are replayed on Vulkan. Everything the build makes (the executable, the generated code, the shader
caches, your saves) comes from your game and stays on your PC. Never share those files.

> [!IMPORTANT]
> This repository does **not** contain the game, keys or an executable, and it does not explain how to get them. You need
> your own dump of the supported version: program `0400486024DEC000`, update v131072, build `e0f3ccfa`
> (`exefs` and `romfs` folders).

## What this fork adds

- **Visual editor (F2).** Press **F2**, or choose **Editor** in the F1 menu, to open a window of its own with five tabs,
  all drawn with the game's own art taken from your romfs: item tabs (equipment, items, masks, upgrades, quest) with the game's
  icons, Link's hearts, rupees and magic, the game's world map to travel by, a time-of-day dial with the age switch, and a
  searchable board of the game's flags (26,654 entries, about 4,600 with their real names).
  It changes the **running** game; nothing is written to the save file by the editor. See [docs/live-editor.md](docs/live-editor.md).
- **Cheats on by default.** The Insert menu's Cheats page (flight, teleport, inventory, time, age switch) no longer
  needs `HOS_OOT_CHEATS=1`. `HOS_OOT_CHEATS=0` turns it off.
- **No update channel.** The game never offers to replace itself with the upstream project's builds.
- **Upstream notice.** The game checks (in the background, silently offline) whether Hookshot has a newer release than the one
  Sheikah is synced with and tells the player to contact the developer. It never downloads anything. See
  [docs/upstream-notice.md](docs/upstream-notice.md).
- **Sheikah Updater.** `tools/sheikah-updater/`: a small Windows program that merges a new Hookshot version into this fork,
  rebuilds the game and publishes it with a generated changelog. See [its readme](tools/sheikah-updater/README.md).

## Build from source (Windows)

1. Python 3 from python.org (not the Store build) and the pipeline's packages:

   ```powershell
   python -m pip install lz4 numpy capstone "unicorn>=2.1,<2.2" zstandard byml
   ```

2. [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (the `ucrt-x86_64` build): set `HOOKSHOT_MINGW` to its folder.
3. A CPM package cache with the Vulkan headers, VMA and FFmpeg (`RX_CPM`) and the WebGPU runtime (`WGPU_NATIVE`), as
   [CONTRIBUTING.md](CONTRIBUTING.md#development-setup) describes.
4. Build, from this folder:

   ```powershell
   python -I nx2/make_game.py --romfs <dump>\romfs --exefs <dump>\exefs --out C:\Games\Sheikah
   ```

   It takes 10 to 45 minutes and about 60 GB of disk. Then start `sheikah.exe` in the output folder.

The build keeps the upstream `HOOKSHOT_*` names for its environment switches (`HOOKSHOT_MINGW`, `HOOKSHOT_JOBS`, ...), so
upstream's documentation of them still applies. The upstream readme, with its status table and the switches for testing,
is kept in [docs/upstream-readme.md](docs/upstream-readme.md).

## Status

Experimental, like the project it is built on: reaching the title screen and gameplay works; the open problems are the
upstream ones (some areas, some graphics cards). The editor edits hearts, rupees and magic (the Link tab); other counters are
not mapped yet.

## Credits and license

All the hard work (the lifter, the Horizon OS and NVN2 layers, the Vulkan renderer, the patches) is the work of the
people credited in [docs/upstream-readme.md](docs/upstream-readme.md#credits): NotRexed and the Hookshot contributors,
Eden, suyu, Orchid, the a64-kit authors and the others named there. This fork only adds the changes listed above.

Sheikah Native is free software under the [GNU General Public License, version 3 or later](LICENSE). Third-party parts keep
their own notices in their folders. It is an unofficial fan project, not affiliated with or endorsed by Nintendo. The
Legend of Zelda and Ocarina of Time are trademarks of Nintendo.
