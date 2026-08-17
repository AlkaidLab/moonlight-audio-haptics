// SPDX-License-Identifier: Apache-2.0

#include "moonlight_haptics/authored_haptics.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

static_assert(sizeof(AhAuthoredProcessInput) ==
                  AH_AUTHORED_PROCESS_INPUT_V2_SIZE,
              "authored input ABI prefix must cover the complete struct");

namespace {

constexpr uint32_t kMinimumSampleRate = 8000;
constexpr uint32_t kMaximumSampleRate = 192000;
constexpr float kSilenceFloor = 1.0F / 32768.0F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTactileHighPassHz = 50.0F;
constexpr float kTactileLowPassHz = 400.0F;
constexpr float kLowBandSplitHz = 160.0F;
constexpr float kZeroCrossingWindowSeconds = 0.040F;
constexpr std::array<double, 2> kButterworthFourthOrderQ = {
    0.541196100146197,
    1.306562964876377};

struct BiquadCoefficients {
    double b0 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadState {
    double z1 = 0.0;
    double z2 = 0.0;
};

struct LaneAccumulator {
    double squareSum = 0.0;
    double lowSquareSum = 0.0;
    double highSquareSum = 0.0;
    float peak = 0.0F;
    BiquadState tactileHighPass[2];
    BiquadState tactileLowPass[2];
    float lowBandLow1 = 0.0F;
    float lowBandLow2 = 0.0F;
    float highBandLow1 = 0.0F;
    float highBandLow2 = 0.0F;
    float previousSample = 0.0F;
    float previousRms = 0.0F;
    float previousPeak = 0.0F;
    std::vector<uint8_t> crossingWindow;
    uint32_t crossingWindowPosition = 0;
    uint32_t crossingWindowSamples = 0;
    uint32_t crossingWindowSum = 0;
    bool hasPreviousSample = false;
};

float Clamp01(float value) noexcept {
    return std::max(0.0F, std::min(1.0F, value));
}

float FilterAlpha(float frequency, float sampleRate) noexcept {
    return 1.0F - std::exp(-2.0F * kPi * frequency / sampleRate);
}

float LowPass(float input, float alpha, float& state) noexcept {
    state += alpha * (input - state);
    return state;
}

BiquadCoefficients MakeButterworthSection(float frequency,
                                          float sampleRate,
                                          double quality,
                                          bool highPass) noexcept {
    const double omega = 2.0 * static_cast<double>(kPi) * frequency /
                         sampleRate;
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega) / (2.0 * quality);
    const double a0 = 1.0 + alpha;
    const double numerator = highPass ? 1.0 + cosine : 1.0 - cosine;
    return {
        numerator * 0.5 / a0,
        (highPass ? -numerator : numerator) / a0,
        numerator * 0.5 / a0,
        -2.0 * cosine / a0,
        (1.0 - alpha) / a0};
}

double ProcessBiquad(double input,
                     const BiquadCoefficients& coefficients,
                     BiquadState& state) noexcept {
    const double output = coefficients.b0 * input + state.z1;
    state.z1 = coefficients.b1 * input - coefficients.a1 * output + state.z2;
    state.z2 = coefficients.b2 * input - coefficients.a2 * output;
    return output;
}

float FilterTactileBand(LaneAccumulator& lane,
                        float input,
                        const BiquadCoefficients (&highPass)[2],
                        const BiquadCoefficients (&lowPass)[2]) noexcept {
    double value = input;
    for (size_t section = 0; section < 2U; ++section) {
        value = ProcessBiquad(
            value, highPass[section], lane.tactileHighPass[section]);
    }
    for (size_t section = 0; section < 2U; ++section) {
        value = ProcessBiquad(
            value, lowPass[section], lane.tactileLowPass[section]);
    }
    return static_cast<float>(value);
}

float FilterLowBand(LaneAccumulator& lane,
                    float input,
                    float alpha) noexcept {
    float value = LowPass(input, alpha, lane.lowBandLow1);
    return LowPass(value, alpha, lane.lowBandLow2);
}

float FilterHighBand(LaneAccumulator& lane,
                     float input,
                     float alpha) noexcept {
    float value = input - LowPass(input, alpha, lane.highBandLow1);
    value -= LowPass(value, alpha, lane.highBandLow2);
    return value;
}

void PushCrossing(LaneAccumulator& lane, bool crossing) noexcept {
    if (lane.crossingWindow.empty()) return;
    const uint8_t sample = crossing ? 1U : 0U;
    if (lane.crossingWindowSamples == lane.crossingWindow.size()) {
        lane.crossingWindowSum -=
            lane.crossingWindow[lane.crossingWindowPosition];
    } else {
        ++lane.crossingWindowSamples;
    }
    lane.crossingWindow[lane.crossingWindowPosition] = sample;
    lane.crossingWindowSum += sample;
    lane.crossingWindowPosition = static_cast<uint32_t>(
        (lane.crossingWindowPosition + 1U) % lane.crossingWindow.size());
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
          lowBandAlpha(FilterAlpha(
              kLowBandSplitHz, static_cast<float>(sampleRateValue))) {
        for (size_t section = 0; section < kButterworthFourthOrderQ.size();
             ++section) {
            tactileHighPass[section] = MakeButterworthSection(
                kTactileHighPassHz, static_cast<float>(sampleRateValue),
                kButterworthFourthOrderQ[section], true);
            tactileLowPass[section] = MakeButterworthSection(
                kTactileLowPassHz, static_cast<float>(sampleRateValue),
                kButterworthFourthOrderQ[section], false);
        }
        const uint32_t crossingWindowFrames = std::max(
            1U, static_cast<uint32_t>(std::lround(
                    static_cast<float>(sampleRateValue) *
                    kZeroCrossingWindowSeconds)));
        for (LaneAccumulator& lane : lanes) {
            lane.crossingWindow.resize(crossingWindowFrames, 0U);
        }
    }

    uint32_t sampleRate;
    uint32_t hopFrames;
    BiquadCoefficients tactileHighPass[2];
    BiquadCoefficients tactileLowPass[2];
    float lowBandAlpha;
    uint32_t accumulatedFrames = 0;
    uint32_t expectedSequence = 0;
    uint64_t timelineAnchorUs = 0;
    uint64_t timelineFrames = 0;
    bool hasExpectedSequence = false;
    bool hasTimeline = false;
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
        lane.highSquareSum = 0.0;
        lane.peak = 0.0F;
    }
}

