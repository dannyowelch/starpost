#include "render.h"

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    float x, y, r, g, b;
} Vert;

typedef struct {
    Vert *verts;
    int count;
    int cap;
} Batch;

static GLuint program;
static GLuint vao;
static GLuint vbo;
static GLint scale_loc;
static Vert play_verts[4096];
static Vert hud_verts[2048];
static Batch play_batch;
static Batch hud_batch;
static Batch *batch;

static const char *VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec3 a_color;\n"
    "uniform vec2 u_scale;\n"
    "out vec3 v_color;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos * u_scale, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *FS =
    "#version 330 core\n"
    "in vec3 v_color;\n"
    "out vec4 frag;\n"
    "void main() { frag = vec4(v_color, 1.0); }\n";

static int compile_shader(GLuint shader, const char *src)
{
    GLint ok = 0;
    char log[1024];

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok)
        return 0;
    glGetShaderInfoLog(shader, (GLsizei)sizeof log, NULL, log);
    fprintf(stderr, "shader: %s\n", log);
    return -1;
}

int render_init(void)
{
    GLuint vs;
    GLuint fs;
    GLint ok = 0;
    char log[1024];

    vs = glCreateShader(GL_VERTEX_SHADER);
    fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (!vs || !fs) {
        fprintf(stderr, "glCreateShader failed\n");
        return -1;
    }
    if (compile_shader(vs, VS) || compile_shader(fs, FS))
        return -1;

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        glGetProgramInfoLog(program, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "link: %s\n", log);
        return -1;
    }
    scale_loc = glGetUniformLocation(program, "u_scale");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vert),
                          (void *)(2 * sizeof(float)));
    glBindVertexArray(0);
    return 0;
}

static void vert(float x, float y, float r, float g, float b)
{
    Vert *v;
    if (batch->count >= batch->cap)
        return;
    v = &batch->verts[batch->count++];
    v->x = x;
    v->y = y;
    v->r = r;
    v->g = g;
    v->b = b;
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2,
                float r, float g, float b)
{
    if (batch->count + 3 > batch->cap)
        return;
    vert(x0, y0, r, g, b);
    vert(x1, y1, r, g, b);
    vert(x2, y2, r, g, b);
}

static void stroke(float x0, float y0, float x1, float y1, float half,
                   float r, float g, float b)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    float px, py;
    if (len < 1.0e-6f)
        return;
    px = -dy / len * half;
    py = dx / len * half;
    tri(x0 - px, y0 - py, x0 + px, y0 + py, x1 + px, y1 + py, r, g, b);
    tri(x0 - px, y0 - py, x1 + px, y1 + py, x1 - px, y1 - py, r, g, b);
}

static void polar(int lane, float radius, float *x, float *y)
{
    float a = 1.5707963f - (float)lane * (6.2831853f / (float)STARPOST_LANES);
    *x = cosf(a) * radius;
    *y = sinf(a) * radius;
}

static void spoke(int lane, float r0, float r1, float half, float r, float g, float b)
{
    float x0, y0, x1, y1;
    polar(lane, r0, &x0, &y0);
    polar(lane, r1, &x1, &y1);
    stroke(x0, y0, x1, y1, half, r, g, b);
}

static void ring(float radius, float half, float r, float g, float b)
{
    for (int i = 0; i < STARPOST_LANES; i++) {
        float x0, y0, x1, y1;
        polar(i, radius, &x0, &y0);
        polar((i + 1) % STARPOST_LANES, radius, &x1, &y1);
        stroke(x0, y0, x1, y1, half, r, g, b);
    }
}

static void draw_tracer(int lane)
{
    float a = 1.5707963f - (float)lane * (6.2831853f / (float)STARPOST_LANES);
    float c = cosf(a);
    float s = sinf(a);
    float x = c * 1.0f;
    float y = s * 1.0f;
    float reach = 0.055f;
    stroke(x + s * reach, y - c * reach, x - s * reach, y + c * reach, 0.016f,
           0.45f, 1.0f, 1.0f);
}

