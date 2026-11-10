# The upstream notice

Sheikah Native is only as new as the [Hookshot](https://github.com/NotRexed/Hookshot) version it was last merged with, which is
recorded in `docs/upstream-version.txt` (written by the Sheikah Publisher, `tools/sheikah-updater/`). The build bakes it into the
game as `SHEIKAH_UPSTREAM` (`nx2/version.py`, `nx2/build.py` -> `host/hos_upstream.c`).

About 8 seconds after the game starts, a background thread (`host/hos_upstream.c`, started from `hos_update_check`) asks GitHub's
public API, anonymously and with a generic user agent, for upstream's latest release and compares it with the baked commit
(`/releases/latest`, then `/compare/<baked>...<release>`). Nothing about the player, the PC or the game is sent. Offline, rate
limited or any other failure: nothing is shown. The answer is cached for 6 hours in `user/sheikah_upstream_check.txt`.

If the release is ahead:

- a message box (on that thread; the game keeps running) tells the player a newer Hookshot is out and to **contact the Sheikah
  developer** — once per upstream release (`user/sheikah_upstream_seen.txt`);
- the Insert menu's title bar carries a short amber note for the rest of the session.

It never offers to download or replace anything. (`hos_update_check`'s upstream updater stays disabled.)

| Switch | Effect |
|---|---|
| `SHEIKAH_NO_UPSTREAM_CHECK=1` (or `HOOKSHOT_NO_UPDATE_CHECK=1`) | no check at all |
| `SHEIKAH_UPSTREAM_CHECK=commits` | compare with upstream's newest commit instead of its newest release |
| `SHEIKAH_UPSTREAM_FAKE=v9.9.9` | pretend upstream is at that tag, no network, the box shows every start: to see the notice |

The switches go in the environment or in `sheikah_env.txt` in the game folder.
