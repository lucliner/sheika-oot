/* Sheikah Updater: a small Win32 window that brings Sheikah Native up to a new Hookshot version, rebuilds the game, and publishes
   the result to the developer's own GitHub repository. The work is done by publisher_core.py or builder_core.py (beside this exe), which this window
   runs and whose output (lines, and @@ markers) it shows. No runtime beyond Windows is needed for the window; Git and Python are
   needed by the work itself (Python is needed by the game's build anyway).
   Build: tools\sheikah-updater\build.ps1 (llvm-mingw). Settings: %LOCALAPPDATA%\SheikahUpdater\settings.ini */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef PUBLISHER   /* the developer's edition: also merges Hookshot's changes and publishes to GitHub */
#define APPNAME L"Sheikah Publisher"
#define APPKEY L"SheikahUpdater"
#define CORE_PY L"publisher_core.py"
#else              /* the edition players get: builds the game from their own dump, nothing else, nothing about git */
#define APPNAME L"Sheikah Builder"
#define APPKEY L"SheikahBuilder"
#define CORE_PY L"builder_core.py"
#endif

#include "settings_store.h"

#define WM_LOGLINE (WM_APP + 1)   /* lParam: malloc'd wchar_t line */
#define WM_JOBDONE (WM_APP + 2)
#define WM_ASKPUBLISH (WM_APP + 3)

enum { ID_SYNCED = 100, ID_LATEST, ID_BEHIND, ID_STAGE, ID_PROG, ID_LOG,
       ID_CHECK = 110, ID_PREVIEW, ID_UPDATE, ID_BUILDONLY, ID_PUBLISHONLY, ID_OPEN, ID_CANCEL,
       ID_F_FORK = 130, ID_F_DUMP, ID_F_MINGW, ID_F_CPM, ID_F_WGPU, ID_F_OUT, ID_F_REPO, ID_F_PY, ID_F_NGX,   /* edit fields */
       ID_B_FORK = 150, ID_B_DUMP, ID_B_MINGW, ID_B_CPM, ID_B_WGPU, ID_B_OUT, ID_B_NGX,                         /* browse buttons */
       ID_C_BUILD = 170, ID_C_KEEP, ID_C_PUBLISH, ID_C_MAIN };

static HWND g_wnd, g_log, g_prog, g_stage;
static wchar_t g_exedir[MAX_PATH];
static volatile LONG g_busy = 0;
static HANDLE g_proc = NULL;

typedef struct {
    wchar_t fork[MAX_PATH], dump[MAX_PATH], mingw[MAX_PATH], cpm[MAX_PATH], wgpu[MAX_PATH], ngx[MAX_PATH], out[MAX_PATH], repo[512], py[MAX_PATH];
    int build, keep, publish, usemain;
    int op;   /* 0 check, 1 preview, 2 update(+build,+publish), 3 build only, 4 publish only */
} Job;

static const struct { int id; const wchar_t *key; const wchar_t *label; int browse; } FIELDS[] = {
    {ID_F_FORK, L"fork", L"Sheikah folder (the source)", ID_B_FORK},
    {ID_F_DUMP, L"dump", L"Your game dump (has romfs and exefs)", ID_B_DUMP},
    {ID_F_MINGW, L"mingw", L"llvm-mingw folder", ID_B_MINGW},
    {ID_F_CPM, L"cpm", L"CPM cache folder", ID_B_CPM},
    {ID_F_WGPU, L"wgpu", L"wgpu-native folder", ID_B_WGPU},
    {ID_F_OUT, L"out", L"Game output folder", ID_B_OUT},
    {ID_F_NGX, L"ngx", L"NVIDIA DLSS SDK folder (optional: builds the game with DLSS)", ID_B_NGX},
#ifdef PUBLISHER
    {ID_F_REPO, L"repo", L"Your GitHub repository URL (https://github.com/you/repo.git)", 0},
    {ID_F_PY, L"python", L"Python (leave as python to use the PATH)", 0},
#else
    {ID_F_PY, L"python", L"", 0},
#endif
};

