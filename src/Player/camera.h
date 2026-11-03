#ifndef CAMERA_H
#define CAMERA_H

struct GLFWwindow;

#define FOV_BASE 70.0f  // minecraft's normal fov

#define MOUSE_SENSITIVITY 0.15f  // 100% in minecraft

extern float cam_pos[3];
extern float yaw;
extern float pitch;
extern int   view_mode;
extern float proj[16];

void camera_set_viewport(int w, int h);
void camera_set_fov_modifier(double fov_mod);

void camera_on_mouse(struct GLFWwindow* window, double x, double y);

void camera_direction(float* dir);
void camera_resolve_view(const float* eye, const float* dir, float* cpos, float* vdir);

void mat_perspective(float* m, float fovy, float aspect, float near_plane, float far_plane);
void mat_look(float* m, const float* eye, const float* dir);
void mat_mul(float* out, const float* a, const float* b);

#endif