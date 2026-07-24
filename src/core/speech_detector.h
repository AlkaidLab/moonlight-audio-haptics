// SPDX-License-Identifier: Apache-2.0

#ifndef MOONLIGHT_HAPTICS_CORE_SPEECH_DETECTOR_H
#define MOONLIGHT_HAPTICS_CORE_SPEECH_DETECTOR_H

#include <cstdint>
#include <memory>

namespace moonlight::haptics::core {

/** Fixed-frame speech decision backend used outside the per-sample hot path. */
class SpeechDetector {
public:
    virtual ~SpeechDetector() = default;

    virtual bool ProcessFrame(const int16_t* monoSamples,
                              uint32_t sampleCount) noexcept = 0;
    virtual void Reset() noexcept = 0;
};

bool IsWebRtcVadSampleRate(uint32_t sampleRate) noexcept;

/** Creates the bundled WebRTC GMM VAD backend in aggressive mode. */
std::unique_ptr<SpeechDetector> CreateWebRtcVadSpeechDetector(
    uint32_t sampleRate);

} // namespace moonlight::haptics::core

#endif // MOONLIGHT_HAPTICS_CORE_SPEECH_DETECTOR_H
