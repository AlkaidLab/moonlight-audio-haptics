// SPDX-License-Identifier: Apache-2.0

#ifndef MOONLIGHT_HAPTICS_DSP_BIQUAD_H
#define MOONLIGHT_HAPTICS_DSP_BIQUAD_H

#include <array>
#include <cmath>

namespace moonlight::haptics::dsp {

inline constexpr std::array<double, 2> kButterworthFourthOrderQ = {
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

// Radians per sample; callers retain their frequency-conversion precision.
inline BiquadCoefficients MakeButterworthSection(double angularFrequency,
                                                double quality,
                                                bool highPass) noexcept {
    const double cosine = std::cos(angularFrequency);
    const double alpha = std::sin(angularFrequency) / (2.0 * quality);
    const double a0 = 1.0 + alpha;
    const double numerator = highPass ? 1.0 + cosine : 1.0 - cosine;
    return {
        numerator * 0.5 / a0,
        (highPass ? -numerator : numerator) / a0,
        numerator * 0.5 / a0,
        -2.0 * cosine / a0,
        (1.0 - alpha) / a0};
}

inline double ProcessBiquad(double input,
                            const BiquadCoefficients& coefficients,
                            BiquadState& state) noexcept {
    const double output = coefficients.b0 * input + state.z1;
    state.z1 = coefficients.b1 * input - coefficients.a1 * output + state.z2;
    state.z2 = coefficients.b2 * input - coefficients.a2 * output;
    return output;
}

} // namespace moonlight::haptics::dsp

#endif // MOONLIGHT_HAPTICS_DSP_BIQUAD_H