static void draw_beam(const GameState *g)
{
    float age, env, half, cr, cg, cb, x0, y0, x1, y1;
    if (g->player.beam_timer <= 0.0f)
        return;
    age = 1.0f - (g->player.beam_timer / STARPOST_BEAM_TIME);
    if (age < 0.0f)
        age = 0.0f;
    if (age > 1.0f)
        age = 1.0f;
    env = (age < 0.22f) ? (age / 0.22f) : (1.0f - (age - 0.22f) / 0.78f);
    if (env < 0.0f)
        env = 0.0f;
    half = 0.006f + 0.055f * env;
    cr = 0.55f + 0.45f * env;
    cg = 0.80f + 0.20f * env;
    cb = 1.0f;
    polar(g->player.beam_lane, 0.09f, &x0, &y0);
    polar(g->player.beam_lane, 0.96f, &x1, &y1);
    stroke(x0, y0, x1, y1, half, cr, cg, cb);
}

/* 4x6 grid, up to 6 strokes. A 0,0,0,0 stroke is unused. */
#define FONT_STROKES 6

static const uint8_t FONT[][FONT_STROKES * 4] = {
    /* 0 */ {0, 0, 4, 0, 4, 0, 4, 6, 4, 6, 0, 6, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* 1 */ {2, 0, 2, 6, 2, 6, 0, 4, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* 2 */ {0, 6, 4, 6, 4, 6, 4, 3, 4, 3, 0, 3, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0},
    /* 3 */ {0, 6, 4, 6, 4, 6, 4, 0, 4, 0, 0, 0, 0, 3, 4, 3, 0, 0, 0, 0, 0, 0, 0, 0},
    /* 4 */ {0, 6, 0, 3, 0, 3, 4, 3, 4, 6, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* 5 */ {4, 6, 0, 6, 0, 6, 0, 3, 0, 3, 4, 3, 4, 3, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0},
    /* 6 */ {4, 6, 0, 6, 0, 6, 0, 0, 0, 0, 4, 0, 4, 0, 4, 3, 4, 3, 0, 3, 0, 0, 0, 0},
    /* 7 */ {0, 6, 4, 6, 4, 6, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* 8 */ {0, 0, 4, 0, 4, 0, 4, 6, 4, 6, 0, 6, 0, 6, 0, 0, 0, 3, 4, 3, 0, 0, 0, 0},
    /* 9 */ {4, 0, 4, 6, 4, 6, 0, 6, 0, 6, 0, 3, 0, 3, 4, 3, 0, 0, 0, 0, 0, 0, 0, 0},
    /* W */ {0, 6, 1, 0, 1, 0, 2, 4, 2, 4, 3, 0, 3, 0, 4, 6, 0, 0, 0, 0, 0, 0, 0, 0},
    /* A */ {0, 0, 2, 6, 2, 6, 4, 0, 1, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* V */ {0, 6, 2, 0, 2, 0, 4, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* E */ {4, 6, 0, 6, 0, 6, 0, 0, 0, 0, 4, 0, 0, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0},
    /* C */ {4, 6, 0, 6, 0, 6, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* L */ {0, 6, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* R */ {0, 0, 0, 6, 0, 6, 4, 6, 4, 6, 4, 3, 4, 3, 0, 3, 2, 3, 4, 0, 0, 0, 0, 0},
    /* S */ {4, 6, 0, 6, 0, 6, 0, 3, 0, 3, 4, 3, 4, 3, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0},
    /* O */ {0, 0, 4, 0, 4, 0, 4, 6, 4, 6, 0, 6, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

static int font_index(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    switch (c) {
    case 'W':
        return 10;
    case 'A':
        return 11;
    case 'V':
        return 12;
    case 'E':
        return 13;
    case 'C':
        return 14;
    case 'L':
        return 15;
    case 'R':
        return 16;
    case 'S':
        return 17;
    case 'O':
        return 18;
    default:
        return -1;
    }
}

static void draw_glyph(float x, float y, float h, int index, float r, float g, float b)
{
    float u = h / 6.0f;
    float half = h * 0.09f;
    const uint8_t *s = FONT[index];
    for (int i = 0; i < FONT_STROKES; i++) {
        float x0 = s[0];
        float y0 = s[1];
        float x1 = s[2];
        float y1 = s[3];
        s += 4;
        if (x0 == x1 && y0 == y1)
            continue;
        stroke(x + x0 * u, y + y0 * u, x + x1 * u, y + y1 * u, half, r, g, b);
    }
}

static float text_advance(float h)
{
    return h * (5.2f / 6.0f);
}

static void draw_text(float x, float y, float h, const char *s, float r, float g, float b)
{
    float adv = text_advance(h);
    for (; *s; s++) {
        int index = font_index(*s);
        if (index >= 0)
            draw_glyph(x, y, h, index, r, g, b);
        x += adv;
    }
}

static void draw_shield_bar(const GameState *g)
{
    int n = g->shield_max;
    float seg = 0.045f;
    float gap = 0.012f;
    float width;
    float x;
    if (n < 1)
        return;
    if (n > 16)
        n = 16;
    width = (float)n * seg + (float)(n - 1) * gap;
    x = -width * 0.5f;
    for (int i = 0; i < n; i++) {
        float x0 = x + (float)i * (seg + gap);
        int on = i < g->shield;
        if (on)
            stroke(x0, 0.915f, x0 + seg, 0.915f, 0.012f, 0.45f, 1.0f, 1.0f);
        else
            stroke(x0, 0.915f, x0 + seg, 0.915f, 0.008f, 0.22f, 0.08f, 0.10f);
    }
}

static void draw_hud(const GameState *g)
{
    char buf[32];
    float h = 0.055f;
    float y = 0.875f;
    float width;

    snprintf(buf, sizeof buf, "WAVE %d", g->cfg.wave);
    draw_text(-0.97f, y, h, buf, 0.55f, 0.95f, 1.0f);

    snprintf(buf, sizeof buf, "SCORE %u", g->score);
    width = (float)strlen(buf) * text_advance(h);
    draw_text(0.97f - width, y, h, buf, 0.95f, 0.95f, 1.0f);
    draw_shield_bar(g);

    if (g->state != STATE_WAVE_CLEAR)
        return;
    h = 0.11f;
    snprintf(buf, sizeof buf, "WAVE %d", g->cfg.wave);
    width = (float)strlen(buf) * text_advance(h);
    draw_text(-width * 0.5f, 0.28f, h, buf, 0.65f, 1.0f, 1.0f);
    width = 5.0f * text_advance(h);
    draw_text(-width * 0.5f, 0.12f, h, "CLEAR", 1.0f, 1.0f, 1.0f);
    snprintf(buf, sizeof buf, "%u", g->score);
    width = (float)strlen(buf) * text_advance(h);
    draw_text(-width * 0.5f, -0.26f, h, buf, 0.95f, 0.95f, 1.0f);
}

static void draw_walker(const Enemy *e)
{
    float rad = 0.09f + e->dist * 0.83f;
    float x, y;
    polar(e->lane, rad, &x, &y);
    stroke(x, y + 0.035f, x + 0.035f, y, 0.008f, 1.0f, 0.15f, 0.55f);
    stroke(x + 0.035f, y, x, y - 0.035f, 0.008f, 1.0f, 0.15f, 0.55f);
    stroke(x, y - 0.035f, x - 0.035f, y, 0.008f, 1.0f, 0.15f, 0.55f);
    stroke(x - 0.035f, y, x, y + 0.035f, 0.008f, 1.0f, 0.15f, 0.55f);
}

void render_draw(const GameState *g, int width, int height)
{
    float sx = 0.92f;
    float sy = 0.92f;
    int over = g->state == STATE_GAME_OVER;
    int clearing = g->state == STATE_WAVE_CLEAR;
    float dim = clearing ? 0.35f : 1.0f;
    float base_r = over ? 1.0f : 0.45f;
    float base_g = over ? 0.12f : 1.0f;
    float base_b = over ? 0.18f : 1.0f;

    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    if (width > height)
        sx *= (float)height / (float)width;
    else if (height > width)
        sy *= (float)width / (float)height;

    play_batch.verts = play_verts;
    play_batch.cap = (int)(sizeof play_verts / sizeof play_verts[0]);
    play_batch.count = 0;
    hud_batch.verts = hud_verts;
    hud_batch.cap = (int)(sizeof hud_verts / sizeof hud_verts[0]);
    hud_batch.count = 0;
    batch = &play_batch;

    ring(0.92f, 0.005f, 0.08f * dim, 0.16f * dim, 0.42f * dim);
    ring(0.075f, 0.010f, base_r, base_g, base_b);
    for (int i = 0; i < STARPOST_LANES; i++)
        spoke(i, 0.09f, 0.92f, 0.006f, 0.10f * dim, 0.22f * dim, 0.55f * dim);
    spoke(g->player.lane, 0.09f, 0.92f, 0.012f, 0.25f, 0.75f, 1.0f);

    for (int i = 0; i < g->shield_max && i < STARPOST_LANES; i++) {
        float a = 1.5707963f - ((float)i + 0.5f) * (6.2831853f / (float)STARPOST_LANES);
        float x0 = cosf(a) * 0.115f;
        float y0 = sinf(a) * 0.115f;
        float x1 = cosf(a) * 0.155f;
        float y1 = sinf(a) * 0.155f;
        int on = i < g->shield;
        if (on)
            stroke(x0, y0, x1, y1, 0.010f, 0.55f, 1.0f, 1.0f);
        else
            stroke(x0, y0, x1, y1, 0.006f, 0.18f, 0.05f, 0.08f);
    }

    draw_beam(g);
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (g->enemies[i].alive)
            draw_walker(&g->enemies[i]);
    }
    draw_tracer(g->player.lane);

    batch = &hud_batch;
    draw_hud(g);

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glUniform2f(scale_loc, sx, sy);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(play_batch.count * (int)sizeof(Vert)),
                 play_verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, play_batch.count);
    if (hud_batch.count > 0) {
        glUniform2f(scale_loc, 1.0f, 1.0f);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(hud_batch.count * (int)sizeof(Vert)),
                     hud_verts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, hud_batch.count);
    }
}

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_ready = 1;
}

static uint32_t crc32_bytes(const unsigned char *buf, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    if (!crc_ready)
        crc_init();
    for (size_t i = 0; i < len; i++)
        c = crc_table[(c ^ buf[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)((v >> 24) & 0xFFu);
    p[1] = (unsigned char)((v >> 16) & 0xFFu);
    p[2] = (unsigned char)((v >> 8) & 0xFFu);
    p[3] = (unsigned char)(v & 0xFFu);
}

static int write_chunk(FILE *f, const char type[4], const unsigned char *data, uint32_t len)
{
    unsigned char lenb[4];
    unsigned char crcb[4];
    unsigned char *buf;
    put_be32(lenb, len);
    if (fwrite(lenb, 1, 4, f) != 4)
        return -1;
    if (fwrite(type, 1, 4, f) != 4)
        return -1;
    if (len && fwrite(data, 1, len, f) != len)
        return -1;
    buf = malloc((size_t)len + 4);
    if (!buf)
        return -1;
    memcpy(buf, type, 4);
    if (len)
        memcpy(buf + 4, data, len);
    put_be32(crcb, crc32_bytes(buf, (size_t)len + 4));
    free(buf);
    if (fwrite(crcb, 1, 4, f) != 4)
        return -1;
    return 0;
}

int render_screenshot(const char *path, int width, int height)
{
    unsigned char *bottom;
    unsigned char *raw;
    unsigned char *comp;
    uLongf comp_len;
    size_t stride;
    size_t raw_len;
    FILE *f;
    unsigned char ihdr[13];
    static const unsigned char sig[8] = {
        137, 80, 78, 71, 13, 10, 26, 10
    };

    if (width < 1 || height < 1)
        return -1;
    bottom = malloc((size_t)width * (size_t)height * 4u);
    if (!bottom)
        return -1;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottom);

    stride = (size_t)width * 4u + 1u;
    raw_len = stride * (size_t)height;
    raw = malloc(raw_len);
    if (!raw) {
        free(bottom);
        return -1;
    }
    for (int y = 0; y < height; y++) {
        unsigned char *row = raw + (size_t)y * stride;
        row[0] = 0;
        memcpy(row + 1, bottom + (size_t)(height - 1 - y) * (size_t)width * 4u,
               (size_t)width * 4u);
    }
    free(bottom);

    comp_len = compressBound((uLong)raw_len);
    comp = malloc(comp_len);
    if (!comp) {
        free(raw);
        return -1;
    }
    if (compress(comp, &comp_len, raw, (uLong)raw_len) != Z_OK) {
        free(comp);
        free(raw);
        fprintf(stderr, "png: compress failed\n");
        return -1;
    }
    free(raw);

    f = fopen(path, "wb");
    if (!f) {
        perror(path);
        free(comp);
        return -1;
    }
    put_be32(ihdr, (uint32_t)width);
    put_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (fwrite(sig, 1, 8, f) != 8 || write_chunk(f, "IHDR", ihdr, 13) ||
        write_chunk(f, "IDAT", comp, (uint32_t)comp_len) || write_chunk(f, "IEND", NULL, 0)) {
        fprintf(stderr, "png: write failed\n");
        fclose(f);
        free(comp);
        return -1;
    }
    free(comp);
    if (fclose(f) != 0)
        return -1;
    return 0;
}
