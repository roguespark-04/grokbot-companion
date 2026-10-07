/**
 * @file vad.h
 * @brief Tiny energy-based voice activity detector for always-listen mode (stub quality).
 *
 * Adaptive noise floor + threshold ratio + start/hangover frame counts. Good enough to
 * gate uploads in a quiet room; swap for esp-sr VADNet / WakeNet later.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VAD_EVENT_NONE = 0,
    VAD_EVENT_SPEECH_START,
    VAD_EVENT_SPEECH_END,
} vad_event_t;

typedef struct {
    uint32_t sample_rate;      /* 16000 */
    uint16_t frame_ms;         /* 20 */
    float    start_ratio;      /* energy / noise floor to count as speech, e.g. 3.0 (~+9.5 dB) */
    float    min_rms;          /* absolute floor (int16 RMS), e.g. 300 */
    uint16_t start_frames;     /* consecutive speech frames to trigger start, e.g. 3 */
    uint16_t hangover_frames;  /* consecutive silence frames to end, e.g. 40 (800 ms) */
} vad_config_t;

#define VAD_CONFIG_DEFAULT() { \
    .sample_rate = 16000, .frame_ms = 20, .start_ratio = 3.0f, .min_rms = 300.0f, \
    .start_frames = 3, .hangover_frames = 40 }

void        vad_init(const vad_config_t *cfg);
void        vad_reset(void);
/** Feed one frame (frame_ms worth of mono s16 samples). */
vad_event_t vad_process(const int16_t *samples, size_t n);
int         vad_in_speech(void);
/** Last frame RMS mapped to 0..255 (for avatar level meters). */
uint8_t     vad_level(void);

#ifdef __cplusplus
}
#endif
