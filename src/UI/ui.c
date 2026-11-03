#include "main.h"
#include "stb_image.h"   // no #define STB_IMAGE_IMPLEMENTATION here
// canvas the UI is laid out on, centered in the window
#define UI_CANVAS_W 640
#define UI_CANVAS_H 480

// the reticle is drawn this many pixels tall (width follows the image's
// aspect ratio), no matter how big the PNG itself is
#define RETICLE_SIZE_PX 32

// used only for reticle images that have no real transparency: pixels
// within this distance of the corner color become transparent
#define COLOR_KEY_TOLERANCE 8

// the HUD sprites come from these sheets (see ui_draw for the pixel rects)
// block cubes use the Release 1.0 size inside a 16x16 slot (1.0 = same as the original)
#define ITEM_CUBE_SCALE 1.0f

#define GUI_TEX_PATH   "assets/textures/ui/gui.png"
#define ICONS_TEX_PATH "assets/textures/ui/icons.png"

// shaders
// vertices are in canvas pixels (0,0 = top left of the 640x480 canvas)

static const char* ui_vert =
"#version 330 core\n"
"layout (location = 0) in vec2 pos;\n"
"layout (location = 1) in vec2 uv;\n"
"uniform vec2 screen;\n"
"uniform vec2 origin;\n"
"uniform float ui_scale;\n"
"out vec2 vuv;\n"
"void main() {\n"
"    vuv = uv;\n"
"    vec2 p = pos * ui_scale + origin;\n"
"    gl_Position = vec4(p.x / screen.x * 2.0 - 1.0, 1.0 - p.y / screen.y * 2.0, 0.0, 1.0);\n"
"}\n";

static const char* ui_frag =
"#version 330 core\n"
"in vec2 vuv;\n"
"uniform sampler2D tex;\n"
"uniform vec3 tint;\n"
"out vec4 color;\n"
"void main() {\n"
"    vec4 c = texture(tex, vuv);\n"
"    if (c.a < 0.01) discard;\n"
"    color = vec4(c.rgb * tint, c.a);\n"
"}\n";

static const char* reticle_frag =
"#version 330 core\n"
"in vec2 vuv;\n"
"uniform sampler2D tex;\n"
"uniform sampler2D scene_tex;\n"
"out vec4 color;\n"
"void main() {\n"
"    vec4 reticle = texture(tex, vuv);\n"
"    if (reticle.a < 0.01) discard;\n"
"vec2 scene_size = vec2(textureSize(scene_tex, 0));\n"
"vec2 screen_uv = gl_FragCoord.xy / scene_size;\n"
"    vec3 scene = texture(scene_tex, screen_uv).rgb;\n"
"    color = vec4(vec3(1.0) - scene, reticle.a);\n"
"}\n";

static GLuint ui_prog = 0;
static GLuint ui_vao = 0;
static GLuint ui_vbo = 0;

static GLuint reticle_prog = 0;
static GLint reticle_scene_loc = -1;
static GLint reticle_screen_loc = -1;

static GLint screen_loc = -1;
static GLint origin_loc = -1;
static GLint scale_loc = -1;
static GLint tex_loc = -1;
static GLint tint_loc = -1;

static GLuint reticle_tex = 0;
static int reticle_w = 0;
static int reticle_h = 0;

static GLuint gui_tex = 0;         // gui.png: hotbar + selector
static int    gui_w = 0, gui_h = 0;
static GLuint icons_tex = 0;       // icons.png: hearts, hunger, bubbles, xp bar
static int    icons_w = 0, icons_h = 0;

// survival HUD values (template defaults: full health and hunger, empty xp)
static int   stat_health = 20;
static int   stat_hunger = 20;
static float stat_xp = 0.0f;
static int   stat_level = 0;
static int   stat_air = 10;
static UiText level_text;

static int    survival_hud = 1;                 // 0 = creative: no hearts / hunger / xp
static int    hb_counts[UI_HOTBAR_SLOTS];
static int    hb_counts_on = 0;
static UiText count_text[UI_HOTBAR_SLOTS];

static int screen_w = 1280;
static int screen_h = 720;

static GLuint scene_tex = 0;

