#define _LANGUAGE_C
#include <PR/mbi.h>
#include <PR/gbi.h>
#include "gfx_pc.h"
#include "gfx_window_manager_api.h"
#include "../firmware/sm64/renderer.h"

static void init(const char *name, bool fullscreen) { (void)name; (void)fullscreen; }
static void dimensions(uint32_t *w, uint32_t *h) { *w = 320; *h = 240; }
static bool ready(void) { return true; }
static double now(void) { return 0; }
static void idle(void) {}

unsigned int sm64_display_list_test(void)
{
    struct GfxWindowManagerAPI window = {
        .init = init, .get_dimensions = dimensions, .handle_events = idle,
        .start_frame = ready, .swap_buffers_begin = idle, .swap_buffers_end = idle, .get_time = now,
    };
    static uint16_t color_target;
    Gfx list[] = {
        gsDPSetColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, &color_target),
        gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, 320, 240),
        gsDPSetCycleType(G_CYC_FILL),
        gsDPSetFillColor(0xf801f801),
        gsDPFillRectangle(40, 30, 280, 210),
        gsSPEndDisplayList(),
    };
    gfx_init(&window, &gfx_fixfinder_api, "FixFinder", false);
    gfx_start_frame();
    gfx_run(list);
    gfx_end_frame();
    return sm64_color[48 * SM64_RENDER_WIDTH + 64] != 0xf800 || sm64_color[0] != 0;
}
