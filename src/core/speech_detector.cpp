// SPDX-License-Identifier: Apache-2.0

#include "core/speech_detector.h"

#include "fvad.h"

#include <cstddef>
#include <new>
#include <stdexcept>

namespace moonlight::haptics::core {
namespace {

constexpr int kWebRtcVadAggressiveMode = 2;

class WebRtcVadSpeechDetector final : public SpeechDetector {
public:
    explicit WebRtcVadSpeechDetector(uint32_t sampleRate)
        : sampleRate_(sampleRate), instance_(fvad_new()) {
        if (instance_ == nullptr) {
            throw std::bad_alloc();
        }
        if (!Configure()) {
            fvad_free(instance_);
            instance_ = nullptr;
            throw std::invalid_argument("unsupported WebRTC VAD sample rate");
        }
    }

    ~WebRtcVadSpeechDetector() override {
        if (instance_ != nullptr) {
            fvad_free(instance_);
        }
    }

    bool ProcessFrame(const int16_t* monoSamples,
                      uint32_t sampleCount) noexcept override {
        if (instance_ == nullptr || monoSamples == nullptr) {
            return false;
        }
        return fvad_process(instance_, monoSamples,
                            static_cast<std::size_t>(sampleCount)) > 0;
    }

    void Reset() noexcept override {
        if (instance_ == nullptr) return;
        fvad_reset(instance_);
        (void)Configure();
    }

private:
    bool Configure() noexcept {
        return fvad_set_sample_rate(instance_,
                                    static_cast<int>(sampleRate_)) == 0 &&
               fvad_set_mode(instance_, kWebRtcVadAggressiveMode) == 0;
    }

    uint32_t sampleRate_ = 0U;
    Fvad* instance_ = nullptr;
};

} // namespace

bool IsWebRtcVadSampleRate(uint32_t sampleRate) noexcept {
    return sampleRate == 8000U || sampleRate == 16000U ||
           sampleRate == 32000U || sampleRate == 48000U;
}

std::unique_ptr<SpeechDetector> CreateWebRtcVadSpeechDetector(
    uint32_t sampleRate) {
    if (!IsWebRtcVadSampleRate(sampleRate)) {
        throw std::invalid_argument("unsupported WebRTC VAD sample rate");
    }
    return std::make_unique<WebRtcVadSpeechDetector>(sampleRate);
}

} // namespace moonlight::haptics::core