static GLuint compile_program(const char* vs, const char* fs)
{
    const char* sources[2] = { vs, fs };
    GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint shaders[2];

    for (int i = 0; i < 2; i++) {
        shaders[i] = glCreateShader(types[i]);
        glShaderSource(shaders[i], 1, &sources[i], NULL);
        glCompileShader(shaders[i]);

        GLint ok;
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &ok);

        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(shaders[i], sizeof log, NULL, log);
            fprintf(stderr, "UI shader compilation failed:\n%s\n", log);

            glDeleteShader(shaders[i]);
            if (i == 1)
                glDeleteShader(shaders[0]);

            return 0;
        }
    }

    GLuint program = glCreateProgram();

    glAttachShader(program, shaders[0]);
    glAttachShader(program, shaders[1]);
    glLinkProgram(program);

    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "UI shader linking failed:\n%s\n", log);

        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(shaders[0]);
    glDeleteShader(shaders[1]);

    return program;
}

// uploads RGBA pixels as a texture, mipmapped when it gets shrunk a lot

static GLuint make_texture(int w, int h, const unsigned char* rgba, int mipmaps)
{
    GLuint tex;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mipmaps ? GL_LINEAR : GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    if (mipmaps)
        glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    return tex;
}

// RETICLE

static void load_reticle(const char* path)
{
    stbi_set_flip_vertically_on_load(1);

    int channels;
    unsigned char* data = asset_image(path, &reticle_w, &reticle_h, &channels);

    // not there? try the working directory
    if (!data)
        data = asset_image("Reticle.png", &reticle_w, &reticle_h, &channels);

    if (!data) {
        fprintf(stderr, "Failed to load reticle '%s': %s\n", path, stbi_failure_reason());
        return;
    }

    int count = reticle_w * reticle_h;

    // if every pixel is fully opaque the PNG has no usable alpha (e.g. it was
    // saved with a solid background), so treat the corner color as transparent

    int has_alpha = 0;

    for (int i = 0; i < count; i++) {
        if (data[i * 4 + 3] < 255) {
            has_alpha = 1;
            break;
        }
    }

    if (!has_alpha) {
        int kr = data[0], kg = data[1], kb = data[2];
        int keyed = 0;

        for (int i = 0; i < count; i++) {
            const unsigned char* px = &data[i * 4];

            int dr = px[0] - kr; if (dr < 0) dr = -dr;
            int dg = px[1] - kg; if (dg < 0) dg = -dg;
            int db = px[2] - kb; if (db < 0) db = -db;

            if (dr <= COLOR_KEY_TOLERANCE && dg <= COLOR_KEY_TOLERANCE && db <= COLOR_KEY_TOLERANCE)
                keyed++;
        }

        // never key away the whole image (e.g. a single solid color)
        if (keyed >= count) {
            fprintf(stderr, "Reticle is a single solid color, leaving it opaque\n");
        }
        else {
            fprintf(stderr, "Reticle has no transparency, using its corner color as transparent\n");

            for (int i = 0; i < count; i++) {
                unsigned char* px = &data[i * 4];

                int dr = px[0] - kr; if (dr < 0) dr = -dr;
                int dg = px[1] - kg; if (dg < 0) dg = -dg;
                int db = px[2] - kb; if (db < 0) db = -db;

                if (dr <= COLOR_KEY_TOLERANCE && dg <= COLOR_KEY_TOLERANCE && db <= COLOR_KEY_TOLERANCE)
                    px[3] = 0;
            }
        }
    }

    // report what ended up visible, handy when the reticle looks wrong
    int visible = 0;

    for (int i = 0; i < count; i++)
        if (data[i * 4 + 3] > 2)
            visible++;

    fprintf(stderr, "Reticle: %dx%d, %d channels in file, %d of %d pixels visible\n",
        reticle_w, reticle_h, channels, visible, count);

    reticle_tex = make_texture(reticle_w, reticle_h, data, 0);


    stbi_image_free(data);
}

// HUD SHEETS

// loads a whole PNG sheet, 0 when it is missing
static GLuint load_sheet(const char* path, int* out_w, int* out_h)
{
    stbi_set_flip_vertically_on_load(1);

    int w, h, channels;
    unsigned char* data = asset_image(path, &w, &h, &channels);

    if (!data) {
        fprintf(stderr, "UI: could not load '%s': %s\n", path, stbi_failure_reason());
        *out_w = *out_h = 0;
        return 0;
    }

    GLuint tex = make_texture(w, h, data, 0);
    stbi_image_free(data);

    *out_w = w;
    *out_h = h;
    return tex;
}

// DRAWING

