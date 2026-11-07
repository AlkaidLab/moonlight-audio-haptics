// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "dsp/biquad.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// Internal analysis stages. Observation never reads decisions or rendered body;
// attack evidence never mutates either observation or body decomposition.
namespace moonlight::haptics::authored {
namespace dsp = moonlight::haptics::dsp;
using dsp::kButterworthFourthOrderQ;
constexpr double pi = 3.14159265358979323846;
constexpr double rate = 48000.0;
inline const std::array<double, 128> spectrum_window = [] {
    std::array<double, 128> values{};
    for (unsigned i = 0; i < values.size(); ++i)
        values[i] = 0.5 - 0.5 * std::cos(2 * pi * i / (values.size() - 1));
    return values;
}();
inline const std::array<double, 22> spectrum_coefficients = [] {
    std::array<double, 22> values{};
    for (unsigned i = 0; i < values.size(); ++i)
        values[i] = 2 * std::cos(2 * pi * (i + 4) / 128);
    return values;
}();
constexpr double energy_alpha = 1.0 - 0.9989588756797245; // 20 ms at 48 kHz
constexpr double attack_energy_alpha = 1.0 - 0.99584200184511; // 5 ms at 48 kHz
inline float unit(double value) { return static_cast<float>(std::clamp(value, 0.0, 1.0)); }
struct biquad_t {
    dsp::BiquadCoefficients coefficients;
    dsp::BiquadState state;
    double push(double x) {
        return dsp::ProcessBiquad(x, coefficients, state);
    }
};
inline biquad_t filter(double hz, double q, bool high) {
    return {dsp::MakeButterworthSection(2 * pi * hz / rate, q, high), {}};
}
struct bandpass_t {
    std::array<biquad_t, 4> sections;
    bandpass_t(double lo, double hi)
        : sections{filter(lo, kButterworthFourthOrderQ[0], true),
                   filter(lo, kButterworthFourthOrderQ[1], true),
                   filter(hi, kButterworthFourthOrderQ[0], false),
                   filter(hi, kButterworthFourthOrderQ[1], false)} {}
    double push(double x) {
        for (auto &s : sections)
            x = s.push(x);
        return x;
    }
};
struct peak_change_t {
    bool crossed{};
    double peak{}, reference{}, increase{}, variation{};
    std::uint64_t source_time{}, observed_at{};
    unsigned prior_cycles{};
};
struct peak_tracker_t {
    double previous{}, peak{}, reference{}, change_average{};
    std::uint64_t cycle_start{};
    unsigned cycles{};
    bool have_sample{};
    // Measurement only: carrier-aligned peaks do not depend on chunk boundaries.
    peak_change_t push(double x, std::uint64_t now) {
        peak_change_t change;
        if (have_sample && ((x >= 0) != (previous >= 0))) {
            change = {true, peak, reference, peak - reference, change_average, cycle_start, now, cycles};
            change_average += 0.125 * (std::abs(change.increase) - change_average);
            cycles = std::min(cycles + 1, 8u);
            reference = peak;
            peak = 0;
            cycle_start = now;
        }
        if (!have_sample || peak < 0.000001)
            cycle_start = now;
        peak = std::max(peak, std::abs(x));
        previous = x;
        have_sample = true;
        return change;
    }
};
struct spectrum_context_t {
    std::array<double, 128> samples{};
    unsigned position{}, filled{}, decimation{}, since_update{};
    double sum{}, flatness_average{}, core_flatness_average{}, concentration{};
    void push(double tactile) {
        sum += tactile;
        if (++decimation != 24)
            return;
        samples[position] = sum / 24;
        position = (position + 1) % static_cast<unsigned>(samples.size());
        filled = std::min(filled + 1, static_cast<unsigned>(samples.size()));
        sum = 0;
        decimation = 0;
        if (++since_update < 10 || filled < samples.size())
            return;
        since_update = 0;
        double arithmetic = 0, logarithmic = 0;
        double core_arithmetic = 0, core_logarithmic = 0;
        double older_power = 0, previous_power = 0, concentrated_power = 0;
        for (unsigned bin = 0; bin < spectrum_coefficients.size(); ++bin) {
            const auto coefficient = spectrum_coefficients[bin];
            double previous = 0, older = 0;
            for (unsigned n = 0; n < samples.size(); ++n) {
                const double current =
                    samples[(position + n) % samples.size()] * spectrum_window[n] +
                    coefficient * previous - older;
                older = previous;
                previous = current;
            }
            const double power = std::max(1e-16, previous * previous + older * older -
                                                     coefficient * previous * older);
            arithmetic += power;
            logarithmic += std::log(power);
            // Also inspect the interior 93.75..328.125 Hz. Filter skirts can
            // make band-limited continuous noise look spectrally sparse.
            if (bin >= 2 && bin < 18) {
                core_arithmetic += power;
                core_logarithmic += std::log(power);
                concentrated_power = std::max(concentrated_power, older_power + previous_power + power);
                older_power = previous_power;
                previous_power = power;
            }
        }
        const double flatness = arithmetic > 1e-8 ? std::exp(logarithmic / spectrum_coefficients.size()) /
            (arithmetic / spectrum_coefficients.size()) : 0;
        flatness_average += 0.11750309741540454 * (flatness - flatness_average);
        const double core_flatness = core_arithmetic > 1e-8 ?
            std::exp(core_logarithmic / 16) / (core_arithmetic / 16) : 0;
        core_flatness_average += 0.11750309741540454 * (core_flatness - core_flatness_average);
        concentration = core_arithmetic > 1e-8 ? concentrated_power / core_arithmetic : 0;
    }
};

template<class Sample> struct source_history_t {
    std::array<Sample, 1536> samples{};
    unsigned position{};
    const Sample &before(std::uint64_t now, std::uint64_t onset) const {
        // Five milliseconds before the candidate's source anchor, not its
        // later publication time. Capture before appending this sample.
        const auto ago = std::min<std::uint64_t>(samples.size() - 1,
            (now - onset + 5000) * 48000 / 1000000);
        return samples[(position + samples.size() - ago) % samples.size()];
    }
    void push(Sample value) {
        samples[position] = value;
        position = (position + 1) % static_cast<unsigned>(samples.size());
    }
};
struct measured_energy_t {
    double low{}, high{}, fast{};
    double total() const { return low + high; }
};
struct onset_t {
    bool active{}, upper{}, isolated{}, weak{}, independent_rise{};
    std::uint64_t time{};
    float strength{};
    double background{};
};

// Stage 1: input-only filters, peak observations and unmodified energy history.
struct observation_t {
    bandpass_t tactile_filter{50, 400}, lower{50, 180}, upper{180, 400};
    std::array<biquad_t, 4> onset_upper_highpass{
        filter(180, kButterworthFourthOrderQ[0], true),
        filter(180, kButterworthFourthOrderQ[1], true),
        filter(180, kButterworthFourthOrderQ[0], true),
        filter(180, kButterworthFourthOrderQ[1], true)};
    peak_tracker_t lower_peaks, upper_peaks, isolated_upper_peaks;
    spectrum_context_t spectrum;
    measured_energy_t measured;
    source_history_t<measured_energy_t> history;
    std::uint64_t input_active_until{};
    double low{}, high{}, tactile{};
    std::array<peak_change_t, 3> changes{};

