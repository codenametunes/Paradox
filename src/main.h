#pragma once
#include <glad/gl.h>
#include <stdbool.h>
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <limits.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

#define MAX_SOUNDS 128
#define PI 3.14159265f  // camera.c has its own PI behind an ifndef

enum BlockId {
    BLOCK_AIR = 0,
    BLOCK_STONE,
    BLOCK_BEDROCK,
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_SAND,
    BLOCK_PLANKS,
    BLOCK_LOG,
    BLOCK_LEAVES,
    BLOCK_GLASS,
    BLOCK_BRICK,
    BLOCK_SNOW,
    BLOCK_WATER,
    BLOCK_LAVA,
    BLOCK_TYPES
};

enum BlockFace {
    BLOCK_FACE_FRONT = 0,
    BLOCK_FACE_BACK,
    BLOCK_FACE_LEFT,
    BLOCK_FACE_RIGHT,
    BLOCK_FACE_TOP,
    BLOCK_FACE_BOTTOM,
    BLOCK_FACE_COUNT
};

#define BLOCK_SOUND_VARIANTS 4  // 4 slots but SOUND_RAW only fills 3, the last one stays NULL

extern int snd_break[BLOCK_TYPES][BLOCK_SOUND_VARIANTS];
extern int snd_place[BLOCK_TYPES][BLOCK_SOUND_VARIANTS];
extern int snd_walk[BLOCK_TYPES][BLOCK_SOUND_VARIANTS];
extern int snd_break_n[BLOCK_TYPES];
extern int snd_place_n[BLOCK_TYPES];
extern int snd_walk_n[BLOCK_TYPES];

static inline int snd_pick(const int* ids, int n)
{
    return n > 0 ? ids[rand() % n] : -1;
}

typedef struct BlockDefinition {
    int id;
    const char* name;  // TODO: block list, size, height settings go here
    const char* display_name;
    const char* texture[BLOCK_FACE_COUNT];

    const char* walk_sounds[BLOCK_SOUND_VARIANTS];
    const char* break_sounds[BLOCK_SOUND_VARIANTS];
    const char* place_sounds[BLOCK_SOUND_VARIANTS];

    int breakable;
    int transparent;
    int cull_same;
} BlockDefinition;

#define TEX_ALL(path) \
    { path, path, path, path, path, path }

#define TEX_TOP_BOTTOM_SIDE(top, bottom, side) \
    { side, side, side, side, top, bottom }

#define SOUND_RAW(name) { \
    "assets/sounds/" name "1.wav", \
    "assets/sounds/" name "2.wav", \
    "assets/sounds/" name "3.wav" \
}

#define SOUND_VARIANTS(set, action) SOUND_RAW(set "_" action)

#define SOUND_SET_WALK(name)  .walk_sounds  = SOUND_RAW(name)
#define SOUND_SET_BREAK(name) .break_sounds = SOUND_RAW(name)
#define SOUND_SET_PLACE(name) .place_sounds = SOUND_RAW(name)

#define SOUND_SET(name, kind) SOUND_SET_##kind(name)

#define SOUND_ALL(set) \
    .walk_sounds  = SOUND_VARIANTS(set, "walk"), \
    .break_sounds = SOUND_VARIANTS(set, "break"), \
    .place_sounds = SOUND_VARIANTS(set, "place")

