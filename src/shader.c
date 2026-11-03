#include "main.h"

GLuint shader_load(const char* vert_path, const char* frag_path);

static char* read_file(const char* path) {
    size_t size = 0;
    char* buf = (char*)asset_read(path, &size);
    if (!buf) fprintf(stderr, "Could not open file: %s\n", path);
    return buf;
}

static GLuint compile(GLenum type, const char* path) {
    char* src = read_file(path);
    if (!src) return 0;

    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, (const GLchar* const*)&src, NULL);  // scary cast but it works
    glCompileShader(s);
    free(src);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; 
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "Shader compile error in %s:\n%s\n", path, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint shader_load(const char* vert_path, const char* frag_path) {
    GLuint vs = compile(GL_VERTEX_SHADER, vert_path);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag_path);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; 
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        fprintf(stderr, "Shader link error:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}