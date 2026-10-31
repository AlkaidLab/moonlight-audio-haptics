// SPDX-License-Identifier: Apache-2.0
#include "moonlight_haptics/authored_events.h"
#include "dsp/biquad.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace {
namespace dsp = moonlight::haptics::dsp;
using dsp::kButterworthFourthOrderQ;

constexpr double pi = 3.14159265358979323846;
constexpr double rate = 48000.0;
constexpr std::uint64_t history_us = 20000;
constexpr std::uint64_t attack_ownership_us = 25000;
const std::array<double, 128> spectrum_window = [] {
    std::array<double, 128> values{};
    for (unsigned i = 0; i < values.size(); ++i)
        values[i] = 0.5 - 0.5 * std::cos(2 * pi * i / (values.size() - 1));
    return values;
}();
const std::array<double, 22> spectrum_coefficients = [] {
    std::array<double, 22> values{};
    for (unsigned i = 0; i < values.size(); ++i)
        values[i] = 2 * std::cos(2 * pi * (i + 4) / 128);
    return values;
}();
std::atomic<std::uint32_t> next_epoch{1};
std::uint64_t elapsed_us(std::uint64_t samples) {
    return samples / 48000 * 1000000 + samples % 48000 * 1000000 / 48000;
}
float unit(double value) { return static_cast<float>(std::clamp(value, 0.0, 1.0)); }
struct biquad_t {
    dsp::BiquadCoefficients coefficients;
    dsp::BiquadState state;
    double push(double x) {
        return dsp::ProcessBiquad(x, coefficients, state);
    }
    void clear() { state = {}; }
};
biquad_t filter(double hz, double q, bool high) {
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
    void clear() {
        for (auto &s : sections)
            s.clear();
    }
};
struct peak_tracker_t {
    double previous{}, peak{}, reference{}, attack_peak{};
    std::uint64_t cycle_start{}, last_attack{};
    bool have_sample{}, armed{true}, have_attack{};
    // Carrier-aligned peaks make a steady sine invariant to chunk alignment.
    bool push(double x, std::uint64_t time, std::uint64_t &event_time, float &strength) {
        bool onset = false;
        if (have_sample && ((x >= 0) != (previous >= 0))) {
            if (peak < attack_peak * 0.65)
                armed = true;
            const double increase = peak - reference;
            if (armed && peak >= 0.0035 && increase >= 0.0015 + reference * 0.75) {
                onset = true;
                event_time = cycle_start;
                strength = unit(peak);
                last_attack = time;
                have_attack = true;
                attack_peak = peak;
                armed = false;
            }
            if (!armed)
                attack_peak = std::max(attack_peak, peak);
            reference = peak;
            peak = 0;
            cycle_start = time;
        }
        if (!have_sample || peak < 0.000001)
            cycle_start = time;
        peak = std::max(peak, std::abs(x));
        previous = x;
        have_sample = true;
        return onset;
    }
};
struct spectrum_context_t {
    std::array<double, 128> samples{};
    unsigned position{}, filled{}, decimation{}, since_update{};
    double sum{}, flatness{}, flatness_average{};
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
        for (const auto coefficient : spectrum_coefficients) {
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
        }
        flatness = arithmetic > 1e-8 ? std::exp(logarithmic / 22) / (arithmetic / 22) : 0;
        flatness_average += 0.11750309741540454 * (flatness - flatness_average);
    }
    bool broadband() const { return filled == samples.size() && flatness_average > 0.45; }
};
struct lane_t {
    bandpass_t tactile{50, 400}, lower{50, 180}, upper{180, 400};
    std::array<biquad_t, 4> onset_upper_highpass{
        filter(180, kButterworthFourthOrderQ[0], true),
        filter(180, kButterworthFourthOrderQ[1], true),
        filter(180, kButterworthFourthOrderQ[0], true),
        filter(180, kButterworthFourthOrderQ[1], true)};
    peak_tracker_t lower_peaks, upper_peaks, isolated_upper_peaks;
    spectrum_context_t spectrum;
    double low_energy{}, high_energy{};
    double measured_low_energy{}, measured_high_energy{};
    std::uint64_t input_active_until{};
    double low_level{}, high_level{};
    std::uint64_t low_owned_until{}, high_owned_until{};
    std::array<double, 1536> low_history{}, high_history{};
    unsigned history_position{};
    void clear() {
        tactile.clear();
        lower.clear();
        upper.clear();
        lower_peaks = {};
        upper_peaks = {};
        isolated_upper_peaks = {};
        measured_low_energy = measured_high_energy = 0;
        input_active_until = 0;
        low_energy = high_energy = low_level = high_level = 0;
        spectrum = {};
        low_owned_until = high_owned_until = 0;
        for (auto &section : onset_upper_highpass)
            section.clear();
        low_history = {};
        high_history = {};
        history_position = 0;
    }
};
struct event_t {
    struct component_t {
        std::uint64_t created_at{};
        double background{}, peak{}, held_energy{};
        float strength{};
        bool pending{}, isolated{};
    };
    std::uint32_t id{};
    std::uint64_t time{};
    float left{}, right{}, confidence{1}, sharpness{};
    std::uint64_t created_at{};
    std::uint64_t sealed_at{};
    std::array<component_t, 4> components{};
    bool mature(std::uint64_t now) const {
        return (left > 0 || right > 0) && now - created_at >= 6000 &&
               std::none_of(components.begin(), components.end(),
                            [](const auto &component) { return component.pending; });
    }
};
} // namespace

