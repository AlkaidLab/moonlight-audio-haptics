// SPDX-License-Identifier: Apache-2.0

#include "moonlight_haptics/authored_haptics.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::vector<int16_t> MakeStereoTone(uint32_t frames,
                                    float leftHz,
                                    float rightHz) {
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2U);
    constexpr float kPi = 3.14159265358979323846F;
    for (uint32_t index = 0; index < frames; ++index) {
        const float time = static_cast<float>(index) / 48000.0F;
        pcm[static_cast<size_t>(index) * 2U] = static_cast<int16_t>(
            std::sin(2.0F * kPi * leftHz * time) * 16000.0F);
        pcm[static_cast<size_t>(index) * 2U + 1U] = rightHz == 0.0F
            ? 0
            : static_cast<int16_t>(
                  std::sin(2.0F * kPi * rightHz * time) * 16000.0F);
    }
    return pcm;
}

AhAuthoredProcessInput Input(const int16_t* pcm,
                             uint32_t frames,
                             uint32_t sequence,
                             uint32_t flags,
                             uint64_t timestamp) {
    AhAuthoredProcessInput input{};
    input.struct_size = AH_AUTHORED_PROCESS_INPUT_V2_SIZE;
    input.interleaved_pcm = pcm;
    input.frame_count = frames;
    input.flags = flags;
    input.first_sample_time_us = timestamp;
    input.sequence_number = sequence;
    return input;
}

AhAuthoredHapticFrame AnalyzeSteadyTone(AhAuthoredEngine* engine,
                                        float frequencyHz,
                                        uint32_t sequence) {
    constexpr uint32_t kFrames = 4800U; // 100 ms settles the causal filters.
    const std::vector<int16_t> pcm =
        MakeStereoTone(kFrames, frequencyHz, 0.0F);
    const AhAuthoredProcessInput input = Input(
        pcm.data(), kFrames, sequence, AH_AUTHORED_INPUT_STREAM_START,
        1000000U + static_cast<uint64_t>(sequence) * 100000U);
    AhAuthoredHapticFrame output[24]{};
    uint32_t count = 0U;
    assert(ah_authored_process_i16(
               engine, &input, output, 24U, &count) ==
           AH_STATUS_OUTPUT_AVAILABLE);
    assert(count == 20U);
    return output[count - 1U];
}

} // namespace

