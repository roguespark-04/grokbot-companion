/**
 * @file muse_state.h
 * @brief Pipeline / avatar status enum shared by the UI, relay client and state machine.
 */
#pragma once

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUSE_STATUS_IDLE = 0,
    MUSE_STATUS_LISTENING,
    MUSE_STATUS_UPLOADING,   /* audio → relay (not Meridian) */
    MUSE_STATUS_THINKING,    /* wake sent; waiting for reply */
    MUSE_STATUS_WORKING,     /* Meridian / specialist bot reported "working" */
    MUSE_STATUS_SPEAKING,
    MUSE_STATUS_ERROR,
    MUSE_STATUS_COUNT,
} muse_status_t;

static inline const char *muse_status_str(muse_status_t s)
{
    switch (s) {
    case MUSE_STATUS_IDLE:      return "idle";
    case MUSE_STATUS_LISTENING: return "listening";
    case MUSE_STATUS_UPLOADING: return "uploading";
    case MUSE_STATUS_THINKING:  return "thinking";
    case MUSE_STATUS_WORKING:   return "working";
    case MUSE_STATUS_SPEAKING:  return "speaking";
    case MUSE_STATUS_ERROR:     return "error";
    default:                    return "unknown";
    }
}

/** Parse relay status "state" strings (GET /status/<bot_id>). Unknown → idle. */
static inline muse_status_t muse_status_from_str(const char *s)
{
    if (!s) return MUSE_STATUS_IDLE;
    for (int i = 0; i < MUSE_STATUS_COUNT; i++) {
        if (strcmp(s, muse_status_str((muse_status_t)i)) == 0) {
            return (muse_status_t)i;
        }
    }
    return MUSE_STATUS_IDLE;
}

/** States that mean "a bot is busy" (blocks idle sleep). */
static inline int muse_status_is_busy(muse_status_t s)
{
    return s == MUSE_STATUS_LISTENING || s == MUSE_STATUS_UPLOADING ||
           s == MUSE_STATUS_THINKING || s == MUSE_STATUS_WORKING ||
           s == MUSE_STATUS_SPEAKING;
}

#ifdef __cplusplus
}
#endif