// draws one textured quad, corners in order p0..p3 (two triangles)

static void draw_quad(GLuint tex, const float p[4][2], const float uv[4][2], float tr, float tg, float tb)
{
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };

    float v[6 * 4];

    for (int i = 0; i < 6; i++) {
        v[i * 4 + 0] = p[idx[i]][0];
        v[i * 4 + 1] = p[idx[i]][1];
        v[i * 4 + 2] = uv[idx[i]][0];
        v[i * 4 + 3] = uv[idx[i]][1];
    }

    glBindBuffer(GL_ARRAY_BUFFER, ui_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_DYNAMIC_DRAW);

    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform3f(tint_loc, tr, tg, tb);

    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// the PNGs are loaded flipped, so v = 1 is the top of the image

static void draw_rect(GLuint tex, float x, float y, float w, float h)
{
    float p[4][2] = { {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h} };
    float uv[4][2] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };

    draw_quad(tex, p, uv, 1.0f, 1.0f, 1.0f);
}
static void draw_reticle_rect(GLuint tex, float x, float y, float w, float h)
{
    float p[4][2] = {
        { x,     y     },
        { x + w, y     },
        { x + w, y + h },
        { x,     y + h }
    };

    float uv[4][2] = {
        { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 }
    };

    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    float v[6 * 4];

    for (int i = 0; i < 6; i++) {
        v[i * 4 + 0] = p[idx[i]][0];
        v[i * 4 + 1] = p[idx[i]][1];
        v[i * 4 + 2] = uv[idx[i]][0];
        v[i * 4 + 3] = uv[idx[i]][1];
    }

    glBindBuffer(GL_ARRAY_BUFFER, ui_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_DYNAMIC_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}


// small isometric cube: top face plus the two visible sides
//
//          T
//       UL   UR
//          C
//       LL   LR
//          B

static void draw_item_icon(const UiItem* item, float cx, float cy, float k)
{
    // Release 1.0 proportions, measured from its screenshots (k = slot size / 16,
    // so one k is one gui pixel): the cube is 14 wide and 15 tall, the side
    // edges are 8 long and the top corner sits 3.5 below the top. The slope of
    // every edge is 1:2 (3.5 over 7), so the three faces are parallelograms.
    // It hangs half a pixel above the slot centre, which keeps it inside the
    // 16x16 interior with the same margins as the original.
    const float a = 7.0f * k;     // half width
    const float h = 7.5f * k;     // centre to top / bottom point
    const float s = 4.0f * k;     // centre to the side corners (up / down)
    const float cc = 0.5f * k;     // centre point sits this far above the middle

    cy -= 0.5f * k;

    float T[2] = { cx,     cy - h };
    float UR[2] = { cx + a, cy - s };
    float LR[2] = { cx + a, cy + s };
    float B[2] = { cx,     cy + h };
    float LL[2] = { cx - a, cy + s };
    float UL[2] = { cx - a, cy - s };
    float C[2] = { cx,     cy - cc };

    // image corners: top-left, top-right, bottom-right, bottom-left
    float uv[4][2] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };

    float top[4][2] = { {UL[0], UL[1]}, {T[0], T[1]},   {UR[0], UR[1]}, {C[0], C[1]} };
    float left[4][2] = { {UL[0], UL[1]}, {C[0], C[1]},   {B[0], B[1]},   {LL[0], LL[1]} };
    float right[4][2] = { {C[0], C[1]},   {UR[0], UR[1]}, {LR[0], LR[1]}, {B[0], B[1]} };

    // face brightness measured from Release 1.0: left 0.65, right 0.44 of the top
    draw_quad(item->top, top, uv, 1.00f, 1.00f, 1.00f);
    draw_quad(item->side, left, uv, 0.65f, 0.65f, 0.65f);
    draw_quad(item->side, right, uv, 0.44f, 0.44f, 0.44f);
}