static void load_settings(void) {
    for (int i = 0; i < (int)(sizeof FIELDS / sizeof FIELDS[0]); i++) {
        wchar_t v[MAX_PATH];
        st_get(FIELDS[i].key, v, MAX_PATH, FIELDS[i].id == ID_F_PY ? L"python" : L"");
        if (i == 0 && !v[0]) {   /* the folder two up from tools\sheikah-updater */
            wcscpy(v, g_exedir);
            for (int k = 0; k < 2; k++) { wchar_t *s = wcsrchr(v, L'\\'); if (s) *s = 0; }
        }
        SetDlgItemTextW(g_wnd, FIELDS[i].id, v);
    }
    CheckDlgButton(g_wnd, ID_C_BUILD, st_getint(L"build", 1));
    CheckDlgButton(g_wnd, ID_C_KEEP, st_getint(L"keep", 1));
#ifdef PUBLISHER
    CheckDlgButton(g_wnd, ID_C_PUBLISH, st_getint(L"publish", 0));
    CheckDlgButton(g_wnd, ID_C_MAIN, st_getint(L"usemain", 0));
#endif
}
static void save_settings(void) {
    for (int i = 0; i < (int)(sizeof FIELDS / sizeof FIELDS[0]); i++) {
        wchar_t v[MAX_PATH];
        GetDlgItemTextW(g_wnd, FIELDS[i].id, v, MAX_PATH);
        st_put(FIELDS[i].key, v);
    }
    const struct { int id; const wchar_t *k; } c[] = {{ID_C_BUILD, L"build"}, {ID_C_KEEP, L"keep"},
#ifdef PUBLISHER
        {ID_C_PUBLISH, L"publish"}, {ID_C_MAIN, L"usemain"},
#endif
    };
    for (int i = 0; i < (int)(sizeof c / sizeof c[0]); i++) st_put(c[i].k, IsDlgButtonChecked(g_wnd, c[i].id) ? L"1" : L"0");
    st_save();
}

static void log_line(const wchar_t *s) {
    wchar_t *copy = _wcsdup(s);
    PostMessageW(g_wnd, WM_LOGLINE, 0, (LPARAM)copy);
}
static void set_text(int id, const wchar_t *t) { SetDlgItemTextW(g_wnd, id, t); }

/* runs one command, line by line into the window; returns the last @@result word in result */
static int run_step(const wchar_t *cmdline, wchar_t *result, size_t rcap) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd, wr;
    result[0] = 0;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    wchar_t *mut = _wcsdup(cmdline);
    BOOL ok = CreateProcessW(NULL, mut, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(mut);
    CloseHandle(wr);
    if (!ok) {
        log_line(L"Could not start the command (is Python installed and on the PATH?)");
        CloseHandle(rd);
        return -1;
    }
    g_proc = pi.hProcess;
    char buf[4096], line[8192];
    size_t n = 0;
    DWORD got;
    for (;;) {
        BOOL r = ReadFile(rd, buf, sizeof buf, &got, NULL);
        if (!r || !got) break;
        for (DWORD i = 0; i < got; i++) {
            if (buf[i] == '\n' || n >= sizeof line - 2) {
                line[n] = 0;
                if (n && line[n - 1] == '\r') line[--n] = 0;
                wchar_t w[8192];
                MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 8192);
                if (!wcsncmp(w, L"@@result ", 9)) wcsncpy(result, w + 9, rcap - 1), result[rcap - 1] = 0;
                log_line(w);
                n = 0;
            } else line[n++] = buf[i];
        }
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    g_proc = NULL;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return (int)code;
}

static void q(wchar_t *dst, size_t cap, const wchar_t *s) {   /* append "s" (quoted) */
    size_t l = wcslen(dst);
    _snwprintf(dst + l, cap - l, L" \"%ls\"", s);
}

