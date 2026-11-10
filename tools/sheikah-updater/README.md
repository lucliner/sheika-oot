# Sheikah Builder and Sheikah Publisher

One source (`sheikah_updater.c`), two small Windows programs, built by `build.ps1`:

| Program | For | Has |
|---|---|---|
| `Sheikah-Builder.exe` | **players** (this is what goes in the GitHub release) | the folders (game dump, tools, output) and a **Build the game** button. Nothing about git, GitHub or merging: that code is not even compiled in |
| `Sheikah-Publisher.exe` | **the developer only** (never shipped) | everything below: versions, merge preview, update + build, and publishing to GitHub |

The rest of this page describes the Publisher; the Builder is only its build step. Each has its own Python engine beside it:
`builder_core.py` (public: only runs the build) and `publisher_core.py` (developer only: merge, version check, GitHub; listed in
`.gitignore`, so it is not published with the repository).

A small Windows program (`Sheikah-Publisher.exe`, about 26 KB, nothing to install for the window itself) that keeps Sheikah Native
in step with its upstream, [Hookshot](https://github.com/NotRexed/Hookshot) by NotRexed:

1. shows the Hookshot version Sheikah is synced with and the latest upstream release;
2. **Preview merge**: a dry run that lists every file upstream changed and says which ones can be merged cleanly;
3. **Update + build**: merges upstream's changes into the Sheikah folder, runs the build, and (if ticked) publishes the result to
   your own GitHub repository with a generated changelog;
4. remembers your folders and settings (`%LOCALAPPDATA%\SheikahUpdater\settings.ini` (the Builder: `...\SheikahBuilder\settings.ini`).

It needs **Git** and **Python 3** on the PATH (the game's build needs Python anyway) and, for building, the same tools as a normal
build: llvm-mingw, the CPM cache and wgpu-native (see `docs/development.md`). It reads your game dump from your own disk only; it
never copies, uploads or commits game files.

Build the program (needs only llvm-mingw):

```powershell
powershell -ExecutionPolicy Bypass -File tools\sheikah-updater\build.ps1
```

## How the merge works

Sheikah is Hookshot plus our changes. `docs/upstream-version.txt` records which upstream commit Sheikah is synced with. For an update
the engine (`publisher_core.py`):

1. fetches upstream's history (a small cache in `%LOCALAPPDATA%\SheikahUpdater`);
2. extracts upstream **at the old synced commit** and **at the new version**, and runs `tools/rebrand.py` on both, so that upstream's
   new text is rebranded exactly as the fork was;
3. does a three-way merge per file: *old upstream* (base), *your Sheikah file* (ours) and *new upstream* (theirs). A file only
   upstream changed is replaced; a file only Sheikah changed is left; a file both changed is merged with `git merge-file`;
4. **if anything conflicts, nothing is written.** The conflicting files are listed (`@@conflict` lines in the log) and the merged
   files with `<<<<<<<` markers are left in `%LOCALAPPDATA%\SheikahUpdater\conflicts` for the developer to look at. The updater
   never guesses which side wins;
5. when clean, backs up each file it is about to change (`...\SheikahUpdater\backups\<time>`), applies the changes, and records the
   new version in `docs/upstream-version.txt`. The build bakes that into the game (`nx2/version.py`), which is how the in-game
   notice knows what Sheikah is based on.

Files upstream has that Sheikah leaves out on purpose (`builder/`, `.github/`, `package.json`, ...) are skipped. A file Sheikah
deleted stays deleted. Files only Sheikah has (the editor, the icons, ...) are never touched.

When there are conflicts (this is expected when upstream edits the same few lines as one of our small hooks in `host/hos_main.c`,
`gpu/vk/vk_menu.cpp`, `nx2/make_game.py`, ...): resolve them by hand against the file in `conflicts\`, or ask for help, and run
the update again.

## Build step

Runs `python -I nx2/make_game.py --romfs <dump>\romfs --exefs <dump>\exefs --out <output>` with `HOOKSHOT_MINGW`, `RX_CPM` and
`WGPU_NATIVE` from the fields, and `HOOKSHOT_KEEP_BUILD_FILES=1` when ticked. The build's own progress (`@@progress`, `@@stage`)
drives the progress bar. When it finishes, **Open game folder** shows the built game.

## Publish step (your GitHub, without touching the website)

Tick **Publish to my GitHub** (or press **Publish only**) and give the repository URL once. After a clean update the updater:

- writes the **Sheikah** changelog entry into `SHEIKAH-CHANGELOG.md`: only the changes Sheikah itself has had (the bullets you
  keep in `docs/sheikah-changes.txt`, which the updater empties after each release, plus the subjects of your own commits since
  the last release) and a line pointing to [Hookshot's releases](https://github.com/NotRexed/Hookshot/releases) for Hookshot's
  own changelog, which is never copied. The same text is the commit message and the release notes;
- commits, tags (`sheikah-<date>`) and pushes with the credentials Git already has (Git Credential Manager). The first push needs
  you to sign in once (the updater prints the one command to run);
- creates a **GitHub release** with those notes and attaches `Sheikah-Builder.exe`. It uses the same Git sign-in, only to talk to
  your repository's release API. **The game itself is never uploaded**: its executable and DLLs are made from your own copy of the
  game (see the readme: never share them), so every player builds it from their own dump with the updater/builder.

It asks for a confirmation (showing the repository) before every push, and **refuses to publish** if any staged file looks like
game data (romfs/exefs/image folders, `.nca .nsp .xci .nso .keys .bin`, ...) or is over 20 MB. `.gitignore` already excludes
`work/`, build output and keys.

## Command line

The window is a front end for `publisher_core.py`, which can be used directly (`python -I publisher_core.py --help`):

```powershell
python -I tools\sheikah-updater\publisher_core.py status
python -I tools\sheikah-updater\publisher_core.py merge --dry
python -I tools\sheikah-updater\publisher_core.py detect-base --source E:\Hookshot-main   # only to (re)create docs/upstream-version.txt
```

## The in-game notice

See `docs/upstream-notice.md`: the game itself checks (off the main thread, silently offline) whether upstream has a newer release
than `docs/upstream-version.txt` and tells the player to contact the developer. It never downloads or replaces anything.
