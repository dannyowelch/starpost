#include "render.h"

#include "fx/fx.h"
#include "meta/meta.h"

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
static GLint shake_loc;
static GLuint fs_prog_bright;
static GLuint fs_prog_blur;
static GLuint fs_prog_comp;
static GLint bright_tex_loc;
static GLint blur_tex_loc;
static GLint blur_dir_loc;
static GLint comp_scene_loc;
static GLint comp_bloom_loc;
static GLint comp_strength_loc;
static GLuint fs_vao;
static GLuint fs_vbo;
static GLuint scene_tex;
static GLuint scene_fbo;
static GLuint bloom_tex[2];
static GLuint bloom_fbo[2];
static int fb_w;
static int fb_h;
static int bloom_w;
static int bloom_h;
static int bloom_on = 1;
static int bloom_ok;
static FxState fx;
static int snap_valid;
static int snap_shield;
static uint32_t snap_score;
static GamePlayState snap_state;
static struct {
    int alive;
    EnemyKind kind;
    uint8_t lane;
    float dist;
    float angle;
} snap[STARPOST_MAX_ENEMIES];
static float flicker_time;
static int title_snap_ready;
static Vert play_verts[8192];
static Vert hud_verts[8192];
static Batch play_batch;
static Batch hud_batch;
static Batch *batch;

static const char *VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec3 a_color;\n"
    "uniform vec2 u_scale;\n"
    "uniform vec2 u_shake;\n"
    "out vec3 v_color;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos * u_scale + u_shake, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *FS =
    "#version 330 core\n"
    "in vec3 v_color;\n"
    "out vec4 frag;\n"
    "void main() { frag = vec4(v_color, 1.0); }\n";

static const char *FS_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char *BRIGHT_FS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "void main() {\n"
    "    vec3 c = texture(u_tex, v_uv).rgb;\n"
    "    float l = max(c.r, max(c.g, c.b));\n"
    "    float keep = smoothstep(0.85, 1.60, l);\n"
    "    frag = vec4(c * keep, 1.0);\n"
    "}\n";

static const char *BLUR_FS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_dir;\n"
    "void main() {\n"
    "    vec3 c = texture(u_tex, v_uv).rgb * 0.227027;\n"
    "    c += texture(u_tex, v_uv + u_dir * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture(u_tex, v_uv - u_dir * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture(u_tex, v_uv + u_dir * 3.2307692308).rgb * 0.0702702703;\n"
    "    c += texture(u_tex, v_uv - u_dir * 3.2307692308).rgb * 0.0702702703;\n"
    "    frag = vec4(c, 1.0);\n"
    "}\n";

static const char *COMP_FS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_scene;\n"
    "uniform sampler2D u_bloom;\n"
    "uniform float u_strength;\n"
    "void main() {\n"
    "    vec3 scene = texture(u_scene, v_uv).rgb;\n"
    "    vec3 bloom = texture(u_bloom, v_uv).rgb;\n"
    "    vec3 col = scene + bloom * u_strength;\n"
    "    col = col / (vec3(1.0) + col);\n"
    "    frag = vec4(col, 1.0);\n"
    "}\n";

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

