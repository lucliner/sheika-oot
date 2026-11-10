#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "vk_menu_hit.h"

using HDC = void*;
using VkCommandBuffer = int;
using VkImage = int;
constexpr int VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 = 1, VK_STRUCTURE_TYPE_DEPENDENCY_INFO = 2;
constexpr int VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT = 1, VK_ACCESS_2_TRANSFER_WRITE_BIT = 2;
struct VkMemoryBarrier2 { int sType, srcStageMask = 0, dstStageMask = 0, srcAccessMask = 0, dstAccessMask = 0; };
struct VkDependencyInfo { int sType; int memoryBarrierCount = 0; VkMemoryBarrier2* pMemoryBarriers = nullptr; };
void vkCmdPipelineBarrier2(VkCommandBuffer, const VkDependencyInfo*) {}
struct Bitmap { uint32_t w = 0, h = 0; bool keyed = false; } menu_bmp, fps_bmp, startup_bmp;
struct Upload {} ups[3][3];
std::mutex bmp_mu;
std::atomic<bool> startup_on{false}, open_now{false};
std::atomic<int64_t> shown_frames{100}, game_frames{100};
struct { bool show_fps = true; int framegen = 0; } opts;
struct { struct { uint32_t width = 0, height = 0; } swap_extent; } dev;
namespace menu_hit = mkwvk::menu_hit;
constexpr int kMenuWidth = menu_hit::kWidth, kTeleportLines = 20, kCheatRows = 11, kControls = 20, kTop = 2;
int page = 0, cur_scale = 1, menu_fit = 1 << 20, help_px = 0, pending_px = 0;
uint32_t menu_fit_w = 0, menu_fit_h = 0;
int max_tab_rows() { return 21; }
int measure_help(int s) { return 64 * s; } // authored font measurements; the production sizing formula is extracted
int measure_pending(int s) { return 16 * s; }
void paint_menu(HDC, uint32_t, uint32_t, int) {}
void paint_fps(HDC, uint32_t, uint32_t, int) {}
int fps_font = 0;
void gdi_draw(Bitmap& b, uint32_t w, uint32_t h, void (*paint)(HDC, uint32_t, uint32_t, int), int s) {
    b.w = w; b.h = h;
    if (paint == paint_fps) fps_font = s;
}
int64_t prof_now() { return 1000000000; }
bool fps_take(int64_t, int64_t, int64_t, int64_t*, int64_t*, int64_t*, double* shown, double* game) {
    *shown = *game = 60; return true; // force a bitmap refresh; rate sampling is covered by test_fps_rate.py
}
char fps_text[32] = {}, fps_sub[32] = {};
struct Copy { Bitmap* bitmap; int x, y; };
std::vector<Copy> copies;
void copy_over(VkCommandBuffer, VkImage, Bitmap& b, Upload&, int x, int y, uint32_t, uint32_t) { copies.push_back({&b, x, y}); }
#include "actual_menu.inc"

void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void viewport(uint32_t w, uint32_t h) { dev.swap_extent = {w, h}; }
void fit_menu(int expected) {
    cur_scale = next_scale();
    redraw_menu(cur_scale);
    require(cur_scale == expected && next_scale() == expected, "menu no longer retains its fitted integer step");
    require(menu_bmp.w == uint32_t(480 * expected) && menu_bmp.h == uint32_t(806 * expected), "menu sizing changed");
}
void check_font(int expected) {
    draw_fps(next_scale());
    require(fps_font == expected && fps_bmp.w == uint32_t(120 * expected) && fps_bmp.h == uint32_t(54 * expected),
            "FPS font/bitmap shrank to the fitted menu scale");
    require(fps_bmp.keyed, "FPS lost its keyed bitmap");
}
void check_margins(int expected) {
    copies.clear();
    menu_record(1, 2, 0, dev.swap_extent.width, dev.swap_extent.height);
    int count = 0;
    for (const auto& c : copies) if (c.bitmap == &fps_bmp) {
        ++count;
        require(c.x == int(dev.swap_extent.width) - int(fps_bmp.w) - 12 * expected && c.y == 10 * expected,
                "FPS margins followed the fitted menu scale");
    }
    require(count == 1, "FPS was not copied exactly once");
}
void sequence(uint32_t w, uint32_t h, int menu_scale, int fps_scale, bool closed) {
    viewport(w, h);
    check_font(fps_scale); // before the menu opens
    open_now = true;
    fit_menu(menu_scale);
    check_font(fps_scale); check_margins(fps_scale);
    if (closed) {
        open_now = false;
        check_font(fps_scale); check_margins(fps_scale);
        require(next_scale() == menu_scale, "closing menu reset its fit cache");
    }
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "choose one case");
        const std::string c = argv[1];
        if (c == "font2160" || c == "closed2160") sequence(5120, 2160, 2, 3, c == "closed2160");
        else if (c == "margins2160") {
            viewport(5120, 2160); check_font(3); open_now = true; fit_menu(2); check_margins(3);
        } else if (c == "font1440") sequence(2560, 1440, 1, 2, true);
        else if (c == "control900") sequence(1600, 900, 1, 1, true);
        else if (c == "control720") sequence(1280, 720, 1, 1, true);
        else if (c == "resize") {
            sequence(5120, 2160, 2, 3, true);
            sequence(1600, 900, 1, 1, true);
            sequence(5120, 2160, 2, 3, true);
        } else throw std::runtime_error("unknown case");
        std::printf("PASS: %s\n", c.c_str()); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
