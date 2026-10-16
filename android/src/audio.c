/* Mezclador de efectos y salida AAudio. Ver audio.h. */
#include "audio.h"
#include "music.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

typedef struct {
    const Sound *s;
    double pos, step;
    float vl, vr;
    int slot, active;
} Voice;

static const Sound *g_slots;
static Voice g_voices[AUDIO_VOICES];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void voice_params(Voice *v, float vol, float pan, float pitch) {
    float a = vol * v->s->vol;
    if (pan < -1) pan = -1;
    if (pan > 1) pan = 1;
    v->vl = a * (pan > 0 ? 1 - pan : 1);
    v->vr = a * (pan < 0 ? 1 + pan : 1);
    v->step = v->s->rate * pow(2.0, pitch / 12.0) / AUDIO_RATE;
}

int audio_play(int slot, float vol, float pan, float pitch) {
    if (!g_slots || slot < 0 || slot >= SOUND_SLOTS || !g_slots[slot].pcm) return -1;
    pthread_mutex_lock(&g_lock);
    int best = -1;
    for (int i = 0; i < AUDIO_VOICES; i++)
        if (!g_voices[i].active) { best = i; break; }
    if (best < 0) {   /* todas ocupadas: se roba la mas avanzada que no haga bucle */
        double far = -1;
        for (int i = 0; i < AUDIO_VOICES; i++)
            if (!g_voices[i].s->loops && g_voices[i].pos / g_voices[i].s->n > far) { far = g_voices[i].pos / g_voices[i].s->n; best = i; }
    }
    if (best >= 0) {
        Voice *v = &g_voices[best];
        v->s = &g_slots[slot];
        v->slot = slot;
        v->pos = 0;
        voice_params(v, vol, pan, pitch);
        v->active = 1;
    }
    pthread_mutex_unlock(&g_lock);
    return best;
}

void audio_set(int voice, float vol, float pan, float pitch) {
    if (voice < 0 || voice >= AUDIO_VOICES) return;
    pthread_mutex_lock(&g_lock);
    if (g_voices[voice].active) voice_params(&g_voices[voice], vol, pan, pitch);
    pthread_mutex_unlock(&g_lock);
}

void audio_stop(int voice) {
    if (voice < 0 || voice >= AUDIO_VOICES) return;
    pthread_mutex_lock(&g_lock);
    g_voices[voice].active = 0;
    pthread_mutex_unlock(&g_lock);
}

void audio_stop_all(void) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < AUDIO_VOICES; i++) g_voices[i].active = 0;
    pthread_mutex_unlock(&g_lock);
}

void audio_mix(int16_t *out, int frames) {
    static float acc[2 * 4096];
    while (frames > 0) {
        int nf = frames > 4096 ? 4096 : frames;
        memset(acc, 0, sizeof(float) * 2 * nf);
        pthread_mutex_lock(&g_lock);
        for (int i = 0; i < AUDIO_VOICES; i++) {
            Voice *v = &g_voices[i];
            if (!v->active) continue;
            const Sound *s = v->s;
            for (int f = 0; f < nf; f++) {
                uint32_t k = (uint32_t)v->pos;
                if (k >= s->n) {
                    if (s->loops && s->loop_start < s->n) { v->pos = s->loop_start + (v->pos - s->n); k = (uint32_t)v->pos; }
                    else { v->active = 0; break; }
                }
                /* interpolacion lineal entre muestras */
                float fr = (float)(v->pos - k);
                float a = s->pcm[k], b = k + 1 < s->n ? s->pcm[k + 1] : (s->loops ? s->pcm[s->loop_start] : 0);
                float x = a + (b - a) * fr;
                acc[2 * f] += x * v->vl;
                acc[2 * f + 1] += x * v->vr;
                v->pos += v->step;
            }
        }
        pthread_mutex_unlock(&g_lock);
        music_mix(acc, nf, 0.6f);
        for (int f = 0; f < 2 * nf; f++) {
            float x = acc[f] * 0.7f;
            out[f] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
        out += 2 * nf;
        frames -= nf;
    }
}

#ifdef __ANDROID__
#include <aaudio/AAudio.h>

static AAudioStream *g_stream;

static aaudio_data_callback_result_t cb(AAudioStream *st, void *user, void *data, int32_t frames) {
    (void)st; (void)user;
    audio_mix((int16_t *)data, frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

int audio_init(const Sound *slots) {
    g_slots = slots;
    if (g_stream) return 0;
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return -1;
    AAudioStreamBuilder_setSampleRate(b, AUDIO_RATE);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, cb, NULL);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &g_stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { g_stream = NULL; return -1; }
    AAudioStream_requestStart(g_stream);
    return 0;
}

void audio_pause(int paused) {
    if (!g_stream) return;
    if (paused) AAudioStream_requestPause(g_stream);
    else AAudioStream_requestStart(g_stream);
}

void audio_shutdown(void) {
    if (g_stream) { AAudioStream_close(g_stream); g_stream = NULL; }
}
#else
int audio_init(const Sound *slots) { g_slots = slots; return -1; }
void audio_pause(int paused) { (void)paused; }
void audio_shutdown(void) {}
#endif
