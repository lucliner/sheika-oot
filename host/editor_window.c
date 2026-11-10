/* (sheikah) The visual editor: a window of its own, opened with F2 or from the F1 menu's Editor row.

   Items: every item, upgrade, song and medallion as a tile with a drawn icon, lit when Link has it, with its count.
   Map:   every place the game can travel to, as a node on a map; a click travels there.
   World: the time of day as a dial, and the age switch.
   Flags: the game's flags (host/oot_live_editor.inc) as chips; a click flips one.

   The window lives on a thread of its own (a message loop of its own: the game's window never waits for it) and draws with
   GDI+ (shapes, anti-aliased) and GDI (text). It changes the game only through what the cheats already use: the live
   action (hos_oot_live_action, taken by the player's tick in host/hos_main.c) and the GameData editor's queue (hos_gd_*).
   The counts it shows are the game side's, hos_oot_item_counts, refreshed twice a second.

   Built with -DEDITOR_TEST (tests/editor_render.c) the paint runs on a bitmap and is written out, to check the layout without
   a game. Windows only: the Linux build keeps the empty stubs at the end. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gpu/vk/oot_cheat_items.h"

/* ---- the game's side (host/hos_main.c, host/oot_live_editor.inc) ---- */
extern volatile int hos_oot_live_action;
extern volatile int hos_oot_item_counts[];
int hos_editor_game_alive(void);
int hos_gd_search(const char *needle, int kind_filter);
int hos_gd_row(int match, char *name, int name_cap, char *value, int value_cap);
void hos_gd_view(int first, int count);
void hos_gd_edit(int match, int op);

/* ---- GDI+'s flat API (declared here: its header is C++; the import library is -lgdiplus) ---- */
typedef struct { UINT32 version; void *callback; BOOL no_thread; BOOL no_codecs; } GpStartup;
typedef struct { float x, y; } GpPt;
typedef void GpObj;
int __stdcall GdiplusStartup(ULONG_PTR *token, const GpStartup *input, void *output);
int __stdcall GdipCreateFromHDC(HDC dc, GpObj **g);
int __stdcall GdipDeleteGraphics(GpObj *g);
int __stdcall GdipSetSmoothingMode(GpObj *g, int mode);
int __stdcall GdipSetClipRectI(GpObj *g, int x, int y, int w, int h, int combine);
int __stdcall GdipResetClip(GpObj *g);
int __stdcall GdipCreateSolidFill(UINT32 argb, GpObj **brush);
int __stdcall GdipDeleteBrush(GpObj *brush);
int __stdcall GdipCreatePen1(UINT32 argb, float width, int unit, GpObj **pen);
int __stdcall GdipDeletePen(GpObj *pen);
int __stdcall GdipSetPenLineCap197819(GpObj *pen, int start, int end, int dash);
int __stdcall GdipFillEllipse(GpObj *g, GpObj *brush, float x, float y, float w, float h);
int __stdcall GdipDrawEllipse(GpObj *g, GpObj *pen, float x, float y, float w, float h);
int __stdcall GdipFillPolygon(GpObj *g, GpObj *brush, const GpPt *pts, int count, int fill_mode);
int __stdcall GdipDrawPolygon(GpObj *g, GpObj *pen, const GpPt *pts, int count);
int __stdcall GdipDrawLine(GpObj *g, GpObj *pen, float x1, float y1, float x2, float y2);
int __stdcall GdipDrawArc(GpObj *g, GpObj *pen, float x, float y, float w, float h, float start, float sweep);
int __stdcall GdipCreatePath(int fill_mode, GpObj **path);
int __stdcall GdipDeletePath(GpObj *path);
int __stdcall GdipAddPathArc(GpObj *path, float x, float y, float w, float h, float start, float sweep);
int __stdcall GdipClosePathFigure(GpObj *path);
int __stdcall GdipFillPath(GpObj *g, GpObj *brush, GpObj *path);
int __stdcall GdipDrawPath(GpObj *g, GpObj *pen, GpObj *path);
int __stdcall GdipCreateBitmapFromFile(const WCHAR *file, GpObj **bitmap);
int __stdcall GdipDisposeImage(GpObj *image);
int __stdcall GdipDrawImageRectI(GpObj *g, GpObj *image, int x, int y, int w, int h);
int __stdcall GdipSetInterpolationMode(GpObj *g, int mode);
int __stdcall GdipGetImageWidth(GpObj *image, UINT *width);
int __stdcall GdipGetImageHeight(GpObj *image, UINT *height);
int __stdcall GdipDrawImageRectRectI(GpObj *g, GpObj *image, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int unit, void *attrs, void *callback, void *data);
#ifdef EDITOR_TEST
DWORD hos_game_path_a(char *buf, DWORD cap);   /* (the offline render supplies its own) */
#else
#include "hos_paths.h"   /* hos_game_path_a: the game folder's path */
#endif

/* ---- colours ---- */
#define RGBA(a, r, g, b) ((((UINT32)(a)) << 24) | (((UINT32)(r)) << 16) | (((UINT32)(g)) << 8) | (UINT32)(b))
#define RGBX(r, g, b) RGBA(255, r, g, b)
#define C_BG RGB(16, 20, 18)
#define C_PANEL RGB(25, 31, 28)
#define C_PANEL2 RGB(34, 42, 38)
#define C_LINE RGB(52, 64, 58)
#define C_TEXT RGB(236, 240, 236)
#define C_DIM RGB(140, 150, 144)
#define C_GREEN RGB(96, 176, 112)
#define C_AMBER RGB(222, 168, 70)

static GpObj *G;            /* the graphics being drawn on */
static float g_alpha = 1;   /* 0..1: a dimmed icon (an item Link does not have) */
static float S = 1;         /* the window's scale (dpi / 96) */

