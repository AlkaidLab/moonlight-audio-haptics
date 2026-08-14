// SPDX-License-Identifier: Apache-2.0

#include "moonlight_haptics/authored_haptics.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

static_assert(sizeof(AhAuthoredProcessInput) ==
                  AH_AUTHORED_PROCESS_INPUT_V2_SIZE,
              "authored input ABI prefix must cover the complete struct");

namespace {

constexpr uint32_t kMinimumSampleRate = 8000;
constexpr uint32_t kMaximumSampleRate = 192000;
constexpr float kSilenceFloor = 1.0F / 32768.0F;
constexpr float kPi = 3.14159265358979323846F;

struct LaneAccumulator {
    double squareSum = 0.0;
    double lowSquareSum = 0.0;
    float peak = 0.0F;
    float lowPass = 0.0F;
    float previousSample = 0.0F;
    float previousRms = 0.0F;
    float previousPeak = 0.0F;
    uint32_t zeroCrossings = 0;
    bool hasPreviousSample = false;
};

float Clamp01(float value) noexcept {
    return std::max(0.0F, std::min(1.0F, value));
}

AhStatus ValidateConfig(const AhAuthoredConfig* config) noexcept {
    if (config == nullptr || config->struct_size < AH_AUTHORED_CONFIG_V2_SIZE) {
        return AH_STATUS_INVALID_ARGUMENT;
    }
    if (config->sample_rate < kMinimumSampleRate ||
        config->sample_rate > kMaximumSampleRate ||
        config->channel_count != 2U) {
        return AH_STATUS_UNSUPPORTED;
    }
    if (config->feature_flags != 0U) return AH_STATUS_UNSUPPORTED;
    if (config->analysis_hop_frames > config->sample_rate / 20U) {
        return AH_STATUS_UNSUPPORTED;
    }
    return AH_STATUS_OK;
}

} // namespace

struct AhAuthoredEngine {
    AhAuthoredEngine(uint32_t sampleRateValue, uint32_t hopFramesValue)
        : sampleRate(sampleRateValue),
          hopFrames(hopFramesValue),
          lowPassAlpha(1.0F - std::exp(
              -2.0F * kPi * 200.0F / static_cast<float>(sampleRateValue))) {}

    uint32_t sampleRate;
    uint32_t hopFrames;
    float lowPassAlpha;
    uint32_t accumulatedFrames = 0;
    uint32_t expectedSequence = 0;
    bool hasExpectedSequence = false;
    bool markDiscontinuity = true;
    LaneAccumulator lanes[2];
    double crossSum = 0.0;
};

namespace {

void ResetWindow(AhAuthoredEngine& engine) noexcept {
    engine.accumulatedFrames = 0;
    engine.crossSum = 0.0;
    for (LaneAccumulator& lane : engine.lanes) {
        lane.squareSum = 0.0;
        lane.lowSquareSum = 0.0;
        lane.peak = 0.0F;
        lane.zeroCrossings = 0;
    }
}

void ResetStream(AhAuthoredEngine& engine) noexcept {
    ResetWindow(engine);
    engine.hasExpectedSequence = false;
    engine.markDiscontinuity = true;
    for (LaneAccumulator& lane : engine.lanes) {
        lane.lowPass = 0.0F;
        lane.previousSample = 0.0F;
        lane.previousRms = 0.0F;
        lane.previousPeak = 0.0F;
        lane.hasPreviousSample = false;
    }
}

void EmitFrame(AhAuthoredEngine& engine,
               AhAuthoredHapticFrame& output,
               uint64_t timestampUs,
               uint32_t sourceSequence,
               uint32_t flags) noexcept {
    std::memset(&output, 0, sizeof(output));
    output.struct_size = AH_AUTHORED_HAPTIC_FRAME_V2_SIZE;
    output.flags = flags;
    if (engine.markDiscontinuity) {
        output.flags |= AH_AUTHORED_FRAME_DISCONTINUITY;
        engine.markDiscontinuity = false;
    }
    output.timestamp_us = timestampUs;
    output.source_sequence_number = sourceSequence;
    output.source_frame_count = engine.accumulatedFrames;

    bool silent = true;
    for (uint32_t index = 0; index < 2U; ++index) {
        LaneAccumulator& source = engine.lanes[index];
        AhAuthoredLaneFrame& lane = output.lanes[index];
        const double count = static_cast<double>(engine.accumulatedFrames);
        const float rms = static_cast<float>(std::sqrt(source.squareSum / count));
        const float lowRms = static_cast<float>(
            std::sqrt(source.lowSquareSum / count));
        lane.rms_amplitude = Clamp01(rms);
        lane.peak_amplitude = Clamp01(source.peak);
        lane.transient_strength = Clamp01(
            std::max(0.0F, rms - source.previousRms) * 4.0F +
            std::max(0.0F, source.peak - source.previousPeak) * 0.5F);
        lane.low_band_ratio = Clamp01(
            (lowRms * lowRms) / (rms * rms + 1.0e-12F));
        lane.zero_crossing_rate_hz =
            static_cast<float>(source.zeroCrossings) *
            static_cast<float>(engine.sampleRate) /
            (2.0F * static_cast<float>(engine.accumulatedFrames));
        source.previousRms = rms;
        source.previousPeak = source.peak;
        silent = silent && source.peak <= kSilenceFloor;
    }

    const double denominator = std::sqrt(
        engine.lanes[0].squareSum * engine.lanes[1].squareSum);
    output.lane_correlation = denominator > 1.0e-12
        ? std::max(-1.0F, std::min(1.0F,
              static_cast<float>(engine.crossSum / denominator)))
        : 0.0F;
    if (silent) output.flags |= AH_AUTHORED_FRAME_SILENT;
}

} // namespace

