# Sheikah Native changelog

## Synced with Hookshot v2.0.14 (2026-11-10)

Sheikah Native now includes the upstream Hookshot changes from v2.0.11-312-ge7b54fd up to v2.0.14 (51 commits).
Sheikah's own additions (live editor, flag names, item icons, cheats on by default, no update channel) were kept.

### New features
- smoother play, HDR10, a frame rate cap, Discord status, the Debug tab, sharper shadows (#1485)
- the Hookshot Builder in Python, the same window without an executable of Hookshot's own (#1483)
- warn at boot when memory and swap are below what the game needs

### Fixes
- escape the build folder in glob patterns (a work folder with [ ] found no lifted code) (#1722) (#1758)
- the menu, FPS counter and start-up screen in HDR10's format with HDR on (v2.0.13 line, #1613) (#1743)
- the FPS counter keeps the screen's scale when the menu is drawn smaller to fit (v2.0.13 line) (#1680)
- Discord status starts on Windows (it was only started in the Linux build's main) (#1539)
- remove duplicate motion deadzone formatter (#1496)
- require consecutive frames for flash analysis (#1467)
- quicker, steadier updates: compiled objects kept, a cut-short lift translated again, 45 GB needed (#1484)
- render UTF-8 text in the Insert menu (#1389)
- skip menu fonts that have no Latin letters (#1405)
- recover from failed swapchain replacement (#1403)
- preserve the picture display aspect when resizing
- validate cache paths before building from extracted folders
- honor the menu font face selected by Fontconfig

### Build
- the release does not carry the Python builder (builder/py stays in the repo, unused; the WPF builder is what players get) (#1486)

### Documentation
- a shorter AGENTS.md that points to CONTRIBUTING.md
- PR descriptions open with why; screenshots for visible changes; AGENTS.md

### Tests
- restore renderer regressions after main changes (#1498)

### Maintenance
- the AppImage's package install tried three times, each after a fresh apt-get update (a mirror 404 failed a run) (#1491)

### Maintenance
- the AppImage's package install tried three times, each after a fresh apt-get update (a mirror 404 failed a run) (#1491)

### Other changes
- v2.0.13: the menu fits the screen (F1/Insert at 1440p, 4K, small windows); the game at real speed above a 60 fps cap (#1612)
- Hookshot menu shots: drawn into the shot only headless
- Hookshot menu: Button prompts' Auto hint says when it falls back to the Switch's
- Button prompts: the faces in one enum; the menu never starts their setup
- Button prompts: the Linux AppImage carries their controller fonts
- tests: the device-lost check lets host fields follow log_flush
- Hookshot menu: the Controls page's rows say "(after restart)" as the tabs' rows do
- Button prompts: their packages in the tools' Python, on Linux and Windows
- Button prompts: a mod's own files win over the prompts
- Button prompts: the drawn controllers placed and sized from photos, sharper
- Hookshot menu: the Controls page names the pad inputs as the prompts' controller does
- Hookshot menu: the Controls page says the icons follow after a restart
- Button prompts: the ocarina hint makes room for wide icons
- Button prompts: every face image from a source, dumpable and replaceable by hand
- Button prompts: the Talk ring puts A B X Y where their buttons are
- Button prompts: each face's controller and leader lines in the controls page
- Headless shots: the Insert menu with RX_VK_SHOT_MENU
- Hookshot menu: the layout preset and the button prompts on the Controls page
- Button prompts: each controller face's icons, following the controls
- Harden what the security scan flagged: https-only downloads, no strtok in setup, SHA-1 cache keys marked not for security (#1470)

### In this fork
- 73 files updated, 159 added, 0 removed, 11 merged with Sheikah's changes