static UINT32 fade(UINT32 c) {
    const UINT32 a = (UINT32)((float)(c >> 24) * g_alpha);
    return (c & 0x00FFFFFFu) | (a << 24);
}
static void fill_ellipse(float x, float y, float w, float h, UINT32 c) {
    GpObj *b;
    GdipCreateSolidFill(fade(c), &b);
    GdipFillEllipse(G, b, x, y, w, h);
    GdipDeleteBrush(b);
}
static void stroke_ellipse(float x, float y, float w, float h, UINT32 c, float lw) {
    GpObj *p;
    GdipCreatePen1(fade(c), lw, 2, &p);
    GdipDrawEllipse(G, p, x, y, w, h);
    GdipDeletePen(p);
}
static void fill_poly(const GpPt *pts, int n, UINT32 c) {
    GpObj *b;
    GdipCreateSolidFill(fade(c), &b);
    GdipFillPolygon(G, b, pts, n, 0);
    GdipDeleteBrush(b);
}
static void stroke_poly(const GpPt *pts, int n, UINT32 c, float lw) {
    GpObj *p;
    GdipCreatePen1(fade(c), lw, 2, &p);
    GdipDrawPolygon(G, p, pts, n);
    GdipDeletePen(p);
}
static void line(float x1, float y1, float x2, float y2, UINT32 c, float lw) {
    GpObj *p;
    GdipCreatePen1(fade(c), lw, 2, &p);
    GdipSetPenLineCap197819(p, 2, 2, 0);   /* round caps */
    GdipDrawLine(G, p, x1, y1, x2, y2);
    GdipDeletePen(p);
}
static void arc(float x, float y, float w, float h, float start, float sweep, UINT32 c, float lw) {
    GpObj *p;
    GdipCreatePen1(fade(c), lw, 2, &p);
    GdipSetPenLineCap197819(p, 2, 2, 0);
    GdipDrawArc(G, p, x, y, w, h, start, sweep);
    GdipDeletePen(p);
}
static GpObj *rrect_path(float x, float y, float w, float h, float r) {
    GpObj *path;
    GdipCreatePath(0, &path);
    const float d = r * 2;
    GdipAddPathArc(path, x, y, d, d, 180, 90);
    GdipAddPathArc(path, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(path, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(path, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(path);
    return path;
}
static void fill_rrect(float x, float y, float w, float h, float r, UINT32 c) {
    GpObj *path = rrect_path(x, y, w, h, r), *b;
    GdipCreateSolidFill(fade(c), &b);
    GdipFillPath(G, b, path);
    GdipDeleteBrush(b);
    GdipDeletePath(path);
}
static void stroke_rrect(float x, float y, float w, float h, float r, UINT32 c, float lw) {
    GpObj *path = rrect_path(x, y, w, h, r), *p;
    GdipCreatePen1(fade(c), lw, 2, &p);
    GdipDrawPath(G, p, path);
    GdipDeletePen(p);
    GdipDeletePath(path);
}

/* ---- the icons: a unit square at (x, y) of side s, two colours; each glyph is a few shapes ---- */
#define U(u, v) (x + (u) * s), (y + (v) * s)
#define PT(u, v) {x + (u) * s, y + (v) * s}
#define LW(f) ((f) * s)

static void g_sword(float x, float y, float s, UINT32 c1, UINT32 c2) {
    line(U(0.24, 0.76), U(0.80, 0.20), c1, LW(0.13));
    line(U(0.19, 0.55), U(0.45, 0.81), c2, LW(0.09));
    line(U(0.33, 0.67), U(0.17, 0.83), c2, LW(0.08));
    fill_ellipse(x + 0.09f * s, y + 0.75f * s, 0.14f * s, 0.14f * s, c2);
}
static void g_shield(float x, float y, float s, UINT32 c1, UINT32 c2) {
    const GpPt p[] = {PT(0.18, 0.14), PT(0.82, 0.14), PT(0.82, 0.55), PT(0.5, 0.92), PT(0.18, 0.55)};
    fill_poly(p, 5, c1);
    stroke_poly(p, 5, RGBX(206, 214, 220), LW(0.05));
    fill_ellipse(x + 0.36f * s, y + 0.32f * s, 0.28f * s, 0.28f * s, c2);
}
static void g_tunic(float x, float y, float s, UINT32 c1, UINT32 c2) {
    const GpPt p[] = {PT(0.30, 0.14), PT(0.42, 0.22), PT(0.58, 0.22), PT(0.70, 0.14), PT(0.94, 0.36), PT(0.79, 0.52),
                      PT(0.70, 0.43), PT(0.70, 0.88), PT(0.30, 0.88), PT(0.30, 0.43), PT(0.21, 0.52), PT(0.06, 0.36)};
    fill_poly(p, 12, c1);
    line(U(0.30, 0.64), U(0.70, 0.64), c2, LW(0.07));
}
static void g_boot(float x, float y, float s, UINT32 c1, UINT32 c2) {
    const GpPt p[] = {PT(0.28, 0.10), PT(0.58, 0.10), PT(0.58, 0.56), PT(0.90, 0.68), PT(0.90, 0.88), PT(0.18, 0.88)};
    fill_poly(p, 6, c1);
    line(U(0.18, 0.88), U(0.90, 0.88), c2, LW(0.09));
}
static void g_stick(float x, float y, float s, UINT32 c1, UINT32 c2) {
    line(U(0.26, 0.84), U(0.74, 0.22), c1, LW(0.15));
    fill_ellipse(x + 0.64f * s, y + 0.12f * s, 0.2f * s, 0.2f * s, c2);
}
static void g_nut(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.2f * s, y + 0.28f * s, 0.6f * s, 0.6f * s, c1);
    fill_ellipse(x + 0.3f * s, y + 0.38f * s, 0.16f * s, 0.16f * s, c2);
    line(U(0.5, 0.3), U(0.62, 0.12), c2, LW(0.07));
}
static void g_bomb(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.14f * s, y + 0.30f * s, 0.62f * s, 0.62f * s, c1);
    fill_ellipse(x + 0.26f * s, y + 0.42f * s, 0.14f * s, 0.14f * s, RGBA(170, 255, 255, 255));
    line(U(0.62, 0.36), U(0.78, 0.2), c2, LW(0.07));
    fill_ellipse(x + 0.76f * s, y + 0.10f * s, 0.14f * s, 0.14f * s, RGBX(255, 190, 60));
}
static void g_sling(float x, float y, float s, UINT32 c1, UINT32 c2) {
    line(U(0.5, 0.9), U(0.5, 0.5), c1, LW(0.12));
    line(U(0.5, 0.5), U(0.24, 0.14), c1, LW(0.12));
    line(U(0.5, 0.5), U(0.76, 0.14), c1, LW(0.12));
    line(U(0.24, 0.14), U(0.76, 0.14), c2, LW(0.04));
}
static void g_seed(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.28f * s, y + 0.28f * s, 0.44f * s, 0.44f * s, c1);
    fill_ellipse(x + 0.38f * s, y + 0.36f * s, 0.12f * s, 0.12f * s, c2);
}
static void g_bow(float x, float y, float s, UINT32 c1, UINT32 c2) {
    arc(x + 0.12f * s, y + 0.08f * s, 0.62f * s, 0.84f * s, -80, 160, c1, LW(0.10));
    const float r = 80 * 3.14159265f / 180, cx = x + 0.43f * s, cy = y + 0.5f * s;
    const float ex = cx + 0.31f * s * cosf(r), ey = cy - 0.42f * s * sinf(r);
    line(ex, ey, ex, cy + (cy - ey), c2, LW(0.03));
}
static void g_arrow(float x, float y, float s, UINT32 c1, UINT32 c2) {
    line(U(0.14, 0.86), U(0.74, 0.26), c1, LW(0.07));
    const GpPt head[] = {PT(0.92, 0.08), PT(0.64, 0.20), PT(0.80, 0.36)};
    fill_poly(head, 3, c2);
    line(U(0.14, 0.86), U(0.14, 0.62), c2, LW(0.05));
    line(U(0.14, 0.86), U(0.38, 0.86), c2, LW(0.05));
}
static void g_gem(float x, float y, float s, UINT32 c1, UINT32 c2) {
    const GpPt p[] = {PT(0.5, 0.08), PT(0.86, 0.44), PT(0.5, 0.92), PT(0.14, 0.44)};
    fill_poly(p, 4, c1);
    stroke_poly(p, 4, c2, LW(0.05));
    line(U(0.14, 0.44), U(0.86, 0.44), RGBA(110, 255, 255, 255), LW(0.03));
    line(U(0.5, 0.08), U(0.36, 0.44), RGBA(110, 255, 255, 255), LW(0.03));
}
static void g_ocarina(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.10f * s, y + 0.32f * s, 0.74f * s, 0.46f * s, c1);
    fill_ellipse(x + 0.62f * s, y + 0.18f * s, 0.2f * s, 0.2f * s, c1);
    for (int i = 0; i < 4; i++) fill_ellipse(x + (0.26f + 0.12f * (float)i) * s, y + (0.46f + (i & 1) * 0.1f) * s, 0.07f * s, 0.07f * s, c2);
}
static void g_hook(float x, float y, float s, UINT32 c1, UINT32 c2) {
    line(U(0.14, 0.88), U(0.52, 0.5), c2, LW(0.12));
    const GpPt claw[] = {PT(0.44, 0.58), PT(0.94, 0.08), PT(0.9, 0.46), PT(0.7, 0.46)};
    fill_poly(claw, 4, c1);
}
static void g_boomerang(float x, float y, float s, UINT32 c1, UINT32 c2) {
    line(U(0.2, 0.2), U(0.5, 0.8), c1, LW(0.17));
    line(U(0.5, 0.8), U(0.86, 0.3), c1, LW(0.17));
    (void)c2;
}
static void g_lens(float x, float y, float s, UINT32 c1, UINT32 c2) {
    stroke_ellipse(x + 0.1f * s, y + 0.18f * s, 0.8f * s, 0.64f * s, c2, LW(0.08));
    fill_ellipse(x + 0.34f * s, y + 0.3f * s, 0.32f * s, 0.4f * s, c1);
}
static void g_bean(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.24f * s, y + 0.30f * s, 0.5f * s, 0.6f * s, c1);
    line(U(0.5, 0.32), U(0.58, 0.12), c2, LW(0.06));
}
static void g_hammer(float x, float y, float s, UINT32 c1, UINT32 c2) {
    line(U(0.22, 0.88), U(0.62, 0.38), c2, LW(0.11));
    const GpPt head[] = {PT(0.46, 0.08), PT(0.92, 0.36), PT(0.76, 0.62), PT(0.30, 0.32)};
    fill_poly(head, 4, c1);
}
static void g_bottle(float x, float y, float s, UINT32 c1, UINT32 c2) {
    const GpPt p[] = {PT(0.36, 0.14), PT(0.64, 0.14), PT(0.64, 0.32), PT(0.82, 0.56), PT(0.82, 0.88), PT(0.18, 0.88), PT(0.18, 0.56), PT(0.36, 0.32)};
    fill_poly(p, 8, RGBA(120, 150, 220, 255));
    stroke_poly(p, 8, c1, LW(0.06));
    fill_rrect(x + 0.34f * s, y + 0.05f * s, 0.32f * s, 0.12f * s, LW(0.03), c2);
}
static void g_mask(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.16f * s, y + 0.12f * s, 0.68f * s, 0.76f * s, c1);
    fill_ellipse(x + 0.3f * s, y + 0.36f * s, 0.14f * s, 0.14f * s, c2);
    fill_ellipse(x + 0.56f * s, y + 0.36f * s, 0.14f * s, 0.14f * s, c2);
    line(U(0.36, 0.66), U(0.64, 0.66), c2, LW(0.05));
}
static void g_glove(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_rrect(x + 0.24f * s, y + 0.38f * s, 0.52f * s, 0.46f * s, LW(0.06), c1);
    for (int i = 0; i < 4; i++) fill_rrect(x + (0.24f + 0.13f * (float)i) * s, y + 0.14f * s, 0.1f * s, 0.3f * s, LW(0.04), c1);
    fill_rrect(x + 0.2f * s, y + 0.78f * s, 0.6f * s, 0.12f * s, LW(0.03), c2);
}
static void g_scale(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.18f * s, y + 0.18f * s, 0.64f * s, 0.64f * s, c1);
    const GpPt f1[] = {PT(0.5, 0.18), PT(0.38, 0.04), PT(0.62, 0.04)};
    fill_poly(f1, 3, c2);
    line(U(0.3, 0.5), U(0.7, 0.5), c2, LW(0.05));
    line(U(0.34, 0.64), U(0.66, 0.64), c2, LW(0.05));
}
static void g_bag(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.14f * s, y + 0.32f * s, 0.72f * s, 0.62f * s, c1);
    fill_rrect(x + 0.36f * s, y + 0.14f * s, 0.28f * s, 0.2f * s, LW(0.04), c2);
    line(U(0.34, 0.34), U(0.66, 0.34), c2, LW(0.05));
}
static void g_quiver(float x, float y, float s, UINT32 c1, UINT32 c2) {
    const GpPt p[] = {PT(0.34, 0.24), PT(0.56, 0.2), PT(0.82, 0.86), PT(0.6, 0.92)};
    fill_poly(p, 4, c1);
    line(U(0.40, 0.24), U(0.36, 0.06), c2, LW(0.05));
    line(U(0.47, 0.22), U(0.47, 0.04), c2, LW(0.05));
    line(U(0.54, 0.21), U(0.58, 0.05), c2, LW(0.05));
}
static void g_wallet(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_rrect(x + 0.1f * s, y + 0.26f * s, 0.8f * s, 0.54f * s, LW(0.07), c1);
    fill_ellipse(x + 0.6f * s, y + 0.42f * s, 0.22f * s, 0.22f * s, c2);
}
static void g_jar(float x, float y, float s, UINT32 c1, UINT32 c2) {
    const GpPt p[] = {PT(0.3, 0.2), PT(0.7, 0.2), PT(0.82, 0.42), PT(0.82, 0.88), PT(0.18, 0.88), PT(0.18, 0.42)};
    fill_poly(p, 6, c1);
    fill_rrect(x + 0.3f * s, y + 0.08f * s, 0.4f * s, 0.14f * s, LW(0.03), c2);
}
static void g_medal(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.1f * s, y + 0.1f * s, 0.8f * s, 0.8f * s, RGBX(40, 40, 46));
    stroke_ellipse(x + 0.1f * s, y + 0.1f * s, 0.8f * s, 0.8f * s, c2, LW(0.07));
    fill_ellipse(x + 0.28f * s, y + 0.28f * s, 0.44f * s, 0.44f * s, c1);
    fill_ellipse(x + 0.4f * s, y + 0.38f * s, 0.12f * s, 0.12f * s, RGBA(200, 255, 255, 255));
}
static void g_note(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_ellipse(x + 0.18f * s, y + 0.6f * s, 0.34f * s, 0.24f * s, c1);
    line(U(0.5, 0.7), U(0.5, 0.14), c1, LW(0.07));
    const GpPt flag[] = {PT(0.5, 0.14), PT(0.84, 0.3), PT(0.5, 0.36)};
    fill_poly(flag, 3, c1);
    (void)c2;
}
static void g_eye(float x, float y, float s, UINT32 c1, UINT32 c2) {
    stroke_ellipse(x + 0.08f * s, y + 0.28f * s, 0.84f * s, 0.44f * s, c1, LW(0.07));
    fill_ellipse(x + 0.38f * s, y + 0.34f * s, 0.24f * s, 0.32f * s, c2);
}
static void g_card(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_rrect(x + 0.1f * s, y + 0.22f * s, 0.8f * s, 0.56f * s, LW(0.06), c1);
    fill_ellipse(x + 0.2f * s, y + 0.34f * s, 0.22f * s, 0.22f * s, c2);
    line(U(0.52, 0.42), U(0.8, 0.42), c2, LW(0.05));
    line(U(0.52, 0.58), U(0.8, 0.58), c2, LW(0.05));
}
static void g_token(float x, float y, float s, UINT32 c1, UINT32 c2) {
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * 0.785398f;
        line(x + (0.5f + 0.18f * cosf(a)) * s, y + (0.5f + 0.18f * sinf(a)) * s, x + (0.5f + 0.42f * cosf(a)) * s, y + (0.5f + 0.42f * sinf(a)) * s, c2, LW(0.06));
    }
    fill_ellipse(x + 0.3f * s, y + 0.3f * s, 0.4f * s, 0.4f * s, c1);
}
static void g_generic(float x, float y, float s, UINT32 c1, UINT32 c2) {
    fill_rrect(x + 0.2f * s, y + 0.2f * s, 0.6f * s, 0.6f * s, LW(0.1), c1);
    (void)c2;
}

