#include "main.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
int stb_vorbis_decode_memory(const unsigned char* mem, int len,
    int* channels, int* sample_rate, short** output);

typedef struct {
    char           path[260];
    float          gain;
    unsigned char* data;  // 2 nibbles per byte, sky low / block high
    const short* pcm;
    int            frames, channels, rate;
} Sound;

static Sound sounds[MAX_SOUNDS];
static int   sound_count = 0;

static uint32_t rd32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const unsigned char* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

#define MIX_RATE    44100
#define MIX_FRAMES  512  // ~12ms per buffer
#define MIX_BUFFERS 4
#define MAX_VOICES  32
#define HEAR_RANGE  16.0f  // same as minecraft

typedef struct {
    const Sound* snd;
    double pos;
    float  gl, gr;
    int    active;
} Voice;

static Voice            voices[MAX_VOICES];
static CRITICAL_SECTION voice_lock;
static HWAVEOUT         wave = NULL;
static WAVEHDR          hdr[MIX_BUFFERS];
static short            mixbuf[MIX_BUFFERS][MIX_FRAMES * 2];
static HANDLE           mix_thread = NULL;
static HANDLE           mix_event = NULL;
static int              lock_ready = 0;
static volatile int     mixing = 0;
static volatile float   master_volume = 1.0f;
static double           listener_pos[3];
static float            listener_fx = 1.0f, listener_fz = 0.0f;

static int wav_info(const unsigned char* d, long long size, Sound* s)
{
    long long pos = 12;
    int tag = 0, ch = 0, bits = 0;
    int rate = 0;

    while (pos + 8 <= size) {
        long long csz = rd32(d + pos + 4);
        const unsigned char* body = d + pos + 8;

        if (memcmp(d + pos, "fmt ", 4) == 0 && pos + 8 + 16 <= size) {
            tag = rd16(body);
            ch = rd16(body + 2);
            rate = (int)rd32(body + 4);
            bits = rd16(body + 14);
        }
        else if (memcmp(d + pos, "data", 4) == 0) {
            long long avail = size - (pos + 8);
            long long n = csz < avail ? csz : avail;

            if ((tag != 1 && tag != 0xFFFE) || bits != 16 || (ch != 1 && ch != 2) || rate <= 0)
                return 0;

            s->pcm = (const short*)body;
            s->channels = ch;
            s->rate = rate;
            s->frames = (int)(n / (2 * ch));
            return s->frames > 0;
        }

        pos += 8 + csz + (csz & 1);
    }
    return 0;
}

static void mix_block(short* out)
{
    float acc[MIX_FRAMES * 2];
    memset(acc, 0, sizeof acc);

    EnterCriticalSection(&voice_lock);

    for (int vi = 0; vi < MAX_VOICES; vi++) {
        Voice* v = &voices[vi];
        if (!v->active) continue;

        const Sound* s = v->snd;
        double step = (double)s->rate / (double)MIX_RATE;

        for (int f = 0; f < MIX_FRAMES; f++) {
            int i = (int)v->pos;
            if (i >= s->frames) { v->active = 0; break; }

            int j = (i + 1 < s->frames) ? i + 1 : i;
            float fr = (float)(v->pos - i);
            float l, r;

            if (s->channels == 1) {
                float a = s->pcm[i], b = s->pcm[j];
                l = r = a + (b - a) * fr;
            }
            else {
                l = s->pcm[i * 2] + (s->pcm[j * 2] - s->pcm[i * 2]) * fr;
                r = s->pcm[i * 2 + 1] + (s->pcm[j * 2 + 1] - s->pcm[i * 2 + 1]) * fr;
            }

            acc[f * 2] += l * v->gl;
            acc[f * 2 + 1] += r * v->gr;
            v->pos += step;
        }
    }

    LeaveCriticalSection(&voice_lock);

    for (int k = 0; k < MIX_FRAMES * 2; k++) {
        float x = acc[k] * master_volume;
        x = 32767.0f * tanhf(x / 32767.0f);  // soft limit so loud stuff doesn't crackle
        out[k] = (short)x;
    }
}

static DWORD WINAPI mixer_thread(LPVOID arg)
{
    int next = 0;
    (void)arg;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (mixing) {
        WaitForSingleObject(mix_event, 20);  // wakes when the driver is done, 20ms is just a fallback

        while (mixing && (((volatile WAVEHDR*)&hdr[next])->dwFlags & WHDR_DONE)) {
            mix_block(mixbuf[next]);
            waveOutWrite(wave, &hdr[next], sizeof hdr[next]);
            next = (next + 1) % MIX_BUFFERS;
        }
    }
    return 0;
}

