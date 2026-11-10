/* (sheikah) The live editor: the game's GameData (every flag the save holds) by name, read and written while the game
   runs. Included by hos_main.c after oot_gamedata_set_bool.

   The names come from sheikah_gamedata.tsv beside the executable (nx2/gamedata_names.py makes it from the player's own
   romfs: one entry a line, "hash<TAB>kind<TAB>default<TAB>name", the names the game's data files mention; the rest are
   listed by hash). The menu (gpu/vk/vk_menu.cpp, page 6) searches and shows rows through the MkwVkHost gd_* functions
   below, from its own thread. Everything that touches the game's memory happens on the game thread, in
   oot_gd_tick, called from the player's tick: the menu only asks (gd_view: which rows it shows; gd_edit: what to do).
   Only flags (bools) can be read and written so far: the game's other value types keep their own tables, not yet
   mapped, and show as "n/a". */

typedef struct {
    uint32_t hash;
    uint8_t kind;       /* nx2/gamedata_names.py kind_of: 0 bool, 1 int, 2 float, 3 string, 4 other */
    int8_t state;       /* the last read: -2 not read, -1 not a bool in the game's tables, 0 off, 1 on */
    char *name;         /* NULL: listed by hash */
} GdEntry;

#define GD_VIEW_MAX 128
#define GD_EDITS_MAX 16

static struct {
    CRITICAL_SECTION lock;
    int ready, loaded;
    GdEntry *entries;
    int count;
    int *match;                   /* the entries the last search found, in list order */
    int match_count;
    int view[GD_VIEW_MAX], view_count;   /* the matches the menu shows now: the game thread keeps their state fresh */
    struct { int entry, op; } edits[GD_EDITS_MAX];   /* op 0 flips, 1 turns off, 2 turns on */
    int edit_count;
    char *blob;
} gd;

static void gd_init(void) {
    if (gd.ready) return;
    InitializeCriticalSection(&gd.lock);
    gd.ready = 1;
}