static void core(const Job *j, wchar_t *cmd, size_t cap, const wchar_t *sub) {
    _snwprintf(cmd, cap, L"\"%ls\" -I \"%ls\\" CORE_PY L"\" --fork \"%ls\" %ls", j->py, g_exedir, j->fork, sub);
}

static DWORD WINAPI job_thread(void *p) {
    Job *j = (Job *)p;
    wchar_t cmd[4096], res[64], sub[1024];
    int ok = 1;
#ifdef PUBLISHER
    if (j->op == 0) {
        core(j, cmd, 4096, L"status");
        run_step(cmd, res, 64);
    }
    if (j->op == 1 || j->op == 2) {
        _snwprintf(sub, 1024, L"merge%ls --target %ls", j->op == 1 ? L" --dry" : L"", j->usemain ? L"main" : L"release");
        core(j, cmd, 4096, sub);
        int c = run_step(cmd, res, 64);
        if (c || (wcscmp(res, j->op == 1 ? L"dryok" : L"merged") && wcscmp(res, L"uptodate"))) {
            ok = 0;
            if (!wcscmp(res, L"conflicts")) log_line(L"STOPPED: some files conflict. Nothing was changed. See the lines marked @@conflict above.");
        }
        if (ok && !wcscmp(res, L"uptodate") && j->op == 2) log_line(L"Already up to date: continuing with the build.");
    }
#endif
    if (ok && (j->op == 3 || (j->op == 2 && j->build))) {
        _snwprintf(sub, 1024, L"build --dump \"%ls\" --out \"%ls\"%ls%ls%ls%ls", j->dump, j->out, j->keep ? L" --keep" : L"",
                   j->mingw[0] ? L" --mingw \"" : L"", j->mingw[0] ? j->mingw : L"", j->mingw[0] ? L"\"" : L"");
        if (j->cpm[0]) { wcscat(sub, L" --cpm"); q(sub, 1024, j->cpm); }
        if (j->ngx[0]) { wcscat(sub, L" --ngx"); q(sub, 1024, j->ngx); }
        if (j->wgpu[0]) { wcscat(sub, L" --wgpu"); q(sub, 1024, j->wgpu); }
        core(j, cmd, 4096, sub);
        int c = run_step(cmd, res, 64);
        if (c || wcscmp(res, L"built")) ok = 0;
    }
#ifdef PUBLISHER
    if (ok && (j->op == 4 || (j->op == 2 && j->publish))) {
        if (!j->repo[0]) { log_line(L"No GitHub repository URL is set: nothing published."); ok = 0; }
        else if (SendMessageW(g_wnd, WM_ASKPUBLISH, 0, (LPARAM)j->repo) == IDYES) {
            _snwprintf(sub, 1024, L"publish --remote \"%ls\"", j->repo);
            core(j, cmd, 4096, sub);
            int c = run_step(cmd, res, 64);
            if (c || wcscmp(res, L"published")) ok = 0;
        } else log_line(L"Publishing skipped.");
    }
#endif
    PostMessageW(g_wnd, WM_JOBDONE, ok, 0);
    free(j);
    return 0;
}