static const BlockDefinition BLOCK_DEFINITIONS[] = {
    {
        .id = BLOCK_AIR, .name = "air", .display_name = "Air",
        .texture = TEX_ALL(NULL),
        .breakable = 0
    },
    {
        .id = BLOCK_STONE, .name = "stone", .display_name = "Stone",
        .texture = TEX_ALL("assets/textures/StoneBlock.png"),
        SOUND_SET("stone_walk", WALK),
        SOUND_SET("stone_break", BREAK),
        SOUND_SET("stone_break", PLACE),
        .breakable = 1
    },
    {
        .id = BLOCK_BEDROCK, .name = "bedrock", .display_name = "Bedrock",
        .texture = TEX_ALL("assets/textures/Bedrock.png"),
        SOUND_SET("stone_walk", WALK),
        SOUND_SET("stone_break", BREAK),
        SOUND_SET("stone_break", PLACE),
        .breakable = 0
    },
    {
        .id = BLOCK_GRASS, .name = "grass", .display_name = "Grass Block",
        .texture = TEX_TOP_BOTTOM_SIDE(
            "assets/textures/GrassTop.png",
            "assets/textures/Dirt.png",
            "assets/textures/GrassSide.png"),

        SOUND_SET("grass_break", WALK),
        SOUND_SET("grass_break", BREAK),
        SOUND_SET("grass_break", PLACE),
        .breakable = 1
    },
    {
        .id = BLOCK_DIRT, .name = "dirt", .display_name = "Dirt",
        .texture = TEX_ALL("assets/textures/Dirt.png"),
        SOUND_SET("dirt_walk", WALK),
        SOUND_SET("dirt_walk", BREAK),
        SOUND_SET("dirt_walk", PLACE),
        .breakable = 1
    },
    {
        .id = BLOCK_SAND, .name = "sand", .display_name = "Sand",
        .texture = TEX_ALL("assets/textures/Sand.png"),
        SOUND_SET("sand_walk", WALK),
        SOUND_SET("sand_place", BREAK),
        SOUND_SET("sand_place", PLACE),
        .breakable = 1
    },
    {
        .id = BLOCK_PLANKS, .name = "planks", .display_name = "Oak Planks",
        .texture = TEX_ALL("assets/textures/OakPlanks.png"),
        SOUND_SET("wood_place", WALK),
        SOUND_SET("wood_place", BREAK),
        SOUND_SET("wood_place", PLACE),
        .breakable = 1
    },
    {
        .id = BLOCK_LOG, .name = "log", .display_name = "Oak Log",
        .texture = TEX_TOP_BOTTOM_SIDE(
            "assets/textures/OakLogTop.png",
            "assets/textures/OakLogTop.png",
            "assets/textures/OakLogSide.png"),

        SOUND_SET("wood_place", WALK),
        SOUND_SET("wood_place", BREAK),
        SOUND_SET("wood_place", PLACE),
        .breakable = 1
    },
    {
        .id = BLOCK_LEAVES, .name = "leaves", .display_name = "Oak Leaves",
        .texture = TEX_ALL("assets/textures/Leaves.png"),
        SOUND_SET("grass_break", WALK),
        SOUND_SET("grass_break", BREAK),
        SOUND_SET("grass_break", PLACE),
        .breakable = 1,
        .transparent = 1, .cull_same = 0
    },
    {
        .id = BLOCK_GLASS, .name = "glass", .display_name = "Glass",
        .texture = TEX_ALL("assets/textures/Glass.png"),
        SOUND_SET("stone_walk", WALK),
        SOUND_SET("glass_break", BREAK),
        SOUND_SET("stone_break", PLACE),
        .breakable = 1,
        .transparent = 1, .cull_same = 1
    },
    {
        .id = BLOCK_BRICK, .name = "brick", .display_name = "Red Bricks",
        .texture = TEX_ALL("assets/textures/Bricks.png"),
        SOUND_SET("stone_walk", WALK),
        SOUND_SET("stone_break", BREAK),
        SOUND_SET("stone_break", PLACE),
        .breakable = 1
    },
    {
        .id = BLOCK_SNOW, .name = "snow", .display_name = "Snow",  // me too lazy for dem snow shit
        .texture = TEX_ALL("assets/textures/Bricks.png"),
        SOUND_SET("stone_walk", WALK),
        SOUND_SET("stone_break", BREAK),
        SOUND_SET("stone_break", PLACE),
        .breakable = 1
    },
        {
        .id = BLOCK_WATER, .name = "water", .display_name = "Water",
        .texture = TEX_ALL("assets/textures/Fluids.png"),
        SOUND_SET("stone_walk", WALK),
        SOUND_SET("stone_break", BREAK),
        SOUND_SET("stone_break", PLACE),
        .breakable = 0,
        .transparent = 1, .cull_same = 0
    },
    {
        .id = BLOCK_LAVA, .name = "lava", .display_name = "Lava",
        .texture = TEX_ALL("assets/textures/Fluids.png"),
        SOUND_SET("stone_walk", WALK),
        SOUND_SET("stone_break", BREAK),
        SOUND_SET("stone_break", PLACE),
        .breakable = 0,
        .transparent = 1, .cull_same = 0
    }
};

typedef char block_definition_count_check[  // compile error if the table and the enum don't match
    (sizeof BLOCK_DEFINITIONS / sizeof BLOCK_DEFINITIONS[0] == BLOCK_TYPES)
        ? 1 : -1
];

