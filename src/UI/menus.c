#include "main.h"
#include "menus.h"
#include "stb_image.h"   // no #define STB_IMAGE_IMPLEMENTATION here

// MENUS
//
//   Esc  (in game)      -> pause menu: game freezes, cursor unlocks, screen darkens
//                          two buttons from gui.png: "Back to Game" / "Quit Game"
//   Esc  (pause menu)   -> resume          Q (pause menu) -> quit
//   E    (in game)      -> creative inventory (allitems.png). The game keeps
//                          RUNNING behind it, only the input is taken over.
//   E / Esc (inventory) -> close it
//
// menus_paused()  = 1 only in the pause menu  -> main.c freezes the world
// menus_open()    = 1 in any menu             -> main.c ignores mouse/keys for the player

// how much the world is darkened behind a menu (0 = not at all, 1 = black)
#define MENU_DIM_ALPHA   0.55f

#define GUI_TEX_PATH     "assets/textures/ui/gui.png"

// gui.png (pixels from the top left)
#define BTN_W            200
#define BTN_H            20
#define BTN_Y_DISABLED   46
#define BTN_Y_NORMAL     66
#define BTN_Y_HOVER      86

// allitems.png: 176x208 panel, 8 columns x 9 rows of slots, scrollbar on the
// right and the 9 slot hotbar row at the bottom
#define INV_PANEL_W      176
#define INV_PANEL_H      208
#define INV_COLS         8
#define INV_ROWS         9
#define INV_GRID_X       8
#define INV_GRID_Y       18
#define INV_HOTBAR_COLS  9
#define INV_HOTBAR_Y     184
#define INV_PITCH        18
#define INV_SLOT         16

// scrollbar: track inside the panel, thumb sprite lives under the panel in the sheet
#define SCROLL_X         156
#define SCROLL_Y         18
#define SCROLL_W         12
#define SCROLL_H         160
#define THUMB_SX         2
#define THUMB_SY         209
#define THUMB_W          12
#define THUMB_H          15

// shader
// vertices are in framebuffer pixels (0,0 = top left)

static const char* menu_vert =
"#version 330 core\n"
"layout (location = 0) in vec2 pos;\n"
"layout (location = 1) in vec2 uv;\n"
"uniform vec2 screen;\n"
"out vec2 vuv;\n"
"void main() {\n"
"    vuv = uv;\n"
"    gl_Position = vec4(pos.x / screen.x * 2.0 - 1.0, 1.0 - pos.y / screen.y * 2.0, 0.0, 1.0);\n"
"}\n";

static const char* menu_frag =
"#version 330 core\n"
"in vec2 vuv;\n"
"uniform sampler2D tex;\n"
"uniform vec4 tint;\n"
"out vec4 out_color;\n"
"void main() {\n"
"    vec4 c = texture(tex, vuv) * tint;\n"
"    if (c.a < 0.004) discard;\n"
"    out_color = c;\n"
"}\n";

static GLFWwindow* win = NULL;
static MenuState   state = MENU_NONE;

static GLuint prog = 0, vao = 0, vbo = 0;
static GLint  screen_loc = -1, tex_loc = -1, tint_loc = -1;

static GLuint white_tex = 0;                 // 1x1 white, used for the dark overlay
static GLuint inv_tex = 0;                   // allitems.png
static int    inv_w = 0, inv_h = 0;
static GLuint gui_tex = 0;                   // gui.png (buttons)
static int    gui_w = 0, gui_h = 0;

static int fb_w = 1280, fb_h = 720;

static UiText pause_title;
static UiText btn_resume_text;
static UiText btn_quit_text;
static UiText death_title;
static UiText btn_respawn_text;
static int    respawn_flag = 0;
static int    inventory_enabled = 1;
static UiText tip_text;                      // item name next to the cursor
static UiText inv_title;                     // "Item selection" on the panel

static int* hotbar = NULL;                   // the game's real hotbar
static int            hotbar_n = 0;
static MenuItemInfoFn item_info = NULL;
static int            carried = 0;           // item on the cursor (0 = nothing)
static int            hotbar_changed = 0;

static float scroll_pos = 0.0f;              // 0..1 (top..bottom of the item list)
static int   dragging = 0;                   // holding the scrollbar thumb