static void start_job(int op) {
    if (InterlockedCompareExchange(&g_busy, 1, 0)) return;
    save_settings();
    Job *j = (Job *)calloc(1, sizeof *j);
    j->op = op;
    GetDlgItemTextW(g_wnd, ID_F_FORK, j->fork, MAX_PATH);
    GetDlgItemTextW(g_wnd, ID_F_DUMP, j->dump, MAX_PATH);
    GetDlgItemTextW(g_wnd, ID_F_MINGW, j->mingw, MAX_PATH);
    GetDlgItemTextW(g_wnd, ID_F_CPM, j->cpm, MAX_PATH);
    GetDlgItemTextW(g_wnd, ID_F_WGPU, j->wgpu, MAX_PATH);
    GetDlgItemTextW(g_wnd, ID_F_NGX, j->ngx, MAX_PATH);
    GetDlgItemTextW(g_wnd, ID_F_OUT, j->out, MAX_PATH);
    GetDlgItemTextW(g_wnd, ID_F_REPO, j->repo, 512);
    GetDlgItemTextW(g_wnd, ID_F_PY, j->py, MAX_PATH);
    if (!j->py[0]) wcscpy(j->py, L"python");
    j->build = IsDlgButtonChecked(g_wnd, ID_C_BUILD);
    j->keep = IsDlgButtonChecked(g_wnd, ID_C_KEEP);
    j->publish = IsDlgButtonChecked(g_wnd, ID_C_PUBLISH);
    j->usemain = IsDlgButtonChecked(g_wnd, ID_C_MAIN);
    if ((op == 3 || (op == 2 && j->build)) && (!j->dump[0] || !j->out[0])) {
        MessageBoxW(g_wnd, L"Set the game dump folder and the output folder first.", APPNAME, MB_OK | MB_ICONINFORMATION);
        g_busy = 0;
        free(j);
        return;
    }
    SetWindowTextW(g_log, L"");
    SendMessageW(g_prog, PBM_SETPOS, 0, 0);
    EnableWindow(GetDlgItem(g_wnd, ID_CANCEL), TRUE);
    HANDLE t = CreateThread(NULL, 0, job_thread, j, 0, NULL);
    if (t) CloseHandle(t);
}

static void append_log(const wchar_t *line) {
    if (!wcsncmp(line, L"@@progress ", 11)) {
        SendMessageW(g_prog, PBM_SETPOS, (WPARAM)(_wtof(line + 11) * 1000), 0);
        return;
    }
    if (!wcsncmp(line, L"@@stage ", 8)) {
        const wchar_t *sp = wcschr(line + 8, L' ');
        set_text(ID_STAGE, sp ? sp + 1 : line + 8);
        return;
    }
    if (!wcsncmp(line, L"@@eta ", 6)) return;
    if (!wcsncmp(line, L"@@version ", 10)) {
        const wchar_t *kv = line + 10;
        const wchar_t *eq = wcschr(kv, L'=');
        if (!eq) return;
        if (!wcsncmp(kv, L"synced=", 7)) set_text(ID_SYNCED, eq + 1);
        else if (!wcsncmp(kv, L"latest_release=", 15)) {
            wchar_t t[200];
            _snwprintf(t, 200, L"%ls", eq + 1);
            set_text(ID_LATEST, t);
        } else if (!wcsncmp(kv, L"commits_behind=", 15)) {
            wchar_t t[100];
            _snwprintf(t, 100, L"%ls commits behind the latest release", eq + 1);
            set_text(ID_BEHIND, t);
        } else if (!wcsncmp(kv, L"up_to_date=1", 12)) set_text(ID_BEHIND, L"Up to date");
        return;
    }
    if (!wcsncmp(line, L"@@result", 8)) return;
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    wchar_t *t = (wchar_t *)malloc((wcslen(line) + 3) * sizeof(wchar_t));
    wcscpy(t, line);
    wcscat(t, L"\r\n");
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)t);
    free(t);
}

static void browse(int field) {
    BROWSEINFOW bi = {0};
    bi.hwndOwner = g_wnd;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return;
    wchar_t p[MAX_PATH];
    if (SHGetPathFromIDListW(id, p)) SetDlgItemTextW(g_wnd, field, p);
    CoTaskMemFree(id);
}

static HFONT g_font;
static HWND mk(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, DWORD ex) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_wnd = w;
        g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