static GLuint link_program(const char *vs_src, const char *fs_src)
{
    GLuint vs;
    GLuint fs;
    GLuint prog;
    GLint ok = 0;
    char log[1024];

    vs = glCreateShader(GL_VERTEX_SHADER);
    fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (!vs || !fs || compile_shader(vs, vs_src) || compile_shader(fs, fs_src))
        return 0;
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        glGetProgramInfoLog(prog, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "link: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

int render_init(void)
{
    static const float fs_tri[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 3.0f, -1.0f, 2.0f, 0.0f, -1.0f, 3.0f, 0.0f, 2.0f,
    };

    fx_init(&fx);
    program = link_program(VS, FS);
    if (!program)
        return -1;
    scale_loc = glGetUniformLocation(program, "u_scale");
    shake_loc = glGetUniformLocation(program, "u_shake");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vert),
                          (void *)(2 * sizeof(float)));

    fs_prog_bright = link_program(FS_VS, BRIGHT_FS);
    fs_prog_blur = link_program(FS_VS, BLUR_FS);
    fs_prog_comp = link_program(FS_VS, COMP_FS);
    if (!fs_prog_bright || !fs_prog_blur || !fs_prog_comp) {
        fprintf(stderr, "bloom shaders unavailable\n");
        bloom_ok = 0;
    } else {
        bright_tex_loc = glGetUniformLocation(fs_prog_bright, "u_tex");
        blur_tex_loc = glGetUniformLocation(fs_prog_blur, "u_tex");
        blur_dir_loc = glGetUniformLocation(fs_prog_blur, "u_dir");
        comp_scene_loc = glGetUniformLocation(fs_prog_comp, "u_scene");
        comp_bloom_loc = glGetUniformLocation(fs_prog_comp, "u_bloom");
        comp_strength_loc = glGetUniformLocation(fs_prog_comp, "u_strength");
        glGenVertexArrays(1, &fs_vao);
        glGenBuffers(1, &fs_vbo);
        glBindVertexArray(fs_vao);
        glBindBuffer(GL_ARRAY_BUFFER, fs_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof fs_tri, fs_tri, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void *)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float),
                              (void *)(2 * sizeof(float)));
        bloom_ok = 1;
    }
    glBindVertexArray(0);
    {
        const GLubyte *renderer = glGetString(GL_RENDERER);
        const GLubyte *version = glGetString(GL_VERSION);
        fprintf(stderr, "GL %s / %s\n", renderer ? (const char *)renderer : "?",
                version ? (const char *)version : "?");
    }
    return 0;
}

void render_set_bloom(int enabled)
{
    bloom_on = enabled ? 1 : 0;
}

int render_bloom_enabled(void)
{
    return bloom_on;
}