// setup helpers

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
            char log[1024];
            glGetShaderInfoLog(sh[i], sizeof log, NULL, log);
            fprintf(stderr, "Menu shader compilation failed:\n%s\n", log);
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
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "Menu shader linking failed:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static GLuint make_texture(int w, int h, const unsigned char* rgba)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // keep pixel art sharp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// loads a whole sheet; returns 0 (and prints why) when the file is missing
static GLuint load_sheet(const char* path, int* out_w, int* out_h)
{
    stbi_set_flip_vertically_on_load(1);   // same convention as ui.c: v = 1 is the top

    int w, h, channels;
    unsigned char* data = asset_image(path, &w, &h, &channels);

    if (!data) {
        fprintf(stderr, "Menus: could not load '%s': %s\n", path, stbi_failure_reason());
        *out_w = *out_h = 0;
        return 0;
    }

    GLuint tex = make_texture(w, h, data);
    stbi_image_free(data);

    *out_w = w;
    *out_h = h;
    return tex;
}

int menus_init(GLFWwindow* window, const char* inventory_tex_path)
{
    win = window;
    state = MENU_NONE;

    prog = compile_program(menu_vert, menu_frag);
    if (!prog)
        return 0;

    screen_loc = glGetUniformLocation(prog, "screen");
    tex_loc = glGetUniformLocation(prog, "tex");
    tint_loc = glGetUniformLocation(prog, "tint");

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

    static const unsigned char white[4] = { 255, 255, 255, 255 };
    white_tex = make_texture(1, 1, white);

    inv_tex = load_sheet(inventory_tex_path, &inv_w, &inv_h);
    gui_tex = load_sheet(GUI_TEX_PATH, &gui_w, &gui_h);

    text_set(&pause_title, "Game Paused");
    text_set(&btn_resume_text, "Back to Game");
    text_set(&btn_quit_text, "Quit Game");
    text_set(&death_title, "You died!");
    text_set(&btn_respawn_text, "Respawn");
    text_set(&inv_title, "Item selection");

    return 1;
}

void menus_shutdown(void)
{
    text_free(&pause_title);
    text_free(&btn_resume_text);
    text_free(&btn_quit_text);
    text_free(&death_title);
    text_free(&btn_respawn_text);
    text_free(&tip_text);
    text_free(&inv_title);

    if (inv_tex)   glDeleteTextures(1, &inv_tex);
    if (gui_tex)   glDeleteTextures(1, &gui_tex);
    if (white_tex) glDeleteTextures(1, &white_tex);
    if (vbo)       glDeleteBuffers(1, &vbo);
    if (vao)       glDeleteVertexArrays(1, &vao);
    if (prog)      glDeleteProgram(prog);

    inv_tex = gui_tex = white_tex = vbo = vao = prog = 0;
    win = NULL;
    state = MENU_NONE;
}

void menus_resize(int w, int h)
{
    fb_w = w > 0 ? w : 1;
    fb_h = h > 0 ? h : 1;
}

// state

MenuState menus_state(void) { return state; }
int       menus_open(void) { return state != MENU_NONE; }
int       menus_paused(void) { return state == MENU_PAUSE; }

void menus_set_inventory_enabled(int on) { inventory_enabled = on != 0; }

int menus_take_respawn(void)
{
    int r = respawn_flag;
    respawn_flag = 0;
    return r;
}

