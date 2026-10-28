// SPDX-License-Identifier: Apache-2.0
#include "moonlight_haptics/authored_events.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <vector>

namespace {
  bool watchAllocations = false;
  unsigned allocations = 0;
  void *
  Allocate(std::size_t size) noexcept {
    if (watchAllocations) ++allocations;
    return std::malloc(size ? size : 1);
  }
}  // namespace
void *
operator new(std::size_t size) {
  if (void *value = Allocate(size))
    return value;
  throw std::bad_alloc();
}
void
operator delete(void *value) noexcept { std::free(value); }
void
operator delete(void *value, std::size_t) noexcept { std::free(value); }
void *
operator new[](std::size_t size) { return ::operator new(size); }
void
operator delete[](void *value) noexcept { ::operator delete(value); }
void
operator delete[](void *value, std::size_t) noexcept { ::operator delete(value); }
// Keep the SDK's nothrow allocation on the same malloc/free test hooks. Otherwise
// ASan intercepts nothrow new but reaches our free-based delete and reports a mismatch.
void *
operator new(std::size_t size, const std::nothrow_t &) noexcept { return Allocate(size); }
void
operator delete(void *value, const std::nothrow_t &) noexcept { ::operator delete(value); }
void *
operator new[](std::size_t size, const std::nothrow_t &) noexcept {
  return ::operator new(size, std::nothrow);
}
void
operator delete[](void *value, const std::nothrow_t &) noexcept { ::operator delete(value); }

