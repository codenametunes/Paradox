#include "main.h"
#include "camera.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

int world_block(int x, int y, int z);

float cam_pos[3] = { 32.0f, 12.5f, 42.0f };

float yaw = -90.0f;
float pitch = -20.0f;
int   view_mode = 0;

float proj[16];

static float fov_deg = FOV_BASE;
static int   vp_w = 1280;
static int   vp_h = 720;

void mat_perspective(float* m, float fovy, float aspect, float near_plane, float far_plane) {
    float t = 1.0f / tanf(fovy * 0.5f);

    memset(m, 0, 16 * sizeof(float));

    m[0] = t / aspect;
    m[5] = t;
    m[10] = (far_plane + near_plane) /
        (near_plane - far_plane);
    m[11] = -1.0f;
    m[14] = (2.0f * far_plane * near_plane) /
        (near_plane - far_plane);
}

void mat_look(float* m, const float* eye, const float* dir) {

    float fl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);  // ???? fl is 0 if dir is all zeros

    float f[3] = { dir[0] / fl, dir[1] / fl, dir[2] / fl };

    float s[3] = { -f[2], 0, f[0] };

    float sl = sqrtf(s[0] * s[0] + s[2] * s[2]);  // ???? sl is 0 when looking straight up/down, divide by zero next

    s[0] /= sl;
    s[2] /= sl;

    float u[3] = {
        s[1] * f[2] - s[2] * f[1],
        s[2] * f[0] - s[0] * f[2],
        s[0] * f[1] - s[1] * f[0]
    };

    memset(m, 0, 16 * sizeof(float));

    m[0] = s[0];
    m[4] = s[1];
    m[8] = s[2];

    m[1] = u[0];
    m[5] = u[1];
    m[9] = u[2];

    m[2] = -f[0];
    m[6] = -f[1];
    m[10] = -f[2];

    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);

    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);

    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]; // eyesore

    m[15] = 1.0f;
}

void mat_mul(float* out, const float* a, const float* b) {
    float result[16];

    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            float sum = 0.0f;

            for (int k = 0; k < 4; k++)
                sum += a[k * 4 + r] * b[c * 4 + k];

            result[c * 4 + r] = sum;
        }
    }
    memcpy(out, result, sizeof result);
}

static void update_proj(void)
{
    if (vp_w <= 0 || vp_h <= 0)
        return;

    float aspect = (float)vp_w / (float)vp_h;

    mat_perspective(proj, fov_deg * PI / 180.0f, aspect, 0.1f, 1000);
}

void camera_set_viewport(int w, int h)
{
    vp_w = w;
    vp_h = h;
    update_proj();
}

void camera_set_fov_modifier(double fov_mod)
{
    fov_deg = FOV_BASE * (float)fov_mod;
    update_proj();
}

void camera_on_mouse(GLFWwindow* window, double x, double y) {

    (void)window;

    static double last_x = 0.0;
    static double last_y = 0.0;
    static int first = 1;  // skip the first event or the camera jumps

    if (first) { last_x = x; last_y = y; first = 0; return; }

    float sensitivity = MOUSE_SENSITIVITY;

    yaw += (float)(x - last_x) * sensitivity;
    pitch += (float)(last_y - y) * sensitivity;

    last_x = x;
    last_y = y;

    if (pitch > 89.0f)  pitch = 89.0f;  // so you can't flip upside down
    if (pitch < -89.0f) pitch = -89.0f;
}

void camera_direction(float* dir)
{
    float yaw_r = yaw * PI / 180.0f;
    float pitch_r = pitch * PI / 180.0f;

    dir[0] = cosf(pitch_r) * cosf(yaw_r);
    dir[1] = sinf(pitch_r);
    dir[2] = cosf(pitch_r) * sinf(yaw_r);
}

void camera_resolve_view(const float* eye, const float* dir, float* cpos, float* vdir)
{
    for (int i = 0; i < 3; i++) {
        cpos[i] = eye[i];
        vdir[i] = dir[i];
    }

    if (!view_mode)
        return;

    float sgn = (view_mode == 1) ? -1.0f : 1.0f;
    float dist = 0.0f;

    while (dist < 4.0f) {  // back off 0.1 at a time until we hit a wall
        float s = dist + 0.1f;
        int b = world_block((int)floorf(eye[0] + dir[0] * sgn * s),
            (int)floorf(eye[1] + dir[1] * sgn * s),
            (int)floorf(eye[2] + dir[2] * sgn * s));
        if (b != BLOCK_AIR && b != BLOCK_WATER && b != BLOCK_LAVA)
            break;
        dist += 0.1f;
    }

    for (int i = 0; i < 3; i++)
        cpos[i] = eye[i] + dir[i] * sgn * dist;

    if (view_mode == 2)
        for (int i = 0; i < 3; i++)
            vdir[i] = -dir[i];
}