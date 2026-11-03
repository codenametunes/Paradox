#ifndef MENUS_H
#define MENUS_H

#include "main.h"  

// Only one menu can be open at a time. While any menu is open the cursor is
// free and the player's input belongs to the menu. Only the PAUSE menu freezes
// the game, the inventory leaves the world running (see menus_paused).
typedef enum {
    MENU_NONE = 0,
    MENU_PAUSE,        // Esc
    MENU_INVENTORY,    // E (creative inventory)
    MENU_DEATH         // "You died!": Respawn / Quit Game, nothing else closes it
} MenuState;

// Call after text_init(). `window` is used to lock/unlock the cursor.
// `inventory_tex_path` is e.g. "assets/textures/ui/allitems.png".
// The buttons are read from "assets/textures/ui/gui.png".
// Returns 0 if the shader failed; Esc/E still work, you just see no overlay.
int  menus_init(GLFWwindow* window, const char* inventory_tex_path);
void menus_shutdown(void);

void menus_resize(int fb_w, int fb_h);

// The creative inventory edits the real hotbar: `slots` is the same array the
// game uses (BLOCK_AIR = empty slot). `item_info` fills in the textures used to
// draw an item id (block or ITEM_*). Call once after menus_init().
typedef void (*MenuItemInfoFn)(int id, UiItem* out);
void menus_set_hotbar(int* slots, int count, MenuItemInfoFn item_info);

// Call from your GLFW mouse button callback BEFORE ignoring clicks while a menu is open.
// (pause menu: the buttons, inventory: slots + scrollbar; also handles button release)
void menus_mouse(int button, int action);

// Call from your GLFW scroll callback while a menu is open (scrolls the item list).
void menus_scroll(double yoffset);

// Returns 1 (once) when the inventory changed the hotbar, so the game can
// refresh the held item.
int  menus_hotbar_dirty(void);

// Call from your GLFW key callback (every key, every action).
void menus_key(int key, int action);

// death screen: menus_show_death() opens it, menus_take_respawn() returns 1 (once)
// after the player clicked Respawn
void menus_show_death(void);
int  menus_take_respawn(void);

// the creative inventory (E) can be switched off, survival has no item list
void menus_set_inventory_enabled(int on);

MenuState menus_state(void);
int       menus_open(void);   // 1 while any menu is showing = input belongs to the menu
int       menus_paused(void); // 1 only in the pause menu = freeze the world

// Call once per frame after the HUD and right before glfwSwapBuffers.
void menus_draw(void);

#endif