namespace {
  using Engine = std::unique_ptr<AhAuthoredEventEngine, decltype(&ah_authored_events_destroy)>;
  Engine
  Create() {
    AhAuthoredConfig config {};
    assert(ah_authored_config_init(&config, 48000) == AH_STATUS_OK);
    AhAuthoredEventEngine *engine = nullptr;
    assert(ah_authored_events_create(&config, &engine) == AH_STATUS_OK);
    return Engine(engine, ah_authored_events_destroy);
  }
  AhAuthoredProcessInput
  Input(const int16_t *pcm, uint32_t count, uint32_t sequence,
    uint32_t flags = 0, uint64_t time = 0) {
    AhAuthoredProcessInput input {};
    input.struct_size = AH_AUTHORED_PROCESS_INPUT_V2_SIZE;
    input.interleaved_pcm = pcm;
    input.frame_count = count;
    input.sequence_number = sequence;
    input.flags = flags;
    input.first_sample_time_us = time;
    return input;
  }
  AhAuthoredEventFrame
  Process(AhAuthoredEventEngine *engine, AhAuthoredProcessInput input,
    AhStatus expected = AH_STATUS_OUTPUT_AVAILABLE, uint32_t capacity = 1) {
    AhAuthoredEventFrame frame {};
    uint32_t count = 99;
    // Monitor only the SDK call, including every mature-event publication in Analyze.
    allocations = 0;
    watchAllocations = true;
    const auto status = ah_authored_events_process_i16(engine, &input, &frame, capacity, &count);
    watchAllocations = false;
    assert(status == expected);
    assert(allocations == 0);
    assert(count == (expected == AH_STATUS_OUTPUT_AVAILABLE ? 1u : 0u));
    if (count) {
      assert(frame.struct_size == AH_AUTHORED_EVENT_FRAME_V1_SIZE);
      assert(frame.profile == AH_AUTHORED_EVENT_PROFILE_1);
    }
    return frame;
  }
  std::vector<int16_t>
  Tone(double seconds, double frequency = 0, double amplitude = 0) {
    const auto frames = static_cast<unsigned>(std::lround(seconds * 48000));
    std::vector<int16_t> pcm(frames * 2);
    for (unsigned i = 0; i < frames; ++i) {
      const double t = i / 48000.0;
      const double ramp = std::clamp(std::min(t / .05, (seconds - t) / .05), 0.0, 1.0);
      pcm[i * 2] = pcm[i * 2 + 1] = static_cast<int16_t>(
        std::lround(amplitude * ramp * std::sin(t * frequency * 6.283185307179586) * 32767));
    }
    return pcm;
  }
  void
  Pulse(std::vector<int16_t> &pcm, double at, unsigned lane) {
    const auto start = static_cast<unsigned>(std::lround(at * 48000));
    for (unsigned i = 0; i < 2400 && (start + i) * 2 + lane < pcm.size(); ++i) {
      const double t = i / 48000.0;
      const double value = .12 * (1 - std::exp(-t / .0007)) * std::exp(-t / .006) *
                           std::sin(t * 270 * 6.283185307179586);
      auto &sample = pcm[(start + i) * 2 + lane];
      sample = static_cast<int16_t>(sample + std::lround(value * 32767));
    }
  }
  struct Result {
    std::map<uint32_t, AhAuthoredEvent> events;
    std::map<uint64_t, std::array<float, 4>> continuous;
  };
  Result
  Analyze(const std::vector<int16_t> &pcm, unsigned chunk) {
    auto engine = Create();
    Result result;
    uint32_t sequence = 0;
    for (unsigned start = 0; start < pcm.size() / 2; start += chunk) {
      const auto count = std::min(chunk, static_cast<unsigned>(pcm.size() / 2) - start);
      const auto frame = Process(
        engine.get(), Input(pcm.data() + start * 2, count, sequence++,
                        start ? 0u : static_cast<uint32_t>(AH_AUTHORED_INPUT_STREAM_START),
                        uint64_t(start) * 1000000 / 48000));
      result.continuous[frame.timestamp_us] = {
        frame.lanes[0].continuous_low, frame.lanes[0].continuous_high,
        frame.lanes[1].continuous_low, frame.lanes[1].continuous_high
      };
      for (unsigned i = 0; i < frame.event_count; ++i) {
        const auto &event = frame.events[i];
        const auto added = result.events.emplace(event.event_id, event);
        if (!added.second) {
          const auto &original = added.first->second;
          assert(original.source_time_us == event.source_time_us);
          assert(original.left_strength == event.left_strength &&
                 original.right_strength == event.right_strength);
          assert(original.confidence == event.confidence &&
                 original.sharpness == event.sharpness);
        }
      }
    }
    const auto end =
      Process(engine.get(), Input(nullptr, 0, sequence, AH_AUTHORED_INPUT_STREAM_END));
    assert((end.flags & AH_AUTHORED_FRAME_STREAM_END) && end.event_count == 0);
    assert(end.lanes[0].continuous_low == 0 && end.lanes[1].continuous_high == 0);
    return result;
  }
  void
  ApiAndLifecycle() {
    auto engine = Create();
    std::array<int16_t, 480> silence {};
    auto input =
      Input(silence.data(), 240, UINT32_MAX - 1, AH_AUTHORED_INPUT_STREAM_START, 1000000);
    Process(engine.get(), input, AH_STATUS_BUFFER_TOO_SMALL, 0);
    input.reserved = 1;
    Process(engine.get(), input, AH_STATUS_INVALID_ARGUMENT);
    input.reserved = 0;
    input.flags |= AH_AUTHORED_INPUT_STREAM_END;
    Process(engine.get(), input, AH_STATUS_INVALID_ARGUMENT);
    input.flags = AH_AUTHORED_INPUT_STREAM_START;
    const auto frame = Process(engine.get(), input);
    const auto epoch = frame.epoch;
    assert(frame.frame_sequence == 0 && frame.timestamp_us == 1005000);
    Process(engine.get(), Input(silence.data(), 240, UINT32_MAX));
    assert(Process(engine.get(), Input(silence.data(), 240, 0)).epoch == epoch);
    input = Input(silence.data(), 240, 0);
    Process(engine.get(), input, AH_STATUS_BAD_STATE);
    const auto gap = Process(engine.get(), Input(silence.data(), 240, 3));
    assert(gap.epoch != epoch && (gap.flags & AH_AUTHORED_FRAME_DISCONTINUITY));
    const auto end = Process(engine.get(), Input(nullptr, 0, 4, AH_AUTHORED_INPUT_STREAM_END));
    assert(end.epoch == gap.epoch && end.event_count == 0);
    input = Input(silence.data(), 240, 5);
    Process(engine.get(), input, AH_STATUS_BAD_STATE);
    ah_authored_events_reset(engine.get());
    assert(Process(engine.get(), input).epoch != end.epoch);
    AhAuthoredConfig config {};
    ah_authored_config_init(&config, 44100);
    AhAuthoredEventEngine *unsupported = nullptr;
    assert(ah_authored_events_create(&config, &unsupported) == AH_STATUS_UNSUPPORTED &&
           unsupported == nullptr);
    ah_authored_config_init(&config, 48000);
    config.reserved[6] = 1;
    assert(ah_authored_events_create(&config, &unsupported) == AH_STATUS_INVALID_ARGUMENT);
    ah_authored_events_reset(nullptr);
    ah_authored_events_destroy(nullptr);
    auto boundary = Create();
    const auto nearEnd = Process(boundary.get(), Input(silence.data(), 240, 0,
                                                   AH_AUTHORED_INPUT_STREAM_START, UINT64_MAX - 30000));
    input = Input(silence.data(), 240, 1);
    Process(boundary.get(), input, AH_STATUS_INVALID_ARGUMENT);
    // Reuse the rejected sequence: overflow must not consume input or reset the epoch.
    const auto boundaryEnd = Process(boundary.get(), Input(nullptr, 0, 1, AH_AUTHORED_INPUT_STREAM_END));
    assert(boundaryEnd.epoch == nearEnd.epoch && boundaryEnd.frame_sequence == 1);
    assert(boundaryEnd.timestamp_us == UINT64_MAX - 25000);
  }
  void
  SignalFidelity() {
    for (double frequency : { 85., 250., 300. }) {
      const auto result = Analyze(Tone(1, frequency, .12), 240);
      for (const auto &sample : result.continuous)
        if (sample.first >= 200000 && sample.first <= 800000) {
          assert(std::max(sample.second[0], sample.second[1]) > .01f);
          assert(std::max(sample.second[2], sample.second[3]) > .01f);
        }
      for (const auto &item : result.events)
        assert(item.second.source_time_us < 200000);
    }
    auto noise = Tone(1);
    uint32_t random = 1;
    for (auto &sample : noise) {
      random = random * 1664525u + 1013904223u;
      sample = static_cast<int16_t>((int(random >> 16) - 32768) / 4);
    }
    // Ignore spectrum warm-up; this steady window must suppress broadband onsets.
    const auto broadband = Analyze(noise, 240);
    for (const auto &item : broadband.events)
      assert(item.second.source_time_us < 500000);
    for (unsigned density : { 25u, 50u, 60u, 80u }) {
      auto pcm = Tone(2.8);
      for (unsigned i = 0; i < density * 2; ++i) {
        Pulse(pcm, .4 + i / double(density), 0);
        Pulse(pcm, .4 + i / double(density), 1);
      }
      const auto result = Analyze(pcm, 240);
      assert(result.events.size() == density * 2);
      unsigned index = 0;
      bool rightActive = false;
      for (const auto &item : result.events) {
        const auto &event = item.second;
        const double source = .4 + index++ / double(density);
        assert(event.source_time_us / 1e6 >= source - .005 &&
               event.source_time_us / 1e6 <= source + .020);
        rightActive |= event.right_strength > 0;
      }
      assert(rightActive);
    }
    auto pcm = Tone(.4);
    for (unsigned i = 0; i < 10; ++i)
      Pulse(pcm, .05 + i * .02, 0);
    const auto a = Analyze(pcm, 240), b = Analyze(pcm, 137);
    assert(a.events.size() == 10 && a.events.size() == b.events.size());
    for (const auto &item : a.events) {
      const auto &other = b.events.at(item.first);
      assert(item.second.source_time_us == other.source_time_us);
      assert(item.second.left_strength == other.left_strength &&
             item.second.right_strength == 0 && other.right_strength == 0);
    }
    // Co-prime chunks share only occasional endpoints; keep the signal active there.
    const auto steady = Tone(1.5, 85, .12);
    const auto continuousA = Analyze(steady, 240), continuousB = Analyze(steady, 137);
    unsigned matched = 0;
    for (const auto &sample : continuousA.continuous) {
      const auto other = continuousB.continuous.find(sample.first);
      if (other == continuousB.continuous.end())
        continue;
      for (unsigned i = 0; i < sample.second.size(); ++i)
        assert(std::abs(sample.second[i] - other->second[i]) <= 1e-6f);
      ++matched;
    }
    assert(matched >= 2);
  }
}  // namespace
int
main() {
  ApiAndLifecycle();
  SignalFidelity();
}
