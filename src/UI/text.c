#include "main.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#define TEXT_CANVAS_W 640
#define TEXT_CANVAS_H 480
#define TEXT_PAD      2
#define TEXT_MAX_W    4096

static const char* text_vert =
"#version 330 core\n"
"layout (location = 0) in vec2 pos;\n"
"layout (location = 1) in vec2 uv;\n"
"uniform vec2 screen;\n"
"out vec2 vuv;\n"
"void main() {\n"
"    vuv = uv;\n"
"    gl_Position = vec4(pos.x / screen.x * 2.0 - 1.0, 1.0 - pos.y / screen.y * 2.0, 0.0, 1.0);\n"
"}\n";

static const char* text_frag =
"#version 330 core\n"
"in vec2 vuv;\n"
"uniform sampler2D tex;\n"
"uniform vec4 color;\n"
"out vec4 out_color;\n"
"void main() {\n"
"    float a = texture(tex, vuv).r * color.a;\n"
"    if (a < 0.004) discard;\n"
"    out_color = vec4(color.rgb, a);\n"
"}\n";

static FT_Library ft_lib = NULL;
static FT_Face    ft_face = NULL;
static unsigned char* font_data = NULL;
static int        base_px = 16;

static GLuint prog = 0, vao = 0, vbo = 0;
static GLint  screen_loc = -1, tex_loc = -1, color_loc = -1;

static int fb_w = 1280, fb_h = 720;

static float ui_scale(void)
{
    return (float)fb_h / (float)TEXT_CANVAS_H;
}

static GLuint compile_program(const char* vs, const char* fs)
{
    const char* src[2] = { vs, fs };
    GLenum type[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint sh[2];

    for (int i = 0; i < 2; i++) {
        sh[i] = glCreateShader(type[i]);
        glShaderSource(sh[i], 1, &src[i], NULL);
        glCompileShader(sh[i]);

        GLint ok;
        glGetShaderiv(sh[i], GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];  // hope the log fits
            glGetShaderInfoLog(sh[i], sizeof log, NULL, log);
            fprintf(stderr, "Text shader compilation failed:\n%s\n", log);
            glDeleteShader(sh[i]);
            if (i == 1) glDeleteShader(sh[0]);
            return 0;
        }
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, sh[0]);
    glAttachShader(p, sh[1]);
    glLinkProgram(p);
    glDeleteShader(sh[0]);
    glDeleteShader(sh[1]);

    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];  // hope the log fits
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "Text shader linking failed:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

int text_init(const char* ttf_path, int size_px)
{
    base_px = size_px > 0 ? size_px : 16;

    if (FT_Init_FreeType(&ft_lib)) {
        fprintf(stderr, "FreeType: init failed\n");
        return 0;
    }

    size_t font_size = 0;
    font_data = asset_read(ttf_path, &font_size);  // freetype keeps pointing at this, don't free it early

    if (!font_data || FT_New_Memory_Face(ft_lib, font_data, (FT_Long)font_size, 0, &ft_face)) {
        free(font_data);
        font_data = NULL;
        fprintf(stderr, "FreeType: could not load font '%s'\n", ttf_path);
        FT_Done_FreeType(ft_lib);
        ft_lib = NULL;
        return 0;
    }

    prog = compile_program(text_vert, text_frag);
    if (!prog) {
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_lib);
        ft_face = NULL;
        ft_lib = NULL;
        return 0;
    }

    screen_loc = glGetUniformLocation(prog, "screen");
    tex_loc = glGetUniformLocation(prog, "tex");
    color_loc = glGetUniformLocation(prog, "color");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 1;
}

void text_shutdown(void)
{
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (prog) glDeleteProgram(prog);
    vbo = vao = prog = 0;

    if (ft_face) FT_Done_Face(ft_face);
    if (ft_lib)  FT_Done_FreeType(ft_lib);
    free(font_data);
    font_data = NULL;
    ft_face = NULL;
    ft_lib = NULL;
}