typedef void (*GlyphFn)(float, float, float, UINT32, UINT32);
typedef struct { const char *prefix; GlyphFn fn; UINT32 c1, c2; } IconRule;
#define GOLD RGBX(236, 196, 80)
#define SILVER RGBX(196, 206, 216)
static const IconRule icon_rules[] = {   /* the first rule whose prefix starts the pouch name wins */
    {"Weapon_Sword_Kokiri", g_sword, RGBX(150, 190, 120), RGBX(120, 86, 50)},
    {"Weapon_Sword_Master", g_sword, RGBX(150, 190, 255), RGBX(70, 90, 200)},
    {"Weapon_Sword_Giant", g_sword, GOLD, RGBX(140, 100, 40)},
    {"Weapon_Sword_Biggoron", g_sword, SILVER, RGBX(150, 60, 50)},
    {"Weapon_Shield_Deku", g_shield, RGBX(150, 100, 56), RGBX(200, 160, 90)},
    {"Weapon_Shield_Hylian", g_shield, RGBX(60, 90, 200), RGBX(220, 60, 50)},
    {"Weapon_Shield_Mirror", g_shield, SILVER, RGBX(230, 80, 90)},
    {"Armor_Adult_Goron", g_tunic, RGBX(200, 60, 50), RGBX(240, 200, 90)},
    {"Armor_Adult_Zora", g_tunic, RGBX(60, 130, 220), RGBX(240, 240, 255)},
    {"Armor_Adult_HeavyBoot", g_boot, RGBX(110, 116, 130), SILVER},
    {"Armor_Adult_HoverBoot", g_boot, RGBX(120, 70, 170), RGBX(230, 200, 255)},
    {"Armor_Adult_Gauntlet_Silver", g_glove, SILVER, RGBX(110, 116, 130)},
    {"Armor_Adult_Gauntlet_Gold", g_glove, GOLD, RGBX(150, 110, 40)},
    {"Armor_Child_Mask_Keaton", g_mask, RGBX(240, 190, 70), RGBX(60, 40, 20)},
    {"Armor_Child_Mask_Skull", g_mask, RGBX(230, 230, 210), RGBX(50, 50, 50)},
    {"Armor_Child_Mask_Spooky", g_mask, RGBX(150, 190, 90), RGBX(40, 40, 30)},
    {"Armor_Child_Mask_Bunny", g_mask, RGBX(240, 200, 220), RGBX(120, 60, 90)},
    {"Armor_Child_Mask_Goron", g_mask, RGBX(190, 110, 70), RGBX(50, 30, 20)},
    {"Armor_Child_Mask_Zora", g_mask, RGBX(90, 160, 230), RGBX(20, 40, 80)},
    {"Armor_Child_Mask_Gerudo", g_mask, RGBX(210, 130, 60), RGBX(60, 30, 20)},
    {"Armor_Child_Mask_Truth", g_mask, RGBX(130, 150, 220), RGBX(250, 230, 120)},
    {"Item_DekuStick", g_stick, RGBX(168, 120, 70), RGBX(230, 200, 150)},
    {"Item_DekuNut", g_nut, RGBX(190, 140, 70), RGBX(250, 220, 160)},
    {"Item_Bombchu", g_bomb, RGBX(220, 90, 130), RGBX(240, 200, 120)},
    {"Item_Bomb", g_bomb, RGBX(60, 64, 78), RGBX(190, 170, 120)},
    {"Weapon_Range_SlingShot", g_sling, RGBX(160, 112, 64), RGBX(200, 200, 200)},
    {"Item_DekuSeed", g_seed, RGBX(150, 210, 90), RGBX(240, 255, 200)},
    {"Weapon_Range_Bow", g_bow, RGBX(176, 124, 70), RGBX(230, 230, 230)},
    {"Item_Arrow_Fire", g_arrow, RGBX(230, 130, 60), RGBX(255, 90, 40)},
    {"Item_Arrow_Ice", g_arrow, RGBX(130, 210, 250), RGBX(80, 170, 255)},
    {"Item_Arrow_Light", g_arrow, RGBX(250, 230, 120), RGBX(255, 250, 200)},
    {"Item_Arrow", g_arrow, RGBX(190, 150, 100), SILVER},
    {"Item_DinFire", g_gem, RGBX(230, 70, 60), RGBX(255, 190, 150)},
    {"Item_FaroreWind", g_gem, RGBX(70, 200, 110), RGBX(190, 255, 200)},
    {"Item_NayruLove", g_gem, RGBX(70, 120, 235), RGBX(190, 210, 255)},
    {"Item_Ocarina_Fairy", g_ocarina, RGBX(110, 140, 220), RGBX(30, 40, 90)},
    {"Item_Ocarina_Time", g_ocarina, RGBX(60, 90, 230), RGBX(240, 230, 150)},
    {"Item_Hookshot", g_hook, SILVER, RGBX(140, 100, 60)},
    {"Item_Longshot", g_hook, GOLD, RGBX(140, 100, 60)},
    {"Item_Boomerang", g_boomerang, RGBX(200, 140, 60), 0},
    {"Item_GlassTruth", g_lens, RGBX(120, 220, 150), RGBX(190, 190, 200)},
    {"Item_MagicBean", g_bean, RGBX(150, 210, 90), RGBX(90, 170, 60)},
    {"Item_Hammer", g_hammer, RGBX(150, 156, 170), RGBX(150, 100, 60)},
    {"Item_Bottle", g_bottle, RGBX(190, 210, 235), RGBX(190, 140, 80)},
    {"Item_Gauntlet_Goron", g_glove, RGBX(200, 120, 70), RGBX(120, 70, 40)},
    {"Item_Scale_Silver", g_scale, SILVER, RGBX(90, 140, 220)},
    {"Item_Scale_Gold", g_scale, GOLD, RGBX(90, 140, 220)},
    {"Item_BombBag", g_bag, RGBX(120, 90, 140), RGBX(240, 200, 100)},
    {"Item_Quiver", g_quiver, RGBX(170, 110, 60), RGBX(230, 230, 230)},
    {"Item_BulletBag", g_bag, RGBX(110, 150, 80), RGBX(240, 220, 120)},
    {"Item_Wallet", g_wallet, RGBX(70, 160, 90), GOLD},
    {"Item_FairyBless_MPDouble", g_jar, RGBX(60, 190, 90), RGBX(240, 240, 240)},
    {"Item_FairyBless_MP", g_jar, RGBX(70, 190, 110), RGBX(200, 200, 210)},
    {"Item_FairyBless_Defense", g_jar, RGBX(240, 120, 150), RGBX(240, 240, 240)},
    {"Item_Stone_Forest", g_gem, RGBX(70, 190, 90), RGBX(210, 255, 210)},
    {"Item_Stone_Fire", g_gem, RGBX(220, 70, 60), RGBX(255, 210, 190)},
    {"Item_Stone_Water", g_gem, RGBX(70, 130, 235), RGBX(200, 220, 255)},
    {"Item_Medal_Forest", g_medal, RGBX(70, 190, 90), GOLD},
    {"Item_Medal_Fire", g_medal, RGBX(230, 80, 60), GOLD},
    {"Item_Medal_Water", g_medal, RGBX(70, 130, 235), GOLD},
    {"Item_Medal_Shadow", g_medal, RGBX(150, 80, 190), GOLD},
    {"Item_Medal_Spirit", g_medal, RGBX(235, 150, 60), GOLD},
    {"Item_Medal_Light", g_medal, RGBX(250, 230, 110), GOLD},
    {"Item_Melody_ZeldaLullaby", g_note, RGBX(240, 140, 190), 0},
    {"Item_Melody_EponaSong", g_note, RGBX(230, 140, 70), 0},
    {"Item_Melody_SariaSong", g_note, RGBX(90, 200, 110), 0},
    {"Item_Melody_SunSong", g_note, RGBX(250, 220, 90), 0},
    {"Item_Melody_TimeSong", g_note, RGBX(90, 140, 240), 0},
    {"Item_Melody_StormSong", g_note, RGBX(160, 170, 190), 0},
    {"Item_Melody_ForestMinuet", g_note, RGBX(70, 190, 90), 0},
    {"Item_Melody_FireBolero", g_note, RGBX(230, 80, 60), 0},
    {"Item_Melody_WaterSerenade", g_note, RGBX(70, 130, 235), 0},
    {"Item_Melody_ShadowNocturne", g_note, RGBX(160, 90, 200), 0},
    {"Item_Melody_SpiritRequiem", g_note, RGBX(235, 150, 60), 0},
    {"Item_Melody_LightPrelude", g_note, RGBX(250, 230, 110), 0},
    {"Item_AgonyStone", g_eye, RGBX(190, 150, 230), RGBX(250, 230, 120)},
    {"Item_Token_Gerudo", g_card, RGBX(210, 120, 90), RGBX(250, 230, 180)},
    {"Item_Token_Gold", g_token, GOLD, RGBX(120, 90, 30)},
};
static void draw_icon(const char *pouch_name, float x, float y, float s) {
    for (unsigned i = 0; i < sizeof icon_rules / sizeof icon_rules[0]; i++) {
        if (!strncmp(pouch_name, icon_rules[i].prefix, strlen(icon_rules[i].prefix))) {
            icon_rules[i].fn(x, y, s, icon_rules[i].c1, icon_rules[i].c2);
            return;
        }
    }
    g_generic(x, y, s, RGBX(130, 140, 150), RGBX(200, 200, 200));
}

/* ---- the game's own icons: sheikah_icons\<pouch name>.png, decoded from the player's romfs by the build (nx2/item_icons.py).
   None of them is in this repository; an item without one (or a build without the folder) gets the drawn shape above ---- */
typedef struct { char name[56]; GpObj *img; UINT w, h; } Art;
static Art arts[256];
static int art_count;
static char icon_dir[MAX_PATH];
static void icon_dir_init(void) {
    if (icon_dir[0]) return;
    const char *env = getenv("SHEIKAH_ICON_DIR");   /* (the offline render) */
    if (env) {
        snprintf(icon_dir, sizeof icon_dir, "%s\\", env);
        return;
    }
    DWORD n = hos_game_path_a(icon_dir, MAX_PATH);
    if (!n || n >= MAX_PATH - 20) { strcpy(icon_dir, "?\\"); return; }
    while (n && icon_dir[n - 1] != '/' && icon_dir[n - 1] != '\\') n--;
    snprintf(icon_dir + n, sizeof icon_dir - n, "sheikah_icons\\");
}
/* The game's image <name>.png from sheikah_icons\ (NULL when it is not there), loaded once and kept. */
static GpObj *art_get(const char *name, int *w, int *h) {
    for (int i = 0; i < art_count; i++)
        if (!strcmp(arts[i].name, name)) {
            if (w) *w = (int)arts[i].w;
            if (h) *h = (int)arts[i].h;
            return arts[i].img;
        }
    if (art_count >= 256) return NULL;
    Art *a = &arts[art_count++];
    snprintf(a->name, sizeof a->name, "%s", name);
    icon_dir_init();
    char path[MAX_PATH + 64];
    WCHAR wide[MAX_PATH + 64];
    snprintf(path, sizeof path, "%s%s.png", icon_dir, name);
    MultiByteToWideChar(CP_ACP, 0, path, -1, wide, (int)(sizeof wide / sizeof wide[0]));
    if (GdipCreateBitmapFromFile(wide, &a->img) != 0 || !a->img) a->img = NULL;
    else GdipGetImageWidth(a->img, &a->w), GdipGetImageHeight(a->img, &a->h);
    if (w) *w = (int)a->w;
    if (h) *h = (int)a->h;
    return a->img;
}
/* draws the left frac (0..1) of the image into the rectangle's left frac; 1 when the image exists */
static int draw_art_part(const char *name, float x, float y, float w, float h, float frac) {
    int iw = 0, ih = 0;
    GpObj *img = art_get(name, &iw, &ih);
    if (!img || frac <= 0) return img != NULL;
    if (frac > 1) frac = 1;
    GdipSetInterpolationMode(G, 7);
    GdipDrawImageRectRectI(G, img, (int)x, (int)y, (int)(w * frac + 0.5f), (int)h, 0, 0, (int)((float)iw * frac + 0.5f), ih, 2, NULL, NULL, NULL);
    return 1;
}
static int draw_art(const char *name, float x, float y, float w, float h) { return draw_art_part(name, x, y, w, h, 1.0f); }
static void draw_item_icon(int item, const char *pouch_name, float x, float y, float s) {
    (void)item;
    if (!strcmp(pouch_name, "Item_GlassTruth")) pouch_name = "Armor_GlassTruth";   /* (the game's icon is filed under the other name) */
    if (!draw_art(pouch_name, x, y, s, s)) draw_icon(pouch_name, x, y, s);
}