    void observe(double x, std::uint64_t now) {
        if (std::abs(x) >= 0.0035)
            input_active_until = now + 3000; // Bridge carrier zero crossings.
        tactile = tactile_filter.push(x);
        spectrum.push(tactile);
        low = lower.push(x);
        high = upper.push(x);
        measured.low += energy_alpha * (low * low - measured.low);
        measured.high += energy_alpha * (high * high - measured.high);
        measured.fast += attack_energy_alpha * (low * low + high * high - measured.fast);
        double isolated_high = high;
        for (auto &section : onset_upper_highpass)
            isolated_high = section.push(isolated_high);
        changes = {upper_peaks.push(high, now), isolated_upper_peaks.push(isolated_high, now),
                   lower_peaks.push(low, now)};
    }
    void remember() { history.push(measured); }
    const peak_tracker_t &tracker(unsigned band, bool isolated) const {
        return band == 0 ? lower_peaks : (isolated ? isolated_upper_peaks : upper_peaks);
    }
};

// Stage 2 starts here: candidate proposal is separate from peak measurement.
struct onset_gate_t {
    double attack_peak{}, weak_peak{}, weak_floor{};
    std::uint64_t weak_until{};
    bool armed{true}, weak_armed{true};

    onset_t propose(const peak_change_t &change, bool upper, bool isolated) {
        onset_t result;
        if (!change.crossed)
            return result;
        result.upper = upper;
        result.isolated = isolated;
        result.time = change.source_time;
        result.strength = unit(change.peak);
        result.background = change.reference;
        result.independent_rise = change.prior_cycles >= 8 && change.increase > change.variation * 4;
        if (change.peak < attack_peak * 0.65)
            armed = true;
        if (armed && change.peak >= 0.0035 && change.increase >= 0.0015 + result.background * 0.75) {
            result.active = true;
            attack_peak = change.peak;
            armed = false;
        }
        if (!armed)
            attack_peak = std::max(attack_peak, change.peak);
        if (change.peak < weak_floor + (weak_peak - weak_floor) * 0.65 || change.observed_at >= weak_until)
            weak_armed = true;
        if (!result.active && weak_armed && change.peak >= 0.0035 &&
            change.increase >= 0.0015 + result.background * 0.25 && result.independent_rise) {
            result.active = result.weak = true;
            weak_floor = result.background;
            weak_peak = change.peak;
            weak_until = change.observed_at + 18000;
            weak_armed = false;
        }
        if (!weak_armed)
            weak_peak = std::max(weak_peak, change.peak);
        return result;
    }
};
struct candidate_detector_t {
    onset_gate_t upper, isolated_upper, lower;
    static bool diffuse(const spectrum_context_t &spectrum) {
        // Keep the original wide-band veto. The interior veto additionally
        // requires diffuse power, preserving concentrated damped pulse trains.
        return spectrum.filled == spectrum.samples.size() && (spectrum.flatness_average > 0.45 ||
            (spectrum.core_flatness_average > 0.45 && spectrum.concentration < 0.5));
    }
    std::array<onset_t, 2> propose(const observation_t &observed) {
        const auto wide = upper.propose(observed.changes[0], true, false);
        const auto isolated = isolated_upper.propose(observed.changes[1], true, true);
        const auto low = lower.propose(observed.changes[2], false, false);
        return {observed.measured.low > observed.measured.high * 2 ? isolated : wide, low};
    }
};

enum class attack_state_t { absent, pending, confirmed, rejected };

// Stage 2: evidence and a bounded decision lifecycle. No body or actuator state.
struct attack_evidence_t {
    attack_state_t state{attack_state_t::absent};
    std::uint64_t created_at{}, decided_at{};
    double background{}, peak{}, strength_background{}, strength_peak{};
    double energy_background{}, energy_peak{};
    float strength{};
    bool isolated{}, weak{}, independent_rise{};