int ui_init(const char* reticle_path)
{
    ui_prog = compile_program(ui_vert, ui_frag);
    reticle_prog = compile_program(ui_vert, reticle_frag);

    if (!ui_prog || !reticle_prog)
        return 0;

    reticle_scene_loc = glGetUniformLocation(reticle_prog, "scene_tex");
    reticle_screen_loc = glGetUniformLocation(reticle_prog, "screen");

    if (!ui_prog)
        return 0;

    screen_loc = glGetUniformLocation(ui_prog, "screen");
    origin_loc = glGetUniformLocation(ui_prog, "origin");
    tex_loc = glGetUniformLocation(ui_prog, "tex");
    tint_loc = glGetUniformLocation(ui_prog, "tint");
    scale_loc = glGetUniformLocation(ui_prog, "ui_scale");
    glGenVertexArrays(1, &ui_vao);
    glGenBuffers(1, &ui_vbo);

    glBindVertexArray(ui_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ui_vbo);

    glGenTextures(1, &scene_tex);
    glBindTexture(GL_TEXTURE_2D, scene_tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8,
        screen_w, screen_h, 0,
        GL_RGB, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // position: attribute 0
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // uv: attribute 1
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    gui_tex = load_sheet(GUI_TEX_PATH, &gui_w, &gui_h);
    icons_tex = load_sheet(ICONS_TEX_PATH, &icons_w, &icons_h);

    load_reticle(reticle_path);

    return 1;
}

void ui_resize(int fb_w, int fb_h)
{
    screen_w = fb_w > 0 ? fb_w : 1;
    screen_h = fb_h > 0 ? fb_h : 1;

    if (scene_tex) {
        glBindTexture(GL_TEXTURE_2D, scene_tex);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8,
            screen_w, screen_h, 0,
            GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// HUD API

// whole number scale, like Minecraft's "GUI scale": the HUD is laid out on a
// 320x240 grid and every sprite pixel becomes this many screen pixels
int ui_gui_scale(void)
{
    int s = screen_h / 240;
    int sw = screen_w / 320;

    if (sw < s) s = sw;
    return s < 1 ? 1 : s;
}

// top of the item name above the HUD, in 640x480 canvas pixels (what text_draw takes)
float ui_item_name_y(void)
{
    float k = (float)screen_h / (float)UI_CANVAS_H;
    return ((float)screen_h - 59.0f * (float)ui_gui_scale()) / k;
}

void ui_set_survival_hud(int on) { survival_hud = on != 0; }

void ui_set_hotbar_counts(const int* counts)
{
    hb_counts_on = counts != NULL;

    for (int i = 0; i < UI_HOTBAR_SLOTS; i++)
        hb_counts[i] = counts ? counts[i] : 0;
}

void ui_set_stats(int health, int hunger, float xp, int level, int air)
{
    stat_health = health < 0 ? 0 : health > 20 ? 20 : health;
    stat_hunger = hunger < 0 ? 0 : hunger > 20 ? 20 : hunger;
    stat_xp = xp < 0.0f ? 0.0f : xp > 1.0f ? 1.0f : xp;
    stat_level = level < 0 ? 0 : level;
    stat_air = air < 0 ? 0 : air > 10 ? 10 : air;
}

// the pixel rect (sx, sy, sw, sh) of a sheet (from its top left) drawn at x, y with size w, h
// in framebuffer pixels
static void draw_sprite(GLuint tex, int tw, int th, float sx, float sy, float sw, float sh,
    float x, float y, float w, float h)
{
    float u0 = sx / (float)tw, u1 = (sx + sw) / (float)tw;
    float v0 = 1.0f - sy / (float)th, v1 = 1.0f - (sy + sh) / (float)th;

    float p[4][2] = { {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h} };
    float uv[4][2] = { {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1} };

    draw_quad(tex, p, uv, 1.0f, 1.0f, 1.0f);
}

// icons.png: every heart / drumstick / bubble is a 9x9 cell
//   row y=0   hearts   : container x=0, full x=36, half x=45
//   row y=18  breath   : bubble x=0, popped x=9
//   row y=27  hunger   : container x=0, full x=36, half x=45
//   y=36      xp bar background (182x5),  y=41  xp bar fill
static void draw_icon_cell(float sx, float sy, float x, float y, float g)
{
    draw_sprite(icons_tex, icons_w, icons_h, sx, sy, 9.0f, 9.0f, x, y, 9.0f * g, 9.0f * g);
}

// bar_x / bar_y = top left of the hotbar in pixels, g = gui scale
static void draw_hud_stats(float bar_x, float bar_y, float g)
{
    if (!icons_tex)
        return;

    float xp_y = bar_y - 7.0f * g;        // 29 gui pixels above the bottom
    float row_y = bar_y - 17.0f * g;      // hearts + hunger, 39 above the bottom
    float air_y = bar_y - 27.0f * g;      // bubbles, 49 above the bottom

    // xp bar
    draw_sprite(icons_tex, icons_w, icons_h, 0, 36, 182, 5, bar_x, xp_y, 182.0f * g, 5.0f * g);

    int fill = (int)(stat_xp * 183.0f);
    if (fill > 182) fill = 182;
    if (fill > 0)
        draw_sprite(icons_tex, icons_w, icons_h, 0, 41, (float)fill, 5, bar_x, xp_y, (float)fill * g, 5.0f * g);

    for (int i = 0; i < 10; i++) {
        // hearts: left to right, 2 health points each
        float hx = bar_x + (float)(i * 8) * g;
        draw_icon_cell(0, 0, hx, row_y, g);

        if (stat_health >= i * 2 + 2)      draw_icon_cell(36, 0, hx, row_y, g);
        else if (stat_health == i * 2 + 1) draw_icon_cell(45, 0, hx, row_y, g);

        // hunger: right to left
        float fx = bar_x + (float)(173 - i * 8) * g;
        draw_icon_cell(0, 27, fx, row_y, g);

        if (stat_hunger >= i * 2 + 2)      draw_icon_cell(36, 27, fx, row_y, g);
        else if (stat_hunger == i * 2 + 1) draw_icon_cell(45, 27, fx, row_y, g);

        // breath: only while air is missing, right to left above the hunger
        if (stat_air < 10) {
            if (i < stat_air)       draw_icon_cell(0, 18, fx, air_y, g);
            else if (i == stat_air) draw_icon_cell(9, 18, fx, air_y, g);
        }
    }
}

void ui_draw(const UiItem* items, int count, int selected)
{
    if (!ui_prog)
        return;

    if (!items || count < 0)
        count = 0;

    if (count > UI_HOTBAR_SLOTS)
        count = UI_HOTBAR_SLOTS;

    // the UI sits on top of everything, quads can face either way
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(ui_prog);

    // The reticle still lives on the 640x480 canvas (scaled by screen height)
    float scale = (float)screen_h / (float)UI_CANVAS_H;
    float origin_x = ((float)screen_w - UI_CANVAS_W * scale) * 0.5f;
    float origin_y = 0.0f;

    // The hotbar and stats are drawn straight in framebuffer pixels
    glUniform2f(screen_loc, (float)screen_w, (float)screen_h);
    glUniform2f(origin_loc, 0.0f, 0.0f);
    glUniform1f(scale_loc, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glUniform1i(tex_loc, 0);

    glBindVertexArray(ui_vao);

    // Capture the 3D scene before drawing any UI elements.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_tex);

    glCopyTexSubImage2D(
        GL_TEXTURE_2D, 0,
        0, 0, 0, 0,
        screen_w, screen_h
    );

    glBindTexture(GL_TEXTURE_2D, 0);

    // hotbar: 182x22 sprite, bottom center of the window.
    // slots are 20 sprite pixels apart, the 16x16 item sits 3 pixels in.

    int   gs = ui_gui_scale();
    float g = (float)gs;
    float bar_x = floorf(((float)screen_w - 182.0f * g) * 0.5f);
    float bar_y = (float)screen_h - 22.0f * g;

    if (gui_tex) {
        draw_sprite(gui_tex, gui_w, gui_h, 0, 0, 182, 22, bar_x, bar_y, 182.0f * g, 22.0f * g);

        // selector: 24x24, one pixel bigger than the slot on every side
        if (selected >= 0 && selected < UI_HOTBAR_SLOTS)
            draw_sprite(gui_tex, gui_w, gui_h, 0, 22, 24, 24,
                bar_x + (float)(selected * 20 - 1) * g, bar_y - g, 24.0f * g, 24.0f * g);
    }

    for (int i = 0; i < count; i++) {
        float icx = bar_x + (float)(3 + i * 20 + 8) * g;
        float icy = bar_y + 11.0f * g;
        float size = 16.0f * g;

        // flat items (sword): the sprite itself, like the game's item icons
        if (items[i].flat)
            draw_rect(items[i].flat, icx - size * 0.5f, icy - size * 0.5f, size, size);
        // an empty slot (no textures) has no icon
        else if (items[i].top || items[i].side)
            draw_item_icon(&items[i], icx, icy, size * ITEM_CUBE_SCALE / 16.0f);
    }

    if (survival_hud)
        draw_hud_stats(bar_x, bar_y, g);

    // reticle: center of the canvas

    if (reticle_tex && reticle_prog) {
        float px_h = (float)RETICLE_SIZE_PX;
        float px_w = px_h * (float)reticle_w / (float)reticle_h;

        float x = (float)UI_CANVAS_W * 0.5f - px_w * 0.5f;
        float y = (float)UI_CANVAS_H * 0.5f - px_h * 0.5f;

        glUseProgram(reticle_prog);

        glUniform2f(
            glGetUniformLocation(reticle_prog, "origin"),
            origin_x,
            origin_y
        );

        glUniform1f(
            glGetUniformLocation(reticle_prog, "ui_scale"),
            scale
        );

        glUniform2f(
            reticle_screen_loc,
            (float)screen_w,
            (float)screen_h
        );

        glUniform1i(
            glGetUniformLocation(reticle_prog, "tex"), 0
        );

        glUniform1i(reticle_scene_loc, 1);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, reticle_tex);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, scene_tex);

        draw_reticle_rect(reticle_tex, x, y, px_w, px_h);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);

        glActiveTexture(GL_TEXTURE0);
        glUseProgram(ui_prog);
    }


    // restore the 3D state
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    // xp level: green number centered above the xp bar (text.c, canvas coordinates)
    if (stat_level > 0) {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", stat_level);
        text_set(&level_text, buf);

        float cx = (float)UI_CANVAS_W * 0.5f - text_width(&level_text) * 0.5f;
        float cy = ((float)screen_h - 37.0f * g) / scale;
        text_draw(&level_text, cx, cy, 0.5f, 1.0f, 0.13f, 1.0f, 1);
    }

    // stack sizes: bottom right of the slot, only for stacks of 2 or more (like the game)
    if (hb_counts_on) {
        for (int i = 0; i < count; i++) {
            if (hb_counts[i] < 2)
                continue;

            char buf[16];
            snprintf(buf, sizeof buf, "%d", hb_counts[i]);
            text_set(&count_text[i], buf);

            float right = bar_x + (float)(3 + i * 20 + 17) * g;     // item x + 17
            float bottom = bar_y + (3.0f + 17.0f) * g;              // item y + 17
            float cx = (right - origin_x) / scale - text_width(&count_text[i]);
            float cy = bottom / scale - text_height(&count_text[i]);
            text_draw(&count_text[i], cx, cy, 1.0f, 1.0f, 1.0f, 1.0f, 1);
        }
    }
}

// ICONS IN FRAMEBUFFER PIXELS (used by the inventory menu)
// ui_icons_begin(); ui_draw_item_px(...) any number of times; ui_icons_end();

void ui_icons_begin(void)
{
    if (!ui_prog)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(ui_prog);
    glUniform2f(screen_loc, (float)screen_w, (float)screen_h);
    glUniform2f(origin_loc, 0.0f, 0.0f);   // vertices are already in pixels
    glUniform1f(scale_loc, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(tex_loc, 0);
    glBindVertexArray(ui_vao);
}

// size = width of the icon in pixels (centered on cx, cy)
void ui_draw_item_px(const UiItem* item, float cx, float cy, float size)
{
    if (!ui_prog || !item)
        return;

    if (item->flat)
        draw_rect(item->flat, cx - size * 0.5f, cy - size * 0.5f, size, size);
    else if (item->top || item->side)
        draw_item_icon(item, cx, cy, size * ITEM_CUBE_SCALE / 16.0f);
}

void ui_icons_end(void)
{
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void ui_shutdown(void)
{
    if (reticle_tex)  glDeleteTextures(1, &reticle_tex);
    if (gui_tex)      glDeleteTextures(1, &gui_tex);
    if (icons_tex)    glDeleteTextures(1, &icons_tex);
    text_free(&level_text);
    for (int i = 0; i < UI_HOTBAR_SLOTS; i++)
        text_free(&count_text[i]);
    if (ui_vbo)       glDeleteBuffers(1, &ui_vbo);
    if (ui_vao)       glDeleteVertexArrays(1, &ui_vao);
    if (ui_prog)      glDeleteProgram(ui_prog);
    if (scene_tex)    glDeleteTextures(1, &scene_tex);
    if (reticle_prog) glDeleteProgram(reticle_prog);

    reticle_tex = gui_tex = icons_tex = scene_tex = 0;
    ui_vbo = ui_vao = ui_prog = reticle_prog = 0;
}