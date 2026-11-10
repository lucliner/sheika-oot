/* (sheikah) The upstream notice. Sheikah Native is a fork of Hookshot (github.com/NotRexed/Hookshot) and is only as new as the
   Hookshot version it was last merged with (docs/upstream-version.txt, baked into this file as SHEIKAH_UPSTREAM by nx2/build.py).
   A few seconds after the game starts, a background thread asks GitHub's public API (anonymous, one or two small GET requests, a
   generic User-Agent, nothing about the player or the PC is sent) whether upstream has a release newer than that version. If it
   has, the player is told ONCE per release (a message box on this thread, so the game keeps running) and the Insert menu's title
   bar carries a short note while the game runs (gpu/vk/vk_menu.cpp reads the SHEIKAH_UPSTREAM_NOTICE environment variable this
   file sets). The notice only tells the player to contact the developer: nothing is downloaded, nothing is replaced. Offline,
   a rate limit, a bad answer: nothing is shown and nothing is logged beyond a line in sheikah_log.txt.

   Switches (environment, or sheikah_env.txt in the game folder):
     SHEIKAH_NO_UPSTREAM_CHECK=1       no check at all (HOOKSHOT_NO_UPDATE_CHECK=1 does the same)
     SHEIKAH_UPSTREAM_CHECK=commits    ask about the newest commit of the main branch instead of the newest release
     SHEIKAH_UPSTREAM_FAKE=<tag>       pretend upstream is at <tag> (no network): to see the notice
   The answer is remembered for 6 hours (user/sheikah_upstream_check.txt) so a few starts in a row ask GitHub once. */
#ifdef _WIN32
#include "hos_paths.h"
#include <windows.h>
#include <winhttp.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef SHEIKAH_UPSTREAM
#define SHEIKAH_UPSTREAM "unknown"   /* (nx2/build.py: "<commit sha>" or "<commit sha>/<describe>" from docs/upstream-version.txt) */
#endif

void hos_log_flush(void);

#define API_HOST L"api.github.com"
#define REPO "/repos/NotRexed/Hookshot"
#define CACHE_SECONDS (6 * 3600)

static int safe_name(const char *s) {   /* a tag or a sha as it may go into a URL and a message box */
    size_t n = strlen(s);
    if (!n || n > 80) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i]) && !strchr("._+-", s[i])) return 0;
    return 1;
}

static int env_on(const char *name) {
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0");
}