#ifdef PUBLISHER
        mk(L"STATIC", L"Sheikah is synced with Hookshot:", 0, 16, 14, 250, 20, 0, 0);
        mk(L"STATIC", L"-", SS_LEFT, 270, 14, 560, 20, ID_SYNCED, 0);
        mk(L"STATIC", L"Latest Hookshot release:", 0, 16, 36, 250, 20, 0, 0);
        mk(L"STATIC", L"(press Check versions)", SS_LEFT, 270, 36, 300, 20, ID_LATEST, 0);
        mk(L"STATIC", L"", SS_LEFT, 580, 36, 260, 20, ID_BEHIND, 0);
        int y = 66;
        const int nfields = 9;
#else
        int y = 14;
        const int nfields = 7;   /* (no GitHub address, no Python field) */
#endif
        for (int i = 0; i < nfields; i++, y += 46) {
            mk(L"STATIC", FIELDS[i].label, 0, 16, y, 700, 18, 0, 0);
            mk(L"EDIT", L"", ES_AUTOHSCROLL, 16, y + 18, FIELDS[i].browse ? 740 : 820, 22, FIELDS[i].id, WS_EX_CLIENTEDGE);
            if (FIELDS[i].browse) mk(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 762, y + 17, 74, 24, FIELDS[i].browse, 0);
        }
#ifdef PUBLISHER
        mk(L"BUTTON", L"Build the game after updating", BS_AUTOCHECKBOX, 16, y, 250, 22, ID_C_BUILD, 0);
        mk(L"BUTTON", L"Keep build files (quicker next time, uses lots of disk)", BS_AUTOCHECKBOX, 270, y, 330, 22, ID_C_KEEP, 0);
        mk(L"BUTTON", L"Publish to my GitHub", BS_AUTOCHECKBOX, 610, y, 220, 22, ID_C_PUBLISH, 0);
        y += 24;
        mk(L"BUTTON", L"Follow Hookshot's newest commit instead of its latest release", BS_AUTOCHECKBOX, 16, y, 450, 22, ID_C_MAIN, 0);
        y += 32;
        mk(L"BUTTON", L"Check versions", BS_PUSHBUTTON, 16, y, 130, 30, ID_CHECK, 0);
        mk(L"BUTTON", L"Preview merge", BS_PUSHBUTTON, 154, y, 130, 30, ID_PREVIEW, 0);
        mk(L"BUTTON", L"Update + build", BS_DEFPUSHBUTTON, 292, y, 140, 30, ID_UPDATE, 0);
        mk(L"BUTTON", L"Build only", BS_PUSHBUTTON, 440, y, 100, 30, ID_BUILDONLY, 0);
        mk(L"BUTTON", L"Publish only", BS_PUSHBUTTON, 548, y, 100, 30, ID_PUBLISHONLY, 0);
        mk(L"BUTTON", L"Open game folder", BS_PUSHBUTTON, 656, y, 110, 30, ID_OPEN, 0);
        mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 774, y, 62, 30, ID_CANCEL, 0);
#else
        mk(L"BUTTON", L"Keep build files (quicker next time, uses lots of disk)", BS_AUTOCHECKBOX, 16, y, 420, 22, ID_C_KEEP, 0);
        y += 32;
        mk(L"BUTTON", L"Build the game", BS_DEFPUSHBUTTON, 16, y, 160, 32, ID_BUILDONLY, 0);
        mk(L"BUTTON", L"Open game folder", BS_PUSHBUTTON, 184, y, 140, 32, ID_OPEN, 0);
        mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 774, y, 62, 32, ID_CANCEL, 0);