extern "C" {

AhStatus ah_authored_config_init(AhAuthoredConfig* config,
                                 uint32_t sample_rate) {
    if (config == nullptr) return AH_STATUS_INVALID_ARGUMENT;
    std::memset(config, 0, sizeof(*config));
    config->struct_size = AH_AUTHORED_CONFIG_V2_SIZE;
    config->sample_rate = sample_rate;
    config->channel_count = 2U;
    config->analysis_hop_frames = (sample_rate + 100U) / 200U;
    return ValidateConfig(config);
}

AhStatus ah_authored_create(const AhAuthoredConfig* config,
                            AhAuthoredEngine** out_engine) {
    if (out_engine == nullptr) return AH_STATUS_INVALID_ARGUMENT;
    *out_engine = nullptr;
    const AhStatus validation = ValidateConfig(config);
    if (validation != AH_STATUS_OK) return validation;
    const uint32_t hopFrames = config->analysis_hop_frames == 0U
        ? (config->sample_rate + 100U) / 200U
        : config->analysis_hop_frames;
    if (hopFrames == 0U) return AH_STATUS_UNSUPPORTED;
    try {
        *out_engine = new AhAuthoredEngine(config->sample_rate, hopFrames);
    } catch (const std::bad_alloc&) {
        return AH_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return AH_STATUS_UNSUPPORTED;
    }
    return AH_STATUS_OK;
}

uint32_t ah_authored_get_max_output_frames(const AhAuthoredEngine* engine,
                                           uint32_t input_frame_count,
                                           uint32_t input_flags) {
    if (engine == nullptr) return 0U;
    const bool resets =
        (input_flags & (AH_AUTHORED_INPUT_STREAM_START |
                        AH_AUTHORED_INPUT_DISCONTINUITY)) != 0U;
    const uint64_t total = (resets ? 0U : engine->accumulatedFrames) +
                           static_cast<uint64_t>(input_frame_count);
    uint64_t count = total / engine->hopFrames;
    if ((input_flags & AH_AUTHORED_INPUT_STREAM_END) != 0U &&
        total % engine->hopFrames != 0U) {
        ++count;
    }
    return count > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(count);
}

AhStatus ah_authored_process_i16(AhAuthoredEngine* engine,
                                 const AhAuthoredProcessInput* input,
                                 AhAuthoredHapticFrame* out_frames,
                                 uint32_t out_capacity,
                                 uint32_t* out_count) {
    if (out_count == nullptr) return AH_STATUS_INVALID_ARGUMENT;
    *out_count = 0U;
    constexpr uint32_t kValidInputFlags =
        static_cast<uint32_t>(AH_AUTHORED_INPUT_STREAM_START) |
        static_cast<uint32_t>(AH_AUTHORED_INPUT_DISCONTINUITY) |
        static_cast<uint32_t>(AH_AUTHORED_INPUT_STREAM_END);
    if (engine == nullptr || input == nullptr ||
        input->struct_size < AH_AUTHORED_PROCESS_INPUT_V2_SIZE ||
        (input->frame_count > 0U && input->interleaved_pcm == nullptr) ||
        static_cast<uint64_t>(input->frame_count) * 2U >
            std::numeric_limits<size_t>::max() ||
        (input->flags & ~kValidInputFlags) != 0U) {
        return AH_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t durationUs =
        static_cast<uint64_t>(input->frame_count) * 1000000ULL /
        engine->sampleRate;
    if (input->first_sample_time_us >
        std::numeric_limits<uint64_t>::max() - durationUs) {
        return AH_STATUS_BAD_STATE;
    }

    const bool explicitReset =
        (input->flags & (AH_AUTHORED_INPUT_STREAM_START |
                         AH_AUTHORED_INPUT_DISCONTINUITY)) != 0U;
    const bool sequenceGap = engine->hasExpectedSequence &&
                             input->sequence_number != engine->expectedSequence;
    const uint32_t bufferedBefore = (explicitReset || sequenceGap)
        ? 0U
        : engine->accumulatedFrames;
    const uint64_t total = static_cast<uint64_t>(bufferedBefore) +
                           input->frame_count;
    uint64_t required = total / engine->hopFrames;
    if ((input->flags & AH_AUTHORED_INPUT_STREAM_END) != 0U &&
        total % engine->hopFrames != 0U) {
        ++required;
    }
    if (required > out_capacity) return AH_STATUS_BUFFER_TOO_SMALL;
    if (required > 0U && out_frames == nullptr) return AH_STATUS_INVALID_ARGUMENT;

    if (explicitReset || sequenceGap) ResetStream(*engine);
    engine->expectedSequence = input->sequence_number + 1U;
    engine->hasExpectedSequence = true;

    for (uint32_t frameIndex = 0; frameIndex < input->frame_count; ++frameIndex) {
        const size_t offset = static_cast<size_t>(frameIndex) * 2U;
        const float values[2] = {
            static_cast<float>(input->interleaved_pcm[offset]) / 32768.0F,
            static_cast<float>(input->interleaved_pcm[offset + 1U]) / 32768.0F};
        for (uint32_t laneIndex = 0; laneIndex < 2U; ++laneIndex) {
            LaneAccumulator& lane = engine->lanes[laneIndex];
            const float value = values[laneIndex];
            lane.lowPass += engine->lowPassAlpha * (value - lane.lowPass);
            lane.squareSum += static_cast<double>(value) * value;
            lane.lowSquareSum += static_cast<double>(lane.lowPass) * lane.lowPass;
            lane.peak = std::max(lane.peak, std::abs(value));
            if (lane.hasPreviousSample &&
                ((value >= 0.0F) != (lane.previousSample >= 0.0F))) {
                ++lane.zeroCrossings;
            }
            lane.previousSample = value;
            lane.hasPreviousSample = true;
        }
        engine->crossSum += static_cast<double>(values[0]) * values[1];
        ++engine->accumulatedFrames;

        if (engine->accumulatedFrames == engine->hopFrames) {
            const uint64_t timestamp = input->first_sample_time_us +
                (static_cast<uint64_t>(frameIndex) + 1U) * 1000000ULL /
                    engine->sampleRate;
            uint32_t flags = AH_AUTHORED_FRAME_NONE;
            if ((input->flags & AH_AUTHORED_INPUT_STREAM_END) != 0U &&
                frameIndex + 1U == input->frame_count) {
                flags |= AH_AUTHORED_FRAME_STREAM_END;
            }
            EmitFrame(*engine, out_frames[*out_count], timestamp,
                      input->sequence_number, flags);
            ++(*out_count);
            ResetWindow(*engine);
        }
    }

    if ((input->flags & AH_AUTHORED_INPUT_STREAM_END) != 0U) {
        if (engine->accumulatedFrames > 0U) {
            const uint64_t timestamp = input->first_sample_time_us +
                static_cast<uint64_t>(input->frame_count) * 1000000ULL /
                    engine->sampleRate;
            EmitFrame(*engine, out_frames[*out_count], timestamp,
                      input->sequence_number,
                      AH_AUTHORED_FRAME_PARTIAL |
                          AH_AUTHORED_FRAME_STREAM_END);
            ++(*out_count);
            ResetWindow(*engine);
        }
        engine->hasExpectedSequence = false;
        engine->markDiscontinuity = true;
    }

    return *out_count == 0U ? AH_STATUS_OK : AH_STATUS_OUTPUT_AVAILABLE;
}

void ah_authored_reset(AhAuthoredEngine* engine) {
    if (engine != nullptr) ResetStream(*engine);
}

void ah_authored_destroy(AhAuthoredEngine* engine) {
    delete engine;
}

} // extern "C"
