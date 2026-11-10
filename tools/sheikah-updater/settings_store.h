/* The window's settings: key=value lines in a small UTF-8 file, %LOCALAPPDATA%\<app>\settings.ini (outside the repository and the
   program's folder, so no update of the program or of Sheikah touches it). Written atomically (a temporary file, then a rename) and
   never with every value empty; the last good file is kept as settings.bak and is used when settings.ini is missing, empty or unreadable.
   Reads the older formats too (an INI with a [settings] line, UTF-16 with a byte order mark). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define ST_MAX 32
static struct { wchar_t key[32]; wchar_t val[1024]; } st_items[ST_MAX];
static int st_count;
static wchar_t st_file[MAX_PATH], st_backup[MAX_PATH], st_temp[MAX_PATH];

static void st_put(const wchar_t *k, const wchar_t *v) {
    for (int i = 0; i < st_count; i++)
        if (!wcscmp(st_items[i].key, k)) { wcsncpy(st_items[i].val, v, 1023); st_items[i].val[1023] = 0; return; }
    if (st_count < ST_MAX) {
        wcsncpy(st_items[st_count].key, k, 31);
        st_items[st_count].key[31] = 0;
        wcsncpy(st_items[st_count].val, v, 1023);
        st_items[st_count].val[1023] = 0;
        st_count++;
    }
}

/* the file's text as wide characters (UTF-8, or UTF-16 with a BOM), malloc'd, or NULL */
static wchar_t *st_slurp(const wchar_t *path) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (1 << 20)) { fclose(f); return NULL; }
    unsigned char *raw = (unsigned char *)malloc((size_t)n + 2);
    size_t got = raw ? fread(raw, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!raw || !got) { free(raw); return NULL; }
    raw[got] = raw[got + 1] = 0;
    wchar_t *w = (wchar_t *)malloc(((size_t)got + 2) * sizeof(wchar_t));
    if (!w) { free(raw); return NULL; }
    if (got >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        memcpy(w, raw + 2, got - 2);
        w[(got - 2) / 2] = 0;
    } else {
        const unsigned char *s = raw;
        size_t len = got;
        if (len >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) s += 3, len -= 3;
        int k = MultiByteToWideChar(CP_UTF8, 0, (const char *)s, (int)len, w, (int)got + 1);
        if (k <= 0) k = MultiByteToWideChar(CP_ACP, 0, (const char *)s, (int)len, w, (int)got + 1);
        w[k > 0 ? k : 0] = 0;
    }
    free(raw);
    return w;
}

static int st_parse(const wchar_t *path) {
    wchar_t *text = st_slurp(path);
    if (!text) return 0;
    int found = 0;
    for (wchar_t *line = text; *line;) {
        wchar_t *end = line;
        while (*end && *end != L'\n') end++;
        wchar_t save = *end;
        *end = 0;
        size_t len = wcslen(line);
        while (len && (line[len - 1] == L'\r' || line[len - 1] == L' ')) line[--len] = 0;
        wchar_t *eq = wcschr(line, L'=');
        if (eq && line[0] != L'[' && line[0] != L'#' && line[0] != L';') {
            *eq = 0;
            st_put(line, eq + 1);
            if (eq[1]) found = 1;
        }
        *end = save;
        line = save ? end + 1 : end;
    }
    free(text);
    return found;
}

static void st_init(const wchar_t *dir) {
    _snwprintf(st_file, MAX_PATH, L"%ls\\settings.ini", dir);
    _snwprintf(st_backup, MAX_PATH, L"%ls\\settings.bak", dir);
    _snwprintf(st_temp, MAX_PATH, L"%ls\\settings.tmp", dir);
    st_count = 0;
    if (!st_parse(st_file)) {
        st_count = 0;
        st_parse(st_backup);
    }
}

static void st_get(const wchar_t *key, wchar_t *out, size_t cap, const wchar_t *def) {
    for (int i = 0; i < st_count; i++)
        if (!wcscmp(st_items[i].key, key) && st_items[i].val[0]) { wcsncpy(out, st_items[i].val, cap - 1); out[cap - 1] = 0; return; }
    wcsncpy(out, def, cap - 1);
    out[cap - 1] = 0;
}

static int st_getint(const wchar_t *key, int def) {
    wchar_t v[16];
    st_get(key, v, 16, L"");
    return v[0] ? _wtoi(v) : def;
}

static void st_save(void) {
    int any = 0;
    for (int i = 0; i < st_count; i++)
        if (st_items[i].val[0] && wcscmp(st_items[i].val, L"0") && wcscmp(st_items[i].val, L"1") && wcscmp(st_items[i].val, L"python")) any = 1;
    if (!any) return;   /* (nothing typed or loaded: a window that came up empty must not wipe the file) */
    FILE *f = _wfopen(st_temp, L"wb");
    if (!f) return;
    fputs("# Sheikah settings, one key=value per line (UTF-8)\r\n", f);
    for (int i = 0; i < st_count; i++) {
        char k[96], v[3200];
        WideCharToMultiByte(CP_UTF8, 0, st_items[i].key, -1, k, sizeof k, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, st_items[i].val, -1, v, sizeof v, NULL, NULL);
        fprintf(f, "%s=%s\r\n", k, v);
    }
    fclose(f);
    CopyFileW(st_file, st_backup, FALSE);   /* (the last good file, before it is replaced) */
    MoveFileExW(st_temp, st_file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
