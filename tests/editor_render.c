/* Renders the visual editor (host/editor_window.c) with made-up game data, to .bmp files: the layout can be looked at without
   a game or a build of it.

     clang -DEDITOR_TEST -O1 tests/editor_render.c -o editor_render.exe -lgdiplus -lgdi32 -luser32
     editor_render.exe <out dir> [flag names tsv] [scale]

   Writes items.bmp, map.bmp, world.bmp, flags.bmp. */
#define EDITOR_TEST 1
#include "../host/editor_window.c"

volatile int hos_oot_live_action;
volatile int hos_oot_item_counts[256];
int hos_editor_game_alive(void) { return 1; }
DWORD hos_game_path_a(char *buf, DWORD cap) { (void)buf, (void)cap; return 0; }   /* (SHEIKAH_ICON_DIR names the icons' folder instead) */
volatile int hos_oot_stats[8] = {137, 22, 40, 300, 480, 1, 1, 0};
void hos_stat_set(int which, int value) { (void)which, (void)value; }

/* a few flag names, from a tsv (hash, kind, default, name) when given */
static char names[4000][96];
static int name_count, match_idx[4000], match_count;
static int states[4000];
int hos_gd_search(const char *needle, int kind_filter) {
    (void)kind_filter;
    match_count = 0;
    for (int i = 0; i < name_count; i++)
        if (!*needle || strstr(names[i], needle)) match_idx[match_count++] = i;
    return match_count;
}
int hos_gd_row(int m, char *name, int name_cap, char *value, int value_cap) {
    if (m < 0 || m >= match_count) return 0;
    snprintf(name, (size_t)name_cap, "%s", names[match_idx[m]]);
    const int st = states[match_idx[m]];
    snprintf(value, (size_t)value_cap, "%s", st == 1 ? "On" : st == 0 ? "Off" : "n/a");
    return 1 + (st >= 0);
}
void hos_gd_view(int first, int count) { (void)first, (void)count; }
void hos_gd_edit(int match, int op) { (void)match, (void)op; }

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    const float scale = argc > 3 ? (float)atof(argv[3]) : 1.0f;
    if (argc > 2) {
        FILE *f = fopen(argv[2], "rb");
        char line[512];
        while (f && name_count < 4000 && fgets(line, sizeof line, f)) {
            char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL, *t3 = t2 ? strchr(t2 + 1, '\t') : NULL;
            if (!t3 || !strncmp(t3 + 1, "-", 1)) continue;
            size_t n = strlen(t3 + 1);
            while (n && (t3[n] == '\n' || t3[n] == '\r')) t3[n--] = 0;
            snprintf(names[name_count], sizeof names[0], "%s", t3 + 1);
            states[name_count] = name_count % 3 == 0 ? 1 : name_count % 17 == 0 ? -1 : 0;
            name_count++;
        }
        if (f) fclose(f);
    }
    for (int i = 0; i < 256; i++) hos_oot_item_counts[i] = (i % 3 == 0) ? 0 : (i % 5 == 0 ? 17 : 1);
    flag_dirty = 1;
    for (int sec = 0; sec < 5; sec++) {   /* the Items tab's sections */
        char path[512];
        item_sec = sec;
        snprintf(path, sizeof path, "%s/items%d.bmp", dir, sec);
        if (ed_test_render(path, 0, (int)(1120 * scale), (int)(780 * scale), scale)) return 1;
    }
    item_sec = 0;
    const char *files[] = {"items", "link", "map", "world", "flags"};
    for (int t = 0; t < 5; t++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.bmp", dir, files[t]);
        if (ed_test_render(path, t, (int)(1120 * scale), (int)(780 * scale), scale)) return 1;
    }
    return 0;
}
