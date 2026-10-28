// SPDX-License-Identifier: Apache-2.0
#ifndef MOONLIGHT_HAPTICS_AUTHORED_EVENTS_H
#define MOONLIGHT_HAPTICS_AUTHORED_EVENTS_H

#include "moonlight_haptics/authored_haptics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Additive SDK ABI v2 API for continuous state and discrete authored attacks.
 * This is a native result structure, NOT the authored IR transport wire layout.
 * Profile 1 accepts 48 kHz stereo, 0..240 frames per call (zero only for END).
 * One successful call produces one result. Source chunks are not DSP windows.
 * Engines are independent and must each be accessed serially by their owner.
 */
typedef struct AhAuthoredEventEngine AhAuthoredEventEngine;

#define AH_AUTHORED_EVENT_MAX_EVENTS 8u
#define AH_AUTHORED_EVENT_PROFILE_1 1u

typedef struct AhAuthoredContinuousLane {
    float continuous_low;  /* 50-180 Hz linear full scale. */
    float continuous_high; /* 180-400 Hz linear full scale. */
    float raw_rms;         /* Unfiltered PCM, unlike the old authored API. */
    float raw_peak;
} AhAuthoredContinuousLane;

typedef struct AhAuthoredEvent {
    uint32_t event_id;
    uint32_t reserved;
    uint64_t source_time_us;
    float left_strength;
    float right_strength;
    float confidence; /* Evidence score, not a calibrated probability. */
    float sharpness;
} AhAuthoredEvent;

typedef struct AhAuthoredEventFrame {
    uint32_t struct_size;
    uint32_t flags;        /* AhAuthoredFrameFlags; SILENT does not clear release. */
    uint64_t timestamp_us; /* Represented window end, on the source clock. */
    uint32_t epoch;
    uint32_t frame_sequence;
    uint32_t source_sequence_number;
    uint32_t source_frame_count;
    uint32_t profile;
    uint32_t event_count;
    AhAuthoredContinuousLane lanes[2];
    AhAuthoredEvent events[AH_AUTHORED_EVENT_MAX_EVENTS];
} AhAuthoredEventFrame;

#define AH_AUTHORED_CONTINUOUS_LANE_V1_SIZE 16u
#define AH_AUTHORED_EVENT_V1_SIZE 32u
#define AH_AUTHORED_EVENT_FRAME_V1_SIZE 328u

/* Config is initialized by ah_authored_config_init; profile 1 supports only
 * 48000 Hz, two lanes, hop 0/240 and zero feature/reserved fields.
 * Allocation occurs only in create/destroy, never process/reset.
 */
MOONLIGHT_HAPTICS_API AhStatus ah_authored_events_create(const AhAuthoredConfig *config,
                                                         AhAuthoredEventEngine **out_engine);

/* Reuses AhAuthoredProcessInput and its flag meanings without changing ABI v2.
 * Caller supplies AH_AUTHORED_EVENT_FRAME_V1_SIZE bytes per output slot.
 * BUFFER_TOO_SMALL / invalid input / stale sequence do not consume input.
 * Events mature for 6 ms, remain immutable after publication, and may repeat
 * for up to 20 ms for transport redundancy. Consumers deduplicate by epoch/id.
 * Gaps/START/discontinuity create a new epoch. After END, input must carry
 * START or DISCONTINUITY, or the owner must reset the engine first.
 * END requires frame_count == 0 and is a zero-body, zero-event marker, including empty
 * streams. Explicit reset permits a fresh stream without a START flag.
 * Continuous release may exceed this call's raw_peak. SILENT describes only
 * current tactile-band input. No actuator calibration or platform calls occur.
 */
MOONLIGHT_HAPTICS_API AhStatus ah_authored_events_process_i16(AhAuthoredEventEngine *engine,
                                                              const AhAuthoredProcessInput *input,
                                                              AhAuthoredEventFrame *out_frames,
                                                              uint32_t out_capacity,
                                                              uint32_t *out_count);
MOONLIGHT_HAPTICS_API void ah_authored_events_reset(AhAuthoredEventEngine *engine);
MOONLIGHT_HAPTICS_API void ah_authored_events_destroy(AhAuthoredEventEngine *engine);

#ifdef __cplusplus
} // extern "C"
#endif
#endif // MOONLIGHT_HAPTICS_AUTHORED_EVENTS_H
