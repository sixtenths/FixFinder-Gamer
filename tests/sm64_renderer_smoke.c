#include "../firmware/sm64/renderer.h"
#include "gfx_cc.h"

static uint16_t center(void)
{
    return sm64_color[(SM64_RENDER_HEIGHT / 2) * SM64_RENDER_WIDTH + SM64_RENDER_WIDTH / 2];
}

static void solid(float z, float r, float g, float b)
{
    float verts[] = {
        -.8f, -.8f, z, 1, r, g, b,
         .8f, -.8f, z, 1, r, g, b,
         .0f,  .8f, z, 1, r, g, b,
    };
    gfx_fixfinder_api.draw_triangles(verts, sizeof verts / sizeof *verts, 1);
}

unsigned int sm64_backend_test(void)
{
    struct GfxRenderingAPI *gfx = &gfx_fixfinder_api;
    gfx->init();
    gfx->set_depth_test(true);
    gfx->set_depth_mask(true);
    gfx->create_and_load_new_shader(SHADER_INPUT_1 << 9);
    solid(.4f, 1, 0, 0);
    if (center() != 0xf800) return 1;
    solid(.7f, 0, 1, 0);
    if (center() != 0xf800) return 2;
    solid(.2f, 0, 1, 0);
    if (center() != 0x07e0) return 3;

    gfx->start_frame();
    gfx->set_scissor(0, 0, 100, 240);
    solid(.4f, 0, 0, 1);
    if (center() != 0 || sm64_color[60 * SM64_RENDER_WIDTH + 33] != 0x001f) return 4;

    gfx->start_frame();
    gfx->set_scissor(0, 0, 320, 240);
    gfx->create_and_load_new_shader(SHADER_TEXEL0 << 9);
    uint8_t blue[] = {0, 0, 255, 255};
    gfx->select_texture(0, gfx->new_texture());
    gfx->upload_texture(blue, 1, 1);
    float verts[] = {
        -.8f, -.8f, .4f, 1, 0, 0,
         .8f, -.8f, .4f, 1, 1, 0,
         .0f,  .8f, .4f, 1, .5f, 1,
    };
    gfx->draw_triangles(verts, sizeof verts / sizeof *verts, 1);
    if (center() != 0x001f) return 5;
    return sm64_render_limit_hits ? 6 : 0;
}
