/**
 * @file muse_state.h
 * @brief Charm UI / pipeline status enum shared by display + webhook client.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUSE_STATUS_IDLE = 0,
    MUSE_STATUS_LISTENING,
    MUSE_STATUS_UPLOADING,   /* audio → relay/storage (not Meridian) */
    MUSE_STATUS_THINKING,    /* wake sent; waiting for reply audio */
    MUSE_STATUS_SPEAKING,
    MUSE_STATUS_ERROR,
} muse_status_t;

static inline const char *muse_status_str(muse_status_t s)
{
    switch (s) {
    case MUSE_STATUS_IDLE:      return "idle";
    case MUSE_STATUS_LISTENING: return "listening";
    case MUSE_STATUS_UPLOADING: return "uploading";
    case MUSE_STATUS_THINKING:  return "thinking";
    case MUSE_STATUS_SPEAKING:  return "speaking";
    case MUSE_STATUS_ERROR:     return "error";
    default:                    return "unknown";
    }
}

#ifdef __cplusplus
}
#endif
