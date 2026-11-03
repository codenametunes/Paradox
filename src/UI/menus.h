#ifndef MENUS_H
#define MENUS_H

#include "main.h"

typedef enum {
    MENU_NONE = 0,
    MENU_PAUSE,
    MENU_INVENTORY,
    MENU_DEATH
} MenuState;

int  menus_init(GLFWwindow* window, const char* inventory_tex_path);
void menus_shutdown(void);

void menus_resize(int fb_w, int fb_h);

typedef void (*MenuItemInfoFn)(int id, UiItem* out);
void menus_set_hotbar(int* slots, int count, MenuItemInfoFn item_info);

void menus_mouse(int button, int action);

void menus_scroll(double yoffset);

int  menus_hotbar_dirty(void);

void menus_key(int key, int action);

void menus_show_death(void);
int  menus_take_respawn(void);

void menus_set_inventory_enabled(int on);

MenuState menus_state(void);
int       menus_open(void);
int       menus_paused(void);

void menus_draw(void);

#endif