    static attack_evidence_t begin(const observation_t &observed, const onset_t &onset,
                                    std::uint64_t now) {
        attack_evidence_t result;
        const auto &prior = observed.history.before(now, onset.time);
        result.state = attack_state_t::pending;
        result.created_at = now;
        result.background = onset.background;
        result.peak = std::max(0.0, onset.strength - onset.background);
        // Strength is measured against input history, never a held/owned body.
        const double band_energy = onset.upper ? prior.high : prior.low;
        result.strength_background = std::min(onset.background, std::sqrt(2 * band_energy));
        result.strength_peak = std::max(0.0, onset.strength - result.strength_background);
        result.strength = onset.strength;
        result.isolated = onset.isolated;
        result.weak = onset.weak;
        result.energy_background = onset.upper ? prior.fast : prior.total();
        // Two independent kinds of corroboration: a compact energy burst, or a
        // new local rise above the band's established variation. The latter
        // preserves quiet attacks even if a strong background masks total energy.
        // During global attack/release, local novelty alone is not sufficient.
        const bool settled = prior.fast >= prior.total() * 0.5 &&
                             prior.fast <= prior.total() * 1.5;
        result.independent_rise = onset.independent_rise && settled;
        return result;
    }

    // Components grouped into one attack share its source-time budget. A later
    // filter half-cycle is not a new independent event and cannot restart the
    // 18 ms confirmation window (or exceed transport freshness).
    bool advance(const observation_t &observed, unsigned band,
                 std::uint64_t group_source_time, std::uint64_t now) {
        if (state != attack_state_t::pending)
            return false;
        const auto &peaks = observed.tracker(band, isolated);
        const double current_peak = std::max(peaks.reference, peaks.peak);
        const double excess = std::max(0.0, current_peak - background);
        peak = std::max(peak, excess);
        strength_peak = std::max(strength_peak, current_peak - strength_background);
        const double energy = band == 0 ? observed.measured.total() : observed.measured.fast;
        const double energy_excess = std::max(0.0, energy - energy_background);
        energy_peak = std::max(energy_peak, energy_excess);
        const bool compact_shape = now - created_at >= 2000 && peak > 0 && excess <= peak * 0.75;
        const bool energy_burst = energy_peak > energy_background * (band == 0 ? 0.5 : 0.1) &&
                                  energy_excess <= energy_peak * 0.9;
        if (compact_shape && (energy_burst || independent_rise)) {
            state = attack_state_t::confirmed;
            decided_at = now;
            return true;
        }
        if (now - group_source_time >= 18000) {
            state = attack_state_t::rejected;
            decided_at = now;
        }
        return false;
    }
};

// Stage 3: apply confirmed ownership and pending reservations to the continuous
// layer. Its history is only for reconstruction; it never feeds attack evidence.
struct body_decomposition_t {
    static constexpr std::uint64_t ownership_us = 25000;
    std::array<double, 2> energy{}, level{};
    std::array<std::uint64_t, 2> owned_until{};
    source_history_t<std::array<double, 2>> history;

    void claim(unsigned band, double baseline, std::uint64_t created_at) {
        energy[band] = baseline;
        owned_until[band] = created_at + ownership_us;
    }
    void render(const std::array<double, 2> &signal, std::uint64_t now,
                const std::array<double, 2> &pending_ceiling) {
        for (unsigned band = 0; band < 2; ++band) {
            if (now >= owned_until[band])
                energy[band] += energy_alpha * (signal[band] * signal[band] - energy[band]);
            const double visible = std::min(energy[band], pending_ceiling[band]);
            level[band] = std::sqrt(std::max(0.0, visible));
        }
        // Pending reservations affect presentation only. Reusing the clamped
        // output as a later baseline can turn a temporary reservation into a
        // second ownership hold, even after the first candidate is rejected.
        // Confirmed ownership still applies to energy before it is remembered.
        history.push(energy);
    }
};
} // namespace moonlight::haptics::authored
