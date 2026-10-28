// SPDX-License-Identifier: Apache-2.0

#include "moonlight_haptics/audio_haptics.h"
#include "moonlight_haptics/authored_haptics.h"
#include "moonlight_haptics/authored_events.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    assert(ah_get_abi_version() == 2u);
    assert(strcmp(ah_get_version_string(), "0.7.0") == 0);
    assert(strcmp(ah_get_parameter_set_version(),
                  "action-rpg-p4g-v6-dialogue") == 0);
    assert(strcmp(ah_status_string(AH_STATUS_OK), "ok") == 0);

    AhAuthoredConfig authored_config;
    assert(ah_authored_config_init(&authored_config, 48000u) == AH_STATUS_OK);
    AhAuthoredEngine* authored_engine = NULL;
    assert(ah_authored_create(&authored_config, &authored_engine) == AH_STATUS_OK);
    ah_authored_destroy(authored_engine);

    AhAuthoredEventEngine* event_engine = NULL;
    assert(ah_authored_events_create(&authored_config, &event_engine) == AH_STATUS_OK);
    AhAuthoredProcessInput event_input;
    memset(&event_input, 0, sizeof(event_input));
    event_input.struct_size = AH_AUTHORED_PROCESS_INPUT_V2_SIZE;
    event_input.flags = AH_AUTHORED_INPUT_STREAM_END;
    AhAuthoredEventFrame event_output;
    uint32_t event_count = 0;
    assert(ah_authored_events_process_i16(event_engine, &event_input, &event_output,
                                         1u, &event_count) == AH_STATUS_OUTPUT_AVAILABLE);
    assert(event_count == 1u && event_output.event_count == 0u);
    assert((event_output.flags & AH_AUTHORED_FRAME_STREAM_END) != 0u);
    ah_authored_events_destroy(event_engine);

    AhConfig config;
    assert(ah_config_init(&config, 48000u, 2u) == AH_STATUS_OK);
    assert(config.struct_size == AH_CONFIG_V1_SIZE);
    assert(config.requested_scene == AH_SCENE_GAME);
    assert(config.sensitivity == 1.0f);
    assert(config.output_gain == 1.0f);

    AhEngine* engine = NULL;
    assert(ah_create(&config, &engine) == AH_STATUS_OK);
    assert(engine != NULL);
    assert(ah_get_max_output_frames(engine, 240u) == 3u);

    int16_t pcm[240 * 2] = {0};
    AhProcessInput input;
    memset(&input, 0, sizeof(input));
    input.struct_size = AH_PROCESS_INPUT_V1_SIZE;
    input.interleaved_pcm = pcm;
    input.frame_count = 240u;
    input.first_sample_time_us = 1000000u;

    AhHapticFrame frames[3];
    memset(frames, 0, sizeof(frames));
    uint32_t output_count = 99u;

    assert(ah_process_i16(engine, &input, frames, 2u, &output_count) ==
           AH_STATUS_BUFFER_TOO_SMALL);
    assert(output_count == 0u);

    assert(ah_process_i16(engine, &input, frames, 3u, &output_count) == AH_STATUS_OK);
    assert(output_count == 0u);

    config.sensitivity = 1.5f;
    config.output_gain = 0.7f;
    config.requested_scene = AH_SCENE_MUSIC;
    assert(ah_update_config(engine, &config) == AH_STATUS_OK);

    config.sample_rate = 44100u;
    assert(ah_update_config(engine, &config) == AH_STATUS_RECREATE_REQUIRED);
    config.sample_rate = 48000u;

    config.feature_flags = 1u;
    assert(ah_update_config(engine, &config) == AH_STATUS_UNSUPPORTED);
    config.feature_flags = 0u;

    input.interleaved_pcm = NULL;
    assert(ah_process_i16(engine, &input, frames, 3u, &output_count) ==
           AH_STATUS_INVALID_ARGUMENT);

    input.frame_count = 0u;
    assert(ah_process_i16(engine, &input, NULL, 0u, &output_count) == AH_STATUS_OK);

    ah_reset(engine);
    ah_destroy(engine);

    assert(ah_create(NULL, &engine) == AH_STATUS_INVALID_ARGUMENT);
    assert(engine == NULL);
    assert(ah_create(&config, NULL) == AH_STATUS_INVALID_ARGUMENT);
    return 0;
}