int main() {
    static_assert(sizeof(AhAuthoredConfig) == AH_AUTHORED_CONFIG_V2_SIZE,
                  "authored config ABI changed");
    static_assert(sizeof(AhAuthoredProcessInput) ==
                      AH_AUTHORED_PROCESS_INPUT_V2_SIZE,
                  "authored input ABI changed");
    static_assert(sizeof(AhAuthoredLaneFrame) == AH_AUTHORED_LANE_FRAME_V2_SIZE,
                  "authored lane ABI changed");
    static_assert(sizeof(AhAuthoredHapticFrame) ==
                      AH_AUTHORED_HAPTIC_FRAME_V2_SIZE,
                  "authored frame ABI changed");

    AhAuthoredConfig config{};
    assert(ah_authored_config_init(&config, 7999U) == AH_STATUS_UNSUPPORTED);
    assert(ah_authored_config_init(&config, 192001U) == AH_STATUS_UNSUPPORTED);
    assert(ah_authored_config_init(&config, 48000U) == AH_STATUS_OK);
    assert(config.channel_count == 2U);
    assert(config.analysis_hop_frames == 240U);

    AhAuthoredEngine* engine = nullptr;
    assert(ah_authored_create(&config, &engine) == AH_STATUS_OK);

    const std::vector<int16_t> isolated = MakeStereoTone(240U, 120.0F, 0.0F);
    AhAuthoredProcessInput input = Input(
        isolated.data(), 240U, 10U, AH_AUTHORED_INPUT_STREAM_START, 1000000U);
    AhAuthoredHapticFrame output[2]{};
    uint32_t count = 0U;
    assert(ah_authored_process_i16(engine, &input, output, 2U, nullptr) ==
           AH_STATUS_INVALID_ARGUMENT);
    AhAuthoredProcessInput invalidFlags = input;
    invalidFlags.flags |= 1U << 31U;
    count = 7U;
    assert(ah_authored_process_i16(
               engine, &invalidFlags, output, 2U, &count) ==
           AH_STATUS_INVALID_ARGUMENT);
    assert(count == 0U);
    count = 7U;
    assert(ah_authored_process_i16(engine, &input, output, 0U, &count) ==
           AH_STATUS_BUFFER_TOO_SMALL);
    assert(count == 0U);
    assert(ah_authored_process_i16(engine, &input, output, 2U, &count) ==
           AH_STATUS_OUTPUT_AVAILABLE);
    assert(count == 1U);
    assert(output[0].timestamp_us == 1005000U);
    assert((output[0].flags & AH_AUTHORED_FRAME_DISCONTINUITY) != 0U);
    // The causal four-pole tactile filters are still settling in the first
    // 5 ms hop, but a valid in-band onset must remain clearly observable.
    assert(output[0].lanes[0].rms_amplitude > 0.05F);
    assert(output[0].lanes[1].rms_amplitude == 0.0F);
    assert(output[0].lanes[0].low_band_ratio > 0.2F);
    assert(output[0].lanes[0].zero_crossing_rate_hz >= 180.0F);
    assert(output[0].lanes[0].zero_crossing_rate_hz < 300.0F);
    const float oneShotRms = output[0].lanes[0].rms_amplitude;

    // Authored features describe the tactile passband rather than arbitrary
    // audio energy. Sub-bass drift and high-frequency hiss must not turn into
    // persistent legacy rumble, while the low/high split remains useful.
    const AhAuthoredHapticFrame belowBand =
        AnalyzeSteadyTone(engine, 20.0F, 11U);
    const AhAuthoredHapticFrame lowTactile =
        AnalyzeSteadyTone(engine, 120.0F, 12U);
    const AhAuthoredHapticFrame highTactile =
        AnalyzeSteadyTone(engine, 300.0F, 13U);
    const AhAuthoredHapticFrame aboveBand =
        AnalyzeSteadyTone(engine, 1000.0F, 14U);
    assert(lowTactile.lanes[0].rms_amplitude >
           belowBand.lanes[0].rms_amplitude * 8.0F);
    assert(highTactile.lanes[0].rms_amplitude >
           aboveBand.lanes[0].rms_amplitude * 8.0F);
    assert(lowTactile.lanes[0].low_band_ratio >
           highTactile.lanes[0].low_band_ratio + 0.25F);
    assert(lowTactile.lanes[0].zero_crossing_rate_hz > 180.0F);
    assert(lowTactile.lanes[0].zero_crossing_rate_hz < 300.0F);
    assert(highTactile.lanes[0].zero_crossing_rate_hz > 500.0F);
    assert(highTactile.lanes[0].zero_crossing_rate_hz < 700.0F);

    // Chunk boundaries do not change a complete hop's output.
    ah_authored_reset(engine);
    AhAuthoredProcessInput first = Input(
        isolated.data(), 100U, 20U, AH_AUTHORED_INPUT_STREAM_START, 2000000U);
    assert(ah_authored_process_i16(engine, &first, nullptr, 0U, &count) ==
           AH_STATUS_OK);
    AhAuthoredProcessInput second = Input(
        isolated.data() + 200U, 140U, 21U, AH_AUTHORED_INPUT_NONE, 2002083U);
    assert(ah_authored_process_i16(engine, &second, output, 2U, &count) ==
           AH_STATUS_OUTPUT_AVAILABLE);
    assert(count == 1U);
    assert(std::abs(output[0].lanes[0].rms_amplitude - oneShotRms) < 1.0e-6F);
    assert(output[0].timestamp_us == 2005000U);

    // A sequence gap resets history and marks the next frame for a clean UX.
    AhAuthoredProcessInput gap = Input(
        isolated.data(), 240U, 23U, AH_AUTHORED_INPUT_NONE, 3000000U);
    assert(ah_authored_process_i16(engine, &gap, output, 2U, &count) ==
           AH_STATUS_OUTPUT_AVAILABLE);
    assert((output[0].flags & AH_AUTHORED_FRAME_DISCONTINUITY) != 0U);

    // End-of-stream flushes a partial window instead of dropping the tail.
    AhAuthoredProcessInput tail = Input(
        isolated.data(), 80U, 24U,
        AH_AUTHORED_INPUT_STREAM_START | AH_AUTHORED_INPUT_STREAM_END,
        4000000U);
    assert(ah_authored_get_max_output_frames(engine, 80U, tail.flags) == 1U);
    assert(ah_authored_process_i16(engine, &tail, output, 1U, &count) ==
           AH_STATUS_OUTPUT_AVAILABLE);
    assert(count == 1U);
    assert(output[0].source_frame_count == 80U);
    assert((output[0].flags & AH_AUTHORED_FRAME_PARTIAL) != 0U);
    assert((output[0].flags & AH_AUTHORED_FRAME_STREAM_END) != 0U);

    // STREAM_END establishes a new boundary even if the host omits START on
    // the next stream, allowing the client to apply a clean fade-in.
    AhAuthoredProcessInput afterEnd = Input(
        isolated.data(), 240U, 25U, AH_AUTHORED_INPUT_NONE, 5000000U);
    assert(ah_authored_process_i16(engine, &afterEnd, output, 1U, &count) ==
           AH_STATUS_OUTPUT_AVAILABLE);
    assert((output[0].flags & AH_AUTHORED_FRAME_DISCONTINUITY) != 0U);

    ah_authored_destroy(engine);
    return 0;
}