void text_resize(int w, int h)
{
    fb_w = w > 0 ? w : 1;
    fb_h = h > 0 ? h : 1;
}

static unsigned next_cp(const char** s)
{
    const unsigned char* p = (const unsigned char*)*s;
    unsigned c = *p++;
    unsigned cp;
    int n;

    if (c < 0x80) { cp = c;        n = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
    else { *s = (const char*)p; return 0xFFFD; }

    while (n > 0) {
        if ((*p & 0xC0) != 0x80) {
            *s = (const char*)p;
            return 0xFFFD;
        }
        cp = (cp << 6) | (*p++ & 0x3F);
        n--;
    }

    *s = (const char*)p;
    return cp;
}

static void layout(const char* str, unsigned char* pixels, int tex_w, int tex_h,
    int asc, int line_h, int* out_w)
{
    int pen = 0, line = 0, max_w = 0;
    FT_UInt prev = 0;

    for (const char* s = str; *s; ) {
        unsigned cp = next_cp(&s);

        if (cp == '\n') {
            if (pen > max_w) max_w = pen;
            pen = 0;
            line++;
            prev = 0;
            continue;
        }

        FT_UInt gi = FT_Get_Char_Index(ft_face, cp);

        if (prev && FT_HAS_KERNING(ft_face)) {
            FT_Vector k;
            FT_Get_Kerning(ft_face, prev, gi, FT_KERNING_DEFAULT, &k);
            pen += (int)(k.x >> 6);
        }

        if (FT_Load_Glyph(ft_face, gi, pixels ? FT_LOAD_RENDER : FT_LOAD_DEFAULT)) {
            prev = 0;
            continue;
        }

        FT_GlyphSlot g = ft_face->glyph;

        if (pixels && g->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
            int base_y = asc + line * line_h;

            for (unsigned row = 0; row < g->bitmap.rows; row++) {
                int dy = base_y - g->bitmap_top + (int)row + TEXT_PAD;
                if (dy < 0 || dy >= tex_h) continue;

                for (unsigned col = 0; col < g->bitmap.width; col++) {
                    int dx = pen + g->bitmap_left + (int)col + TEXT_PAD;
                    if (dx < 0 || dx >= tex_w) continue;

                    unsigned char v = g->bitmap.buffer[row * g->bitmap.pitch + col];
                    unsigned char* d = &pixels[dy * tex_w + dx];
                    if (v > *d) *d = v;  // overlapping glyphs: keep the brighter one
                }
            }
        }

        pen += (int)((g->advance.x + 32) >> 6);
        prev = gi;
    }

    if (pen > max_w) max_w = pen;
    if (out_w) *out_w = max_w;
}

static int raster(UiText* t)
{
    float scale = ui_scale();
    int px = (int)((float)base_px * scale + 0.5f);
    if (px < 4) px = 4;

    t->scale = scale;

    if (FT_Set_Pixel_Sizes(ft_face, 0, (FT_UInt)px))
        return 0;

    FT_Size_Metrics* m = &ft_face->size->metrics;
    int asc = (int)((m->ascender + 63) >> 6);
    int desc = (int)((-m->descender + 63) >> 6);
    int line_h = (int)((m->height + 63) >> 6);

    int lines = 1;
    for (const char* c = t->str; *c; c++)
        if (*c == '\n') lines++;

    int text_w = 0;
    layout(t->str, NULL, 0, 0, asc, line_h, &text_w);

    int w = text_w + TEXT_PAD * 2;
    int h = (lines - 1) * line_h + asc + desc + TEXT_PAD * 2;
    if (w > TEXT_MAX_W) w = TEXT_MAX_W;  // ???? really long strings just get cut off
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    unsigned char* pixels = (unsigned char*)calloc((size_t)w * h, 1);
    if (!pixels)
        return 0;

    layout(t->str, pixels, w, h, asc, line_h, NULL);

    if (!t->tex)
        glGenTextures(1, &t->tex);

    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // 1 byte rows aren't 4 aligned, text comes out skewed without this
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glBindTexture(GL_TEXTURE_2D, 0);

    free(pixels);

    t->w = w;
    t->h = h;
    return 1;
}

void text_set(UiText* t, const char* str)
{
    if (!ft_face || !t)
        return;

    if (!str) str = "";

    if (t->str && strcmp(t->str, str) == 0)
        return;

    size_t n = strlen(str) + 1;
    char* copy = (char*)malloc(n);
    if (!copy)
        return;
    memcpy(copy, str, n);

    free(t->str);
    t->str = copy;

    if (!*str) {
        if (t->tex) glDeleteTextures(1, &t->tex);
        t->tex = 0;
        t->w = t->h = 0;
        return;
    }

    raster(t);
}

float text_width(const UiText* t)
{
    return (t && t->tex && t->scale > 0) ? (float)(t->w - TEXT_PAD * 2) / t->scale : 0.0f;
}

float text_height(const UiText* t)
{
    return (t && t->tex && t->scale > 0) ? (float)(t->h - TEXT_PAD * 2) / t->scale : 0.0f;
}

static void draw_quad(GLuint tex, float x, float y, float w, float h,
    float r, float g, float b, float a)
{
    float v[6 * 4] = {
        x,     y,     0, 0,
        x + w, y,     1, 0,
        x + w, y + h, 1, 1,
        x,     y,     0, 0,
        x + w, y + h, 1, 1,
        x,     y + h, 0, 1,
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_DYNAMIC_DRAW);

    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform4f(color_loc, r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void text_draw(UiText* t, float x, float y, float r, float g, float b, float a, int shadow)
{
    if (!prog || !t || !t->str || !t->str[0])
        return;

    float scale = ui_scale();

    if (t->scale != scale)
        raster(t);

    if (!t->tex)
        return;

    float origin_x = ((float)fb_w - TEXT_CANVAS_W * scale) * 0.5f;
    float sx = floorf(origin_x + x * scale + 0.5f) - TEXT_PAD;
    float sy = floorf(y * scale + 0.5f) - TEXT_PAD;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(prog);
    glUniform2f(screen_loc, (float)fb_w, (float)fb_h);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(tex_loc, 0);
    glBindVertexArray(vao);

    if (shadow) {
        float off = floorf(scale + 0.5f);
        if (off < 1.0f) off = 1.0f;
        draw_quad(t->tex, sx + off, sy + off, (float)t->w, (float)t->h,
            r * 0.25f, g * 0.25f, b * 0.25f, a);
    }

    draw_quad(t->tex, sx, sy, (float)t->w, (float)t->h, r, g, b, a);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void text_free(UiText* t)
{
    if (!t) return;
    if (t->tex) glDeleteTextures(1, &t->tex);
    free(t->str);
    *t = (UiText){ 0 };
}

void textbuf_clear(TextBuf* b)
{
    b->len = 0;
    b->buf[0] = '\0';
}

void textbuf_add(TextBuf* b, unsigned cp)
{
    if (cp < 32 || cp == 127)
        return;

    char enc[4];
    int n;

    if (cp < 0x80) { enc[0] = (char)cp; n = 1; }
    else if (cp < 0x800) {
        enc[0] = (char)(0xC0 | (cp >> 6));
        enc[1] = (char)(0x80 | (cp & 0x3F)); n = 2;
    }
    else if (cp < 0x10000) {
        enc[0] = (char)(0xE0 | (cp >> 12));
        enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        enc[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
    }
    else {
        enc[0] = (char)(0xF0 | (cp >> 18));
        enc[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        enc[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        enc[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
    }

    if (b->len + n >= TEXTBUF_SIZE)
        return;

    memcpy(b->buf + b->len, enc, (size_t)n);
    b->len += n;
    b->buf[b->len] = '\0';
}

void textbuf_backspace(TextBuf* b)
{
    if (b->len <= 0)
        return;

    b->len--;
    while (b->len > 0 && ((unsigned char)b->buf[b->len] & 0xC0) == 0x80)
        b->len--;

    b->buf[b->len] = '\0';
}