#include "main.h"
#include "stb_image.h"

extern const char* const asset_paths[];
extern const int         asset_ids[];
extern const int         asset_count;

static int find_id(const char* path)
{
    for (int i = 0; i < asset_count; i++)
        if (_stricmp(asset_paths[i], path) == 0)  // windows only
            return asset_ids[i];
    return 0;
}

unsigned char* asset_read(const char* path, size_t* out_size)
{
    unsigned char* buf = NULL;
    size_t size = 0;

    int id = find_id(path);
    HRSRC res = id ? FindResourceA(NULL, MAKEINTRESOURCEA(id), RT_RCDATA) : NULL;

    if (res) {
        HGLOBAL h = LoadResource(NULL, res);
        const void* p = h ? LockResource(h) : NULL;
        DWORD n = SizeofResource(NULL, res);

        if (p) {
            buf = (unsigned char*)malloc((size_t)n + 1);
            if (buf) {
                memcpy(buf, p, n);
                buf[n] = 0;  // extra zero so shader text is a valid string
                size = n;
            }
        }
    }
    else {
        FILE* f = NULL;
#ifdef _MSC_VER
        fopen_s(&f, path, "rb");
#else
        f = fopen(path, "rb");  // ???? what if the file isn't there
#endif
        if (f) {
            fseek(f, 0, SEEK_END);  // lazy way to get the file size
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (n >= 0) {
                buf = (unsigned char*)malloc((size_t)n + 1);
                if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
                    buf[n] = 0;  // extra zero so shader text is a valid string
                    size = (size_t)n;
                }
                else {
                    free(buf);
                    buf = NULL;
                }
            }
            fclose(f);
        }
    }

    if (!buf)
        fprintf(stderr, "asset NOT FOUND (not embedded, not on disk): %s\n", path);

    if (out_size) *out_size = size;
    return buf;
}

int asset_exists(const char* path)
{
    if (find_id(path))
        return 1;

    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");  // ???? what if the file isn't there
#endif
    if (f) { fclose(f); return 1; }
    return 0;
}

unsigned char* asset_image(const char* path, int* w, int* h, int* ch)
{
    size_t size = 0;
    unsigned char* file = asset_read(path, &size);

    if (!file)
        return NULL;

    unsigned char* px = stbi_load_from_memory(file, (int)size, w, h, ch, 4);
    free(file);
    return px;
}

void asset_debug_list(void)
{
    fprintf(stderr, "%d assets embedded in the exe\n", asset_count);
}