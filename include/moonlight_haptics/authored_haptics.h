// SPDX-License-Identifier: Apache-2.0

#ifndef MOONLIGHT_HAPTICS_AUTHORED_HAPTICS_H
#define MOONLIGHT_HAPTICS_AUTHORED_HAPTICS_H

#include <stddef.h>
#include <stdint.h>

#include "moonlight_haptics/audio_haptics.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Device-independent, two-lane authored haptics analysis.
 *
 * This API is a fallback for transports that cannot carry the original
 * haptics PCM. It intentionally preserves the two source lanes and leaves
 * actuator calibration and rendering to the client.
 */
typedef struct AhAuthoredEngine AhAuthoredEngine;

typedef uint32_t AhAuthoredInputFlags;
enum {
    AH_AUTHORED_INPUT_NONE = 0,
    AH_AUTHORED_INPUT_STREAM_START = 1u << 0,
    AH_AUTHORED_INPUT_DISCONTINUITY = 1u << 1,
    AH_AUTHORED_INPUT_STREAM_END = 1u << 2
};

typedef uint32_t AhAuthoredFrameFlags;
enum {
    AH_AUTHORED_FRAME_NONE = 0,
    AH_AUTHORED_FRAME_DISCONTINUITY = 1u << 0,
    AH_AUTHORED_FRAME_PARTIAL = 1u << 1,
    AH_AUTHORED_FRAME_STREAM_END = 1u << 2,
    AH_AUTHORED_FRAME_SILENT = 1u << 3
};

typedef struct AhAuthoredConfig {
    uint32_t struct_size;
    uint32_t sample_rate;
    uint32_t channel_count;       /* Must be 2. */
    uint32_t analysis_hop_frames; /* 0 selects 5 ms. */
    uint32_t feature_flags;       /* Must be 0 in ABI v2. */
    uint32_t reserved[7];
} AhAuthoredConfig;

typedef struct AhAuthoredProcessInput {
    uint32_t struct_size;
    const int16_t* interleaved_pcm;
    uint32_t frame_count;
    uint32_t flags;               /* AhAuthoredInputFlags bitset. */
    uint64_t first_sample_time_us;
    uint32_t sequence_number;     /* Monotonic per stream, wraps naturally. */
    uint32_t reserved;
} AhAuthoredProcessInput;

typedef struct AhAuthoredLaneFrame {
    float rms_amplitude;          /* Linear full scale, 0.0 .. 1.0. */
    float peak_amplitude;         /* Linear full scale, 0.0 .. 1.0. */
    float transient_strength;     /* Relative attack, 0.0 .. 1.0. */
    float low_band_ratio;         /* Energy below roughly 200 Hz, 0.0 .. 1.0. */
    float zero_crossing_rate_hz;  /* Texture hint; not a pitch estimate. */
    uint32_t reserved[3];
} AhAuthoredLaneFrame;

typedef struct AhAuthoredHapticFrame {
    uint32_t struct_size;
    uint32_t flags;               /* AhAuthoredFrameFlags bitset. */
    uint64_t timestamp_us;        /* End time of the represented PCM window. */
    uint32_t source_sequence_number;
    uint32_t source_frame_count;
    AhAuthoredLaneFrame lanes[2]; /* Source order: left, right. */
    float lane_correlation;       /* -1.0 .. 1.0; diagnostic only. */
    uint32_t reserved[3];
} AhAuthoredHapticFrame;

#define AH_AUTHORED_CONFIG_V2_SIZE 48u
#define AH_AUTHORED_PROCESS_INPUT_V2_SIZE 40u
#define AH_AUTHORED_LANE_FRAME_V2_SIZE 32u
#define AH_AUTHORED_HAPTIC_FRAME_V2_SIZE 104u

MOONLIGHT_HAPTICS_API AhStatus ah_authored_config_init(
    AhAuthoredConfig* config,
    uint32_t sample_rate);

MOONLIGHT_HAPTICS_API AhStatus ah_authored_create(
    const AhAuthoredConfig* config,
    AhAuthoredEngine** out_engine);

MOONLIGHT_HAPTICS_API uint32_t ah_authored_get_max_output_frames(
    const AhAuthoredEngine* engine,
    uint32_t input_frame_count,
    uint32_t input_flags);

/*
 * No allocation, locking, logging, or platform calls occur in this function.
 * On AH_STATUS_BUFFER_TOO_SMALL, input is not consumed and out_count is zero.
 */
MOONLIGHT_HAPTICS_API AhStatus ah_authored_process_i16(
    AhAuthoredEngine* engine,
    const AhAuthoredProcessInput* input,
    AhAuthoredHapticFrame* out_frames,
    uint32_t out_capacity,
    uint32_t* out_count);

MOONLIGHT_HAPTICS_API void ah_authored_reset(AhAuthoredEngine* engine);
MOONLIGHT_HAPTICS_API void ah_authored_destroy(AhAuthoredEngine* engine);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MOONLIGHT_HAPTICS_AUTHORED_HAPTICS_H