/* GET https://api.github.com<path>: the body (up to 256 KB) in a malloc'd string, or NULL */
static char *api_get(const wchar_t *path) {
    char *body = NULL;
    HINTERNET s = WinHttpOpen(L"sheikah-upstream-check", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return NULL;
    WinHttpSetTimeouts(s, 3000, 3000, 3000, 4000);
    HINTERNET c = WinHttpConnect(s, API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            const size_t max = 256 << 10;
            body = (char *)malloc(max + 1);
            size_t have = 0;
            DWORD got = 0;
            while (body && have < max && WinHttpReadData(r, body + have, (DWORD)(max - have), &got) && got) have += got;
            if (body) body[have] = 0;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return body;
}

/* the string value of "key":"value" (first occurrence) */
static int json_str(const char *body, const char *key, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *k = strstr(body, pat);
    if (!k || !(k = strchr(k + strlen(pat), '"'))) return 0;
    const char *e = strchr(++k, '"');
    if (!e || e == k || (size_t)(e - k) >= cap) return 0;
    memcpy(out, k, (size_t)(e - k));
    out[e - k] = 0;
    return 1;
}

/* (a line "name=value" of a small text file) */
static int read_value(const wchar_t *path, const char *name, char *out, size_t cap) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return 0;
    char line[512];
    const size_t n = strlen(name);
    int found = 0;
    while (!found && fgets(line, sizeof line, f)) {
        if (strncmp(line, name, n) || line[n] != '=') continue;
        char *s = line + n + 1;
        size_t len = strlen(s);
        while (len && (s[len - 1] == 10 || s[len - 1] == 13 || s[len - 1] == ' ')) s[--len] = 0;
        if (len && len < cap) memcpy(out, s, len + 1), found = 1;
    }
    fclose(f);
    return found;
}

static void write_file(const wchar_t *dir, const wchar_t *name, const char *text) {
    wchar_t user[MAX_PATH + 16], file[MAX_PATH + 64];
    _snwprintf(user, MAX_PATH + 16, L"%ls\\user", dir);
    CreateDirectoryW(user, NULL);
    _snwprintf(file, MAX_PATH + 64, L"%ls\\user\\%ls", dir, name);
    FILE *f = _wfopen(file, L"wb");
    if (f) fputs(text, f), fclose(f);
}

/* the baked version: "<sha>[/<describe>]" -> sha and a name to show */
static void baked(char *sha, size_t cap, char *name, size_t ncap) {
    const char *v = SHEIKAH_UPSTREAM, *slash = strchr(v, '/');
    size_t n = slash ? (size_t)(slash - v) : strlen(v);
    if (n >= cap) n = cap - 1;
    memcpy(sha, v, n);
    sha[n] = 0;
    snprintf(name, ncap, "%s", slash ? slash + 1 : sha);
}

static DWORD WINAPI check_thread(void *unused) {
    (void)unused;
    Sleep(8000);   /* (the game's start is busy enough) */
    char sha[96], mine[96], latest[96] = "", status[32] = "", ahead[24] = "0";
    baked(sha, sizeof sha, mine, sizeof mine);
    const int commits = getenv("SHEIKAH_UPSTREAM_CHECK") && !strcmp(getenv("SHEIKAH_UPSTREAM_CHECK"), "commits");
    const char *fake = getenv("SHEIKAH_UPSTREAM_FAKE");
    int newer = 0;
    wchar_t dir[MAX_PATH], file[MAX_PATH + 64];
    DWORD n = hos_game_path_w(dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;
    while (n && dir[n - 1] != L'\\' && dir[n - 1] != L'/') n--;
    if (!n) return 0;
    dir[n - 1] = 0;

    if (fake && *fake) {
        if (!safe_name(fake)) return 0;
        snprintf(latest, sizeof latest, "%s", fake);
        newer = 1;
    } else {
        if (!strcmp(sha, "unknown") || !safe_name(sha)) return 0;   /* (a build from a folder with no record: nothing to compare) */
        _snwprintf(file, MAX_PATH + 64, L"%ls\\user\\sheikah_upstream_check.txt", dir);
        char t[32] = "", cached_for[96] = "";
        if (read_value(file, "time", t, sizeof t) && read_value(file, "for", cached_for, sizeof cached_for) && !strcmp(cached_for, sha) &&
            (long long)time(NULL) - atoll(t) < CACHE_SECONDS && read_value(file, "latest", latest, sizeof latest)) {
            read_value(file, "status", status, sizeof status);
            newer = !strcmp(status, "ahead");
        } else {
            char *body = NULL;
            if (commits) {
                body = api_get(L"" REPO "/commits/HEAD");
                if (body && !json_str(body, "sha", latest, sizeof latest)) latest[0] = 0;   /* (the first "sha" is the commit's) */
            } else {
                body = api_get(L"" REPO "/releases/latest");
                if (body && !json_str(body, "tag_name", latest, sizeof latest)) latest[0] = 0;
            }
            free(body);
            if (!latest[0] || !safe_name(latest)) {
                fprintf(stderr, "upstream: the latest Hookshot could not be asked for (offline?)\n");
                return 0;
            }
            /* (is it ahead of the version Sheikah is based on: GitHub's compare, one commit asked for to keep the answer small) */
            wchar_t path[512], wl[128], ws[128];
            MultiByteToWideChar(CP_UTF8, 0, latest, -1, wl, 128);
            MultiByteToWideChar(CP_UTF8, 0, sha, -1, ws, 128);
            _snwprintf(path, 512, L"" REPO "/compare/%ls...%ls?per_page=1", ws, wl);
            body = api_get(path);
            if (!body || !json_str(body, "status", status, sizeof status)) {
                free(body);
                fprintf(stderr, "upstream: could not compare %s with %s\n", sha, latest);
                return 0;
            }
            const char *a = strstr(body, "\"ahead_by\"");
            if (a && (a = strchr(a, ':'))) snprintf(ahead, sizeof ahead, "%d", atoi(a + 1));
            free(body);
            newer = !strcmp(status, "ahead");
            char text[512];
            snprintf(text, sizeof text, "time=%lld\nfor=%s\nlatest=%s\nstatus=%s\nahead=%s\n", (long long)time(NULL), sha, latest, status, ahead);
            write_file(dir, L"sheikah_upstream_check.txt", text);
        }
    }
    fprintf(stderr, "upstream: Sheikah is based on %s, Hookshot's %s is %s\n", mine, latest, newer ? "newer" : "not newer");
    if (!newer) return 0;

    char note[256];
    snprintf(note, sizeof note, "Newer Hookshot (%s) is out: tell the Sheikah dev", latest);
    SetEnvironmentVariableA("SHEIKAH_UPSTREAM_NOTICE", note);   /* (the Insert menu's title bar shows it, gpu/vk/vk_menu.cpp) */

    /* (the message box: once for each upstream release) */
    char seen[96] = "";
    _snwprintf(file, MAX_PATH + 64, L"%ls\\user\\sheikah_upstream_seen.txt", dir);
    read_value(file, "seen", seen, sizeof seen);
    if (!fake && !strcmp(seen, latest)) return 0;
    wchar_t text[1024], wl[128], wm[128];
    MultiByteToWideChar(CP_UTF8, 0, latest, -1, wl, 128);
    MultiByteToWideChar(CP_UTF8, 0, mine, -1, wm, 128);
    _snwprintf(text, 1024,
               L"The Hookshot project that Sheikah Native is built on has a newer version: %ls (this Sheikah is based on %ls).\n\n"
               L"Sheikah Native does not update itself and nothing is downloaded. Please contact the Sheikah Native developer so they can bring "
               L"the newer Hookshot into Sheikah and give you a new build.\n\n"
               L"Your game keeps working as it is. This message shows once for each new Hookshot version "
               L"(SHEIKAH_NO_UPSTREAM_CHECK=1 turns the check off).",
               wl, wm);
    if (!fake) {
        char line[128];
        snprintf(line, sizeof line, "seen=%s\n", latest);
        write_file(dir, L"sheikah_upstream_seen.txt", line);
    }
    MessageBoxW(NULL, text, L"Sheikah Native: newer Hookshot available", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    return 0;
}

void hos_upstream_start(void) {
    if (env_on("SHEIKAH_NO_UPSTREAM_CHECK") || env_on("HOOKSHOT_NO_UPDATE_CHECK")) return;
    HANDLE t = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
#else
void hos_upstream_start(void) {}
#endif
