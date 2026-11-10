/* Bounded software backend for the sm64-port Fast3D rendering interface. */
#include "renderer.h"
#include "gfx_cc.h"
#include <string.h>

enum { W = SM64_RENDER_WIDTH, H = SM64_RENDER_HEIGHT,
       SHADERS = 64, TEXTURES = 8, TEXELS = 1024, ATTRS = 22 };

struct ShaderProgram {
    uint32_t id;
    struct CCFeatures cc;
};
struct texture {
    uint16_t texels[TEXELS];
    int width, height;
    uint32_t s_mode, t_mode;
};
struct rectangle { int x0, y0, x1, y1; };
struct vertex {
    float x, y, z, inv_w, attrs[ATTRS];
};

uint16_t sm64_color[W * H];
unsigned int sm64_render_limit_hits;
static uint16_t depth[W * H];
static struct ShaderProgram shaders[SHADERS];
static struct texture textures[TEXTURES];
static unsigned int shader_count, texture_count, tiles[2];
static struct ShaderProgram *active;
static struct rectangle viewport, scissor;
static bool depth_test, depth_write, depth_decal, blend;

static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

static struct rectangle scaled_rect(int x, int y, int width, int height)
{
    return (struct rectangle){x*W/320, H-(y+height)*H/240,
                              (x+width)*W/320, H-y*H/240};
}

static void set_viewport(int x, int y, int w, int h) { viewport = scaled_rect(x,y,w,h); }
static void set_scissor(int x, int y, int w, int h) { scissor = scaled_rect(x,y,w,h); }
static void set_depth_test(bool value) { depth_test = value; }
static void set_depth_mask(bool value) { depth_write = value; }
static void set_zmode_decal(bool value) { depth_decal = value; }
static void set_use_alpha(bool value) { blend = value; }
static bool z_zero_to_one(void) { return true; }
static void no_action(void) {}

static struct ShaderProgram *lookup(uint32_t id)
{
    for (unsigned int i = 0; i < shader_count; ++i)
        if (shaders[i].id == id) return &shaders[i];
    return NULL;
}

static struct ShaderProgram *create(uint32_t id)
{
    active = lookup(id);
    if (active) return active;
    if (shader_count == SHADERS) {
        ++sm64_render_limit_hits;
        return NULL;
    }
    active = &shaders[shader_count++];
    active->id = id;
    gfx_cc_get_features(id, &active->cc);
    return active;
}

static void load(struct ShaderProgram *shader) { active = shader; }
static void unload(struct ShaderProgram *shader) { if (shader == active) active = NULL; }

static void shader_info(struct ShaderProgram *shader, uint8_t *inputs, bool used[2])
{
    *inputs = shader ? shader->cc.num_inputs : 0;
    used[0] = shader && shader->cc.used_textures[0];
    used[1] = shader && shader->cc.used_textures[1];
}

static uint32_t new_texture(void)
{
    if (texture_count < TEXTURES) return texture_count++;
    ++sm64_render_limit_hits;
    return TEXTURES;
}

static unsigned int upload_tile;

static void select_texture(int tile, uint32_t id)
{
    if (tile < 0 || tile > 1) return;
    upload_tile = tile;
    tiles[tile] = id < TEXTURES ? id : TEXTURES;
}

static void upload_texture(const uint8_t *rgba, int width, int height)
{
    unsigned int id = tiles[upload_tile];
    if (id == TEXTURES) return;
    struct texture *t = &textures[id];
    if (width <= 0 || height <= 0 || width > TEXELS / height) {
        t->width = 0;
        ++sm64_render_limit_hits;
        return;
    }
    t->width = width;
    t->height = height;
    for (int i = 0; i < width * height; ++i) {
        const uint8_t *p = rgba + 4*i;
        t->texels[i] = (p[0] >> 4) << 12 | (p[1] >> 4) << 8 |
                       (p[2] >> 4) << 4 | (p[3] >> 4);
    }
}

