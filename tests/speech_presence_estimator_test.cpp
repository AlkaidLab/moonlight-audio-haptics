// SPDX-License-Identifier: Apache-2.0

#include "core/speech_detector.h"
#include "core/speech_presence_estimator.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>

namespace {

class AlwaysSpeechDetector final
    : public moonlight::haptics::core::SpeechDetector {
public:
    bool ProcessFrame(const int16_t*, uint32_t sampleCount) noexcept override {
        ++frameCount;
        lastSampleCount = sampleCount;
        return true;
    }

    void Reset() noexcept override { ++resetCount; }

    uint32_t frameCount = 0U;
    uint32_t lastSampleCount = 0U;
    uint32_t resetCount = 0U;
};

void PushStereo(moonlight::haptics::core::SpeechPresenceEstimator& estimator,
                int16_t left,
                int16_t right,
                uint32_t frameCount) {
    const std::array<int16_t, 2U> sample{left, right};
    for (uint32_t frame = 0U; frame < frameCount; ++frame) {
        estimator.PushInterleavedSample(sample.data());
    }
}

void AssertArbitraryRateIsFramedWithoutAllocating() {
    auto detector = std::make_unique<AlwaysSpeechDetector>();
    AlwaysSpeechDetector* detectorView = detector.get();
    moonlight::haptics::core::SpeechPresenceEstimator estimator(
        44100U, 2U, std::move(detector));

    assert(estimator.VadSampleRate() == 16000U);
    PushStereo(estimator, 3000, 3000, 4410U);
    assert(detectorView->frameCount == 10U);
    assert(detectorView->lastSampleCount == 160U);
    assert(estimator.SpeechProbability() > 0.99F);
    assert(estimator.CenterDominance() > 0.98F);

    estimator.Reset();
    assert(detectorView->resetCount == 1U);
    assert(estimator.SpeechProbability() == 0.0F);
    PushStereo(estimator, 3000, -3000, 4410U);
    assert(estimator.SpeechProbability() > 0.99F);
    assert(estimator.CenterDominance() < 0.02F);
}

void AssertBundledVadRejectsSilence() {
    constexpr std::array<uint32_t, 4U> sampleRates{
        8000U, 16000U, 32000U, 48000U};
    std::array<int16_t, 480U> silence{};
    for (const uint32_t sampleRate : sampleRates) {
        auto detector =
            moonlight::haptics::core::CreateWebRtcVadSpeechDetector(sampleRate);
        const uint32_t frameSamples = sampleRate / 100U;
        for (uint32_t frame = 0U; frame < 20U; ++frame) {
            assert(!detector->ProcessFrame(silence.data(), frameSamples));
        }
    }
}

void AssertBundledVadRecognizesVoicedPattern() {
    auto detector =
        moonlight::haptics::core::CreateWebRtcVadSpeechDetector(16000U);
    std::array<int16_t, 160U> frame{};
    uint32_t speechFrames = 0U;
    constexpr double kPi = 3.14159265358979323846;
    for (uint32_t frameIndex = 0U; frameIndex < 100U; ++frameIndex) {
        const double fundamental = frameIndex < 50U ? 120.0 : 175.0;
        for (uint32_t index = 0U; index < frame.size(); ++index) {
            const uint32_t sampleIndex = frameIndex *
                static_cast<uint32_t>(frame.size()) + index;
            const double time = static_cast<double>(sampleIndex) / 16000.0;
            const double envelope = 0.72 + 0.28 *
                std::sin(2.0 * kPi * 4.5 * time);
            const double value = envelope *
                (7200.0 * std::sin(2.0 * kPi * fundamental * time) +
                 3600.0 * std::sin(2.0 * kPi * 2.0 * fundamental * time) +
                 2200.0 * std::sin(2.0 * kPi * 720.0 * time) +
                 1200.0 * std::sin(2.0 * kPi * 2350.0 * time));
            frame[index] = static_cast<int16_t>(value);
        }
        if (detector->ProcessFrame(
                frame.data(), static_cast<uint32_t>(frame.size()))) {
            ++speechFrames;
        }
    }
    assert(speechFrames >= 60U);
}

} // namespace

int main() {
    AssertArbitraryRateIsFramedWithoutAllocating();
    AssertBundledVadRejectsSilence();
    AssertBundledVadRecognizesVoicedPattern();
    return 0;
}
