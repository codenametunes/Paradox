#include "main.h"

static void on_glfw_error(int code, const char* desc) {
    fprintf(stderr, "[GLFW %d] %s\n", code, desc);
}

static void on_framebuffer_size(GLFWwindow* h, int width, int height) {
    Window* w = glfwGetWindowUserPointer(h);
    if (w) { w->width = width; w->height = height; }
    glViewport(0, 0, width, height);
}

bool window_create(Window* w, int width, int height, const char* title) {
    *w = (Window){ 0 };

    glfwSetErrorCallback(on_glfw_error);
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 0);  // no MSAA

    w->handle = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!w->handle) { glfwTerminate(); return false; }

    glfwMakeContextCurrent(w->handle);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        fprintf(stderr, "Failed to load OpenGL functions\n");
        glfwDestroyWindow(w->handle);
        glfwTerminate();
        return false;
    }

    glfwSetWindowUserPointer(w->handle, w);
    glfwSetFramebufferSizeCallback(w->handle, on_framebuffer_size);

    glfwGetFramebufferSize(w->handle, &w->width, &w->height);
    glViewport(0, 0, w->width, w->height);

    window_set_vsync(w, true);

    printf("OpenGL   : %s\n", glGetString(GL_VERSION));
    printf("Renderer : %s\n", glGetString(GL_RENDERER));
    return true;
}

void window_destroy(Window* w) {
    if (w->handle) glfwDestroyWindow(w->handle);
    w->handle = NULL;
    glfwTerminate();
}

bool window_should_close(const Window* w) { return glfwWindowShouldClose(w->handle); }
void window_request_close(Window* w) { glfwSetWindowShouldClose(w->handle, GLFW_TRUE); }
void window_swap_and_poll(Window* w) { glfwSwapBuffers(w->handle); glfwPollEvents(); }

void window_set_vsync(Window* w, bool on) {
    w->vsync = on;
    glfwSwapInterval(on ? 1 : 0);
}

void window_set_title(Window* w, const char* title) {
    glfwSetWindowTitle(w->handle, title);
}

void window_toggle_fullscreen(Window* w) {
    if (!w->fullscreen) {
        glfwGetWindowPos(w->handle, &w->saved_x, &w->saved_y);
        glfwGetWindowSize(w->handle, &w->saved_w, &w->saved_h);
        GLFWmonitor* m = glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = glfwGetVideoMode(m);  // ???? NULL if the monitor goes away
        glfwSetWindowMonitor(w->handle, m, 0, 0, vm->width, vm->height, vm->refreshRate);  // ???? m could be NULL too
    }
    else {
        glfwSetWindowMonitor(w->handle, NULL, w->saved_x, w->saved_y,
            w->saved_w, w->saved_h, 0);
    }
    w->fullscreen = !w->fullscreen;
    window_set_vsync(w, w->vsync);  // vsync can reset after a mode change
}