static inline const BlockDefinition* block_definition(int id)
{
    if (id < 0 || id >= BLOCK_TYPES)
        return &BLOCK_DEFINITIONS[BLOCK_AIR];
    return &BLOCK_DEFINITIONS[id];
}

static inline int blocks_validate(void)
{
    int ok = 1;

    for (int i = 0; i < BLOCK_TYPES; i++) {
        if (BLOCK_DEFINITIONS[i].id != i) {
            fprintf(stderr, "blocks.h: '%s' is at index %d but has id %d\n",
                BLOCK_DEFINITIONS[i].name, i, BLOCK_DEFINITIONS[i].id);
            ok = 0;
        }
    }

    return ok;
}

typedef struct GLFWwindow GLFWwindow;

typedef struct Window {
    GLFWwindow* handle;
    int  width, height;
    bool vsync;
    bool fullscreen;

    int  saved_x, saved_y, saved_w, saved_h;
} Window;

#define UI_HOTBAR_SLOTS 9

typedef struct {
    unsigned int top;
    unsigned int side;
    unsigned int flat;
} UiItem;

enum { ITEM_DIAMOND_SWORD = BLOCK_TYPES };

int  audio_init(void);
int  audio_load(const char* path, float gain);
void audio_play(int id);
void audio_play_vol(int id, float volume);
void audio_play_3d(int id, double x, double y, double z, float volume);
void audio_set_listener(double x, double y, double z, float yaw_deg);
void audio_set_master(float volume);
float audio_get_master(void);
void audio_shutdown(void);
int audio_exists(const char* path);

enum { HAND_EMPTY = 0, HAND_STONE = 1 };
int  hand_init(const char* skin_path);
void hand_shutdown(void);
void hand_tick(double moved_x, double moved_z, double vel_x, double vel_y, double vel_z, int on_ground, float yaw_deg, float pitch_deg);
void hand_select(int item);
int hand_item(void);
void hand_swing(void);
void hand_frame(double dt);
void hand_use(void);
void hand_reject(void);
void hand_bob_matrix(double alpha, float* out);
int  hand_load_sword(const char* path);
void hand_draw(double alpha, float yaw_deg, float pitch_deg, const float* view, int fb_w, int fb_h, const GLuint* block_tex);
float hand_attack_time(void);

bool window_create(Window* w, int width, int height, const char* title);
void window_destroy(Window* w);
bool window_should_close(const Window* w);
void window_request_close(Window* w);
void window_swap_and_poll(Window* w);
void window_set_vsync(Window* w, bool on);
void window_toggle_fullscreen(Window* w);
void window_set_title(Window* w, const char* title);

typedef enum { MODE_SURVIVAL = 0, MODE_CREATIVE = 1 } GameMode;
extern GameMode game_mode;

int ui_init(const char* reticle_path);
void ui_resize(int fb_w, int fb_h);
void ui_draw(const UiItem* items, int count, int selected);

void  ui_set_stats(int health, int hunger, float xp, int level, int air);
void  ui_set_survival_hud(int on);
void  ui_set_hotbar_counts(const int* counts);
int   ui_gui_scale(void);
float ui_item_name_y(void);
void ui_shutdown(void);
void ui_icons_begin(void);
void ui_draw_item_px(const UiItem* item, float cx, float cy, float size);
void ui_icons_end(void);

typedef struct UiText {
    char* str;
    unsigned int tex;
    int          w, h;
    float        scale;
} UiText;

#define TEXTBUF_SIZE 256

typedef struct TextBuf {
    char buf[TEXTBUF_SIZE];
    int  len;
} TextBuf;

int   text_init(const char* ttf_path, int size_px);
void  text_shutdown(void);
void  text_resize(int w, int h);
void  text_set(UiText* t, const char* str);
float text_width(const UiText* t);
float text_height(const UiText* t);
void  text_draw(UiText* t, float x, float y, float r, float g, float b, float a, int shadow);
void  text_free(UiText* t);

void  textbuf_clear(TextBuf* b);
void  textbuf_add(TextBuf* b, unsigned cp);
void  textbuf_backspace(TextBuf* b);

unsigned char* asset_read(const char* path, size_t* size);
int            asset_exists(const char* path);
unsigned char* asset_image(const char* path, int* w, int* h, int* channels);

void hand_set_world_light(float brightness);