int render_shard_count(void)
{
    return fx_count(&fx);
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

static void stroke_raw(float x0, float y0, float x1, float y1, float half,
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

static void stroke(float x0, float y0, float x1, float y1, float half,
                   float r, float g, float b)
{
    stroke_raw(x0, y0, x1, y1, half * 2.5f, r * 0.42f, g * 0.42f, b * 0.42f);
    stroke_raw(x0, y0, x1, y1, half, r, g, b);
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

static void draw_tracer(const GameState *g)
{
    float a = 1.5707963f - (float)g->player.lane * (6.2831853f / (float)STARPOST_LANES);
    float c = cosf(a);
    float s = sinf(a);
    float x = c * 1.0f;
    float y = s * 1.0f;
    float reach = 0.055f;
    int dry = !starpost_normal_shot_allowed(energy_tune(), g->energy);
    if (dry)
        stroke(x + s * reach, y - c * reach, x - s * reach, y + c * reach, 0.022f, 1.7f, 0.16f,
               0.22f);
    else
        stroke(x + s * reach, y - c * reach, x - s * reach, y + c * reach, 0.018f, 0.55f, 1.45f,
               1.7f);
}

static void draw_shots(const GameState *g)
{
    int i;
    for (i = 0; i < STARPOST_MAX_SHOTS; i++) {
        const Shot *s = &g->shots[i];
        float rad;
        float x0, y0, x1, y1;
        float cr, cg, cb;
        if (!s->alive)
            continue;
        rad = 0.09f + s->dist * 0.83f;
        polar(s->lane, rad, &x0, &y0);
        polar(s->lane, rad + 0.09f, &x1, &y1);
        if (s->spread) {
            cr = 1.9f;
            cg = 0.55f;
            cb = 2.1f;
        } else {
            cr = 2.0f;
            cg = 2.2f;
            cb = 2.6f;
        }
        stroke(x0, y0, x1, y1, 0.020f, cr * 0.45f, cg * 0.45f, cb * 0.45f);
        stroke(x0, y0, x1, y1, 0.008f, cr, cg, cb);
    }
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
    /* Z */ {0, 6, 4, 6, 4, 6, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* P */ {0, 0, 0, 6, 0, 6, 4, 6, 4, 6, 4, 3, 4, 3, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0},
    /* B */ {0, 0, 0, 6, 0, 6, 4, 6, 4, 6, 4, 3, 4, 3, 0, 3, 0, 0, 4, 0, 4, 0, 4, 3},
    /* D */ {0, 0, 0, 6, 0, 6, 3, 6, 3, 6, 4, 3, 4, 3, 3, 0, 3, 0, 0, 0, 0, 0, 0, 0},
    /* F */ {0, 0, 0, 6, 0, 6, 4, 6, 0, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* G */ {4, 6, 0, 6, 0, 6, 0, 0, 0, 0, 4, 0, 4, 0, 4, 3, 4, 3, 2, 3, 0, 0, 0, 0},
    /* H */ {0, 0, 0, 6, 4, 0, 4, 6, 0, 3, 4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* I */ {2, 0, 2, 6, 1, 0, 3, 0, 1, 6, 3, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* M */ {0, 0, 0, 6, 0, 6, 2, 3, 2, 3, 4, 6, 4, 6, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* N */ {0, 0, 0, 6, 0, 6, 4, 0, 4, 0, 4, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Q */ {0, 0, 4, 0, 4, 0, 4, 6, 4, 6, 0, 6, 0, 6, 0, 0, 2, 2, 4, 0, 0, 0, 0, 0},
    /* T */ {0, 6, 4, 6, 2, 6, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* U */ {0, 6, 0, 0, 0, 0, 4, 0, 4, 0, 4, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Y */ {0, 6, 2, 3, 4, 6, 2, 3, 2, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* - */ {0, 3, 4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* J */ {1, 6, 3, 6, 3, 6, 3, 1, 3, 1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0},
    /* K */ {0, 0, 0, 6, 0, 3, 4, 6, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* X */ {0, 6, 4, 0, 4, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
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
    case 'Z':
        return 19;
    case 'P':
        return 20;
    case 'B':
        return 21;
    case 'D':
        return 22;
    case 'F':
        return 23;
    case 'G':
        return 24;
    case 'H':
        return 25;
    case 'I':
        return 26;
    case 'M':
        return 27;
    case 'N':
        return 28;
    case 'Q':
        return 29;
    case 'T':
        return 30;
    case 'U':
        return 31;
    case 'Y':
        return 32;
    case '-':
        return 33;
    case 'J':
        return 34;
    case 'K':
        return 35;
    case 'X':
        return 36;
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
        stroke_raw(x + x0 * u, y + y0 * u, x + x1 * u, y + y1 * u, half * 1.7f,
                   r * 0.45f, g * 0.45f, b * 0.45f);
        stroke_raw(x + x0 * u, y + y0 * u, x + x1 * u, y + y1 * u, half, r, g, b);
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

static void text_at(float x, float y, float h, const char *s, float r, float g, float b)
{
    batch = &hud_batch;
    draw_text(x, y, h, s, r, g, b);
}

static void text_center(float y, float h, const char *s, float r, float g, float b)
{
    float width = (float)strlen(s) * text_advance(h);
    text_at(-width * 0.5f, y, h, s, r, g, b);
}

static void menu_line(int index, int selected, float y, const char *label)
{
    if (index == selected)
        text_center(y, 0.055f, label, 0.75f, 1.35f, 1.55f);
    else
        text_center(y, 0.055f, label, 0.28f, 0.48f, 0.62f);
}

static void reset_batches(void)
{
    play_batch.verts = play_verts;
    play_batch.cap = (int)(sizeof play_verts / sizeof play_verts[0]);
    play_batch.count = 0;
    hud_batch.verts = hud_verts;
    hud_batch.cap = (int)(sizeof hud_verts / sizeof hud_verts[0]);
    hud_batch.count = 0;
}

static void observe(const GameState *g, float dt);
static void build_scene(const GameState *g);

static void scale_play(float scale, float oy)
{
    int i;
    for (i = 0; i < play_batch.count; i++) {
        play_verts[i].x *= scale;
        play_verts[i].y = play_verts[i].y * scale + oy;
    }
}

static void draw_title(const Session *s, float dt)
{
    static const char *labels[TITLE_COUNT] = {"START", "DIFFICULTY", "HIGH SCORES", "QUIT"};
    int i;
    if (!title_snap_ready)
        snap_valid = 0;
    title_snap_ready = 1;
    observe(&s->game, dt);
    build_scene(&s->game);
    /* The playfield is the full web. Shrink it so the title and menu stay clear. */
    scale_play(0.40f, 0.02f);
    hud_batch.count = 0;
    text_center(0.78f, 0.095f, "STARPOST", 0.65f, 1.25f, 1.55f);
    text_center(0.64f, 0.048f, "RETURNS", 1.35f, 0.85f, 0.35f);
    text_center(0.50f, 0.032f, diff_name(s->difficulty), 0.45f, 0.85f, 1.05f);
    for (i = 0; i < TITLE_COUNT; i++)
        menu_line(i, s->title_item, -0.52f - (float)i * 0.108f, labels[i]);
}

static void draw_difficulty(const Session *s)
{
    int i;
    reset_batches();
    text_center(0.55f, 0.07f, "DIFFICULTY", 0.65f, 1.15f, 1.45f);
    for (i = 0; i < DIFF_COUNT; i++)
        menu_line(i, s->diff_item, 0.22f - (float)i * 0.14f, diff_name((Difficulty)i));
    text_center(-0.72f, 0.036f, "ESC BACK", 0.28f, 0.48f, 0.62f);
}

static void draw_scores(const Session *s)
{
    char buf[32];
    int d = s->score_diff;
    int i;
    if (d < 0 || d >= DIFF_COUNT)
        d = 0;
    reset_batches();
    text_center(0.82f, 0.06f, "HIGH SCORES", 0.65f, 1.15f, 1.45f);
    text_center(0.70f, 0.04f, diff_name((Difficulty)d), 1.25f, 0.9f, 0.35f);
    if (s->scores.count[d] == 0) {
        text_center(0.2f, 0.05f, "NO SCORES", 0.35f, 0.55f, 0.7f);
    } else {
        for (i = 0; i < s->scores.count[d]; i++) {
            const ScoreRow *row = &s->scores.rows[d][i];
            snprintf(buf, sizeof buf, "%d %c%c%c %u", i + 1, row->initials[0], row->initials[1],
                     row->initials[2], row->score);
            text_center(0.48f - (float)i * 0.09f, 0.04f, buf, 0.75f, 1.2f, 1.4f);
        }
    }
    text_center(-0.82f, 0.032f, "LEFT RIGHT", 0.28f, 0.48f, 0.62f);
}

static void draw_initials(const Session *s)
{
    char one[2];
    float h = 0.16f;
    float adv = text_advance(h);
    float gap = adv * 0.35f;
    float end_h = 0.07f;
    float end_w = 3.0f * text_advance(end_h);
    float width = adv * 3.0f + gap * 2.0f + gap + end_w;
    float x = -width * 0.5f;
    int i;
    char buf[32];
    int on_end = s->initial_slot >= SCORE_INITIALS;
    reset_batches();
    text_center(0.55f, 0.055f, "ENTER INITIALS", 0.65f, 1.15f, 1.45f);
    snprintf(buf, sizeof buf, "%u", s->pending_score);
    text_center(0.40f, 0.045f, buf, 0.9f, 0.95f, 1.0f);
    one[1] = '\0';
    for (i = 0; i < SCORE_INITIALS; i++) {
        one[0] = s->initials[i];
        if (i == s->initial_slot)
            text_at(x, 0.05f, h, one, 1.45f, 1.15f, 0.35f);
        else
            text_at(x, 0.05f, h, one, 0.55f, 1.05f, 1.35f);
        if (i == s->initial_slot)
            text_at(x, -0.08f, 0.04f, "-", 1.45f, 1.15f, 0.35f);
        x += adv + gap;
    }
    if (on_end)
        text_at(x, 0.08f, end_h, "END", 1.45f, 1.15f, 0.35f);
    else
        text_at(x, 0.08f, end_h, "END", 0.35f, 0.55f, 0.70f);
    if (on_end)
        text_at(x, -0.04f, 0.04f, "-", 1.45f, 1.15f, 0.35f);
}

static void draw_pause_menu(const Session *s)
{
    text_center(0.28f, 0.09f, "PAUSED", 0.7f, 1.3f, 1.55f);
    menu_line(PAUSE_RESUME, s->pause_item, 0.02f, "RESUME");
    menu_line(PAUSE_QUIT, s->pause_item, -0.12f, "QUIT TO TITLE");
}

static void draw_wave_gauge(const GameState *g)
{
    float gauge = starpost_wave_gauge(g);
    float width = 0.72f;
    float y = 0.915f;
    float x0 = -width * 0.5f;
    float fill = width * gauge;
    stroke(x0, y, x0 + width, y, 0.007f, 0.16f, 0.28f, 0.36f);
    if (fill > 0.004f)
        stroke(x0, y, x0 + fill, y, 0.011f, 0.45f, 1.25f, 1.45f);
}

static void fill_rect(float x0, float y0, float x1, float y1, float r, float g, float b)
{
    tri(x0, y0, x1, y0, x1, y1, r, g, b);
    tri(x0, y0, x1, y1, x0, y1, r, g, b);
}

static void draw_hud(const GameState *g)
{
    char buf[32];
    float h = 0.055f;
    float y = 0.875f;
    float width;

    if (g->zap_flash > 0.0f) {
        float k = g->zap_flash / STARPOST_ZAP_FLASH;
        fill_rect(-1.0f, -1.0f, 1.0f, 1.0f, 0.45f * k, 0.95f * k, 1.25f * k);
    }

    snprintf(buf, sizeof buf, "WAVE %d", g->cfg.wave);
    draw_text(-0.97f, y, h, buf, 0.55f, 0.95f, 1.0f);

    snprintf(buf, sizeof buf, "SCORE %u", g->score);
    width = (float)strlen(buf) * text_advance(h);
    draw_text(0.97f - width, y, h, buf, 0.95f, 0.95f, 1.0f);
    snprintf(buf, sizeof buf, "EN %d", g->energy);
    draw_text(-0.97f, 0.79f, 0.042f, buf, 1.45f, 1.15f, 0.30f);
    if (g->deny_flash > 0.0f && energy_tune()->deny_time > 0.0f) {
        float k = g->deny_flash / energy_tune()->deny_time;
        fill_rect(-1.0f, -1.0f, 1.0f, 1.0f, 0.7f * k, 0.08f * k, 0.1f * k);
    }
    draw_wave_gauge(g);

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

static void draw_flyer(const Enemy *e)
{
    float rad = 0.09f + (e->on_ring ? 1.0f : e->dist) * 0.83f;
    float c = cosf(e->angle);
    float s = sinf(e->angle);
    float x = c * rad;
    float y = s * rad;
    float flick = sinf(flicker_time * 34.0f + e->angle * 3.0f) > 0.0f ? 1.0f : 0.28f;
    float inward_x = -c;
    float inward_y = -s;
    float side = 0.042f;
    float tip = 0.055f;
    float dim = e->on_ring ? 0.40f : 1.0f;
    if (e->on_ring) {
        side *= 0.55f;
        tip *= 0.55f;
        flick *= dim;
    }
    stroke(x + (-s) * side, y + c * side, x + inward_x * tip, y + inward_y * tip, 0.008f,
           1.7f * flick, 1.25f * flick, 0.22f * flick);
    stroke(x - (-s) * side, y - c * side, x + inward_x * tip, y + inward_y * tip, 0.008f,
           1.7f * flick, 1.25f * flick, 0.22f * flick);
    stroke(x + (-s) * side * 0.65f, y + c * side * 0.65f, x - (-s) * side * 0.65f,
           y - c * side * 0.65f, 0.006f, 1.4f * flick, 1.05f * flick, 0.18f * flick);
}

static void draw_walker(const Enemy *e)
{
    float rad = 0.09f + (e->on_ring ? 1.0f : e->dist) * 0.83f;
    float x, y;
    float s = e->on_ring ? 0.020f : 0.035f;
    float cr = e->on_ring ? 0.70f : 1.65f;
    float cg = e->on_ring ? 0.08f : 0.16f;
    float cb = e->on_ring ? 0.42f : 0.95f;
    if (e->on_ring) {
        x = cosf(e->angle) * rad;
        y = sinf(e->angle) * rad;
    } else {
        polar(e->lane, rad, &x, &y);
    }
    stroke(x, y + s, x + s, y, 0.009f, cr, cg, cb);
    stroke(x + s, y, x, y - s, 0.009f, cr, cg, cb);
    stroke(x, y - s, x - s, y, 0.009f, cr, cg, cb);
    stroke(x - s, y, x, y + s, 0.009f, cr, cg, cb);
}

static void draw_hex(float x, float y, float r, float half, float cr, float cg, float cb)
{
    int i;
    for (i = 0; i < 6; i++) {
        float a0 = 1.04719755f * (float)i;
        float a1 = 1.04719755f * (float)(i + 1);
        stroke(x + cosf(a0) * r, y + sinf(a0) * r, x + cosf(a1) * r, y + sinf(a1) * r, half, cr,
               cg, cb);
    }
}

static void draw_splitter(const Enemy *e)
{
    float rad = 0.09f + (e->on_ring ? 1.0f : e->dist) * 0.83f;
    float x, y;
    int small = e->kind == ENEMY_SPLIT;
    float r = small ? 0.024f : 0.052f;
    float half = small ? 0.006f : 0.009f;
    if (e->on_ring) {
        r *= 0.55f;
        x = cosf(e->angle) * rad;
        y = sinf(e->angle) * rad;
        draw_hex(x, y, r, half, 0.22f, 0.75f, 0.14f);
        return;
    }
    polar(e->lane, rad, &x, &y);
    if (small) {
        draw_hex(x, y, r, half, 0.85f, 1.55f, 0.42f);
    } else {
        draw_hex(x, y, r, half, 0.45f, 1.85f, 0.28f);
        stroke(x - r * 0.7f, y, x + r * 0.7f, y, 0.006f, 0.9f, 1.7f, 0.45f);
    }
}

static void walker_center(int lane, float dist, float *x, float *y)
{
    polar(lane, 0.09f + dist * 0.83f, x, y);
}

static void draw_shards(void)
{
    int i;
    for (i = 0; i < FX_MAX; i++) {
        const FxShard *s = &fx.shards[i];
        float k;
        if (s->life <= 0.0f)
            continue;
        k = s->life;
        stroke_raw(s->x, s->y, s->x - s->vx * 0.07f, s->y - s->vy * 0.07f, 0.008f * (0.45f + k),
                   s->r * k * 1.9f, s->g * k * 1.9f, s->b * k * 1.9f);
    }
}

static void remember(const GameState *g)
{
    int i;
    for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        snap[i].alive = g->enemies[i].alive ? 1 : 0;
        snap[i].kind = g->enemies[i].kind;
        snap[i].lane = g->enemies[i].lane;
        snap[i].dist = g->enemies[i].dist;
        snap[i].angle = g->enemies[i].angle;
    }
    snap_shield = g->shield;
    snap_score = g->score;
    snap_state = g->state;
    snap_valid = 1;
}

static void observe(const GameState *g, float dt)
{
    int i;
    if (dt < 0.0f)
        dt = 0.0f;
    if (snap_valid && g->score >= snap_score &&
        !(snap_state == STATE_GAME_OVER && g->state == STATE_PLAYING)) {
        for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
            float x, y;
            if (!snap[i].alive || g->enemies[i].alive || snap[i].dist <= 0.05f)
                continue;
            if (snap[i].kind == ENEMY_FLYER) {
                float rad = 0.09f + snap[i].dist * 0.83f;
                x = cosf(snap[i].angle) * rad;
                y = sinf(snap[i].angle) * rad;
                fx_burst(&fx, x, y, 1.6f, 1.2f, 0.25f);
            } else if (snap[i].kind == ENEMY_SPLITTER || snap[i].kind == ENEMY_SPLIT) {
                walker_center(snap[i].lane, snap[i].dist, &x, &y);
                fx_burst(&fx, x, y, 0.55f, 1.7f, 0.35f);
            } else {
                walker_center(snap[i].lane, snap[i].dist, &x, &y);
                fx_burst(&fx, x, y, 1.55f, 0.2f, 0.95f);
            }
        }
        if (g->shield < snap_shield)
            fx_hit(&fx);
    }
    fx_update(&fx, dt);
    remember(g);
}

static void destroy_targets(void)
{
    if (scene_fbo)
        glDeleteFramebuffers(1, &scene_fbo);
    if (scene_tex)
        glDeleteTextures(1, &scene_tex);
    if (bloom_fbo[0])
        glDeleteFramebuffers(2, bloom_fbo);
    if (bloom_tex[0])
        glDeleteTextures(2, bloom_tex);
    scene_fbo = 0;
    scene_tex = 0;
    bloom_fbo[0] = bloom_fbo[1] = 0;
    bloom_tex[0] = bloom_tex[1] = 0;
    fb_w = 0;
    fb_h = 0;
}

static GLuint make_color_tex(int w, int h)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

static int attach_fbo(GLuint *fbo, GLuint tex)
{
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

static int ensure_targets(int width, int height)
{
    int bw;
    int bh;
    if (!bloom_ok)
        return 0;
    if (scene_fbo && width == fb_w && height == fb_h)
        return 1;
    destroy_targets();
    bw = width / 4;
    bh = height / 4;
    if (bw < 1)
        bw = 1;
    if (bh < 1)
        bh = 1;
    scene_tex = make_color_tex(width, height);
    bloom_tex[0] = make_color_tex(bw, bh);
    bloom_tex[1] = make_color_tex(bw, bh);
    if (!attach_fbo(&scene_fbo, scene_tex) || !attach_fbo(&bloom_fbo[0], bloom_tex[0]) ||
        !attach_fbo(&bloom_fbo[1], bloom_tex[1])) {
        fprintf(stderr, "HDR framebuffer unavailable, bloom off\n");
        destroy_targets();
        bloom_ok = 0;
        return 0;
    }
    fb_w = width;
    fb_h = height;
    bloom_w = bw;
    bloom_h = bh;
    return 1;
}

static void draw_fullscreen(void)
{
    glBindVertexArray(fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void present(int width, int height, float sx, float sy)
{
    float shake_x = 0.0f;
    float shake_y = 0.0f;
    fx_shake_offset(&fx, &shake_x, &shake_y);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(program);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    if (ensure_targets(width, height)) {
        glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
        glViewport(0, 0, width, height);
        glClearColor(0.012f, 0.014f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform2f(scale_loc, sx, sy);
        glUniform2f(shake_loc, shake_x, shake_y);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(play_batch.count * (int)sizeof(Vert)),
                     play_verts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, play_batch.count);
        if (hud_batch.count > 0) {
            glUniform2f(scale_loc, 1.0f, 1.0f);
            glUniform2f(shake_loc, 0.0f, 0.0f);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(hud_batch.count * (int)sizeof(Vert)),
                         hud_verts, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, hud_batch.count);
        }

        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[0]);
        glViewport(0, 0, bloom_w, bloom_h);
        glUseProgram(fs_prog_bright);
        glUniform1i(bright_tex_loc, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene_tex);
        draw_fullscreen();

        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[1]);
        glUseProgram(fs_prog_blur);
        glUniform1i(blur_tex_loc, 0);
        glUniform2f(blur_dir_loc, 1.0f / (float)bloom_w, 0.0f);
        glBindTexture(GL_TEXTURE_2D, bloom_tex[0]);
        draw_fullscreen();

        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[0]);
        glUniform2f(blur_dir_loc, 0.0f, 1.0f / (float)bloom_h);
        glBindTexture(GL_TEXTURE_2D, bloom_tex[1]);
        draw_fullscreen();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glUseProgram(fs_prog_comp);
        glUniform1i(comp_scene_loc, 0);
        glUniform1i(comp_bloom_loc, 1);
        glUniform1f(comp_strength_loc, bloom_on ? 1.15f : 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene_tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloom_tex[0]);
        draw_fullscreen();
        glActiveTexture(GL_TEXTURE0);
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.012f, 0.014f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform2f(scale_loc, sx, sy);
    glUniform2f(shake_loc, shake_x, shake_y);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(play_batch.count * (int)sizeof(Vert)),
                 play_verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, play_batch.count);
    if (hud_batch.count > 0) {
        glUniform2f(scale_loc, 1.0f, 1.0f);
        glUniform2f(shake_loc, 0.0f, 0.0f);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(hud_batch.count * (int)sizeof(Vert)),
                     hud_verts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, hud_batch.count);
    }
}

static void view_scale(int width, int height, float *sx, float *sy)
{
    *sx = 0.92f;
    *sy = 0.92f;
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    if (width > height)
        *sx *= (float)height / (float)width;
    else if (height > width)
        *sy *= (float)width / (float)height;
}

static void build_scene(const GameState *g)
{
    int over = g->state == STATE_GAME_OVER;
    int clearing = g->state == STATE_WAVE_CLEAR;
    float dim = clearing ? 0.35f : 1.0f;
    float base_r = over ? 1.7f : 0.55f;
    float base_g = over ? 0.18f : 1.55f;
    float base_b = over ? 0.28f : 1.75f;

    play_batch.verts = play_verts;
    play_batch.cap = (int)(sizeof play_verts / sizeof play_verts[0]);
    play_batch.count = 0;
    hud_batch.verts = hud_verts;
    hud_batch.cap = (int)(sizeof hud_verts / sizeof hud_verts[0]);
    hud_batch.count = 0;
    batch = &play_batch;

    if (g->slow_timer > 0.0f) {
        ring(0.92f, 0.006f, 1.05f * dim, 0.38f * dim, 0.08f * dim);
    } else {
        ring(0.92f, 0.006f, 0.16f * dim, 0.28f * dim, 0.85f * dim);
    }
    ring(0.075f, 0.012f, base_r, base_g, base_b);
    for (int i = 0; i < STARPOST_LANES; i++) {
        if (g->slow_timer > 0.0f)
            spoke(i, 0.09f, 0.92f, 0.008f, 1.25f * dim, 0.45f * dim, 0.08f * dim);
        else
            spoke(i, 0.09f, 0.92f, 0.007f, 0.22f * dim, 0.38f * dim, 1.05f * dim);
    }
    spoke(g->player.lane, 0.09f, 0.92f, 0.013f, 0.45f, 1.35f, 1.7f);
    if (energy_tune()->energize_time > 0.0f) {
        int lane;
        float pulse = 0.6f + 0.4f * sinf(flicker_time * 16.0f);
        for (lane = 0; lane < STARPOST_LANES; lane++) {
            float k;
            if (g->energize_left[lane] <= 0.0f)
                continue;
            k = g->energize_left[lane] / energy_tune()->energize_time;
            if (k > 1.0f)
                k = 1.0f;
            spoke(lane, 0.09f, 0.92f, 0.012f + 0.016f * k, 0.75f * pulse * k, 1.55f * pulse,
                  1.75f * pulse);
        }
    }

    for (int i = 0; i < g->shield_max && i < STARPOST_LANES; i++) {
        float a = 1.5707963f - ((float)i + 0.5f) * (6.2831853f / (float)STARPOST_LANES);
        float x0 = cosf(a) * 0.115f;
        float y0 = sinf(a) * 0.115f;
        float x1 = cosf(a) * 0.155f;
        float y1 = sinf(a) * 0.155f;
        int on = i < g->shield;
        int fresh = on && g->repair_flash > 0.0f && i == g->shield - 1;
        if (fresh)
            stroke(x0, y0, x1, y1, 0.02f, 1.6f, 1.9f, 1.4f);
        else if (on)
            stroke(x0, y0, x1, y1, 0.011f, 0.55f, 1.35f, 1.55f);
        else
            stroke(x0, y0, x1, y1, 0.007f, 0.28f, 0.08f, 0.12f);
    }

    draw_shots(g);
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive)
            continue;
        if (g->enemies[i].kind == ENEMY_FLYER)
            draw_flyer(&g->enemies[i]);
        else if (g->enemies[i].kind == ENEMY_SPLITTER || g->enemies[i].kind == ENEMY_SPLIT)
            draw_splitter(&g->enemies[i]);
        else
            draw_walker(&g->enemies[i]);
    }
    draw_shards();
    draw_tracer(g);

    batch = &hud_batch;
    draw_hud(g);
}

static void finish_frame(int width, int height)
{
    float sx, sy;
    view_scale(width, height, &sx, &sy);
    present(width, height, sx, sy);
}

void render_redraw(const GameState *g, int width, int height)
{
    build_scene(g);
    finish_frame(width, height);
}

void render_draw(const GameState *g, int width, int height, float dt)
{
    if (dt > 0.0f)
        flicker_time += dt;
    observe(g, dt);
    render_redraw(g, width, height);
}

void render_session(const struct Session *session, int width, int height, float dt)
{
    const Session *s = session;
    if (!s)
        return;
    if (s->screen != SCREEN_TITLE)
        title_snap_ready = 0;
    if (s->screen == SCREEN_PLAY) {
        render_draw(&s->game, width, height, dt);
        return;
    }
    if (dt > 0.0f)
        flicker_time += dt;
    if (s->screen == SCREEN_PAUSE) {
        build_scene(&s->game);
        draw_pause_menu(s);
        finish_frame(width, height);
        return;
    }
    if (s->screen == SCREEN_TITLE)
        draw_title(s, dt);
    else if (s->screen == SCREEN_DIFFICULTY)
        draw_difficulty(s);
    else if (s->screen == SCREEN_SCORES)
        draw_scores(s);
    else
        draw_initials(s);
    finish_frame(width, height);
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
