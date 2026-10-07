/**
 * Energy VAD — see vad.h. Pure C, no hardware dependency (unit-testable on host).
 */
#include "vad.h"

#include <math.h>
#include <string.h>

static vad_config_t s_cfg = VAD_CONFIG_DEFAULT();
static float    s_noise = 200.0f;
static uint16_t s_speech_run;
static uint16_t s_silence_run;
static int      s_in_speech;
static uint8_t  s_level;

void vad_init(const vad_config_t *cfg)
{
    if (cfg) s_cfg = *cfg;
    vad_reset();
}

void vad_reset(void)
{
    s_noise = s_cfg.min_rms * 0.6f;
    s_speech_run = 0;
    s_silence_run = 0;
    s_in_speech = 0;
    s_level = 0;
}

vad_event_t vad_process(const int16_t *samples, size_t n)
{
    if (!samples || n == 0) return VAD_EVENT_NONE;
    double acc = 0;
    for (size_t i = 0; i < n; i++) {
        acc += (double)samples[i] * samples[i];
    }
    float rms = (float)sqrt(acc / (double)n);
    float lvl = rms / 32.0f;              /* ~8000 RMS → 255 */
    s_level = (uint8_t)(lvl > 255.0f ? 255.0f : lvl);

    int speechy = (rms > s_noise * s_cfg.start_ratio) && (rms > s_cfg.min_rms);

    if (!s_in_speech) {
        /* Track noise floor only while not in speech; slow attack, faster decay. */
        float a = (rms > s_noise) ? 0.01f : 0.05f;
        if (!speechy) s_noise += a * (rms - s_noise);
        if (s_noise < 50.0f) s_noise = 50.0f;

        s_speech_run = speechy ? (uint16_t)(s_speech_run + 1) : 0;
        if (s_speech_run >= s_cfg.start_frames) {
            s_in_speech = 1;
            s_silence_run = 0;
            return VAD_EVENT_SPEECH_START;
        }
        return VAD_EVENT_NONE;
    }

    s_silence_run = speechy ? 0 : (uint16_t)(s_silence_run + 1);
    if (s_silence_run >= s_cfg.hangover_frames) {
        s_in_speech = 0;
        s_speech_run = 0;
        return VAD_EVENT_SPEECH_END;
    }
    return VAD_EVENT_NONE;
}

int vad_in_speech(void) { return s_in_speech; }
uint8_t vad_level(void) { return s_level; }