static void set_sampler(int tile, bool linear, uint32_t s, uint32_t t)
{
    (void)linear;
    if (tile < 0 || tile > 1 || tiles[tile] == TEXTURES) return;
    textures[tiles[tile]].s_mode = s;
    textures[tiles[tile]].t_mode = t;
}

static int tex_coord(float coordinate, int size, uint32_t mode)
{
    float value = coordinate * size;
    int pixel = (int)value;
    if (pixel > value) --pixel;
    if (mode & 2U) return min(size-1, max(0, pixel));
    int period = (mode & 1U) ? 2 * size : size;
    pixel %= period;
    if (pixel < 0) pixel += period;
    return pixel < size ? pixel : period - 1 - pixel;
}

static void sample(unsigned int tile, float u, float v, float rgb[4])
{
    unsigned int id = tiles[tile];
    if (id == TEXTURES || !textures[id].width) return;
    const struct texture *t = &textures[id];
    int x = tex_coord(u, t->width, t->s_mode);
    int y = tex_coord(v, t->height, t->t_mode);
    uint16_t pixel = t->texels[y*t->width+x];
    for (int i = 0; i < 4; ++i)
        rgb[i] = ((pixel >> (12-4*i)) & 15) * (1.0f/15.0f);
}

static float edge(const struct vertex *a, const struct vertex *b, float x, float y)
{
    return (x-a->x)*(b->y-a->y) - (y-a->y)*(b->x-a->x);
}

static float combine(const uint8_t terms[4], const float values[8][4], unsigned int channel)
{
    return clamp01((values[terms[0]][channel] - values[terms[1]][channel]) *
                    values[terms[2]][channel] + values[terms[3]][channel]);
}

static uint16_t shade(const struct CCFeatures *cc, const float a[ATTRS], uint16_t old, bool *visible)
{
    float values[8][4] = {0};
    unsigned int at = 0;
    if (cc->used_textures[0] || cc->used_textures[1]) {
        if (cc->used_textures[0]) sample(0, a[0], a[1], values[SHADER_TEXEL0]);
        if (cc->used_textures[1]) sample(1, a[0], a[1], values[SHADER_TEXEL1]);
        for (unsigned int i = 0; i < 4; ++i)
            values[SHADER_TEXEL0A][i] = values[SHADER_TEXEL0][3];
        at += 2;
    }
    const float *fog = cc->opt_fog ? a + at : NULL;
    if (fog) at += 4;
    for (int i = 1; i <= cc->num_inputs; ++i) {
        for (int c = 0; c < 3; ++c) values[i][c] = a[at++];
        values[i][3] = cc->opt_alpha ? a[at++] : 1.0f;
    }

    float alpha = cc->opt_alpha ? combine(cc->c[1], values, 3) : 1.0f;
    *visible = !cc->opt_texture_edge || alpha >= .5f;
    if (!*visible) return old;
    float rgb[3];
    for (int c = 0; c < 3; ++c) {
        rgb[c] = combine(cc->c[0], values, c);
        if (fog) rgb[c] += (fog[c] - rgb[c]) * fog[3];
    }
    if (blend && alpha < 1.0f) {
        const float back[] = {(old >> 11) / 31.0f, ((old >> 5) & 63) / 63.0f, (old & 31) / 31.0f};
        for (int c = 0; c < 3; ++c) rgb[c] = back[c] + (rgb[c] - back[c]) * alpha;
    }
    return (uint16_t)(clamp01(rgb[0]) * 31 + .5f) << 11 |
           (uint16_t)(clamp01(rgb[1]) * 63 + .5f) << 5 |
           (uint16_t)(clamp01(rgb[2]) * 31 + .5f);
}

