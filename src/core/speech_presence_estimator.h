// SPDX-License-Identifier: Apache-2.0

#ifndef MOONLIGHT_HAPTICS_CORE_SPEECH_PRESENCE_ESTIMATOR_H
#define MOONLIGHT_HAPTICS_CORE_SPEECH_PRESENCE_ESTIMATOR_H

#include "core/speech_detector.h"

#include <array>
#include <cstdint>
#include <memory>

namespace moonlight::haptics::core {

/**
 * Converts arbitrary SDK PCM into allocation-free 10 ms VAD frames and
 * exposes smoothed speech and stereo-centre evidence to GAME authoring.
 */
class SpeechPresenceEstimator {
public:
    SpeechPresenceEstimator(
        uint32_t sampleRate,
        uint32_t channelCount,
        std::unique_ptr<SpeechDetector> detector = nullptr);

    void PushInterleavedSample(const int16_t* samples) noexcept;
    void Reset() noexcept;

    float SpeechProbability() const noexcept { return speechProbability_; }
    float CenterDominance() const noexcept { return centerDominance_; }
    uint32_t VadSampleRate() const noexcept { return vadSampleRate_; }

private:
    static constexpr uint32_t kMaximumVadFrameSamples = 480U;

    void PushVadSample(int16_t sample) noexcept;
    void CompleteVadFrame() noexcept;

    uint32_t sampleRate_ = 0U;
    uint32_t channelCount_ = 0U;
    uint32_t vadSampleRate_ = 0U;
    uint32_t vadFrameSamples_ = 0U;
    std::unique_ptr<SpeechDetector> detector_;
    std::array<int16_t, kMaximumVadFrameSamples> vadFrame_{};
    uint32_t vadWriteIndex_ = 0U;
    uint32_t resamplePhase_ = 0U;
    int64_t resampleSum_ = 0;
    uint32_t resampleCount_ = 0U;
    double centerMidEnergy_ = 0.0;
    double centerSideEnergy_ = 0.0;
    float speechProbability_ = 0.0F;
    float centerDominance_ = 0.5F;
};

} // namespace moonlight::haptics::core

#endif // MOONLIGHT_HAPTICS_CORE_SPEECH_PRESENCE_ESTIMATOR_H