/* ---- the items (the cheats' list: gpu/vk/oot_cheat_items.h) ---- */
#define ITEM_LABEL(label, name, kind) label,
#define ITEM_NAME(label, name, kind) name,
#define ITEM_KIND(label, name, kind) kind,
#define ITEM_SECTION(name)
#define SECTION_ROW(name) name,
#define SECTION_ITEM(label, name, kind) "\x01",
static const char *const item_labels[] = {OOT_CHEAT_ITEMS(ITEM_SECTION, ITEM_LABEL)};
static const char *const item_names[] = {OOT_CHEAT_ITEMS(ITEM_SECTION, ITEM_NAME)};
static const unsigned char item_kinds[] = {OOT_CHEAT_ITEMS(ITEM_SECTION, ITEM_KIND)};
#define ITEMS (int)(sizeof item_names / sizeof item_names[0])
/* the section each item is in: the heading rows and the items in one list, headings marked "\x01" apart */
static const char *const item_layout[] = {OOT_CHEAT_ITEMS(SECTION_ROW, SECTION_ITEM)};

/* ---- the map: the cheats' destinations (the order of oot_destinations in host/hos_main.c), laid out as a graph ---- */
typedef struct { const char *name; float x, y; int kind; } Place;   /* kind 0 area, 1 dungeon; colour by region */
static const Place places[] = {
    {"Kokiri Forest", 160, 420, 0}, {"Hyrule Field", 480, 330, 0}, {"Kakariko Village", 680, 260, 0}, {"Market", 480, 215, 0},
    {"Hyrule Castle", 480, 110, 0}, {"Lost Woods", 300, 340, 0}, {"Sacred Forest Meadow", 230, 235, 0}, {"Lake Hylia", 520, 500, 0},
    {"Graveyard", 810, 260, 0}, {"Death Mountain Trail", 690, 150, 0}, {"Death Mountain Crater", 880, 50, 0}, {"Goron City", 800, 120, 0},
    {"Zora's River", 640, 400, 0}, {"Zora's Domain", 780, 430, 0}, {"Zora's Fountain", 900, 430, 0}, {"Gerudo Valley", 330, 480, 0},
    {"Gerudo Fortress", 210, 540, 0}, {"Haunted Wasteland", 330, 610, 0}, {"Desert Colossus", 450, 615, 0}, {"Deku Tree", 60, 500, 1},
    {"Dodongo's Cavern", 590, 95, 1}, {"Jabu-Jabu's Belly", 930, 520, 1}, {"Forest Temple", 110, 150, 1}, {"Fire Temple", 950, 130, 1},
    {"Water Temple", 580, 600, 1}, {"Shadow Temple", 920, 330, 1}, {"Spirit Temple", 400, 545, 1}, {"Bottom of the Well", 760, 340, 1},
    {"Ice Cavern", 820, 520, 1}, {"Gerudo Training Ground", 90, 590, 1}, {"Ganon's Castle", 330, 70, 1},
};
#define PLACES (int)(sizeof places / sizeof places[0])
static const unsigned char edges[][2] = {
    {1, 3}, {3, 4}, {4, 30}, {1, 5}, {5, 0}, {0, 19}, {5, 6}, {6, 22}, {1, 2}, {2, 8}, {8, 25}, {2, 27}, {2, 9}, {9, 20}, {9, 11}, {11, 10},
    {10, 23}, {1, 12}, {12, 13}, {13, 14}, {14, 21}, {14, 28}, {1, 7}, {7, 24}, {1, 15}, {15, 16}, {16, 29}, {16, 17}, {17, 18}, {18, 26},
};
static UINT32 place_colour(int i) {
    switch (i) {
    case 19: return RGBX(150, 110, 60);    /* Deku Tree */
    case 20: return RGBX(200, 90, 60);     /* Dodongo's */
    case 21: return RGBX(90, 190, 220);    /* Jabu-Jabu */
    case 22: return RGBX(70, 190, 90);     /* Forest */
    case 23: return RGBX(230, 80, 60);     /* Fire */
    case 24: return RGBX(70, 130, 235);    /* Water */
    case 25: return RGBX(160, 90, 200);    /* Shadow */
    case 26: return RGBX(235, 150, 60);    /* Spirit */
    case 27: return RGBX(150, 150, 160);   /* Well */
    case 28: return RGBX(170, 225, 250);   /* Ice Cavern */
    case 29: return GOLD;                  /* Training Ground */
    case 30: return RGBX(110, 60, 140);    /* Ganon's Castle */
    default: break;
    }
    if (i == 0 || i == 5 || i == 6) return RGBX(90, 190, 110);          /* the forest */
    if (i == 2 || i == 8) return RGBX(190, 150, 110);                   /* Kakariko, the graveyard */
    if (i == 9 || i == 10 || i == 11) return RGBX(210, 110, 80);        /* Death Mountain */
    if (i == 12 || i == 13 || i == 14 || i == 7) return RGBX(90, 160, 230);   /* the waters */
    if (i == 15 || i == 16 || i == 17 || i == 18) return RGBX(228, 180, 100);   /* the desert */
    return RGBX(200, 210, 200);                                         /* the field, the market, the castle */
}

/* ---- state ---- */
enum { TAB_ITEMS, TAB_LINK, TAB_MAP, TAB_WORLD, TAB_FLAGS, TABS };
static const char *const tab_names[TABS] = {"Items", "Link", "Map", "World", "Flags"};
enum { H_TAB = 1, H_ITEM, H_PLACE, H_AGE, H_HOUR, H_FLAG, H_KIND, H_SEARCH, H_HEART, H_STAT, H_MPBAR, H_SEC, H_NONE };
static int mouse_x;
extern volatile int hos_oot_stats[8];   /* host/oot_live_editor.inc: rupees, life, max life, magic, max magic, valid */
void hos_stat_set(int which, int value);
typedef struct { RECT r; int kind, arg; } Hit;
#define HITS_MAX 1024
static Hit hits[HITS_MAX];
static int hit_count, hover_kind, hover_arg, tab = TAB_ITEMS;
static int scroll_y[TABS], content_h, view_h;
static char flag_search[64];
static int flag_kind = -1, flag_matches, flag_dirty = 1;
static char toast[120];
static DWORD toast_until;
static HFONT f_small, f_body, f_bold, f_title;
static int font_for_dpi;