static void raster_triangle(const struct CCFeatures *cc, const struct vertex v[3], unsigned int attr_count)
{
    float area = edge(v, v+1, v[2].x, v[2].y);
    if (area == 0.0f) return;
    int x0 = max(max(0, scissor.x0), min((int)v[0].x, min((int)v[1].x,(int)v[2].x)));
    int y0 = max(max(0, scissor.y0), min((int)v[0].y, min((int)v[1].y,(int)v[2].y)));
    int x1 = min(min(W, scissor.x1), 1 + max((int)v[0].x, max((int)v[1].x,(int)v[2].x)));
    int y1 = min(min(H, scissor.y1), 1 + max((int)v[0].y, max((int)v[1].y,(int)v[2].y)));
    float inv_area = 1.0f/area;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float a = edge(v+1, v+2, x+.5f, y+.5f) * inv_area;
            float b = edge(v+2, v, x+.5f, y+.5f) * inv_area;
            float c = 1.0f-a-b;
            if (a < 0 || b < 0 || c < 0) continue;
            unsigned int pos = y*W+x;
            unsigned int z = clamp01(a*v[0].z + b*v[1].z + c*v[2].z) * 65534;
            if (depth_decal && z) --z;
            if (depth_test && z > depth[pos]) continue;
            float p = a*v[0].inv_w, q = b*v[1].inv_w, r = c*v[2].inv_w;
            float reciprocal = 1.0f/(p+q+r);
            float attrs[ATTRS];
            for (unsigned int i = 0; i < attr_count; ++i)
                attrs[i] = (p*v[0].attrs[i] + q*v[1].attrs[i] + r*v[2].attrs[i])*reciprocal;
            bool visible;
            sm64_color[pos] = shade(cc, attrs, sm64_color[pos], &visible);
            if (visible && depth_write) depth[pos] = z;
        }
    }
}

static void draw_triangles(float buffer[], size_t length, size_t triangles)
{
    if (!active) return;
    const struct CCFeatures *cc = &active->cc;
    unsigned int attr_count = (cc->used_textures[0] || cc->used_textures[1] ? 2 : 0) +
                              (cc->opt_fog ? 4 : 0) + cc->num_inputs*(cc->opt_alpha ? 4 : 3);
    unsigned int stride = 4 + attr_count;
    if (attr_count > ATTRS || triangles > length/(3*stride)) return;
    for (size_t triangle = 0; triangle < triangles; ++triangle) {
        struct vertex v[3];
        bool valid = true;
        for (unsigned int i = 0; i < 3; ++i, buffer += stride) {
            if (buffer[3] <= 0.0f) {
                valid = false;
                continue;
            }
            float inv = 1.0f / buffer[3];
            v[i].x = viewport.x0 + (buffer[0]*inv + 1.0f) * .5f * (viewport.x1-viewport.x0);
            v[i].y = viewport.y0 + (1.0f - buffer[1]*inv) * .5f * (viewport.y1-viewport.y0);
            v[i].z = buffer[2]*inv;
            v[i].inv_w = inv;
            memcpy(v[i].attrs, buffer+4, attr_count*sizeof(float));
        }
        if (valid) raster_triangle(cc, v, attr_count);
    }
}

static void start_frame(void)
{
    memset(sm64_color, 0, sizeof sm64_color);
    memset(depth, 0xff, sizeof depth);
}

static void initialize(void)
{
    set_viewport(0, 0, 320, 240);
    set_scissor(0, 0, 320, 240);
    tiles[0] = tiles[1] = TEXTURES;
    start_frame();
}

struct GfxRenderingAPI gfx_fixfinder_api = {
    .z_is_from_0_to_1 = z_zero_to_one,
    .unload_shader = unload, .load_shader = load,
    .create_and_load_new_shader = create, .lookup_shader = lookup, .shader_get_info = shader_info,
    .new_texture = new_texture, .select_texture = select_texture, .upload_texture = upload_texture,
    .set_sampler_parameters = set_sampler, .set_depth_test = set_depth_test,
    .set_depth_mask = set_depth_mask, .set_zmode_decal = set_zmode_decal,
    .set_viewport = set_viewport, .set_scissor = set_scissor, .set_use_alpha = set_use_alpha,
    .draw_triangles = draw_triangles, .init = initialize, .on_resize = no_action,
    .start_frame = start_frame, .end_frame = no_action, .finish_render = no_action,
};