static void gd_load(void) {
    if (gd.loaded) return;
    gd.loaded = 1;
    char path[MAX_PATH + 64];
    DWORD n = hos_game_path_a(path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    while (n && path[n - 1] != '/' && path[n - 1] != '\\') n--;
    snprintf(path + n, sizeof path - n, "sheikah_gamedata.tsv");
    FILE *f = fopen(path, "rb");
    if (!f) {
        hos_log("editor: %s not found: the live editor has no names (nx2/gamedata_names.py makes it)", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *blob = size > 0 && size < (64 << 20) ? (char *)malloc((size_t)size + 1) : NULL;
    if (!blob || fread(blob, 1, (size_t)size, f) != (size_t)size) {
        free(blob);
        fclose(f);
        return;
    }
    fclose(f);
    blob[size] = 0;
    int lines = 0;
    for (long i = 0; i < size; i++) lines += blob[i] == '\n';
    GdEntry *entries = (GdEntry *)calloc((size_t)lines + 1, sizeof *entries);
    int *match = (int *)malloc(((size_t)lines + 1) * sizeof *match);
    if (!entries || !match) {
        free(blob), free(entries), free(match);
        return;
    }
    int count = 0;
    for (char *p = blob; *p;) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = 0;
        char *tab1 = strchr(p, '\t'), *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL, *tab3 = tab2 ? strchr(tab2 + 1, '\t') : NULL;
        if (tab3) {
            *tab1 = *tab2 = *tab3 = 0;
            size_t len = strlen(tab3 + 1);
            if (len && tab3[len] == '\r') tab3[len] = 0;
            GdEntry *e = &entries[count++];
            e->hash = (uint32_t)strtoul(p, NULL, 16);
            e->kind = (uint8_t)atoi(tab1 + 1);
            e->state = -2;
            e->name = strcmp(tab3 + 1, "-") ? tab3 + 1 : NULL;
        }
        if (!eol) break;
        p = eol + 1;
    }
    EnterCriticalSection(&gd.lock);
    gd.blob = blob, gd.entries = entries, gd.count = count, gd.match = match;
    for (int i = 0; i < count; i++) match[i] = i;
    gd.match_count = count;
    LeaveCriticalSection(&gd.lock);
    hos_log("editor: %d game data entries from %s", count, path);
}

static int gd_contains(const char *hay, const char *needle) {   /* (case-insensitive; every word of the needle, any order) */
    char word[64];
    const char *p = needle;
    for (;;) {
        while (*p == ' ') p++;
        if (!*p) return 1;
        size_t n = 0;
        while (p[n] && p[n] != ' ' && n + 1 < sizeof word) word[n] = (char)((p[n] >= 'A' && p[n] <= 'Z') ? p[n] + 32 : p[n]), n++;
        word[n] = 0;
        p += n;
        int found = 0;
        for (const char *h = hay; *h && !found; h++) {
            size_t i = 0;
            while (word[i] && h[i] && ((h[i] >= 'A' && h[i] <= 'Z') ? h[i] + 32 : h[i]) == word[i]) i++;
            found = !word[i];
        }
        if (!found) return 0;
    }
}

/* Searches the entries (the menu's thread): returns how many match. Named entries are matched on their name, any
   entry on its hash in hex; kind_filter -1 lists every kind. */
int hos_gd_search(const char *needle, int kind_filter) {
    gd_init();
    gd_load();
    EnterCriticalSection(&gd.lock);
    int n = 0;
    char hex[16];
    for (int i = 0; i < gd.count; i++) {
        const GdEntry *e = &gd.entries[i];
        if (kind_filter >= 0 && e->kind != kind_filter) continue;
        if (*needle) {
            snprintf(hex, sizeof hex, "%08x", e->hash);
            if (!(e->name ? gd_contains(e->name, needle) : 0) && !gd_contains(hex, needle)) continue;
        }
        gd.match[n++] = i;
    }
    gd.match_count = n;
    gd.view_count = 0;
    LeaveCriticalSection(&gd.lock);
    return n;
}

/* One row of the last search: its name (or its hash), the value as text ("On", "Off", "n/a", "..." not read yet).
   Returns 0 when there is no such row, else 1 + whether the row can be edited. */
int hos_gd_row(int match, char *name, int name_cap, char *value, int value_cap) {
    gd_init();
    int result = 0;
    EnterCriticalSection(&gd.lock);
    if (match >= 0 && match < gd.match_count) {
        const GdEntry *e = &gd.entries[gd.match[match]];
        if (e->name) snprintf(name, (size_t)name_cap, "%s", e->name);
        else snprintf(name, (size_t)name_cap, "#%08x", e->hash);
        snprintf(value, (size_t)value_cap, "%s", e->state == 1 ? "On" : e->state == 0 ? "Off" : e->state == -1 ? "n/a" : "...");
        result = 1 + (e->state >= 0);
    }
    LeaveCriticalSection(&gd.lock);
    return result;
}

/* The rows the menu shows now (first match and how many): the game thread keeps those fresh. */
void hos_gd_view(int first, int count) {
    gd_init();
    EnterCriticalSection(&gd.lock);
    gd.view_count = 0;
    for (int i = 0; i < count && i < GD_VIEW_MAX; i++)
        if (first + i >= 0 && first + i < gd.match_count) gd.view[gd.view_count++] = gd.match[first + i];
    LeaveCriticalSection(&gd.lock);
}

/* An edit of a row of the last search, done on the game thread: op 0 flips the flag, 1 turns it off, 2 turns it on. */
void hos_gd_edit(int match, int op) {
    gd_init();
    EnterCriticalSection(&gd.lock);
    if (match >= 0 && match < gd.match_count && gd.edit_count < GD_EDITS_MAX) {
        gd.edits[gd.edit_count].entry = gd.match[match];
        gd.edits[gd.edit_count++].op = op;
    }
    LeaveCriticalSection(&gd.lock);
}

/* The game thread, once a frame (the player's tick): the shown rows read, the queued edits applied. */
static void oot_gd_probe(Ctx *c) {   /* HOS_GD_PROBE: logs the containers of GameDataMgr and where three known ints live, once */
    static int done;
    if (done || !getenv("HOS_GD_PROBE")) return;
    const uint64_t mgr = oot_singleton(c, OOT_GAMEDATA_SLOT);
    if (!mgr) return;
    done = 1;
    static const uint32_t want[3] = {0xa77921d7u, 0x31ab5580u, 0x12d63a97u};
    for (int k = 0; k < 14; k++) {
        const uint64_t base = mgr + 0x10 + 0x60ULL * k;
        const uint32_t count = recomp_load32(c, base), size = recomp_load32(c, base + 28);
        const uint64_t entries = recomp_load64(c, base + 8), table = recomp_load64(c, base + 16);
        hos_log("gdprobe: container %d count %u entries %llx table %llx size %u", k, count, (unsigned long long)entries, (unsigned long long)table, size);
        if (!table || !size || !entries) continue;
        for (int w = 0; w < 3; w++)
            for (uint32_t probe = 0, slot = want[w] % size; probe < size; probe++, slot = (slot + 1) % size) {
                const uint32_t key = recomp_load32(c, table + slot * 8ULL);
                if (!key) break;
                if (key != want[w]) continue;
                const uint32_t index = recomp_load32(c, table + slot * 8ULL + 4);
                hos_log("gdprobe: %08x is container %d index %u", want[w], k, index);
                for (int e = 0; e < 3; e++) {
                    char hex[200] = {0};
                    for (int b = 0; b < 40; b++) snprintf(hex + b * 3, 4, "%02x ", recomp_load8(c, entries + (index + e) * 40ULL + b));
                    hos_log("gdprobe: entry %u (40 byte stride): %s", index + e, hex);
                }
                break;
            }
    }
}

/* The numbers: PlayerStatus.* ints live in GameDataMgr's third container (+0x70, 0x60 after the bools'), 40 byte entries:
   +24 the default, +28 the value in play, +32 a copy of it. Found by probing the running game (HOS_GD_PROBE). Life is in
   quarter hearts, MPMax in the game's magic units (480 for the first bar is what a save with magic holds). */
#define OOT_INT_CONTAINER 2
static uint64_t oot_gamedata_int_entry(Ctx *c, uint32_t hash) {
    const uint64_t mgr = oot_singleton(c, OOT_GAMEDATA_SLOT);
    if (!mgr) return 0;
    const uint64_t base = mgr + 0x10 + 0x60ULL * OOT_INT_CONTAINER;
    const uint64_t entries = recomp_load64(c, base + 8), table = recomp_load64(c, base + 16);
    const uint32_t count = recomp_load32(c, base), size = recomp_load32(c, base + 28);
    if (!entries || !table || !size) return 0;
    for (uint32_t probe = 0, slot = hash % size; probe < size; probe++, slot = (slot + 1) % size) {
        const uint32_t key = recomp_load32(c, table + slot * 8ULL);
        if (!key) return 0;
        if (key != hash) continue;
        const uint32_t index = recomp_load32(c, table + slot * 8ULL + 4);
        return index < count ? entries + index * 40ULL : 0;
    }
    return 0;
}

static int oot_gamedata_int(Ctx *c, uint32_t hash, int *value) {
    const uint64_t e = oot_gamedata_int_entry(c, hash);
    if (!e) return 0;
    *value = (int)recomp_load32(c, e + 28);
    return 1;
}

static uint64_t gd_player;   /* the player actor, set by oot_gd_tick */
/* Link's own copies of Life (+8) and MaxLife (+24), in the object at player+7184 (Hookshot's heart patch swaps +8):
   the game keeps these and writes GameData from them, so they are what has to change */
static uint64_t oot_life_object(Ctx *c) {
    const uint64_t obj = gd_player ? recomp_load64(c, gd_player + 7184) : 0;
    return obj && recomp_load32(c, obj + 12) == 0x31ab5580u && recomp_load32(c, obj + 28) == 0xfbe01da1u ? obj : 0;
}
static int oot_gamedata_set_int(Ctx *c, uint32_t hash, int value) {
    const uint64_t e = oot_gamedata_int_entry(c, hash);
    const uint64_t life = (hash == 0x31ab5580u || hash == 0xfbe01da1u) ? oot_life_object(c) : 0;
    if (life) recomp_store32(c, life + (hash == 0x31ab5580u ? 8 : 24), (uint32_t)value);
    if (!e) return 0;
    recomp_store32(c, e + 28, (uint32_t)value);
    recomp_store32(c, e + 32, (uint32_t)value);
    if (hash == 0x31ab5580u || hash == 0xfbe01da1u) recomp_store32(c, e + 24, (uint32_t)value);   /* hearts: the default too, as the known cheat does */
    return 1;
}

/* hos_oot_stats: the values the editor shows, kept by the game thread; [5] is 1 once they were read */
static const uint32_t oot_stat_hash[5] = {0xa77921d7u /* CurrentRupee */, 0x31ab5580u /* Life */, 0xfbe01da1u /* MaxLife */,
                                         0x76b18200u /* MP */, 0x12d63a97u /* MPMax */};
volatile int hos_oot_stats[6];
static volatile struct { uint32_t hash; int value; } gd_int_edits[8];
static volatile int gd_int_edit_count;

/* The editor asks (any thread): the game thread writes it next frame. which indexes oot_stat_hash. */
void hos_stat_set(int which, int value) {
    if (which < 0 || which >= 5 || gd_int_edit_count >= 8) return;
    const int n = gd_int_edit_count;
    gd_int_edits[n].hash = oot_stat_hash[which], gd_int_edits[n].value = value;
    gd_int_edit_count = n + 1;
}

static void oot_stats_tick(Ctx *c) {
    static int test_done;
    if (!test_done && getenv("HOS_GD_TEST")) {   /* "rupee,life,maxlife,mp,mpmax": each written once (-1: left) */
        int v[5] = {-1, -1, -1, -1, -1};
        sscanf(getenv("HOS_GD_TEST"), "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
        for (int i = 0; i < 5; i++) if (v[i] >= 0) hos_stat_set(i, v[i]);
        test_done = 1;
    }
    static int ticks;
    ticks++;
    for (int phase = 0; phase < 2; phase++) {   /* HOS_GD_TEST2 at tick 600, HOS_GD_TEST3 at tick 700: later writes, as later clicks would be */
        const char *e = getenv(phase ? "HOS_GD_TEST3" : "HOS_GD_TEST2");
        if (e && ticks == 600 + 100 * phase) {
            int v[5] = {-1, -1, -1, -1, -1};
            sscanf(e, "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
            for (int i = 0; i < 5; i++) if (v[i] >= 0) hos_stat_set(i, v[i]);
        }
    }
    /* The HUD's hearts take a new life only after the new maximum has been seen: written on the same frame, the hearts are drawn
       empty but for the last. So a life write waits 30 ticks after a maximum's (found by testing the order in the running game) */
    static int max_tick = -1000, pend_life, pend_due, hud_hide_tick;
    const int n = gd_int_edit_count;
    for (int i = 0; i < n; i++) {
        const uint32_t hash = gd_int_edits[i].hash;
        if (hash == 0x31ab5580u && ticks - max_tick < 30) {   /* Life: after the maximum */
            pend_life = gd_int_edits[i].value, pend_due = max_tick + 30;
            continue;
        }
        if (hash == 0xfbe01da1u) {   /* MaxLife */
            max_tick = ticks;
            /* The HUD lays the magic bar out under the hearts when it is built: a second row of hearts made now would be drawn over
               it. Opening and closing the pause menu builds it anew (hiding it with the game's event flag does not), so the editor
               taps Plus and B through the game's input once the new life is in. HOS_EDITOR_NO_HUD_REFRESH=1: not */
            if (((hos_oot_stats[2] + 39) / 40) != ((gd_int_edits[i].value + 39) / 40) && !getenv("HOS_EDITOR_NO_HUD_REFRESH"))
                hud_hide_tick = ticks + 40;
        }
        const int now = oot_gamedata_set_int(c, hash, gd_int_edits[i].value);
        hos_log("editor: int %08x = %d%s", hash, gd_int_edits[i].value, now ? "" : " (not found)");
    }
    if (hud_hide_tick && ticks >= hud_hide_tick) {
        extern void hos_hid_inject_pause(void);   /* host/nn_hid.c */
        hos_hid_inject_pause();
        hud_hide_tick = 0;
        hos_log("editor: the pause menu opened and closed, to lay the HUD out again");
    }
    if (pend_due && ticks >= pend_due) {
        oot_gamedata_set_int(c, 0x31ab5580u, pend_life);
        hos_log("editor: int 31ab5580 = %d (after the maximum)", pend_life);
        pend_due = 0;
    }
    if (n) gd_int_edit_count = 0;
    int ok = 1;
    for (int i = 0; i < 5; i++) {
        int v = 0;
        if (oot_gamedata_int(c, oot_stat_hash[i], &v)) hos_oot_stats[i] = v; else ok = 0;
    }
    const uint64_t life_obj = oot_life_object(c);
    if (life_obj) {   /* health as the player holds it */
        hos_oot_stats[1] = (int)recomp_load32(c, life_obj + 8);
        hos_oot_stats[2] = (int)recomp_load32(c, life_obj + 24);
    }
    hos_oot_stats[5] = ok;
    static int logged;
    if (getenv("HOS_GD_TEST") && ++logged % 120 == 0)
        hos_log("editor: stats rupee %d life %d maxlife %d mp %d mpmax %d", hos_oot_stats[0], hos_oot_stats[1], hos_oot_stats[2], hos_oot_stats[3], hos_oot_stats[4]);
}

static void oot_gd_tick(Ctx *c, uint64_t actor) {
    gd_player = hos_oot_is_player(c, actor) ? actor : 0;
    oot_gd_probe(c);
    if (getenv("HOS_GD_PROBE")) {   /* the player's life object (Hookshot's heart patch: *(actor+7184)+8 is the life) */
        static int n;
        const uint64_t obj = recomp_load64(c, actor + 7184);
        if ((n++ % 240) == 0) hos_log("gdprobe: actor %llx is_player %d life obj %llx", (unsigned long long)actor, hos_oot_is_player(c, actor), (unsigned long long)obj);
        if (obj && (n % 240) == 1) {
            char hex[200] = {0};
            for (int b = 0; b < 64; b++) snprintf(hex + b * 3, 4, "%02x ", recomp_load8(c, obj + 8 + b));
            hos_log("gdprobe: life object %llx: %s", (unsigned long long)obj, hex);
        }
    }
    oot_stats_tick(c);
    if (!gd.ready || !gd.entries) return;
    int view[GD_VIEW_MAX], view_count, edit_entry[GD_EDITS_MAX], edit_op[GD_EDITS_MAX], edit_count;
    EnterCriticalSection(&gd.lock);
    view_count = gd.view_count;
    memcpy(view, gd.view, sizeof(int) * (size_t)view_count);
    edit_count = gd.edit_count;
    for (int i = 0; i < edit_count; i++) edit_entry[i] = gd.edits[i].entry, edit_op[i] = gd.edits[i].op;
    gd.edit_count = 0;
    LeaveCriticalSection(&gd.lock);
    for (int i = 0; i < edit_count; i++) {
        GdEntry *e = &gd.entries[edit_entry[i]];
        int now = 0;
        if (!oot_gamedata_bool(c, e->hash, &now)) {
            hos_log("editor: %s is not a flag the game keeps in its flag table", e->name ? e->name : "(hash)");
            continue;
        }
        const int want = edit_op[i] == 0 ? !now : edit_op[i] == 2;
        const int ok = oot_gamedata_set_bool(c, e->hash, want);
        hos_log("editor: %s %08x %s -> %s%s", e->name ? e->name : "flag", e->hash, now ? "On" : "Off", want ? "On" : "Off",
                ok ? "" : " (not written: the flag lives outside the plain flag table)");
    }
    for (int i = 0; i < view_count; i++) {
        int now = 0;
        const int8_t state = oot_gamedata_bool(c, gd.entries[view[i]].hash, &now) ? (int8_t)(now ? 1 : 0) : (int8_t)-1;
        EnterCriticalSection(&gd.lock);
        gd.entries[view[i]].state = state;
        LeaveCriticalSection(&gd.lock);
    }
}
