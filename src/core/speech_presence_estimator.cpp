// SPDX-License-Identifier: Apache-2.0

#include "core/speech_presence_estimator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace moonlight::haptics::core {
namespace {

constexpr float kSpeechAttack = 0.65F;
constexpr float kSpeechRelease = 0.10F;
constexpr float kCenterSmoothing = 0.35F;
constexpr float kMonoCenterPrior = 0.65F;
constexpr float kNeutralCenterEvidence = 0.50F;

float InitialCenterDominance(uint32_t channelCount) noexcept {
    return channelCount == 1U ? kMonoCenterPrior : kNeutralCenterEvidence;
}

uint32_t SelectVadSampleRate(uint32_t sampleRate) noexcept {
    if (IsWebRtcVadSampleRate(sampleRate)) return sampleRate;
    return sampleRate < 16000U ? 8000U : 16000U;
}

int16_t ClampToInt16(int64_t value) noexcept {
    return static_cast<int16_t>(std::max<int64_t>(
        -32768, std::min<int64_t>(32767, value)));
}

} // namespace

SpeechPresenceEstimator::SpeechPresenceEstimator(
    uint32_t sampleRate,
    uint32_t channelCount,
    std::unique_ptr<SpeechDetector> detector)
    : sampleRate_(sampleRate),
      channelCount_(channelCount),
      vadSampleRate_(SelectVadSampleRate(sampleRate)),
      vadFrameSamples_(vadSampleRate_ / 100U),
      detector_(detector == nullptr
                    ? CreateWebRtcVadSpeechDetector(vadSampleRate_)
                    : std::move(detector)),
      centerDominance_(InitialCenterDominance(channelCount)) {}

void SpeechPresenceEstimator::PushInterleavedSample(
    const int16_t* samples) noexcept {
    if (samples == nullptr || channelCount_ == 0U) return;

    int64_t mono = 0;
    if (channelCount_ == 2U) {
        const int32_t left = samples[0];
        const int32_t right = samples[1];
        const int32_t mid = (left + right) / 2;
        const int32_t side = (left - right) / 2;
        mono = mid;
        centerMidEnergy_ += static_cast<double>(mid) *
                            static_cast<double>(mid);
        centerSideEnergy_ += static_cast<double>(side) *
                             static_cast<double>(side);
    } else {
        for (uint32_t channel = 0U; channel < channelCount_; ++channel) {
            mono += samples[channel];
        }
        mono /= static_cast<int64_t>(channelCount_);
    }

    if (vadSampleRate_ == sampleRate_) {
        PushVadSample(ClampToInt16(mono));
        return;
    }

    // A causal box-average resampler is sufficient for VAD classification and
    // prevents high-rate game effects from aliasing directly into speech bands.
    resampleSum_ += mono;
    ++resampleCount_;
    resamplePhase_ += vadSampleRate_;
    if (resamplePhase_ >= sampleRate_) {
        resamplePhase_ -= sampleRate_;
        const int64_t averaged = resampleSum_ /
            static_cast<int64_t>(resampleCount_);
        resampleSum_ = 0;
        resampleCount_ = 0U;
        PushVadSample(ClampToInt16(averaged));
    }
}

void SpeechPresenceEstimator::PushVadSample(int16_t sample) noexcept {
    vadFrame_[vadWriteIndex_++] = sample;
    if (vadWriteIndex_ == vadFrameSamples_) {
        CompleteVadFrame();
        vadWriteIndex_ = 0U;
    }
}

void SpeechPresenceEstimator::CompleteVadFrame() noexcept {
    const bool speech = detector_->ProcessFrame(
        vadFrame_.data(), vadFrameSamples_);
    const float speechTarget = speech ? 1.0F : 0.0F;
    const float speechSmoothing = speechTarget > speechProbability_
        ? kSpeechAttack
        : kSpeechRelease;
    speechProbability_ += speechSmoothing *
        (speechTarget - speechProbability_);

    float centerTarget = InitialCenterDominance(channelCount_);
    if (channelCount_ == 2U) {
        const double total = centerMidEnergy_ + centerSideEnergy_;
        centerTarget = total <= 1.0e-9
            ? kNeutralCenterEvidence
            : static_cast<float>(centerMidEnergy_ / total);
    }
    centerDominance_ += kCenterSmoothing *
        (centerTarget - centerDominance_);
    centerMidEnergy_ = 0.0;
    centerSideEnergy_ = 0.0;
}

void SpeechPresenceEstimator::Reset() noexcept {
    detector_->Reset();
    vadFrame_.fill(0);
    vadWriteIndex_ = 0U;
    resamplePhase_ = 0U;
    resampleSum_ = 0;
    resampleCount_ = 0U;
    centerMidEnergy_ = 0.0;
    centerSideEnergy_ = 0.0;
    speechProbability_ = 0.0F;
    centerDominance_ = InitialCenterDominance(channelCount_);
}

} // namespace moonlight::haptics::core