static int prepare_wav(unsigned char* d, long long size, float gain, const char* path)
{
    if (size < 44 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "Sound '%s' is not a valid .wav file (.ogg/.mp3 are not supported)\n", path);
        return 0;
    }

    int have_fmt = 0;
    int pcm16 = 0;
    long long pos = 12;

    while (pos + 8 <= size) {
        long long csz = rd32(d + pos + 4);
        unsigned char* body = d + pos + 8;

        if (memcmp(d + pos, "fmt ", 4) == 0 && pos + 8 + 16 <= size) {
            unsigned tag = rd16(body);
            unsigned bits = rd16(body + 14);

            have_fmt = 1;
            pcm16 = (tag == 1 || tag == 0xFFFE) && bits == 16;
        }
        else if (memcmp(d + pos, "data", 4) == 0) {
            if (!have_fmt) {
                fprintf(stderr, "Sound '%s' has no format chunk\n", path);
                return 0;
            }

            long long avail = size - (pos + 8);
            long long n = csz < avail ? csz : avail;

            if (gain < 0.999f) {
                if (pcm16) {
                    for (long long i = 0; i + 1 < n; i += 2) {
                        int16_t s = (int16_t)(body[i] | (body[i + 1] << 8));
                        s = (int16_t)((float)s * gain);
                        body[i] = (unsigned char)(s & 0xFF);
                        body[i + 1] = (unsigned char)((s >> 8) & 0xFF);
                    }
                }
                else {
                    fprintf(stderr, "Sound '%s' is not 16 bit PCM, playing at full volume\n", path);
                }
            }

            return 1;
        }

        pos += 8 + csz + (csz & 1);
    }

    fprintf(stderr, "Sound '%s' has no audio data\n", path);
    return 0;
}

int audio_init(void)
{
    memset(sounds, 0, sizeof sounds);
    sound_count = 0;
    memset(voices, 0, sizeof voices);
    if (!lock_ready) { InitializeCriticalSection(&voice_lock); lock_ready = 1; }
    if (wave) return 1;

    mix_event = CreateEvent(NULL, FALSE, FALSE, NULL);  // ???? never checked

    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = MIX_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = MIX_RATE * 4;

    if (waveOutOpen(&wave, WAVE_MAPPER, &fmt, (DWORD_PTR)mix_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Could not open the audio device, there will be no sound\n");
        wave = NULL;
        CloseHandle(mix_event);
        mix_event = NULL;
        return 0;
    }

    memset(mixbuf, 0, sizeof mixbuf);
    memset(hdr, 0, sizeof hdr);
    for (int i = 0; i < MIX_BUFFERS; i++) {
        hdr[i].lpData = (LPSTR)mixbuf[i];
        hdr[i].dwBufferLength = sizeof mixbuf[i];
        waveOutPrepareHeader(wave, &hdr[i], sizeof hdr[i]);
        waveOutWrite(wave, &hdr[i], sizeof hdr[i]);
    }

    mixing = 1;
    mix_thread = CreateThread(NULL, 0, mixer_thread, NULL, 0, NULL);  // ???? never checked, no thread = silence forever
    return 1;
}
static int sound_resolve(const char* path, char* out, size_t n)
{
    char base[260];
    size_t len = strlen(path);

    if (len >= sizeof base)
        return 0;

    memcpy(base, path, len + 1);

    if (len > 4 &&
        (strcmp(base + len - 4, ".wav") == 0 ||
            strcmp(base + len - 4, ".ogg") == 0 ||
            strcmp(base + len - 4, ".mp3") == 0))
        base[len - 4] = '\0';

    static const char* ext[3] = { ".ogg", ".wav", ".mp3" };

    for (int i = 0; i < 3; i++) {
        snprintf(out, n, "%s%s", base, ext[i]);

        if (asset_exists(out))
            return 1;
    }

    return 0;
}

int audio_exists(const char* path)
{
    char tmp[260];
    return path && sound_resolve(path, tmp, sizeof tmp);
}
int audio_load(const char* path, float gain)
{
    if (!path)
        return -1;

    char real[260];

    if (!sound_resolve(path, real, sizeof real)) {
        fprintf(stderr, "Sound '%s' not found (looked for .ogg, .wav and .mp3)\n", path);
        return -1;
    }

    path = real;

    for (int i = 0; i < sound_count; i++)
        if (strcmp(sounds[i].path, path) == 0 && sounds[i].gain == gain)
            return i;

    if (sound_count >= MAX_SOUNDS) {
        fprintf(stderr, "Too many sounds, can't load '%s'\n", path);
        return -1;
    }

    size_t asize = 0;
    unsigned char* data = asset_read(path, &asize);
    long size = (long)asize;  // ???? breaks above 2GB, fine for sounds

    if (!data) {
        fprintf(stderr, "Failed to open sound '%s'\n", path);
        return -1;
    }

    if (size < 44) {
        fprintf(stderr, "Sound '%s' is too small to be a sound file\n", path);
        free(data);
        return -1;
    }

    Sound* s = &sounds[sound_count];
    memset(s, 0, sizeof * s);

    if (size >= 4 && memcmp(data, "OggS", 4) == 0) {
        int ch = 0, rate = 0;
        short* pcm = NULL;

        int frames = stb_vorbis_decode_memory(
            data, (int)size, &ch, &rate, &pcm
        );

        free(data);

        if (frames <= 0 || !pcm || ch < 1 || ch > 2) {
            fprintf(stderr, "Sound '%s' could not be decoded\n", path);
            free(pcm);
            return -1;
        }

        if (gain < 0.999f)
            for (int i = 0; i < frames * ch; i++)
                pcm[i] = (short)((float)pcm[i] * gain);

        data = (unsigned char*)pcm;
        s->pcm = pcm;
        s->frames = frames;
        s->channels = ch;
        s->rate = rate;
    }
    else if ((size >= 3 && memcmp(data, "ID3", 3) == 0) ||
        (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)) {

        drmp3_config config;
        drmp3_uint64 frames = 0;

        drmp3_int16* pcm = drmp3_open_memory_and_read_pcm_frames_s16(
            data, (size_t)size, &config, &frames, NULL
        );

        free(data);

        if (!pcm || frames == 0 ||
            config.channels < 1 || config.channels > 2 ||
            frames > INT_MAX) {
            fprintf(stderr, "Sound '%s' could not be decoded\n", path);
            drmp3_free(pcm, NULL);
            return -1;
        }

        if (gain < 0.999f)
            for (drmp3_uint64 i = 0; i < frames * config.channels; i++)
                pcm[i] = (short)((float)pcm[i] * gain);

        data = (unsigned char*)pcm;
        s->pcm = pcm;
        s->frames = (int)frames;
        s->channels = config.channels;
        s->rate = config.sampleRate;
    }
    else {
        if (!prepare_wav(data, size, gain, path)) {
            free(data);
            return -1;
        }

        if (!wav_info(data, size, s)) {
            fprintf(stderr, "Sound '%s' must be 16 bit PCM, mono or stereo\n", path);
            free(data);
            return -1;
        }
    }

    strncpy(s->path, path, sizeof s->path - 1);
    s->path[sizeof s->path - 1] = '\0';
    s->gain = gain;
    s->data = data;

    return sound_count++;
}

static void start_voice(int id, float gl, float gr)
{
    if (!wave || id < 0 || id >= sound_count || !sounds[id].pcm)
        return;

    EnterCriticalSection(&voice_lock);

    int slot = -1;
    double oldest = -1.0;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) { slot = i; break; }
        double progress = voices[i].pos / (double)voices[i].snd->frames;
        if (progress > oldest) { oldest = progress; slot = i; }
    }

    Voice* v = &voices[slot];
    v->snd = &sounds[id];
    v->pos = 0.0;
    v->gl = gl;
    v->gr = gr;
    v->active = 1;

    LeaveCriticalSection(&voice_lock);
}