struct AhAuthoredEventEngine {
    std::array<lane_t, 2> lanes;
    std::array<event_t, 8> events{};
    unsigned event_count{};
    std::uint32_t epoch{}, frame_sequence{}, event_sequence{}, expected_source{};
    std::uint64_t anchor{}, sample_count{};
    bool active{}, seen_source{};
    void reset(std::uint64_t timestamp) {
        for (auto &lane : lanes)
            lane.clear();
        event_count = 0;
        sample_count = 0;
        anchor = timestamp;
        frame_sequence = event_sequence = 0;
        epoch = next_epoch.fetch_add(1);
        if (epoch == 0)
            epoch = next_epoch.fetch_add(1);
        active = true;
    }
    void expire(std::uint64_t now) {
        unsigned kept = 0;
        for (unsigned i = 0; i < event_count; ++i) {
            const auto retained_at = events[i].sealed_at ? events[i].sealed_at : events[i].created_at;
            if (now >= retained_at && now - retained_at <= history_us)
                events[kept++] = events[i];
        }
        event_count = kept;
    }
    void candidate(std::uint64_t time, unsigned lane, float strength, bool upper,
                   bool isolated, double background, std::uint64_t now) {
        if (lanes[lane].spectrum.broadband())
            return;
        // Near-simultaneous bands/lanes are one physical attack with two strengths.
        event_t *target = nullptr;
        for (unsigned i = event_count; i > 0; --i) {
            auto &candidate = events[i - 1];
            const auto distance =
                time > candidate.time ? time - candidate.time : candidate.time - time;
            // The lower filter's delayed ringing belongs to the same upper-band
            // attack. Still qualify its shape before giving it body ownership.
            const auto &upper_component = candidate.components[lane * 2 + 1];
            if ((!upper && upper_component.strength > 0 &&
                 now - upper_component.created_at <= attack_ownership_us) ||
                (!candidate.sealed_at && now - candidate.created_at < 6000 && distance <= 6000)) {
                target = &candidate;
                break;
            }
        }
        if (!target) {
            // Confirmed upper attacks can outlive their redundant event record.
            // Their lower-filter tail must not open a second point in that gap.
            if (!upper && now < lanes[lane].high_owned_until)
                return;
            // Do not turn a filter's release ringing after PCM stops into a new attack.
            if (now > lanes[lane].input_active_until)
                return;
            if (event_count == events.size()) {
                std::move(events.begin() + 1, events.end(), events.begin());
                --event_count;
            }
            target = &events[event_count++];
            *target = {};
            target->id = ++event_sequence;
            target->time = time;
            target->confidence = 1;
            target->created_at = now;
        }
        auto &component = target->components[lane * 2 + (upper ? 1u : 0u)];
        if (component.pending)
            return;
        const auto &history = upper ? lanes[lane].high_history : lanes[lane].low_history;
        const auto ago = std::min<std::uint64_t>(
            history.size() - 1, (now - time + 5000) * 48000 / 1000000);
        component = {now, background, std::max(0.0, strength - background),
                     history[(lanes[lane].history_position + history.size() - ago) % history.size()],
                     strength, true, isolated};
    }
    void qualify(unsigned lane_index, std::uint64_t now) {
        auto &lane = lanes[lane_index];
        for (unsigned i = 0; i < event_count; ++i) {
            auto &event = events[i];
            for (unsigned band = 0; band < 2; ++band) {
                auto &component = event.components[lane_index * 2 + band];
                if (!component.pending)
                    continue;
                const auto &tracker = band == 0 ? lane.lower_peaks :
                    (component.isolated ? lane.isolated_upper_peaks : lane.upper_peaks);
                const double excess = std::max(0.0,
                    std::max(tracker.reference, tracker.peak) - component.background);
                component.peak = std::max(component.peak, excess);
                // A falling local excess, rather than the carrier frequency, qualifies
                // a compact attack. A valley can arrive before the next dense impulse.
                const bool compact = now - component.created_at >= 2000 &&
                    component.peak > 0 && excess <= component.peak * 0.75;
                if (compact) {
                    if (!event.sealed_at) {
                        auto &strength = lane_index == 0 ? event.left : event.right;
                        strength = std::max(strength, component.strength);
                        event.sharpness = std::max(event.sharpness, band == 0 ? 0.25f : 0.85f);
                    }
                    (band == 0 ? lane.low_energy : lane.high_energy) = component.held_energy;
                    (band == 0 ? lane.low_owned_until : lane.high_owned_until) =
                        component.created_at + attack_ownership_us;
                }
                // Keep the decision inside the existing transport freshness budget.
                if (compact || now - event.time >= 18000)
                    component.pending = false;
            }
        }
    }
    double visible_energy(unsigned lane_index, unsigned band) const {
        double energy = band == 0 ? lanes[lane_index].low_energy : lanes[lane_index].high_energy;
        for (unsigned i = 0; i < event_count; ++i) {
            const auto &component = events[i].components[lane_index * 2 + band];
            if (component.pending)
                energy = std::min(energy, component.held_energy);
        }
        return energy;
    }
};

