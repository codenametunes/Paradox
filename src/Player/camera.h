#ifndef CAMERA_H
#define CAMERA_H

// Camera: view direction, third person modes, projection, mouse look, matrices.

struct GLFWwindow;

// Minecraft's default "Normal" FOV (vertical, degrees); sprinting and
// flying widen it, see player_fov_modifier
#define FOV_BASE 70.0f

// Minecraft's default mouse sensitivity (100%) is 0.15 degrees per pixel
#define MOUSE_SENSITIVITY 0.15f

extern float cam_pos[3];     // eye position (written by player_eye_pos)
extern float yaw;            // degrees
extern float pitch;          // degrees
extern int   view_mode;      // 0 first person, 1 behind, 2 in front
extern float proj[16];       // projection matrix

// viewport / projection
void camera_set_viewport(int w, int h);           // call on init + resize
void camera_set_fov_modifier(double fov_mod);     // FOV_BASE * modifier, rebuilds proj

// input
void camera_on_mouse(struct GLFWwindow* window, double x, double y);   // cursor pos callback

// view
void camera_direction(float* dir);                // unit look vector from yaw / pitch
// resolves the actual camera for the current view_mode (pulls back in third
// person, stopping at walls). eye/dir in, cpos/vdir out.
void camera_resolve_view(const float* eye, const float* dir, float* cpos, float* vdir);

// matrices (column major)
void mat_perspective(float* m, float fovy, float aspect, float near_plane, float far_plane);
void mat_look(float* m, const float* eye, const float* dir);
void mat_mul(float* out, const float* a, const float* b);

#endif