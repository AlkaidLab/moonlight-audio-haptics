// SPDX-License-Identifier: Apache-2.0
#include "moonlight_haptics/authored_events.h"
#include "core/authored_signal_analysis.h"
#include <atomic>
#include <new>

namespace {
using namespace moonlight::haptics::authored;
constexpr std::uint64_t history_us = 20000;
constexpr std::uint64_t lower_ringing_merge_us = 25000;
std::atomic<std::uint32_t> next_epoch{1};
std::uint64_t elapsed_us(std::uint64_t samples) {
    return samples / 48000 * 1000000 + samples % 48000 * 1000000 / 48000;
}
struct lane_t {
    observation_t observed;
    candidate_detector_t detector;
    body_decomposition_t body;
    // Detector history for merging lower filter ringing into a confirmed upper
    // attack; independent of whether/how the body stage renders that attack.
    std::uint64_t upper_attack_until{};
    void clear() { *this = {}; }
};
struct event_t {
    struct component_t {
        attack_evidence_t attack;
        double body_baseline{};
    };
    std::uint32_t id{};
    std::uint64_t time{};
    float left{}, right{}, sharpness{};
    std::array<float, 2> primary_strength{};
    float primary_sharpness{};
    std::uint64_t created_at{}, sealed_at{};
    std::array<component_t, 4> components{};
    bool mature(std::uint64_t now) const {
        return (left > 0 || right > 0) && now - created_at >= 6000 &&
               (now - time >= 18000 || std::any_of(components.begin(), components.end(),
                   [](const auto &c) { return c.attack.state == attack_state_t::confirmed && !c.attack.weak; })) &&
               std::none_of(components.begin(), components.end(),
                   [](const auto &c) { return c.attack.state == attack_state_t::pending; });
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
    void candidate(const onset_t &onset, unsigned lane_index, std::uint64_t now) {
        auto &lane = lanes[lane_index];
        if (candidate_detector_t::diffuse(lane.observed.spectrum))
            return;
        event_t *target = nullptr;
        for (unsigned i = event_count; i > 0; --i) {
            auto &candidate = events[i - 1];
            const auto distance = onset.time > candidate.time ?
                onset.time - candidate.time : candidate.time - onset.time;
            const auto &upper = candidate.components[lane_index * 2 + 1].attack;
            if ((!onset.upper && upper.strength > 0 && now - upper.created_at <= lower_ringing_merge_us) ||
                (!candidate.sealed_at && now - candidate.created_at < 6000 && distance <= 6000)) {
                target = &candidate;
                break;
            }
        }
        if (!target) {
            if ((!onset.upper && now < lane.upper_attack_until) || now > lane.observed.input_active_until)
                return;
            if (event_count == events.size()) {
                std::move(events.begin() + 1, events.end(), events.begin());
                --event_count;
            }
            target = &events[event_count++];
            *target = {};
            target->id = ++event_sequence;
            target->time = onset.time;
            target->created_at = now;
        }
        const unsigned band = onset.upper ? 1 : 0;
        auto &component = target->components[lane_index * 2 + band];
        const auto &attack = component.attack;
        if (attack.state == attack_state_t::pending && (!attack.weak || onset.weak))
            return;
        if (onset.weak && attack.strength > 0 && !attack.weak)
            return;
        if (!onset.weak && !target->sealed_at &&
            std::none_of(target->components.begin(), target->components.end(),
                [](const auto &c) { return c.attack.strength > 0 && !c.attack.weak; })) {
            target->time = onset.time;
            target->created_at = now;
        }
        component.attack = attack_evidence_t::begin(lane.observed, onset, now);
        component.body_baseline = lane.body.history.before(now, onset.time)[band];
    }

    // Only observations enter eligibility and strength estimation. Publication
    // bookkeeping cannot change observation or continuous-layer state.
    void qualify(unsigned lane_index, std::uint64_t now) {
        auto &lane = lanes[lane_index];
        for (unsigned i = 0; i < event_count; ++i) {
            auto &event = events[i];
            for (unsigned band = 0; band < 2; ++band) {
                auto &attack = event.components[lane_index * 2 + band].attack;
                if (!attack.advance(lane.observed, band, event.time, now))
                    continue;
                if (band == 1)
                    lane.upper_attack_until = attack.created_at + lower_ringing_merge_us;
                if (!event.sealed_at && !attack.weak) {
                    event.primary_strength[lane_index] =
                        std::max(event.primary_strength[lane_index], unit(attack.strength_peak));
                    event.primary_sharpness = std::max(event.primary_sharpness, band == 0 ? 0.25f : 0.85f);
                }
            }
            if (!event.sealed_at) {
                event.sharpness = event.primary_sharpness;
                for (unsigned index = 0; index < 2; ++index) {
                    auto &strength = index == 0 ? event.left : event.right;
                    strength = event.primary_strength[index];
                    if (strength > 0)
                        continue;
                    for (unsigned band = 0; band < 2; ++band) {
                        const auto &attack = event.components[index * 2 + band].attack;
                        if (attack.state == attack_state_t::confirmed && attack.weak) {
                            strength = std::max(strength, unit(attack.strength_peak));
                            event.sharpness = std::max(event.sharpness, band == 0 ? 0.25f : 0.85f);
                        }
                    }
                }
            }
        }
    }

    void decompose(unsigned lane_index, std::uint64_t now) {
        auto &lane = lanes[lane_index];
        std::array<double, 2> ceiling{
            std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        for (unsigned i = 0; i < event_count; ++i) {
            for (unsigned band = 0; band < 2; ++band) {
                const auto &component = events[i].components[lane_index * 2 + band];
                const auto &attack = component.attack;
                if (attack.state == attack_state_t::pending)
                    ceiling[band] = std::min(ceiling[band], component.body_baseline);
                else if (attack.state == attack_state_t::confirmed && attack.decided_at == now)
                    lane.body.claim(band, component.body_baseline, attack.created_at);
            }
        }
        lane.body.render({lane.observed.low, lane.observed.high}, now, ceiling);
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
    const auto reserve = count ? std::max(lower_ringing_merge_us, body_decomposition_t::ownership_us) : 0;
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
    for (unsigned frame = 0; frame < count; ++frame) {
        const auto time = s.anchor + elapsed_us(s.sample_count);
        s.expire(time);
        for (unsigned lane_index = 0; lane_index < 2; ++lane_index) {
            const double x = pcm[frame * 2 + lane_index] / 32768.0;
            auto &lane = s.lanes[lane_index];
            raw_energy[lane_index] += x * x;
            raw_peak[lane_index] = std::max(raw_peak[lane_index], std::abs(x));
            lane.observed.observe(x, time);
            const auto onsets = lane.detector.propose(lane.observed);
            tactile_peak = std::max(tactile_peak, std::abs(lane.observed.tactile));
            for (const auto &onset : onsets)
                if (onset.active)
                    s.candidate(onset, lane_index, time);
            s.qualify(lane_index, time);
            s.decompose(lane_index, time);
            lane.observed.remember();
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
        lane.continuous_low = ended ? 0 : unit(s.lanes[i].body.level[0]);
        lane.continuous_high = ended ? 0 : unit(s.lanes[i].body.level[1]);
        lane.raw_rms = count ? unit(std::sqrt(raw_energy[i] / count)) : 0;
        lane.raw_peak = unit(raw_peak[i]);
    }
    unsigned emitted = 0;
    for (unsigned i = 0; i < s.event_count; ++i) {
        const auto &event = s.events[i];
        if (!event.sealed_at)
            continue;
        result.events[emitted++] = {
            event.id, 0, event.time, event.left, event.right, 1.0f, event.sharpness};
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