static void make_fonts(void) {
    const int key = (int)(S * 100);
    if (font_for_dpi == key) return;
    font_for_dpi = key;
    HFONT *fs[] = {&f_small, &f_body, &f_bold, &f_title};
    for (int i = 0; i < 4; i++) if (*fs[i]) DeleteObject(*fs[i]);
    f_small = CreateFontA(-(int)(12 * S), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    f_body = CreateFontA(-(int)(14 * S), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    f_bold = CreateFontA(-(int)(14 * S), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    f_title = CreateFontA(-(int)(22 * S), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
}
static int px(float v) { return (int)(v * S + 0.5f); }

static void say_w(HDC dc, HFONT f, const char *s, int x, int y, COLORREF c, UINT fmt, int w) {
    /* x: the left edge, or the middle with DT_CENTER, or the right edge with DT_RIGHT; w: the width to fit (0: none, and no ellipsis) */
    const int span = w ? w : 2000;
    RECT r = {x, y, x + span, y + 100};
    if (fmt & DT_CENTER) r.left = x - span / 2, r.right = x + span / 2;
    if (fmt & DT_RIGHT) r.left = x - span, r.right = x;
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextA(dc, s, -1, &r, fmt | DT_SINGLELINE | DT_NOPREFIX | (w ? 0 : DT_NOCLIP));
}
static void say(HDC dc, HFONT f, const char *s, int x, int y, COLORREF c, UINT fmt) { say_w(dc, f, s, x, y, c, fmt, 0); }
static int text_w(HDC dc, HFONT f, const char *s) {
    SIZE z;
    SelectObject(dc, f);
    GetTextExtentPoint32A(dc, s, (int)strlen(s), &z);
    return z.cx;
}
static void add_hit(int x, int y, int w, int h, int kind, int arg, int clip_top, int clip_bottom) {
    if (hit_count >= HITS_MAX || y + h < clip_top || y > clip_bottom) return;
    hits[hit_count].r = (RECT){x, y, x + w, y + h};
    hits[hit_count].kind = kind;
    hits[hit_count].arg = arg;
    hit_count++;
}
static int hovered(int kind, int arg) {
    return hover_kind == kind && hover_arg == arg;
}

/* ---- the game's side of a click ---- */
static int game_alive(void) { return hos_editor_game_alive(); }
static void say_toast(const char *s) {
    snprintf(toast, sizeof toast, "%s", s);
    toast_until = GetTickCount() + 3500;
}
static int post_action(int action) {
    if (!game_alive()) {
        say_toast("Start playing first: changes are made in the running world.");
        return 0;
    }
    if (InterlockedCompareExchange((volatile LONG *)&hos_oot_live_action, action, 0) != 0) {
        say_toast("The last change is still being made, try again.");
        return 0;
    }
    return 1;
}

/* ---- the tabs ---- */
static int item_sec;   /* the section shown: Equipment, Items, Masks, Upgrades, Quest (the cheats' list's headings) */
static void paint_items(HDC dc, int W, int top, int bottom) {
    const int tile = px(104), gap = px(10), pad = px(24), strip = px(58);
    const int cols = (W - 2 * pad + gap) / (tile + gap) > 0 ? (W - 2 * pad + gap) / (tile + gap) : 1;
    const int alive = game_alive();
    const int nlayout = (int)(sizeof item_layout / sizeof item_layout[0]);
    int y = top + strip + px(10) - scroll_y[TAB_ITEMS];
    int item = 0, col = 0, sec = -1, shown = 0;
    for (int i = 0; i < nlayout; i++) {
        if (item_layout[i][0] != '') { sec++; continue; }   /* a section heading */
        if (sec == item_sec) {
            const int x = pad + col * (tile + gap);
            const int count = alive ? hos_oot_item_counts[item] : 0;
            const int have = count > 0;
            const int over = hovered(H_ITEM, item);
            if (y + tile >= top && y <= bottom) {
                fill_rrect((float)x, (float)y, (float)tile, (float)tile, (float)px(10), over ? RGBX(44, 58, 50) : RGBX(30, 38, 34));
                stroke_rrect((float)x, (float)y, (float)tile, (float)tile, (float)px(10), have ? RGBX(96, 176, 112) : over ? RGBX(110, 130, 118) : RGBX(46, 58, 52), have ? 2.0f : 1.0f);
                const int isz = px(68);
                draw_item_icon(item, item_names[item], (float)(x + (tile - isz) / 2), (float)(y + px(6)), (float)isz);
                if (!have) fill_rrect((float)(x + px(2)), (float)(y + px(2)), (float)(tile - px(4)), (float)(tile - px(4)), (float)px(9), RGBA(165, 24, 30, 27));   /* an item Link lacks: veiled */
                say_w(dc, f_small, item_labels[item], x + tile / 2, y + tile - px(24), have ? C_TEXT : C_DIM, DT_CENTER | DT_END_ELLIPSIS, tile - px(8));
                if (item_kinds[item] && alive) {   /* a quantity: its count in a badge */
                    char n[16];
                    snprintf(n, sizeof n, "%d", count);
                    const int bw = text_w(dc, f_small, n) + px(12);
                    fill_rrect((float)(x + tile - bw - px(6)), (float)(y + px(6)), (float)bw, (float)px(18), (float)px(9), have ? RGBX(96, 176, 112) : RGBX(60, 70, 66));
                    say(dc, f_small, n, x + tile - bw / 2 - px(6), y + px(7), have ? RGB(10, 20, 12) : C_DIM, DT_CENTER);
                }
            }
            add_hit(x, y, tile, tile, H_ITEM, item, top + strip, bottom);
            shown++;
            if (++col == cols) col = 0, y += tile + gap;
        }
        item++;
    }
    if (col) y += tile + gap;
    content_h = y + scroll_y[TAB_ITEMS] - top + pad;
    (void)shown;
    /* the section tabs, pinned above the tiles that scroll under them */
    fill_rrect(0, (float)top, (float)W, (float)strip, 0, RGBX(16, 20, 18));
    add_hit(0, top, W, strip, H_NONE, 0, top, bottom);
    int x = pad, k = -1;
    for (int i = 0; i < nlayout; i++) {
        if (item_layout[i][0] == '') continue;
        k++;
        const int w = text_w(dc, f_bold, item_layout[i]) + px(34), h = px(34), yy = top + px(12), on = item_sec == k, over = hovered(H_SEC, k);
        fill_rrect((float)x, (float)yy, (float)w, (float)h, (float)px(17), on ? RGBX(70, 130, 84) : over ? RGBX(44, 56, 50) : RGBX(30, 38, 34));
        say(dc, f_bold, item_layout[i], x + w / 2, yy + px(8), on ? RGB(255, 255, 255) : RGB(196, 204, 198), DT_CENTER);
        add_hit(x, yy, w, h, H_SEC, k, top, bottom);
        x += w + px(8);
    }
    line(0, (float)(top + strip), (float)W, (float)(top + strip), RGBX(40, 50, 45), 1.0f);
}

static void paint_map_graph(HDC dc, int W, int top, int bottom) {   /* (the schematic: without the game's map image) */
    const int pad = px(24);
    const float mw = (float)(W - 2 * pad), mh = (float)(bottom - top - 2 * pad);
    const float sx = mw / 1040.0f, sy = mh / 660.0f, k = sx < sy ? sx : sy;
    const float ox = (float)pad + (mw - 1040.0f * k) / 2, oy = (float)(top + pad) + (mh - 660.0f * k) / 2;
    fill_rrect(ox - (float)px(8), oy - (float)px(8), 1040.0f * k + (float)px(16), 660.0f * k + (float)px(16), (float)px(14), RGBX(22, 28, 25));
    for (unsigned i = 0; i < sizeof edges / sizeof edges[0]; i++)
        line(ox + places[edges[i][0]].x * k, oy + places[edges[i][0]].y * k, ox + places[edges[i][1]].x * k, oy + places[edges[i][1]].y * k, RGBX(58, 72, 64), 2.0f * S);
    for (int i = 0; i < PLACES; i++) {
        const float cx = ox + places[i].x * k, cy = oy + places[i].y * k, r = (float)px(places[i].kind ? 15 : 19);
        const int over = hovered(H_PLACE, i);
        const UINT32 c = place_colour(i);
        if (over) fill_ellipse(cx - r - (float)px(7), cy - r - (float)px(7), 2 * r + (float)px(14), 2 * r + (float)px(14), (c & 0xFFFFFF) | 0x40000000u);
        if (places[i].kind) {
            const GpPt d[] = {{cx, cy - r - (float)px(3)}, {cx + r + (float)px(3), cy}, {cx, cy + r + (float)px(3)}, {cx - r - (float)px(3), cy}};
            fill_poly(d, 4, RGBX(20, 24, 22));
            fill_poly(d, 4, (c & 0xFFFFFF) | 0xCC000000u);
            stroke_poly(d, 4, over ? RGBX(255, 255, 255) : c, 2.0f);
        } else {
            fill_ellipse(cx - r, cy - r, 2 * r, 2 * r, RGBX(20, 24, 22));
            fill_ellipse(cx - r + (float)px(3), cy - r + (float)px(3), 2 * r - (float)px(6), 2 * r - (float)px(6), (c & 0xFFFFFF) | 0xCC000000u);
            stroke_ellipse(cx - r, cy - r, 2 * r, 2 * r, over ? RGBX(255, 255, 255) : c, 2.0f);
        }
        say(dc, f_small, places[i].name, (int)cx, (int)(cy + r + (float)px(6)), over ? C_TEXT : RGB(190, 200, 194), DT_CENTER);
        add_hit((int)(cx - r - (float)px(8)), (int)(cy - r - (float)px(8)), (int)(2 * r + (float)px(16)), (int)(2 * r + (float)px(30)), H_PLACE, i, top, bottom);
    }
    content_h = 0;
}

/* where each place (the order of places[]) is on the game's world map image, in a 1152 x 840 frame */
static const short place_xy[][2] = {
    {965, 515}, {500, 480}, {770, 335}, {490, 325}, {462, 205}, {740, 535}, {905, 560}, {450, 690}, {870, 285}, {650, 255}, {725, 150},
    {755, 215}, {690, 410}, {835, 405}, {985, 400}, {270, 455}, {235, 345}, {140, 300}, {165, 230}, {790, 595}, {620, 205}, {1010, 430},
    {850, 500}, {705, 175}, {470, 720}, {915, 275}, {200, 205}, {800, 315}, {940, 405}, {215, 330}, {480, 165},
};
static void paint_map(HDC dc, int W, int top, int bottom) {
    int iw = 0, ih = 0;
    GpObj *map = art_get("ui_world_map", &iw, &ih);
    if (!map) {
        paint_map_graph(dc, W, top, bottom);
        return;
    }
    const int pad = px(18);
    const float aw = (float)(W - 2 * pad), ah = (float)(bottom - top - 2 * pad);
    const float k = aw / 1152.0f < ah / 840.0f ? aw / 1152.0f : ah / 840.0f;
    const float ox = (float)pad + (aw - 1152.0f * k) / 2, oy = (float)(top + pad) + (ah - 840.0f * k) / 2;
    GdipSetInterpolationMode(G, 7);
    GdipDrawImageRectI(G, map, (int)ox, (int)oy, (int)(1152.0f * k), (int)(840.0f * k));
    stroke_rrect(ox, oy, 1152.0f * k, 840.0f * k, 2.0f, RGBX(70, 56, 30), 2.0f);
    int over_place = -1;
    for (int i = 0; i < PLACES; i++) {   /* the markers: the game's boss skull for a dungeon, a ring for an area */
        const float cx = ox + (float)place_xy[i][0] * k, cy = oy + (float)place_xy[i][1] * k;
        const float r = (float)px(places[i].kind ? 15 : 9);
        const int over = hovered(H_PLACE, i);
        if (over) {
            over_place = i;
            fill_ellipse(cx - r - (float)px(7), cy - r - (float)px(7), 2 * r + (float)px(14), 2 * r + (float)px(14), RGBA(120, 255, 255, 255));
        }
        if (places[i].kind) {
            if (!draw_art("ui_boss", cx - r, cy - r, 2 * r, 2 * r)) {
                const GpPt d[] = {{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}};
                fill_poly(d, 4, RGBX(150, 30, 30));
                stroke_poly(d, 4, RGBX(250, 235, 200), 2.0f);
            }
        } else {
            fill_ellipse(cx - r, cy - r, 2 * r, 2 * r, over ? RGBX(255, 255, 255) : RGBA(225, 235, 250, 244));
            stroke_ellipse(cx - r, cy - r, 2 * r, 2 * r, RGBX(70, 40, 20), 2.5f);
            fill_ellipse(cx - r * 0.35f, cy - r * 0.35f, r * 0.7f, r * 0.7f, RGBX(70, 40, 20));
        }
        add_hit((int)(cx - r - (float)px(6)), (int)(cy - r - (float)px(6)), (int)(2 * r + (float)px(12)), (int)(2 * r + (float)px(12)), H_PLACE, i, top, bottom);
    }
    if (over_place >= 0) {   /* the name of the place under the cursor, above it */
        const float cx = ox + (float)place_xy[over_place][0] * k, cy = oy + (float)place_xy[over_place][1] * k;
        const int tw = text_w(dc, f_bold, places[over_place].name) + px(20), th = px(28);
        int lx = (int)cx - tw / 2, ly = (int)cy - px(48);
        if (lx < pad) lx = pad;
        if (lx + tw > W - pad) lx = W - pad - tw;
        if (ly < top + px(4)) ly = (int)cy + px(24);
        fill_rrect((float)lx, (float)ly, (float)tw, (float)th, (float)px(8), RGBA(235, 28, 22, 18));
        stroke_rrect((float)lx, (float)ly, (float)tw, (float)th, (float)px(8), RGBX(214, 178, 96), 1.5f);
        say(dc, f_bold, places[over_place].name, lx + tw / 2, ly + px(5), RGB(250, 240, 214), DT_CENTER);
    }
    content_h = 0;
}

static void paint_world(HDC dc, int W, int top, int bottom) {
    const int pad = px(24), cw = (W - 3 * pad) / 2;
    /* the time of day: a dial of 24 hours, noon at the top */
    const int x0 = pad, y0 = top + pad, ch = bottom - top - 2 * pad;
    fill_rrect((float)x0, (float)y0, (float)cw, (float)ch, (float)px(14), RGBX(25, 31, 28));
    say(dc, f_bold, "Time of day", x0 + px(20), y0 + px(16), C_GREEN, 0);
    const float cx = (float)(x0 + cw / 2), cy = (float)(y0 + ch / 2 + px(10));
    float R = (float)(cw < ch ? cw : ch) / 2 - (float)px(52);
    if (R < (float)px(60)) R = (float)px(60);
    stroke_ellipse(cx - R, cy - R, 2 * R, 2 * R, RGBX(52, 64, 58), 2.0f);
    int hover_hour = -1;
    for (int h = 0; h < 24; h++) {
        const float a = ((float)(h - 12) / 24.0f) * 6.2831853f - 1.5707963f;
        const float hx = cx + R * cosf(a), hy = cy + R * sinf(a), r = (float)px(17);
        const float day = 0.5f + 0.5f * cosf(((float)(h - 12) / 12.0f) * 3.14159265f);   /* 1 at noon, 0 at midnight */
        const int cr = (int)(40 + 210 * day), cg = (int)(50 + 170 * day), cb = (int)(110 - 20 * day);
        const int over = hovered(H_HOUR, h);
        if (over) hover_hour = h;
        fill_ellipse(hx - r, hy - r, 2 * r, 2 * r, over ? RGBX(255, 255, 255) : RGBX(cr, cg, cb));
        if (!over) stroke_ellipse(hx - r, hy - r, 2 * r, 2 * r, RGBX(10, 14, 12), 1.0f);
        char n[8];
        snprintf(n, sizeof n, "%d", h);
        say(dc, f_small, n, (int)hx, (int)hy - px(8), day > 0.5f || over ? RGB(30, 30, 20) : RGB(220, 230, 255), DT_CENTER);
        add_hit((int)(hx - r), (int)(hy - r), (int)(2 * r), (int)(2 * r), H_HOUR, h, top, bottom);
    }
    {   /* the middle: a sun or a moon for the hour under the cursor, and what a click does */
        const int night = hover_hour >= 0 && (hover_hour < 6 || hover_hour >= 18);
        const char *glyph = (hover_hour < 0 || !night) ? "Item_Melody_SunSong" : "Item_Melody_TimeSong";   /* the songs that move the clock */
        if (!draw_art(glyph, cx - (float)px(36), cy - (float)px(58), (float)px(72), (float)px(72))) {
        if (hover_hour < 0 || !night) {
            fill_ellipse(cx - (float)px(26), cy - (float)px(38), (float)px(52), (float)px(52), RGBX(250, 214, 90));
            for (int i = 0; i < 8; i++) {
                const float a = (float)i * 0.785398f;
                line(cx + (float)px(34) * cosf(a), cy - (float)px(12) + (float)px(34) * sinf(a), cx + (float)px(46) * cosf(a), cy - (float)px(12) + (float)px(46) * sinf(a), RGBX(250, 214, 90), 3.0f * S);
            }
        } else {
            fill_ellipse(cx - (float)px(26), cy - (float)px(38), (float)px(52), (float)px(52), RGBX(220, 226, 245));
            fill_ellipse(cx - (float)px(10), cy - (float)px(46), (float)px(46), (float)px(46), RGBX(25, 31, 28));
        }
        }
        char msg[48];
        if (hover_hour >= 0) snprintf(msg, sizeof msg, "Set the clock to %02d:00", hover_hour);
        else snprintf(msg, sizeof msg, "Choose an hour");
        say(dc, f_body, msg, (int)cx, (int)cy + px(30), C_TEXT, DT_CENTER);
    }
    /* the age switch */
    const int x1 = pad * 2 + cw;
    fill_rrect((float)x1, (float)y0, (float)cw, (float)ch, (float)px(14), RGBX(25, 31, 28));
    say(dc, f_bold, "Age", x1 + px(20), y0 + px(16), C_GREEN, 0);
    const int bx = x1 + cw / 2 - px(150), by = y0 + ch / 2 - px(100), bw = px(300), bh = px(200);
    const int over = hovered(H_AGE, 0);
    fill_rrect((float)bx, (float)by, (float)bw, (float)bh, (float)px(16), over ? RGBX(44, 62, 50) : RGBX(32, 42, 36));
    stroke_rrect((float)bx, (float)by, (float)bw, (float)bh, (float)px(16), over ? RGBX(120, 220, 140) : RGBX(96, 176, 112), 2.0f);
    {   /* a child and an adult, two figures of different height, an arrow between */
        const float fx1 = (float)(bx + bw / 2 - px(70)), fx2 = (float)(bx + bw / 2 + px(70)), base = (float)(by + px(128));
        if (art_get("Weapon_Sword_Kokiri", NULL, NULL) && art_get("Weapon_Sword_Master", NULL, NULL)) {   /* the game's own: a child's gear, an adult's */
            draw_art("Weapon_Shield_Deku", fx1 - (float)px(46), base - (float)px(86), (float)px(64), (float)px(64));
            draw_art("Weapon_Sword_Kokiri", fx1 - (float)px(14), base - (float)px(92), (float)px(64), (float)px(64));
            draw_art("Weapon_Shield_Hylian", fx2 - (float)px(50), base - (float)px(100), (float)px(80), (float)px(80));
            draw_art("Weapon_Sword_Master", fx2 - (float)px(14), base - (float)px(108), (float)px(80), (float)px(80));
        } else {
            fill_ellipse(fx1 - (float)px(15), base - (float)px(62), (float)px(30), (float)px(30), RGBX(236, 200, 160));
            fill_poly((GpPt[]){{fx1, base - (float)px(34)}, {fx1 + (float)px(20), base}, {fx1 - (float)px(20), base}}, 3, RGBX(70, 170, 90));
            fill_ellipse(fx2 - (float)px(18), base - (float)px(90), (float)px(36), (float)px(36), RGBX(236, 200, 160));
            fill_poly((GpPt[]){{fx2, base - (float)px(50)}, {fx2 + (float)px(26), base}, {fx2 - (float)px(26), base}}, 3, RGBX(70, 170, 90));
        }
        line((float)(bx + bw / 2 - px(26)), base - (float)px(30), (float)(bx + bw / 2 + px(26)), base - (float)px(30), RGBX(236, 240, 236), 3.0f * S);
        line((float)(bx + bw / 2 + px(12)), base - (float)px(42), (float)(bx + bw / 2 + px(26)), base - (float)px(30), RGBX(236, 240, 236), 3.0f * S);
        line((float)(bx + bw / 2 + px(12)), base - (float)px(18), (float)(bx + bw / 2 + px(26)), base - (float)px(30), RGBX(236, 240, 236), 3.0f * S);
        say(dc, f_small, "Child", (int)fx1, (int)base + px(8), C_DIM, DT_CENTER);
        say(dc, f_small, "Adult", (int)fx2, (int)base + px(8), C_DIM, DT_CENTER);
        say(dc, f_bold, "Switch age", bx + bw / 2, by + bh - px(34), C_TEXT, DT_CENTER);
    }
    add_hit(bx, by, bw, bh, H_AGE, 0, top, bottom);
    say(dc, f_small, "Swaps Link between child and adult and reloads this map in the other age.", x1 + cw / 2, by + bh + px(18), C_DIM, DT_CENTER);
    content_h = 0;
}

/* ---- Link: hearts, rupees, magic (the game's PlayerStatus numbers, host/oot_live_editor.inc) ---- */
static void fill_heart(float cx, float cy, float s, UINT32 c) {
    fill_ellipse(cx - 0.50f * s, cy - 0.40f * s, 0.54f * s, 0.54f * s, c);
    fill_ellipse(cx - 0.04f * s, cy - 0.40f * s, 0.54f * s, 0.54f * s, c);
    const GpPt t[] = {{cx - 0.49f * s, cy - 0.02f * s}, {cx + 0.49f * s, cy - 0.02f * s}, {cx, cy + 0.52f * s}};
    fill_poly(t, 3, c);
}
static void ui_button(HDC dc, int x, int y, int w, int h, const char *label, int id, int clip_top, int clip_bottom) {
    const int over = hovered(H_STAT, id);
    fill_rrect((float)x, (float)y, (float)w, (float)h, (float)px(8), over ? RGBX(60, 96, 70) : RGBX(36, 48, 41));
    stroke_rrect((float)x, (float)y, (float)w, (float)h, (float)px(8), over ? RGBX(120, 220, 140) : RGBX(62, 84, 70), 1.0f);
    say(dc, f_bold, label, x + w / 2, y + (h - px(19)) / 2, over ? RGB(255, 255, 255) : C_TEXT, DT_CENTER);
    add_hit(x, y, w, h, H_STAT, id, clip_top, clip_bottom);
}
static void paint_link(HDC dc, int W, int top, int bottom) {
    const int pad = px(24), cw = W - 2 * pad, alive = game_alive() && hos_oot_stats[5];
    const int rupee = hos_oot_stats[0], life = hos_oot_stats[1], maxlife = hos_oot_stats[2], mp = hos_oot_stats[3], mpmax = hos_oot_stats[4];
    int y = top + pad;
    /* hearts */
    {
        const int ch = px(190), hs = px(36), per = 10, gap = px(8);
        fill_rrect((float)pad, (float)y, (float)cw, (float)ch, (float)px(14), RGBX(25, 31, 28));
        say(dc, f_bold, "Hearts", pad + px(20), y + px(14), C_GREEN, 0);
        char n[64];
        snprintf(n, sizeof n, "Waiting for gameplay");
        if (alive && life % 4 == 0) snprintf(n, sizeof n, "%d of %d hearts", life / 4, maxlife / 4);
        else if (alive) snprintf(n, sizeof n, "%d.%d of %d hearts", life / 4, (life % 4) * 25, maxlife / 4);
        say(dc, f_body, n, pad + cw - px(20), y + px(14), C_TEXT, DT_RIGHT);
        const int hearts = alive ? (maxlife + 3) / 4 : 0, shown = hearts < 20 ? 20 : hearts;
        for (int i = 0; i < shown && i < 20; i++) {
            const int row = i / per, col = i % per;
            const float cx = (float)(pad + px(40) + col * (hs + gap) + hs / 2), cy = (float)(y + px(60) + row * (hs + gap) + hs / 2);
            const int fill = life - i * 4 > 4 ? 4 : life - i * 4 < 0 ? 0 : life - i * 4;   /* quarters of this heart */
            const int over = hovered(H_HEART, i);
            const float as = (float)hs * 1.5f, hx = cx - as / 2, hy = cy - as / 2;   /* (the game's heart images have a margin: drawn larger) */
            if (i < hearts) {
                if (over) fill_ellipse(cx - (float)hs * 0.62f, cy - (float)hs * 0.62f, (float)hs * 1.24f, (float)hs * 1.24f, RGBA(60, 255, 255, 255));
                if (!draw_art("ui_heart_empty", hx, hy, as, as)) fill_heart(cx, cy, (float)hs, over ? RGBX(90, 40, 46) : RGBX(58, 30, 34));
                if (fill && !draw_art_part("ui_heart_full", hx, hy, as, as, (float)fill / 4.0f))   /* (the game's heart, a quarter at a time) */
                    fill_heart(cx, cy, (float)hs * (fill == 4 ? 1.0f : 0.5f + 0.125f * fill), fill == 4 ? RGBX(226, 54, 70) : RGBX(236, 130, 140));
            } else {   /* a slot for a container not yet there */
                g_alpha = over ? 0.55f : 0.25f;
                if (!draw_art("ui_heart_empty", cx - as * 0.35f, cy - as * 0.35f, as * 0.7f, as * 0.7f))
                    fill_heart(cx, cy, (float)hs * 0.7f, over ? RGBX(60, 70, 64) : RGBX(40, 48, 43));
                g_alpha = 1;
            }
            add_hit((int)cx - hs / 2, (int)cy - hs / 2, hs, hs, H_HEART, i, top, bottom);
        }
        const int bx = pad + px(40) + per * (hs + gap) + px(24), bw = px(150), bh = px(34);
        ui_button(dc, bx, y + px(56), bw, bh, "Refill hearts", 1, top, bottom);
        ui_button(dc, bx, y + px(100), bw, bh, "More containers", 2, top, bottom);
        ui_button(dc, bx, y + px(144), bw, bh, "Fewer containers", 3, top, bottom);
        y += ch + px(16);
    }
    /* rupees */
    {
        const int ch = px(150);
        fill_rrect((float)pad, (float)y, (float)cw, (float)ch, (float)px(14), RGBX(25, 31, 28));
        say(dc, f_bold, "Rupees", pad + px(20), y + px(14), C_GREEN, 0);
        char n[32];
        if (alive) snprintf(n, sizeof n, "%d", rupee); else snprintf(n, sizeof n, "-");
        if (!draw_art("ui_rupee", (float)(pad + px(22)), (float)(y + px(46)), (float)px(44), (float)px(44)))
            fill_ellipse((float)(pad + px(26)), (float)(y + px(56)), (float)px(34), (float)px(34), RGBX(70, 190, 100));
        say(dc, f_title, n, pad + px(76), y + px(56), C_TEXT, 0);
        static const char *const names[] = {"-100", "-10", "-1", "+1", "+10", "+100"};
        static const int ids[] = {14, 11, 12, 13, 15, 16};
        const int bw = px(70), bh = px(32), x0 = pad + px(230);
        for (int i = 0; i < 6; i++) ui_button(dc, x0 + i * (bw + px(8)), y + px(52), bw, bh, names[i], ids[i], top, bottom);
        static const char *const pn[] = {"0", "99", "200", "500", "999"};
        say(dc, f_small, "Set to", x0, y + px(100), C_DIM, 0);
        for (int i = 0; i < 5; i++) ui_button(dc, x0 + px(54) + i * (bw + px(8)), y + px(94), bw, bh, pn[i], 20 + i, top, bottom);
        y += ch + px(16);
    }
    /* magic */
    {
        const int ch = px(170);
        fill_rrect((float)pad, (float)y, (float)cw, (float)ch, (float)px(14), RGBX(25, 31, 28));
        say(dc, f_bold, "Magic", pad + px(20), y + px(14), C_GREEN, 0);
        char n[48];
        if (alive && mpmax) snprintf(n, sizeof n, "%d of %d", mp, mpmax);
        else if (alive) snprintf(n, sizeof n, "No magic yet");
        else snprintf(n, sizeof n, "Waiting for gameplay");
        say(dc, f_body, n, pad + cw - px(20), y + px(14), C_TEXT, DT_RIGHT);
        const int bx = pad + px(24), by = y + px(52), bw = cw - px(48) - px(0), bh = px(30);
        const int over = hovered(H_MPBAR, 0);
        fill_rrect((float)bx, (float)by, (float)bw, (float)bh, (float)px(15), over ? RGBX(38, 58, 44) : RGBX(30, 40, 34));
        if (alive && mpmax > 0) {
            const float f = (float)mp / (float)mpmax, w = f < 0 ? 0 : f > 1 ? 1 : f;
            const float maxw = (float)bw * (mpmax > 240 ? 1.0f : 0.5f);   /* the double bar is twice as long */
            if (w > 0.01f && !draw_art_part("ui_mp_fill", (float)bx, (float)by, maxw, (float)bh, w))   /* (the game's gauge) */
                fill_rrect((float)bx, (float)by, maxw * w, (float)bh, (float)px(15), RGBX(80, 200, 110));
            stroke_rrect((float)bx, (float)by, maxw, (float)bh, (float)px(15), RGBX(120, 220, 140), 1.5f);
        }
        if (alive && mpmax > 0) add_hit(bx, by, (int)((float)bw * (mpmax > 240 ? 1.0f : 0.5f)), bh, H_MPBAR, 0, top, bottom);
        const int gap2 = px(8), h2 = px(34), w2 = (bw - 4 * gap2) / 5 < px(150) ? (bw - 4 * gap2) / 5 : px(150);   /* five buttons that fit the card */
        ui_button(dc, bx, y + px(110), w2, h2, "Refill magic", 30, top, bottom);
        ui_button(dc, bx + w2 + gap2, y + px(110), w2, h2, "Empty", 31, top, bottom);
        ui_button(dc, bx + 2 * (w2 + gap2), y + px(110), w2, h2, "Single bar", 32, top, bottom);
        ui_button(dc, bx + 3 * (w2 + gap2), y + px(110), w2, h2, "Double bar", 33, top, bottom);
        ui_button(dc, bx + 4 * (w2 + gap2), y + px(110), w2, h2, "No magic", 34, top, bottom);
        y += ch + px(16);
    }
    content_h = y + scroll_y[TAB_LINK] - top + pad;
}

static int stat_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void link_click(int kind, int arg, int button) {
    if (!game_alive() || !hos_oot_stats[5]) {
        say_toast("Start playing first: changes are made in the running world.");
        return;
    }
    const int rupee = hos_oot_stats[0], life = hos_oot_stats[1], maxlife = hos_oot_stats[2], mpmax = hos_oot_stats[4];
    (void)button;
    if (kind == H_HEART) {   /* a heart: health up to it, a container too when it is past the last one */
        if ((arg + 1) * 4 > maxlife) hos_stat_set(2, (arg + 1) * 4);
        hos_stat_set(1, life == (arg + 1) * 4 ? arg * 4 + 2 : (arg + 1) * 4);
        return;
    }
    if (kind == H_MPBAR) {
        hos_stat_set(3, mpmax * stat_clamp(arg, 0, 100) / 100);
        return;
    }
    switch (arg) {
    case 1: hos_stat_set(1, maxlife); break;
    case 2: hos_stat_set(2, stat_clamp(maxlife + 4, 4, 80)); break;
    case 3: hos_stat_set(2, stat_clamp(maxlife - 4, 4, 80)); if (life > maxlife - 4) hos_stat_set(1, stat_clamp(maxlife - 4, 4, 80)); break;
    case 11: hos_stat_set(0, stat_clamp(rupee - 10, 0, 9999)); break;
    case 12: hos_stat_set(0, stat_clamp(rupee - 1, 0, 9999)); break;
    case 13: hos_stat_set(0, stat_clamp(rupee + 1, 0, 9999)); break;
    case 14: hos_stat_set(0, stat_clamp(rupee - 100, 0, 9999)); break;
    case 15: hos_stat_set(0, stat_clamp(rupee + 10, 0, 9999)); break;
    case 16: hos_stat_set(0, stat_clamp(rupee + 100, 0, 9999)); break;
    case 20: hos_stat_set(0, 0); break;
    case 21: hos_stat_set(0, 99); break;
    case 22: hos_stat_set(0, 200); break;
    case 23: hos_stat_set(0, 500); break;
    case 24: hos_stat_set(0, 999); break;
    case 30: hos_stat_set(3, mpmax); break;
    case 31: hos_stat_set(3, 0); break;
    case 32: hos_stat_set(4, 240), hos_stat_set(3, 240); break;   /* the single bar (a save with both blessings holds 480) */
    case 33: hos_stat_set(4, 480), hos_stat_set(3, 480); break;
    case 34: hos_stat_set(4, 0), hos_stat_set(3, 0); break;
    default: break;
    }
}

static void flag_refresh(void) {
    if (!flag_dirty) return;
    flag_dirty = 0;
    flag_matches = hos_gd_search(flag_search, flag_kind);
    scroll_y[TAB_FLAGS] = 0;
}
static void paint_flags(HDC dc, int W, int top, int bottom) {
    const int pad = px(24);
    flag_refresh();
    /* the search box and the kind chips */
    const int sy = top + px(16), sh = px(36);
    fill_rrect((float)pad, (float)sy, (float)(W - 2 * pad - px(210)), (float)sh, (float)px(10), RGBX(30, 38, 34));
    stroke_rrect((float)pad, (float)sy, (float)(W - 2 * pad - px(210)), (float)sh, (float)px(10), hovered(H_SEARCH, 0) ? RGBX(96, 176, 112) : RGBX(52, 64, 58), 1.5f);
    {
        char shown[96];
        snprintf(shown, sizeof shown, "%s%s", flag_search, (GetTickCount() / 500) & 1 ? "_" : " ");
        if (!flag_search[0]) say(dc, f_body, "Type to search the game's flags...", pad + px(14), sy + px(9), C_DIM, 0);
        else say(dc, f_body, shown, pad + px(14), sy + px(9), C_TEXT, 0);
    }
    for (int i = 0; i < 2; i++) {
        const int kx = W - pad - px(200) + i * px(104), on = (flag_kind == 0) == (i == 1);
        fill_rrect((float)kx, (float)sy, (float)px(96), (float)sh, (float)px(18), on ? RGBX(70, 130, 84) : RGBX(30, 38, 34));
        say(dc, f_body, i ? "Flags" : "All", kx + px(48), sy + px(9), on ? RGB(255, 255, 255) : C_DIM, DT_CENTER);
        add_hit(kx, sy, px(96), sh, H_KIND, i, top, bottom);
    }
    add_hit(pad, sy, W - 2 * pad - px(210), sh, H_SEARCH, 0, top, bottom);
    /* the chips, three a row, scrolled by rows */
    const int cols = (W - 2 * pad) / px(300) > 0 ? (W - 2 * pad) / px(300) : 1, cgap = px(10), cw = (W - 2 * pad - (cols - 1) * cgap) / cols, ch = px(38);
    const int list_top = sy + sh + px(34), rows = (bottom - list_top) / (ch + cgap);
    int first_row = scroll_y[TAB_FLAGS] / (ch + cgap);
    const int total_rows = (flag_matches + cols - 1) / cols;
    if (first_row > total_rows - rows) first_row = total_rows - rows;
    if (first_row < 0) first_row = 0;
    scroll_y[TAB_FLAGS] = first_row * (ch + cgap);
    hos_gd_view(first_row * cols, (rows + 1) * cols);
    for (int r = 0; r <= rows; r++) {
        for (int c = 0; c < cols; c++) {
            const int m = (first_row + r) * cols + c;
            if (m >= flag_matches) break;
            char name[112], value[16];
            const int row = hos_gd_row(m, name, sizeof name, value, sizeof value);
            if (!row) break;
            const int x = pad + c * (cw + cgap), y = list_top + r * (ch + cgap);
            if (y + ch > bottom) break;
            const int on = !strcmp(value, "On"), off = !strcmp(value, "Off"), over = hovered(H_FLAG, m);
            fill_rrect((float)x, (float)y, (float)cw, (float)ch, (float)px(10), on ? RGBX(34, 62, 44) : over ? RGBX(40, 52, 46) : RGBX(30, 38, 34));
            stroke_rrect((float)x, (float)y, (float)cw, (float)ch, (float)px(10), on ? RGBX(96, 176, 112) : over ? RGBX(110, 130, 118) : RGBX(46, 58, 52), on ? 1.8f : 1.0f);
            /* a switch on the left: a track and a knob */
            fill_rrect((float)(x + px(10)), (float)(y + px(11)), (float)px(32), (float)px(16), (float)px(8), on ? RGBX(96, 176, 112) : RGBX(64, 72, 68));
            fill_ellipse((float)(x + px(on ? 27 : 12)), (float)(y + px(13)), (float)px(12), (float)px(12), row == 1 ? RGBX(90, 96, 94) : RGBX(236, 240, 236));
            say_w(dc, f_small, name, x + px(52), y + px(10), on ? C_TEXT : off ? RGB(190, 200, 194) : C_DIM, DT_END_ELLIPSIS, cw - px(62));
            add_hit(x, y, cw, ch, H_FLAG, m, top, bottom);
        }
    }
    {
        char count[48];
        snprintf(count, sizeof count, "%d flags", flag_matches);
        say(dc, f_small, count, W - pad, sy + sh + px(10), C_DIM, DT_RIGHT);
    }
    content_h = total_rows * (ch + cgap);
    view_h = rows * (ch + cgap);
}

/* ---- the window ---- */
#define HEADER_H 64
#define FOOTER_H 40
static void paint_all(HDC dc, int W, int H) {
    make_fonts();
    GdipSetSmoothingMode(G, 4);
    RECT all = {0, 0, W, H};
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    hit_count = 0;
    const int head = px(HEADER_H), foot = px(FOOTER_H), top = head, bottom = H - foot;
    /* the header: title, the tabs, whether the game is in the world */
    fill_rrect(0, 0, (float)W, (float)head, 0, RGBX(22, 27, 24));
    line(0, (float)head, (float)W, (float)head, RGBX(52, 64, 58), 1.0f);
    say(dc, f_title, "Sheikah Native", px(24), px(17), C_TEXT, 0);
    {
        int x = px(250);
        for (int i = 0; i < TABS; i++) {
            const int w = px(84), h = px(36), y = px(14), on = tab == i, over = hovered(H_TAB, i);
            fill_rrect((float)x, (float)y, (float)w, (float)h, (float)px(18), on ? RGBX(70, 130, 84) : over ? RGBX(44, 56, 50) : RGBX(30, 38, 34));
            say(dc, f_bold, tab_names[i], x + w / 2, y + px(8), on ? RGB(255, 255, 255) : RGB(196, 204, 198), DT_CENTER);
            add_hit(x, y, w, h, H_TAB, i, 0, H);
            x += w + px(8);
        }
        const int alive = game_alive();
        if (W >= px(1000)) {   /* room for the words; in a narrow window the dot alone, so it cannot run into the tabs */
            fill_ellipse((float)(W - px(180)), (float)px(26), (float)px(12), (float)px(12), alive ? RGBX(96, 176, 112) : RGBX(222, 168, 70));
            say(dc, f_body, alive ? "In the world" : "Waiting for gameplay", W - px(158), px(21), alive ? C_GREEN : C_AMBER, 0);
        } else {
            fill_ellipse((float)(W - px(30)), (float)px(26), (float)px(12), (float)px(12), alive ? RGBX(96, 176, 112) : RGBX(222, 168, 70));
        }
    }
    /* the content, clipped between the header and the footer */
    GdipSetClipRectI(G, 0, top, W, bottom - top, 0);
    IntersectClipRect(dc, 0, top, W, bottom);
    content_h = 0;
    view_h = bottom - top;
    switch (tab) {
    case TAB_ITEMS: paint_items(dc, W, top, bottom); break;
    case TAB_LINK: paint_link(dc, W, top, bottom); break;
    case TAB_MAP: paint_map(dc, W, top, bottom); break;
    case TAB_WORLD: paint_world(dc, W, top, bottom); break;
    default: paint_flags(dc, W, top, bottom); break;
    }
    GdipResetClip(G);
    SelectClipRgn(dc, NULL);
    /* the footer: what the mouse does here, or the last answer */
    fill_rrect(0, (float)bottom, (float)W, (float)foot, 0, RGBX(22, 27, 24));
    line(0, (float)bottom, (float)W, (float)bottom, RGBX(52, 64, 58), 1.0f);
    {
        const char *hint = tab == TAB_ITEMS ? "Click an item to give it or take it away. Counts: left click +1, right click -1, Shift for 10. Wheel scrolls."
                         : tab == TAB_LINK ? "Click a heart to set health to it (past the last one adds a container). Click the magic bar to set it."
                         : tab == TAB_MAP ? "Click a place on the map to travel there through the game's own map change (a skull is a dungeon)."
                         : tab == TAB_WORLD ? "Click an hour to set the clock. Switch age reloads the map as the other age."
                         : "Type to search. Click a flag to flip it. Changes are made in the running game; nothing is written to your save.";
        if (toast[0] && GetTickCount() < toast_until) say(dc, f_body, toast, px(24), bottom + px(10), C_AMBER, 0);
        else say(dc, f_small, hint, px(24), bottom + px(12), C_DIM, 0);
    }
}

static void clamp_scroll(void) {
    int max = content_h - view_h;
    if (max < 0) max = 0;
    if (scroll_y[tab] > max) scroll_y[tab] = max;
    if (scroll_y[tab] < 0) scroll_y[tab] = 0;
}

static void do_click(int kind, int arg, int button, int shift) {
    if (!kind) return;
    switch (kind) {
    case H_TAB: tab = arg; break;
    case H_ITEM:
        if (item_kinds[arg]) {
            const int step = shift ? 10 : 1;
            post_action(OOT_CHEAT_ITEM_ACTION + arg * 256 + 64 + (button == 1 ? step : -step));
        } else {
            post_action(OOT_CHEAT_ITEM_ACTION + arg * 256 + 200);
        }
        break;
    case H_PLACE: {
        /* A dungeon is made for one age: loaded as the other, the game builds an incomplete level (missing surfaces). The editor
           travels as Link's current age, so it says so first */
        const int adult_only = arg == 22 || arg == 23 || arg == 24 || arg == 25 || arg == 26 || arg == 28 || arg == 29 || arg == 30;
        const int child_only = arg == 19 || arg == 20 || arg == 21;
        if (game_alive() && hos_oot_stats[5] && ((adult_only && hos_oot_stats[6]) || (child_only && !hos_oot_stats[6]))) {
            char t[140];
            snprintf(t, sizeof t, "%s is %s's: switch age on the World tab first.", places[arg].name, adult_only ? "an adult" : "a child");
            say_toast(t);
            break;
        }
        if (post_action(1000 + arg)) {
            char t[96];
            snprintf(t, sizeof t, "Traveling to %s...", places[arg].name);
            say_toast(t);
        }
        break;
    }
    case H_SEC: item_sec = arg; scroll_y[TAB_ITEMS] = 0; break;
    case H_NONE: break;
    case H_HEART: link_click(kind, arg, button); break;
    case H_STAT: link_click(kind, arg, button); break;
    case H_MPBAR:
        for (int i = 0; i < hit_count; i++)
            if (hits[i].kind == H_MPBAR) {
                const int w = hits[i].r.right - hits[i].r.left;
                link_click(kind, w > 0 ? (mouse_x - hits[i].r.left) * 100 / w : 0, button);
            }
        break;
    case H_AGE: post_action(999); break;
    case H_HOUR: post_action(100 + arg); break;
    case H_KIND: flag_kind = arg ? 0 : -1, flag_dirty = 1; break;
    case H_FLAG:
        if (game_alive()) hos_gd_edit(arg, button == 1 ? 0 : 1);
        else say_toast("Start playing first: changes are made in the running world.");
        break;
    default: break;
    }
}

#ifndef EDITOR_TEST
void hos_log(const char *fmt, ...);   /* (host/hos.h) */
static HWND g_hwnd;
static HANDLE g_ready;
static volatile LONG g_started;
static ULONG_PTR g_gdip;

static void hide_window(HWND w) {
    ShowWindow(w, SW_HIDE);
    KillTimer(w, 1);
    HWND game = FindWindowA("Sheikah Native", NULL);
    if (game) SetForegroundWindow(game);
}
static LRESULT CALLBACK ed_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_APP + 1: {   /* 1 show, 2 toggle */
        hos_log("editor: window %s", IsWindowVisible(w) && wp == 2 ? "hidden" : "shown");
        if (IsWindowVisible(w) && wp == 2) {
            hide_window(w);
        } else {
            ShowWindow(w, IsIconic(w) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(w);
            SetTimer(w, 1, 100, NULL);
            flag_dirty = 1;
        }
        return 0;
    }
    case WM_TIMER: InvalidateRect(w, NULL, FALSE); return 0;
    case WM_CLOSE: hide_window(w); return 0;
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lp;
        SetWindowPos(w, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(w, &ps);
        RECT rc;
        GetClientRect(w, &rc);
        typedef UINT(WINAPI * GetDpi)(HWND);
        static GetDpi get_dpi;
        if (!get_dpi) get_dpi = (GetDpi)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
        S = get_dpi ? (float)get_dpi(w) / 96.0f : 1.0f;
        const int W = rc.right, H = rc.bottom;
        HDC mem = CreateCompatibleDC(ps.hdc);
        HBITMAP bmp = CreateCompatibleBitmap(ps.hdc, W, H);
        HGDIOBJ old = SelectObject(mem, bmp);
        GdipCreateFromHDC(mem, &G);
        paint_all(mem, W, H);
        clamp_scroll();
        GdipDeleteGraphics(G);
        G = NULL;
        BitBlt(ps.hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        mouse_x = x;
        int kind = 0, arg = 0;
        for (int i = hit_count - 1; i >= 0; i--)
            if (x >= hits[i].r.left && x < hits[i].r.right && y >= hits[i].r.top && y < hits[i].r.bottom) { kind = hits[i].kind, arg = hits[i].arg; break; }
        if (kind != hover_kind || arg != hover_arg) hover_kind = kind, hover_arg = arg, InvalidateRect(w, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        mouse_x = (short)LOWORD(lp);
        do_click(hover_kind, hover_arg, m == WM_LBUTTONDOWN ? 1 : 2, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        InvalidateRect(w, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        scroll_y[tab] -= (short)HIWORD(wp) * px(60) / WHEEL_DELTA;
        clamp_scroll();
        InvalidateRect(w, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_F2) { hide_window(w); return 0; }
        if (wp == VK_F1) { hide_window(w); return 0; }
        if (wp == VK_BACK && tab == TAB_FLAGS && flag_search[0]) {
            flag_search[strlen(flag_search) - 1] = 0;
            flag_dirty = 1;
        } else if (wp == VK_PRIOR || wp == VK_NEXT) {
            scroll_y[tab] += (wp == VK_NEXT ? 1 : -1) * view_h * 9 / 10;
            clamp_scroll();
        } else if (wp == VK_TAB) {
            tab = (tab + (GetKeyState(VK_SHIFT) & 0x8000 ? TABS - 1 : 1)) % TABS;
        }
        InvalidateRect(w, NULL, FALSE);
        return 0;
    case WM_CHAR:
        if (tab == TAB_FLAGS && wp >= 32 && wp < 127 && strlen(flag_search) + 1 < sizeof flag_search) {
            flag_search[strlen(flag_search)] = (char)wp;
            flag_dirty = 1;
            InvalidateRect(w, NULL, FALSE);
        }
        return 0;
    default: break;
    }
    return DefWindowProcA(w, m, wp, lp);
}

static DWORD WINAPI ed_thread(LPVOID unused) {
    (void)unused;
    typedef BOOL(WINAPI * SetCtx)(void *);
    SetCtx set_ctx = (SetCtx)GetProcAddress(GetModuleHandleA("user32.dll"), "SetThreadDpiAwarenessContext");
    if (set_ctx) set_ctx((void *)-4);   /* per-monitor v2: the window draws at the screen's own pixels */
    GpStartup in = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&g_gdip, &in, NULL);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = ed_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "SheikahEditor";
    wc.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1));
    RegisterClassA(&wc);
    g_hwnd = CreateWindowExA(0, "SheikahEditor", "Sheikah Native - Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1120, 780, NULL, NULL, wc.hInstance, NULL);
    hos_log("editor: window %s (error %lu), gdi+ start %d", g_hwnd ? "created" : "NOT created", g_hwnd ? 0UL : GetLastError(), (int)(g_gdip != 0));
    SetEvent(g_ready);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

static void ed_send(WPARAM how) {
    hos_log("editor: %s requested", how == 2 ? "toggle" : "open");
    if (getenv("HOS_HEADLESS") && !getenv("HOS_EDITOR_FORCE")) return;   /* (test runs without a window) */
    if (InterlockedCompareExchange(&g_started, 1, 0) == 0) {
        g_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
        CloseHandle(CreateThread(NULL, 0, ed_thread, NULL, 0, NULL));
    }
    WaitForSingleObject(g_ready, 5000);
    if (g_hwnd) PostMessageA(g_hwnd, WM_APP + 1, how, 0);
}
void hos_editor_open(void) { ed_send(1); }
void hos_editor_toggle(void) { ed_send(2); }

#else /* EDITOR_TEST: the paint on a bitmap, written as a .bmp (tests/editor_render.c) */
int ed_test_render(const char *path, int which_tab, int W, int H, float scale) {
    ULONG_PTR token;
    GpStartup in = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&token, &in, NULL);
    S = scale;
    tab = which_tab;
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mem, bmp);
    GdipCreateFromHDC(mem, &G);
    paint_all(mem, W, H);
    GdipDeleteGraphics(G);
    GdiFlush();
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    const uint32_t bytes = (uint32_t)W * (uint32_t)H * 4, off = 54, total = off + bytes, info = 40;
    const int32_t td = -H;
    const uint16_t planes = 1, bpp = 32;
    unsigned char head[54] = {'B', 'M'};
    memcpy(head + 2, &total, 4);
    memcpy(head + 10, &off, 4);
    memcpy(head + 14, &info, 4);
    memcpy(head + 18, &W, 4);
    memcpy(head + 22, &td, 4);
    memcpy(head + 26, &planes, 2);
    memcpy(head + 28, &bpp, 2);
    memcpy(head + 34, &bytes, 4);
    fwrite(head, 1, 54, f);
    fwrite(bits, 1, bytes, f);
    fclose(f);
    return 0;
}
#endif

#else /* not Windows: no editor window (the menu's Editor row does nothing) */
void hos_editor_open(void) {}
void hos_editor_toggle(void) {}
#endif