static void set_state(MenuState s)
{
    if (s == state)
        return;

    int was_open = state != MENU_NONE;
    state = s;
    dragging = 0;

    if (!win)
        return;

    if (s != MENU_NONE && !was_open) {
        // free the cursor and put it in the middle of the window
        int w, h;
        glfwGetWindowSize(win, &w, &h);
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursorPos(win, w * 0.5, h * 0.5);
    }
    else if (s == MENU_NONE) {
        carried = 0;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

void menus_show_death(void)
{
    respawn_flag = 0;
    set_state(MENU_DEATH);
}

void menus_key(int key, int action)
{
    if (action != GLFW_PRESS)
        return;

    switch (state) {
    case MENU_NONE:
        if (key == GLFW_KEY_ESCAPE)  set_state(MENU_PAUSE);
        else if (key == GLFW_KEY_E && inventory_enabled)  set_state(MENU_INVENTORY);
        break;

    case MENU_DEATH:       // only the buttons leave this screen
        break;

    case MENU_PAUSE:
        if (key == GLFW_KEY_ESCAPE)  set_state(MENU_NONE);
        else if (key == GLFW_KEY_Q && win) glfwSetWindowShouldClose(win, GLFW_TRUE);
        break;

    case MENU_INVENTORY:
        if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_E)
            set_state(MENU_NONE);
        break;
    }
}

// creative inventory: layout and picking

void menus_set_hotbar(int* slots, int count, MenuItemInfoFn fn)
{
    hotbar = slots;
    hotbar_n = count;
    item_info = fn;
}

int menus_hotbar_dirty(void)
{
    int d = hotbar_changed;
    hotbar_changed = 0;
    return d;
}

// how many item ids the grid offers (every block, then the extra items)
static int item_count(void)
{
    return ITEM_DIAMOND_SWORD;                // ids 1..ITEM_DIAMOND_SWORD
}

// rows that don't fit on screen at once = how far the list can scroll
static int max_scroll_rows(void)
{
    int rows = (item_count() + INV_COLS - 1) / INV_COLS;
    return rows > INV_ROWS ? rows - INV_ROWS : 0;
}

static int first_row(void)
{
    return (int)(scroll_pos * (float)max_scroll_rows() + 0.5f);
}

// item shown in grid slot `index` (0 = empty slot), taking the scroll into account
static int grid_item(int index)
{
    int id = 1 + first_row() * INV_COLS + index;     // skip BLOCK_AIR
    return id <= ITEM_DIAMOND_SWORD ? id : 0;
}

// whole number gui scale (same one the HUD uses) and top left of the panel
static int panel_rect(int* s_out, float* x_out, float* y_out)
{
    if (!inv_tex)
        return 0;

    int s = ui_gui_scale();

    *s_out = s;
    *x_out = floorf(((float)fb_w - (float)(INV_PANEL_W * s)) * 0.5f);
    *y_out = floorf(((float)fb_h - (float)(INV_PANEL_H * s)) * 0.5f);
    return 1;
}

// top left of a slot on screen; row -1 = the hotbar row
static void slot_pos(int s, float px, float py, int col, int row, float* x, float* y)
{
    *x = px + (float)((INV_GRID_X + col * INV_PITCH) * s);
    *y = py + (float)((row < 0 ? INV_HOTBAR_Y : INV_GRID_Y + row * INV_PITCH) * s);
}

static void cursor_px(float* mx, float* my)
{
    double cx = 0, cy = 0;
    int ww = 1, wh = 1;

    if (win) {
        glfwGetCursorPos(win, &cx, &cy);
        glfwGetWindowSize(win, &ww, &wh);
    }
    if (ww < 1) ww = 1;
    if (wh < 1) wh = 1;

    *mx = (float)cx * (float)fb_w / (float)ww;
    *my = (float)cy * (float)fb_h / (float)wh;
}

// what is under the cursor: 1 = grid slot, 2 = hotbar slot, 0 = nothing
static int pick_slot(int* index)
{
    int s; float px, py;
    if (!panel_rect(&s, &px, &py))
        return 0;

    float mx, my;
    cursor_px(&mx, &my);

    for (int row = -1; row < INV_ROWS; row++) {
        int cols = row < 0 ? INV_HOTBAR_COLS : INV_COLS;

        for (int col = 0; col < cols; col++) {
            float x, y;
            slot_pos(s, px, py, col, row, &x, &y);

            if (mx >= x && mx < x + INV_SLOT * s && my >= y && my < y + INV_SLOT * s) {
                if (row < 0) {
                    if (col >= hotbar_n) return 0;
                    *index = col;
                    return 2;
                }
                *index = row * INV_COLS + col;
                return 1;
            }
        }
    }
    return 0;
}

// scrollbar

static int over_scroll_track(int s, float px, float py)
{
    float mx, my;
    cursor_px(&mx, &my);

    float x = px + (float)(SCROLL_X * s);
    float y = py + (float)(SCROLL_Y * s);

    return mx >= x && mx < x + SCROLL_W * s && my >= y && my < y + SCROLL_H * s;
}

// puts the middle of the thumb under the cursor
static void scroll_from_cursor(int s, float py)
{
    if (max_scroll_rows() <= 0) {
        scroll_pos = 0.0f;
        return;
    }

    float mx, my;
    cursor_px(&mx, &my);

    float top = py + (float)(SCROLL_Y * s) + (float)(THUMB_H * s) * 0.5f;
    float range = (float)((SCROLL_H - THUMB_H) * s);
    float f = (my - top) / range;

    scroll_pos = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

// mouse wheel while the inventory is open: one row per notch
void menus_scroll(double yoffset)
{
    int m = max_scroll_rows();

    if (state != MENU_INVENTORY || m <= 0 || yoffset == 0.0)
        return;

    int row = first_row() + (yoffset > 0.0 ? -1 : 1);
    if (row < 0) row = 0;
    if (row > m) row = m;

    scroll_pos = (float)row / (float)m;
}

// pause menu buttons

// gui scale and top left of the two buttons: 0 = Back to Game, 1 = Quit Game
static void pause_buttons(int* s_out, float* x_out, float* y_out)
{
    int s = ui_gui_scale();
    float x = floorf(((float)fb_w - (float)(BTN_W * s)) * 0.5f);
    float y = floorf((float)fb_h * 0.5f - (float)(24 * s));

    *s_out = s;
    *x_out = x;
    y_out[0] = y;
    y_out[1] = y + (float)(24 * s);          // 20 tall + 4 gap
}

static int pick_button(void)
{
    if (!gui_tex)
        return -1;

    int s; float bx, by[2];
    pause_buttons(&s, &bx, by);

    float mx, my;
    cursor_px(&mx, &my);

    for (int i = 0; i < 2; i++)
        if (mx >= bx && mx < bx + BTN_W * s && my >= by[i] && my < by[i] + BTN_H * s)
            return i;

    return -1;
}

// clicks:
//  pause menu : the two buttons
//  inventory  : click a block to pick it up, click a hotbar slot to put it there
//               (swaps with whatever the slot held), click anywhere else to drop it,
//               click / drag the scrollbar to scroll
void menus_mouse(int button, int action)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;

    if (action == GLFW_RELEASE) {
        dragging = 0;
        return;
    }

    if (action != GLFW_PRESS)
        return;

    if (state == MENU_DEATH) {
        int b = pick_button();

        if (b == 0) {
            respawn_flag = 1;
            set_state(MENU_NONE);
        }
        else if (b == 1 && win) glfwSetWindowShouldClose(win, GLFW_TRUE);
        return;
    }

    if (state == MENU_PAUSE) {
        int b = pick_button();

        if (b == 0)                set_state(MENU_NONE);
        else if (b == 1 && win)    glfwSetWindowShouldClose(win, GLFW_TRUE);
        return;
    }

    if (state != MENU_INVENTORY)
        return;

    int s; float px, py;
    if (panel_rect(&s, &px, &py) && over_scroll_track(s, px, py)) {
        dragging = 1;
        scroll_from_cursor(s, py);
        return;
    }

    int index = 0;
    int where = pick_slot(&index);

    if (where == 1) {
        carried = grid_item(index);
    }
    else if (where == 2 && hotbar) {
        int old = hotbar[index];
        hotbar[index] = carried;
        carried = old;
        hotbar_changed = 1;
    }
    else {
        carried = 0;
    }
}

// drawing

// one textured quad in framebuffer pixels. v0 is the top edge of the image.
static void draw_quad(GLuint tex, float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    float r, float g, float b, float a)
{
    float v[6 * 4] = {
        x,     y,     u0, v0,
        x + w, y,     u1, v0,
        x + w, y + h, u1, v1,
        x,     y,     u0, v0,
        x + w, y + h, u1, v1,
        x,     y + h, u0, v1,
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_DYNAMIC_DRAW);

    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform4f(tint_loc, r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// the pixel rect (sx, sy, sw, sh) of a sheet (from its top left), drawn at x, y with size w, h
static void draw_sheet(GLuint tex, int tw, int th, int sx, int sy, int sw, int sh,
    float x, float y, float w, float h,
    float r, float g, float b, float a)
{
    draw_quad(tex, x, y, w, h,
        (float)sx / (float)tw, 1.0f - (float)sy / (float)th,
        (float)(sx + sw) / (float)tw, 1.0f - (float)(sy + sh) / (float)th,
        r, g, b, a);
}

// framebuffer pixels -> the 640x480 canvas text.c works in
static void to_canvas(float px, float py, float* cx, float* cy)
{
    float k = (float)fb_h / 480.0f;
    float ox = ((float)fb_w - 640.0f * k) * 0.5f;

    *cx = (px - ox) / k;
    *cy = py / k;
}

static void draw_button_label(UiText* t, float bx, float by, int s, int hover)
{
    float k = (float)fb_h / 480.0f;
    float cx, cy;

    to_canvas(bx + (float)(BTN_W * s) * 0.5f, by + (float)(BTN_H * s) * 0.5f, &cx, &cy);

    float x = cx - text_width(t) * 0.5f;
    float y = cy - text_height(t) * 0.5f;
    (void)k;

    if (hover) text_draw(t, x, y, 1.0f, 1.0f, 0.63f, 1.0f, 1);   // yellow, like vanilla
    else       text_draw(t, x, y, 0.88f, 0.88f, 0.88f, 1.0f, 1);
}

void menus_draw(void)
{
    if (state == MENU_NONE || !prog)
        return;

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);   // F4 wireframe must not touch the menu
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(prog);
    glUniform2f(screen_loc, (float)fb_w, (float)fb_h);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(tex_loc, 0);
    glBindVertexArray(vao);

    // dark background, shared by every menu
    if (state == MENU_DEATH)   // red tint, like the vanilla game over screen
        draw_quad(white_tex, 0.0f, 0.0f, (float)fb_w, (float)fb_h,
            0.0f, 0.0f, 1.0f, 1.0f,
            0.31f, 0.0f, 0.0f, 0.63f);
    else
        draw_quad(white_tex, 0.0f, 0.0f, (float)fb_w, (float)fb_h,
            0.0f, 0.0f, 1.0f, 1.0f,
            0.03f, 0.03f, 0.05f, MENU_DIM_ALPHA);

    int hover_button = -1;

    if ((state == MENU_PAUSE || state == MENU_DEATH) && gui_tex) {
        int s; float bx, by[2];
        pause_buttons(&s, &bx, by);
        hover_button = pick_button();

        for (int i = 0; i < 2; i++)
            draw_sheet(gui_tex, gui_w, gui_h, 0, i == hover_button ? BTN_Y_HOVER : BTN_Y_NORMAL,
                BTN_W, BTN_H, bx, by[i], (float)(BTN_W * s), (float)(BTN_H * s),
                1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (state == MENU_INVENTORY && inv_tex) {
        int s; float x, y;
        panel_rect(&s, &x, &y);

        // dragging the thumb: follow the cursor while the button is held
        if (dragging) {
            if (win && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
                scroll_from_cursor(s, y);
            else
                dragging = 0;
        }

        draw_sheet(inv_tex, inv_w, inv_h, 0, 0, INV_PANEL_W, INV_PANEL_H,
            x, y, (float)(INV_PANEL_W * s), (float)(INV_PANEL_H * s),
            1.0f, 1.0f, 1.0f, 1.0f);

        // scrollbar thumb (dimmed when there is nothing to scroll)
        {
            float t = max_scroll_rows() > 0 ? scroll_pos : 0.0f;
            float tx = x + (float)(SCROLL_X * s);
            float ty = y + (float)(SCROLL_Y * s) + floorf(t * (float)((SCROLL_H - THUMB_H) * s));
            float dim = max_scroll_rows() > 0 ? 1.0f : 0.6f;

            draw_sheet(inv_tex, inv_w, inv_h, THUMB_SX, THUMB_SY, THUMB_W, THUMB_H,
                tx, ty, (float)(THUMB_W * s), (float)(THUMB_H * s),
                dim, dim, dim, 1.0f);
        }

        // hover highlight
        int hover_i = 0;
        int hover_kind = pick_slot(&hover_i);
        if (hover_kind) {
            float hx, hy;
            if (hover_kind == 2) slot_pos(s, x, y, hover_i, -1, &hx, &hy);
            else                 slot_pos(s, x, y, hover_i % INV_COLS, hover_i / INV_COLS, &hx, &hy);
            draw_quad(white_tex, hx, hy, (float)(INV_SLOT * s), (float)(INV_SLOT * s),
                0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.45f);
        }

        // item icons: the visible part of the list (grid), the real hotbar (bottom row), the carried item
        if (item_info) {
            float icon = (float)(INV_SLOT * s);   // fills the slot interior exactly
            float half = (float)(INV_SLOT * s) * 0.5f;
            UiItem it;

            ui_icons_begin();

            for (int i = 0; i < INV_COLS * INV_ROWS; i++) {
                int id = grid_item(i);
                if (!id) break;
                float sx, sy;
                slot_pos(s, x, y, i % INV_COLS, i / INV_COLS, &sx, &sy);
                it = (UiItem){ 0 };
                item_info(id, &it);
                ui_draw_item_px(&it, sx + half, sy + half, icon);
            }

            for (int i = 0; hotbar && i < hotbar_n && i < INV_HOTBAR_COLS; i++) {
                if (hotbar[i] == BLOCK_AIR) continue;
                float sx, sy;
                slot_pos(s, x, y, i, -1, &sx, &sy);
                it = (UiItem){ 0 };
                item_info(hotbar[i], &it);
                ui_draw_item_px(&it, sx + half, sy + half, icon);
            }

            if (carried) {
                float mx, my;
                cursor_px(&mx, &my);
                it = (UiItem){ 0 };
                item_info(carried, &it);
                ui_draw_item_px(&it, mx, my, icon);
            }

            ui_icons_end();
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    // pause text goes through text.c (640x480 canvas coordinates)
    if (state == MENU_PAUSE || state == MENU_DEATH) {
        int s; float bx, by[2];
        pause_buttons(&s, &bx, by);

        UiText* title = state == MENU_DEATH ? &death_title : &pause_title;
        UiText* first = state == MENU_DEATH ? &btn_respawn_text : &btn_resume_text;

        float cx, cy;
        to_canvas((float)fb_w * 0.5f, by[0] - (float)(14 * s), &cx, &cy);
        text_draw(title, cx - text_width(title) * 0.5f, cy - text_height(title),
            1.0f, 1.0f, 1.0f, 1.0f, 1);

        if (gui_tex) {
            draw_button_label(first, bx, by[0], s, hover_button == 0);
            draw_button_label(&btn_quit_text, bx, by[1], s, hover_button == 1);
        }
    }

    // panel title, top left like Release 1.0
    if (state == MENU_INVENTORY && inv_tex) {
        int s; float px, py, cx, cy;
        panel_rect(&s, &px, &py);
        to_canvas(px + (float)(8 * s), py + (float)(6 * s), &cx, &cy);
        text_draw(&inv_title, cx, cy, 0.25f, 0.25f, 0.25f, 1.0f, 0);
    }

    // item name next to the cursor
    if (state == MENU_INVENTORY && !carried) {
        int i = 0;
        int where = pick_slot(&i);
        int id = where == 1 ? grid_item(i) : (where == 2 && hotbar) ? hotbar[i] : 0;

        if (id) {
            const char* name = id == ITEM_DIAMOND_SWORD ? "Diamond Sword" : block_definition(id)->display_name;
            text_set(&tip_text, name);

            float mx, my;
            cursor_px(&mx, &my);
            float k = (float)fb_h / 480.0f;                       // pixels -> 640x480 canvas
            float ox = ((float)fb_w - 640.0f * k) * 0.5f;
            float tx = (mx - ox) / k + 8.0f;
            float ty = my / k - 16.0f;

            // black semi transparent box behind the text
            float pad = 3.0f * k;
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(prog);
            glUniform2f(screen_loc, (float)fb_w, (float)fb_h);
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(tex_loc, 0);
            glBindVertexArray(vao);
            draw_quad(white_tex, floorf(ox + tx * k - pad), floorf(ty * k - pad),
                text_width(&tip_text) * k + pad * 2.0f, text_height(&tip_text) * k + pad * 2.0f,
                0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.7f);
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
            glEnable(GL_DEPTH_TEST);

            text_draw(&tip_text, tx, ty, 1.0f, 1.0f, 1.0f, 1.0f, 1);
        }
    }
}