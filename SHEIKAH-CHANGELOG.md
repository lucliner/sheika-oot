# Sheikah Native changelog

Hookshot's own changelog is at https://github.com/NotRexed/Hookshot/releases

## Sheikah Native 2026-11-13

Based on Hookshot v2.1.0. Hookshot's own changes are listed on its releases page: https://github.com/NotRexed/Hookshot/releases

### What changed in Sheikah Native
- Updated to Hookshot v2.1.0 (from v2.1.0-rc.1): shader recompiler and texture fixes, new pipeline-state tests and an AMD pipeline-state file; merged with no conflicts
- Updater: fetching upstream no longer fails when Hookshot moves its nightly tag
- Asset swap tool: updated model-replacement tools (tools/assetswap)
- Frame generation: tuned the interpolation shaders to reduce artefacts around fast motion
- Sync with Hookshot v2.1.0; asset swap tool; frame generation tweaks


## Sheikah Native 2026-11-10

Based on Hookshot v2.1.0-rc.1. Hookshot's own changes are listed on its releases page: https://github.com/NotRexed/Hookshot/releases

### What changed in Sheikah Native
- Visual live editor: press F2 (or choose Editor in the Insert menu) for a window with four tabs: an item grid with drawn icons (click to give or take, counts for quantities), a map of places to travel to, a time-of-day dial with an age switch, and a searchable board of the game's flags (26,654 entries, about 4,600 with their real names)
- The editor changes the running game only; it never writes to your save file. It keeps working while the world is paused, and can open and close the pause menu so the HUD lays itself out again after an edit
- Item icons are the game's own textures, decoded on your PC from your own game files at build time and never shipped; without them the editor draws simple shapes
- Flag and item names are generated at build time from your own game files (nx2/gamedata_names.py); without them the editor lists entries by hash
- Cheats are on by default: flight, free camera, teleport, inventory, time and the age switch in the Insert menu no longer need HOS_OOT_CHEATS=1 (HOS_OOT_CHEATS=0 turns them off)
- Inventory list fixes: the game's own items keep their real names (Hookshot, Longshot, ...) instead of being renamed by the rebrand, and the three Deku Stick bags were added
- Rebranded to Sheikah Native: names, executable and data files, and the shader cache name (no space in the kernel object's name); Hookshot's credits, the GPL license and its attribution are kept (docs/upstream-readme.md)
- No update channel: the game never offers to replace itself with Hookshot's builds
- New: the game tells the player when Hookshot has a newer release than the one Sheikah is based on, and to contact the developer; it only reads public version information, runs in the background, stays silent offline, and downloads nothing (SHEIKAH_NO_UPSTREAM_CHECK=1 turns it off)
- New: Sheikah Builder, a small Windows program that builds the game from your own dump, with a progress display and remembered settings
- New: a changelog for Sheikah only (this one); Hookshot's own changes are on its releases page
- Settings: the Builder remembers your folders between runs and never loses them when it is updated
- DLSS: the Builder has an optional NVIDIA DLSS SDK folder field. With it the game is built with NVIDIA DLSS Super Resolution (Hookshot's DLSS back end); NVIDIA's SDK files are never shipped, each player fetches them with scripts/ngx_sdk.py
- New: the Sheikah Native logo at the top of the readme


