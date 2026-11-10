#ifndef FIXFINDER_SM64_RENDERER_H
#define FIXFINDER_SM64_RENDERER_H

#include <stdint.h>
#include "gfx_rendering_api.h"

enum { SM64_RENDER_WIDTH = 128, SM64_RENDER_HEIGHT = 96 };
extern struct GfxRenderingAPI gfx_fixfinder_api;
extern uint16_t sm64_color[SM64_RENDER_WIDTH * SM64_RENDER_HEIGHT];
extern unsigned int sm64_render_limit_hits;

#endif