extern "C" {
AhStatus ah_authored_events_create(const AhAuthoredConfig *config,
                                   AhAuthoredEventEngine **out_engine) {
    if (!out_engine)
        return AH_STATUS_INVALID_ARGUMENT;
    *out_engine = nullptr;
    if (!config || config->struct_size < AH_AUTHORED_CONFIG_V2_SIZE)
        return AH_STATUS_INVALID_ARGUMENT;
    if (config->sample_rate != 48000 || config->channel_count != 2 ||
        (config->analysis_hop_frames != 0 && config->analysis_hop_frames != 240) ||
        config->feature_flags != 0)
        return AH_STATUS_UNSUPPORTED;
    for (const auto reserved : config->reserved)
        if (reserved != 0)
            return AH_STATUS_INVALID_ARGUMENT;
    *out_engine = new (std::nothrow) AhAuthoredEventEngine;
    return *out_engine ? AH_STATUS_OK : AH_STATUS_OUT_OF_MEMORY;
}

AhStatus ah_authored_events_process_i16(AhAuthoredEventEngine *engine,
                                        const AhAuthoredProcessInput *input,
                                        AhAuthoredEventFrame *out_frames, uint32_t out_capacity,
                                        uint32_t *out_count) {
    if (!out_count)
        return AH_STATUS_INVALID_ARGUMENT;
    *out_count = 0;
    if (!engine || !input || input->struct_size < AH_AUTHORED_PROCESS_INPUT_V2_SIZE ||
        input->reserved != 0 || input->frame_count > 240 || (input->flags & ~7u) ||
        (input->frame_count && !input->interleaved_pcm) ||
        (!input->frame_count && !(input->flags & AH_AUTHORED_INPUT_STREAM_END)) ||
        (input->frame_count && (input->flags & AH_AUTHORED_INPUT_STREAM_END)))
        return AH_STATUS_INVALID_ARGUMENT;
    if (!out_capacity)
        return AH_STATUS_BUFFER_TOO_SMALL;
    if (!out_frames)
        return AH_STATUS_INVALID_ARGUMENT;
    const auto count = input->frame_count;
    const auto sequence = input->sequence_number;
    const auto timestamp = input->first_sample_time_us;
    const auto *pcm = input->interleaved_pcm;
    const auto source_flags = input->flags;
    auto &s = *engine;
    const bool explicit_reset =
        (source_flags & (AH_AUTHORED_INPUT_STREAM_START | AH_AUTHORED_INPUT_DISCONTINUITY)) != 0;
    if (s.seen_source && !explicit_reset &&
        (!s.active || static_cast<std::int32_t>(sequence - s.expected_source) < 0))
        return AH_STATUS_BAD_STATE;
    const bool reset = !s.active || explicit_reset || sequence != s.expected_source;
    const auto anchor = reset ? timestamp : s.anchor;
    const auto samples = reset ? 0 : s.sample_count;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto reserve = count ? attack_ownership_us : 0;
    if (anchor > maximum - reserve || samples > maximum - count)
        return AH_STATUS_INVALID_ARGUMENT;
    const auto end_samples = samples + count;
    const auto budget = maximum - anchor - reserve;
    // Compare quotient and remainder before multiplying or mutating stream state.
    if (end_samples / 48000 > budget / 1000000 ||
        end_samples % 48000 * 1000000 / 48000 > budget - end_samples / 48000 * 1000000)
        return AH_STATUS_INVALID_ARGUMENT;
    if (reset)
        s.reset(timestamp);
    s.seen_source = true;
    s.expected_source = sequence + 1;
    std::array<double, 2> raw_energy{}, raw_peak{};
    double tactile_peak = 0;
    constexpr double energy_alpha = 1.0 - 0.9989588756797245; // 20 ms at 48 kHz
    for (unsigned frame = 0; frame < count; ++frame) {
        const auto time = s.anchor + elapsed_us(s.sample_count);
        s.expire(time);
        for (unsigned lane_index = 0; lane_index < 2; ++lane_index) {
            const double x = pcm[frame * 2 + lane_index] / 32768.0;
            auto &lane = s.lanes[lane_index];
            if (std::abs(x) >= 0.0035)
                lane.input_active_until = time + 3000; // Bridge carrier zero crossings.
            raw_energy[lane_index] += x * x;
            raw_peak[lane_index] = std::max(raw_peak[lane_index], std::abs(x));
            const double tactile = lane.tactile.push(x);
            tactile_peak = std::max(tactile_peak, std::abs(tactile));
            lane.spectrum.push(tactile);
            const double low = lane.lower.push(x), high = lane.upper.push(x);
            lane.measured_low_energy += energy_alpha * (low * low - lane.measured_low_energy);
            lane.measured_high_energy += energy_alpha * (high * high - lane.measured_high_energy);
            const bool low_dominant = lane.measured_low_energy > lane.measured_high_energy * 2;
            double onset_high = high;
            for (auto &section : lane.onset_upper_highpass)
                onset_high = section.push(onset_high);
            std::uint64_t event_time = 0;
            float strength = 0;
            const double wide_background = lane.upper_peaks.reference;
            const bool wide_onset = lane.upper_peaks.push(high, time, event_time, strength);
            std::uint64_t isolated_time = 0;
            float isolated_strength = 0;
            const double isolated_background = lane.isolated_upper_peaks.reference;
            const bool isolated_onset =
                lane.isolated_upper_peaks.push(onset_high, time, isolated_time, isolated_strength);
            if (low_dominant) {
                event_time = isolated_time;
                strength = isolated_strength;
            }
            if ((low_dominant && isolated_onset) || (!low_dominant && wide_onset)) {
                s.candidate(event_time, lane_index, strength, true, low_dominant,
                            low_dominant ? isolated_background : wide_background, time);
            }
            const double low_background = lane.lower_peaks.reference;
            if (lane.lower_peaks.push(low, time, event_time, strength)) {
                s.candidate(event_time, lane_index, strength, false, false, low_background, time);
            }
            s.qualify(lane_index, time);
            const bool low_owned = time < lane.low_owned_until;
            const bool high_owned = time < lane.high_owned_until;
            if (!low_owned)
                lane.low_energy += energy_alpha * (low * low - lane.low_energy);
            if (!high_owned)
                lane.high_energy += energy_alpha * (high * high - lane.high_energy);
            const double visible_low = s.visible_energy(lane_index, 0);
            const double visible_high = s.visible_energy(lane_index, 1);
            lane.low_history[lane.history_position] = visible_low;
            lane.high_history[lane.history_position] = visible_high;
            lane.history_position =
                (lane.history_position + 1) % static_cast<unsigned>(lane.low_history.size());
            lane.low_level = std::sqrt(std::max(0.0, visible_low));
            lane.high_level = std::sqrt(std::max(0.0, visible_high));
        }
        ++s.sample_count;
        // Commit evidence on the sample clock, not the caller's chunk boundary.
        const auto now = s.anchor + elapsed_us(s.sample_count);
        for (unsigned i = 0; i < s.event_count; ++i)
            if (!s.events[i].sealed_at && s.events[i].mature(now))
                s.events[i].sealed_at = now;
    }
    const auto end_time = s.anchor + elapsed_us(s.sample_count);
    s.expire(end_time);
    const bool ended = (source_flags & AH_AUTHORED_INPUT_STREAM_END) != 0;
    if (ended)
        s.event_count = 0;
    unsigned mature_count = 0;
    for (unsigned i = 0; i < s.event_count; ++i)
        if (s.events[i].sealed_at)
            ++mature_count;
    AhAuthoredEventFrame result{};
    result.struct_size = AH_AUTHORED_EVENT_FRAME_V1_SIZE;
    result.flags =
        (reset ? static_cast<uint32_t>(AH_AUTHORED_FRAME_DISCONTINUITY) : 0u) |
        (count < 240 && count ? static_cast<uint32_t>(AH_AUTHORED_FRAME_PARTIAL) : 0u) |
        (ended ? static_cast<uint32_t>(AH_AUTHORED_FRAME_STREAM_END) : 0u) |
        (ended || tactile_peak <= 1.0 / 32768 ? static_cast<uint32_t>(AH_AUTHORED_FRAME_SILENT)
                                              : 0u);
    result.profile = AH_AUTHORED_EVENT_PROFILE_1;
    result.epoch = s.epoch;
    result.frame_sequence = s.frame_sequence++;
    result.timestamp_us = end_time;
    result.source_sequence_number = sequence;
    result.source_frame_count = count;
    result.event_count = mature_count;
    for (unsigned i = 0; i < 2; ++i) {
        auto &lane = result.lanes[i];
        lane.continuous_low = ended ? 0 : unit(s.lanes[i].low_level);
        lane.continuous_high = ended ? 0 : unit(s.lanes[i].high_level);
        lane.raw_rms = count ? unit(std::sqrt(raw_energy[i] / count)) : 0;
        lane.raw_peak = unit(raw_peak[i]);
    }
    unsigned emitted = 0;
    for (unsigned i = 0; i < s.event_count; ++i) {
        const auto &event = s.events[i];
        if (!event.sealed_at)
            continue;
        result.events[emitted++] = {
            event.id, 0, event.time, event.left, event.right, event.confidence, event.sharpness};
    }
    if (ended)
        s.active = false;
    out_frames[0] = result;
    *out_count = 1;
    return AH_STATUS_OUTPUT_AVAILABLE;
}

void ah_authored_events_reset(AhAuthoredEventEngine *engine) {
    if (engine) {
        engine->active = false;
        engine->seen_source = false;
    }
}
void ah_authored_events_destroy(AhAuthoredEventEngine *engine) { delete engine; }
} // extern "C"