#endif
        EnableWindow(GetDlgItem(w, ID_CANCEL), FALSE);
        y += 40;
        g_stage = mk(L"STATIC", L"Ready.", SS_LEFT, 16, y, 820, 20, ID_STAGE, 0);
        g_prog = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 16, y + 22, 820, 16, w, (HMENU)ID_PROG, NULL, NULL);
        SendMessageW(g_prog, PBM_SETRANGE32, 0, 1000);
        g_log = mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 16, y + 46, 820, 180, ID_LOG, WS_EX_CLIENTEDGE);
        SendMessageW(g_log, EM_SETLIMITTEXT, 4000000, 0);
        load_settings();
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (HIWORD(wp) == EN_KILLFOCUS) { save_settings(); return 0; }   /* (saved as each field is left, not only at close) */
        if (HIWORD(wp) != BN_CLICKED) return 0;
        if (id >= ID_B_FORK && id <= ID_B_NGX) { browse(FIELDS[id - ID_B_FORK].id); return 0; }
        switch (id) {
#ifdef PUBLISHER
        case ID_CHECK: start_job(0); break;
        case ID_PREVIEW: start_job(1); break;
        case ID_UPDATE:
            if (MessageBoxW(w, L"Merge the newest Hookshot into Sheikah now, and then build and publish as ticked? Changed files are backed up first, and nothing is changed if a file conflicts.",
                            APPNAME, MB_YESNO | MB_ICONQUESTION) == IDYES) start_job(2);
            break;
#endif
        case ID_BUILDONLY: start_job(3); break;
#ifdef PUBLISHER
        case ID_PUBLISHONLY: start_job(4); break;
#endif
        case ID_OPEN: {
            wchar_t p[MAX_PATH];
            GetDlgItemTextW(w, ID_F_OUT, p, MAX_PATH);
            if (p[0]) ShellExecuteW(w, L"open", p, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case ID_CANCEL:
            if (g_proc) TerminateProcess(g_proc, 1);
            break;
        }
        return 0;
    }
    case WM_LOGLINE: append_log((wchar_t *)lp); free((void *)lp); return 0;
#ifdef PUBLISHER
    case WM_ASKPUBLISH: {
        wchar_t t[900];
        _snwprintf(t, 900, L"Publish Sheikah Native to your GitHub repository?\n\n%ls\n\nThe source changes and a generated changelog are pushed. Game files are never included (the updater refuses if it sees any).", (wchar_t *)lp);
        return MessageBoxW(w, t, L"Publish to GitHub", MB_YESNO | MB_ICONQUESTION);
    }
#endif
    case WM_JOBDONE:
        g_busy = 0;
        EnableWindow(GetDlgItem(w, ID_CANCEL), FALSE);
        set_text(ID_STAGE, wp ? L"Finished." : L"Stopped (see the log).");
        if (wp) SendMessageW(g_prog, PBM_SETPOS, 1000, 0);
        return 0;
    case WM_CLOSE:
        if (g_busy && MessageBoxW(w, L"A job is still running. Close anyway?", APPNAME, MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
        save_settings();
        DestroyWindow(w);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_CTLCOLORSTATIC: return DefWindowProcW(w, m, wp, lp);
    }
    return DefWindowProcW(w, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
    (void)prev; (void)cmd;
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&ic);
    CoInitialize(NULL);
    GetModuleFileNameW(NULL, g_exedir, MAX_PATH);
    *wcsrchr(g_exedir, L'\\') = 0;
    wchar_t dir[MAX_PATH];
    /* (the settings live in the user's own folder, not in AppData: a program started from a packaged app such as the Claude desktop
       app sees a private copy of AppData, so a file saved there from one place was not the file read from another) */
    GetEnvironmentVariableW(L"USERPROFILE", dir, MAX_PATH);
    wcscat(dir, L"\\.sheikah");
    CreateDirectoryW(dir, NULL);
    wcscat(dir, L"\\" APPKEY);
    CreateDirectoryW(dir, NULL);
    st_init(dir);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = APPKEY;
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
    RegisterClassW(&wc);
    #ifdef PUBLISHER
    RECT r = {0, 0, 852, 816};
#else
    RECT r = {0, 0, 852, 686};
#endif
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND w = CreateWindowW(APPKEY, APPNAME, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(w, show);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(w, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
