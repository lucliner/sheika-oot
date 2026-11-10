# The visual editor

Press **F2** in the game, or choose **Editor** on the Gameplay tab of the Insert (F1) menu, to open the editor in a window of
its own. F2 or Esc closes it again. It changes the running game; nothing is written to the save file by the editor, and
whatever you change is saved only when the game itself saves. Changes are made while you are in the world: before that the
header says "Waiting for gameplay" and the editor tells you so instead of queueing changes.

## The tabs

**Items.** Every item, upgrade, mask, song and medallion the game's inventory knows, in section tabs (Equipment, Items, Masks,
Upgrades, Quest), each a tile with the game's icon, lit when Link has it. Click a tile to give the item or take it away. Quantities (bombs, arrows, seeds, sticks, nuts, beans, skulltula
tokens) show their count in a badge: left click adds 1, right click takes 1 away, Shift makes it 10. The mouse wheel scrolls.

**Map.** The game's world map, with a marker on each place the game can travel to (a skull is a dungeon; the name shows when you
point at one). A click travels there through the game's own map change, as Link's current age; the editor then hides itself.

**Link.** Hearts (click a heart; past the last one adds a container), rupees, and the magic bar, as large controls.

**World.** The time of day as a dial of 24 hours, noon at the top: click an hour to set the clock. Switch age swaps Link
between child and adult and reloads the map as the other age.

**Flags.** The game's flags, the entries its save is made of (story progress, dungeon state, NPC and event flags), as chips
with a switch. Type to search (every word must appear in the name, in any order); a click flips a flag. Entries whose name is
not known are listed by hash.

## The game's art

Everything the editor shows is **the game's own art**, taken from the romfs you supply: the build (`nx2/make_game.py`, through
`nx2/item_icons.py`) decodes the game's textures to small PNGs in `sheikah_icons/` beside the game, and the editor loads them from
there:

- the item icons (`UI/Tex/Icon/*.bntx.zs`, one per pouch item), and the stones, medallions and songs from the shared UI textures
  (`UI/Layout2Archive/Common.*.bntx.zs`: `DataFileImportantIcon_*` and `OcarinaMelodyIcon_*`, the songs' white glyphs tinted by
  the build);
- the world map (`MapTexWorldAll`) behind the Map tab's markers, the boss skull for dungeons, the hearts, the rupee and the
  magic gauge of the Link tab, and the sword and shield icons of the Age switch.

None of it is in this repository or shipped: it is made on your PC from your copy, like the rest of the build's output, and is not
to be shared. Where an image is missing (a build without the folder, a texture a future version drops) the editor draws a small
shape of its own instead, so it still works. The markers' positions on the world map are fitted by hand (`place_xy` in
`host/editor_window.c`).

## Where the data comes from

- Items, travel, time and age use what Hookshot's cheats already used: the game's own inventory ("pouch") functions, its map
  change event and its clock, reached through one action slot that the player's tick takes (`hos_oot_live_action`).
- Flags come from the game's data list. It holds only 32-bit hashes (MurMur3 of the name). `nx2/gamedata_names.py` finds names by
  hashing every identifier-like string in the data files of **your own** romfs (events, logic, messages, parameters) and in
  the game's code, and keeping those that match. At the moment that names about 4,600 of 26,654 entries. The result,
  `sheikah_gamedata.tsv`, is made by the build (`nx2/make_game.py`), kept in the work folder, and copied beside
  `sheikah.exe`. It is derived from your game: do not share it.

## How it works

- `host/editor_window.c`: the window. It runs on a thread of its own with its own message loop, and draws with GDI+ (the
  shapes and icons, anti-aliased) and GDI (the text). It needs `-lgdiplus`. The icons are drawn in code, a few shapes each.
- `host/oot_live_editor.inc` (included by `host/hos_main.c`): loads the flag names, answers the window's searches, and on the
  game thread once a frame reads the shown flags and applies queued edits, with the game's own flag getter and a direct write
  to `GameDataMgr`'s flag table.
- `gpu/vk/vk_menu.cpp`: F2 and the menu's Editor row ask the host to open the window (`MkwVkHost.editor_toggle`, `editor_open`).
- `tests/editor_render.c`: draws every tab with made-up data to `.bmp` files, so the layout can be looked at without the game.
  `clang -DEDITOR_TEST -O1 tests/editor_render.c -o editor_render.exe -lgdiplus -lgdi32 -luser32`, then
  `editor_render.exe <out dir> work/gamedata.tsv`.

## While the game is paused

The editor also works over the inventory screen and other menus: item, flag and number edits are made from the game's input
polling (`hos_oot_idle_tick`, `host/hos_main.c`, called from `host/nn_hid.c`) when the player's tick has stopped. Travel, time and
age need the world and wait until you leave the menu. Giving a counted item (sticks, nuts, bombs, arrows, seeds) with no bag
for it gives the smallest bag first.

## Limits

- Numbers other than hearts, rupees and magic are not editable yet. Those three (the **Link** tab) are `PlayerStatus.Life`,
  `MaxLife`, `CurrentRupee`, `MP` and `MPMax` in `GameDataMgr`'s third container (40-byte entries: default at +24, value
  at +28, a copy at +32). Hearts also live in Link's own object (`player+7184`: Life at +8, MaxLife at +24, which the game
  copies back into GameData every frame), so those are written too. Life is in quarter hearts (80 is 20 hearts). The
  magic bar (240 single, 480 double: a save with both blessings holds 480) was set from a headless test only; writing magic is untested in play.
  When the heart rows change (past 10 hearts, or back) the editor also opens and closes the pause menu for a moment (two scripted
  presses, Plus then B, taken through the game's own input): the game lays the magic bar out under the hearts only when the HUD
  is built, and the pause menu is what builds it anew. `HOS_EDITOR_NO_HUD_REFRESH=1` turns that off.
  The HUD draws new hearts correctly only when the new maximum comes first: a life write is held 30 ticks after a maximum's
  (written together, the hearts show empty but for the last). `HOS_GD_TEST2` / `HOS_GD_TEST3` (same format as below) write
  again 600 and 700 ticks after gameplay starts, to test that.
  `HOS_GD_PROBE=1` logs the containers; `HOS_GD_TEST=rupee,life,maxlife,mp,mpmax` writes values once (-1 skips).
- A flag that the game keeps in its second (dynamic) table can be read but not written: the log says so.
- The inventory's item names are the game's own pouch names; if the game does not know one, its tile stays dark.
- Flipping flags can leave the game in states it never meets: events that do not start, doors that stay shut. Use a copy of
  your save.