void audio_play_vol(int id, float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    start_voice(id, volume, volume);
}

void audio_play(int id)
{
    audio_play_vol(id, 1.0f);
}

void audio_set_listener(double x, double y, double z, float yaw_deg)
{
    listener_pos[0] = x; listener_pos[1] = y; listener_pos[2] = z;
    listener_fx = cosf(yaw_deg * PI / 180.0f);
    listener_fz = sinf(yaw_deg * PI / 180.0f);
}

void audio_play_3d(int id, double x, double y, double z, float volume)
{
    double dx = x - listener_pos[0];
    double dy = y - listener_pos[1];
    double dz = z - listener_pos[2];
    double dist = sqrt(dx * dx + dy * dy + dz * dz);

    if (dist >= HEAR_RANGE)
        return;

    float vol = volume * (1.0f - (float)(dist / HEAR_RANGE));
    if (vol <= 0.0f)
        return;

    float pan = 0.0f;
    double h = sqrt(dx * dx + dz * dz);
    if (h > 0.01) {
        pan = (float)((dx * -listener_fz + dz * listener_fx) / h);
        if (h < 2.0) pan *= (float)(h / 2.0);
    }

    float gl = vol, gr = vol;
    if (pan > 0.0f) gl *= 1.0f - 0.8f * pan;
    else            gr *= 1.0f + 0.8f * pan;

    start_voice(id, gl, gr);
}

void audio_set_master(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 2.0f) volume = 2.0f;
    master_volume = volume;
}

float audio_get_master(void)
{
    return master_volume;
}

void audio_shutdown(void)
{
    if (wave) {
        mixing = 0;
        if (mix_event) SetEvent(mix_event);
        WaitForSingleObject(mix_thread, 1000);
        CloseHandle(mix_thread);
        mix_thread = NULL;

        waveOutReset(wave);
        for (int i = 0; i < MIX_BUFFERS; i++)
            waveOutUnprepareHeader(wave, &hdr[i], sizeof hdr[i]);
        waveOutClose(wave);
        wave = NULL;

        if (mix_event) { CloseHandle(mix_event); mix_event = NULL; }
    }

    EnterCriticalSection(&voice_lock);
    memset(voices, 0, sizeof voices);
    LeaveCriticalSection(&voice_lock);

    for (int i = 0; i < sound_count; i++) {
        free(sounds[i].data);
        sounds[i].data = NULL;
        sounds[i].pcm = NULL;
    }

    sound_count = 0;
}