void ResetStream(AhAuthoredEngine& engine) noexcept {
    ResetWindow(engine);
    engine.hasExpectedSequence = false;
    engine.hasTimeline = false;
    engine.timelineAnchorUs = 0;
    engine.timelineFrames = 0;
    engine.markDiscontinuity = true;
    for (LaneAccumulator& lane : engine.lanes) {
        for (BiquadState& state : lane.tactileHighPass) state = {};
        for (BiquadState& state : lane.tactileLowPass) state = {};
        lane.lowBandLow1 = 0.0F;
        lane.lowBandLow2 = 0.0F;
        lane.highBandLow1 = 0.0F;
        lane.highBandLow2 = 0.0F;
        lane.previousSample = 0.0F;
        lane.previousRms = 0.0F;
        lane.previousPeak = 0.0F;
        std::fill(lane.crossingWindow.begin(), lane.crossingWindow.end(), 0U);
        lane.crossingWindowPosition = 0U;
        lane.crossingWindowSamples = 0U;
        lane.crossingWindowSum = 0U;
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
        lane.rms_amplitude = Clamp01(rms);
        lane.peak_amplitude = Clamp01(source.peak);
        lane.transient_strength = Clamp01(
            std::max(0.0F, rms - source.previousRms) * 4.0F +
            std::max(0.0F, source.peak - source.previousPeak) * 0.5F);
        lane.low_band_ratio = Clamp01(
            static_cast<float>(source.lowSquareSum /
                (source.lowSquareSum + source.highSquareSum + 1.0e-12)));
        lane.zero_crossing_rate_hz = source.crossingWindowSamples > 0U
            ? static_cast<float>(source.crossingWindowSum) *
                  static_cast<float>(engine.sampleRate) /
                  static_cast<float>(source.crossingWindowSamples)
            : 0.0F;
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

bool TimestampForFramePosition(uint64_t anchorUs,
                               uint64_t framePosition,
                               uint32_t sampleRate,
                               uint64_t& timestampUs) noexcept {
    const uint64_t wholeSeconds = framePosition / sampleRate;
    const uint64_t remainingFrames = framePosition % sampleRate;
    if (wholeSeconds > std::numeric_limits<uint64_t>::max() / 1000000ULL) {
        return false;
    }
    const uint64_t deltaUs = wholeSeconds * 1000000ULL +
        remainingFrames * 1000000ULL / sampleRate;
    if (anchorUs > std::numeric_limits<uint64_t>::max() - deltaUs) {
        return false;
    }
    timestampUs = anchorUs + deltaUs;
    return true;
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
    const bool explicitReset =
        (input->flags & (AH_AUTHORED_INPUT_STREAM_START |
                         AH_AUTHORED_INPUT_DISCONTINUITY)) != 0U;
    const bool sequenceGap = engine->hasExpectedSequence &&
                             input->sequence_number != engine->expectedSequence;
    const bool startsTimeline = explicitReset || sequenceGap || !engine->hasTimeline;
    const uint64_t timelineFramesBefore = startsTimeline ? 0U : engine->timelineFrames;
    if (timelineFramesBefore > std::numeric_limits<uint64_t>::max() - input->frame_count) {
        return AH_STATUS_BAD_STATE;
    }
    const uint64_t timelineFramesAfter = timelineFramesBefore + input->frame_count;
    uint64_t inputEndTimestamp = 0;
    if (!TimestampForFramePosition(
            startsTimeline ? input->first_sample_time_us : engine->timelineAnchorUs,
            timelineFramesAfter, engine->sampleRate, inputEndTimestamp)) {
        return AH_STATUS_BAD_STATE;
    }
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
    if (!engine->hasTimeline) {
        engine->timelineAnchorUs = input->first_sample_time_us;
        engine->timelineFrames = 0;
        engine->hasTimeline = true;
    }
    engine->expectedSequence = input->sequence_number + 1U;
    engine->hasExpectedSequence = true;

    for (uint32_t frameIndex = 0; frameIndex < input->frame_count; ++frameIndex) {
        const size_t offset = static_cast<size_t>(frameIndex) * 2U;
        const float values[2] = {
            static_cast<float>(input->interleaved_pcm[offset]) / 32768.0F,
            static_cast<float>(input->interleaved_pcm[offset + 1U]) / 32768.0F};
        float tactileValues[2] = {};
        for (uint32_t laneIndex = 0; laneIndex < 2U; ++laneIndex) {
            LaneAccumulator& lane = engine->lanes[laneIndex];
            const float tactile = FilterTactileBand(
                lane, values[laneIndex], engine->tactileHighPass,
                engine->tactileLowPass);
            tactileValues[laneIndex] = tactile;
            const float lowBand = FilterLowBand(
                lane, tactile, engine->lowBandAlpha);
            const float highBand = FilterHighBand(
                lane, tactile, engine->lowBandAlpha);
            lane.squareSum += static_cast<double>(tactile) * tactile;
            lane.lowSquareSum += static_cast<double>(lowBand) * lowBand;
            lane.highSquareSum += static_cast<double>(highBand) * highBand;
            lane.peak = std::max(lane.peak, std::abs(tactile));
            const bool crossing = lane.hasPreviousSample &&
                ((tactile >= 0.0F) != (lane.previousSample >= 0.0F));
            PushCrossing(lane, crossing);
            lane.previousSample = tactile;
            lane.hasPreviousSample = true;
        }
        engine->crossSum +=
            static_cast<double>(tactileValues[0]) * tactileValues[1];
        ++engine->accumulatedFrames;
        ++engine->timelineFrames;

        if (engine->accumulatedFrames == engine->hopFrames) {
            uint64_t timestamp = 0;
            const bool validTimestamp = TimestampForFramePosition(
                engine->timelineAnchorUs, engine->timelineFrames,
                engine->sampleRate, timestamp);
            (void)validTimestamp;
            assert(validTimestamp);
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
            uint64_t timestamp = 0;
            const bool validTimestamp = TimestampForFramePosition(
                engine->timelineAnchorUs, engine->timelineFrames,
                engine->sampleRate, timestamp);
            (void)validTimestamp;
            assert(validTimestamp);
            EmitFrame(*engine, out_frames[*out_count], timestamp,
                      input->sequence_number,
                      AH_AUTHORED_FRAME_PARTIAL |
                          AH_AUTHORED_FRAME_STREAM_END);
            ++(*out_count);
            ResetWindow(*engine);
        }
        ResetStream(*engine);
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
