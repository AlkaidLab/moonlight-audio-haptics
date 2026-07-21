// SPDX-License-Identifier: Apache-2.0

#include "moonlight_haptics/audio_haptics.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kExpectedSampleRate = 16000U;
constexpr uint32_t kStereoChannels = 2U;
constexpr uint32_t kChunkFrames = 160U;
constexpr uint64_t kWarmupUs = 500000U;
constexpr double kPi = 3.14159265358979323846;

struct WavData {
    uint32_t sampleRate = 0U;
    uint16_t channelCount = 0U;
    std::vector<int16_t> samples;
};

uint16_t ReadLe16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8U);
}

uint32_t ReadLe32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8U) |
        (static_cast<uint32_t>(bytes[2]) << 16U) |
        (static_cast<uint32_t>(bytes[3]) << 24U);
}

bool ChunkIdEquals(const uint8_t* bytes, const char* id) {
    return bytes[0] == static_cast<uint8_t>(id[0]) &&
        bytes[1] == static_cast<uint8_t>(id[1]) &&
        bytes[2] == static_cast<uint8_t>(id[2]) &&
        bytes[3] == static_cast<uint8_t>(id[3]);
}

WavData ReadMonoPcm16Wav(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    const std::istreambuf_iterator<char> begin(stream);
    const std::istreambuf_iterator<char> end;
    const std::vector<uint8_t> bytes(
        begin,
        end);
    assert(bytes.size() >= 44U);
    assert(ChunkIdEquals(bytes.data(), "RIFF"));
    assert(ChunkIdEquals(bytes.data() + 8U, "WAVE"));

    WavData wav;
    uint16_t format = 0U;
    uint16_t bitsPerSample = 0U;
    size_t dataOffset = 0U;
    size_t dataSize = 0U;
    for (size_t offset = 12U; offset + 8U <= bytes.size();) {
        const uint32_t chunkSize = ReadLe32(bytes.data() + offset + 4U);
        const size_t payloadOffset = offset + 8U;
        assert(payloadOffset <= bytes.size());
        assert(static_cast<size_t>(chunkSize) <= bytes.size() - payloadOffset);
        if (ChunkIdEquals(bytes.data() + offset, "fmt ")) {
            assert(chunkSize >= 16U);
            format = ReadLe16(bytes.data() + payloadOffset);
            wav.channelCount = ReadLe16(bytes.data() + payloadOffset + 2U);
            wav.sampleRate = ReadLe32(bytes.data() + payloadOffset + 4U);
            bitsPerSample = ReadLe16(bytes.data() + payloadOffset + 14U);
        } else if (ChunkIdEquals(bytes.data() + offset, "data")) {
            dataOffset = payloadOffset;
            dataSize = chunkSize;
        }
        const size_t paddedSize = static_cast<size_t>(chunkSize) +
            static_cast<size_t>(chunkSize & 1U);
        assert(paddedSize <= bytes.size() - payloadOffset);
        offset = payloadOffset + paddedSize;
    }

    assert(format == 1U);
    assert(wav.channelCount == 1U);
    assert(wav.sampleRate == kExpectedSampleRate);
    assert(bitsPerSample == 16U);
    assert(dataOffset > 0U && dataSize > 0U && dataSize % 2U == 0U);
    wav.samples.reserve(dataSize / 2U);
    for (size_t offset = dataOffset; offset < dataOffset + dataSize; offset += 2U) {
        wav.samples.push_back(static_cast<int16_t>(ReadLe16(bytes.data() + offset)));
    }
    return wav;
}

std::vector<int16_t> CenterStereo(const WavData& wav) {
    std::vector<int16_t> stereo;
    stereo.reserve(wav.samples.size() * kStereoChannels);
    for (const int16_t sample : wav.samples) {
        stereo.push_back(sample);
        stereo.push_back(sample);
    }
    return stereo;
}

int16_t SaturatingAdd(int16_t sample, double addition) {
    const double mixed = static_cast<double>(sample) + addition;
    return static_cast<int16_t>(std::max(
        static_cast<double>(std::numeric_limits<int16_t>::min()),
        std::min(static_cast<double>(std::numeric_limits<int16_t>::max()), mixed)));
}

void MixPhysicalImpacts(std::vector<int16_t>& stereo,
                        const std::array<uint64_t, 4U>& impactTimesUs) {
    constexpr double kImpactFrequencyHz = 72.0;
    constexpr double kImpactDurationSeconds = 0.18;
    constexpr double kImpactAmplitude = 22000.0;
    const uint32_t impactFrames = static_cast<uint32_t>(
        kImpactDurationSeconds * static_cast<double>(kExpectedSampleRate));
    for (const uint64_t impactTimeUs : impactTimesUs) {
        const uint32_t firstFrame = static_cast<uint32_t>(
            impactTimeUs * kExpectedSampleRate / 1000000ULL);
        for (uint32_t frame = 0U; frame < impactFrames; ++frame) {
            const uint32_t targetFrame = firstFrame + frame;
            if (targetFrame * kStereoChannels + 1U >= stereo.size()) break;
            const double time = static_cast<double>(frame) /
                static_cast<double>(kExpectedSampleRate);
            const double envelope = std::exp(-18.0 * time);
            const double body = kImpactAmplitude * envelope *
                std::sin(2.0 * kPi * kImpactFrequencyHz * time);
            const double attack = frame < 24U
                ? 9000.0 * (1.0 - static_cast<double>(frame) / 24.0)
                : 0.0;
            const double addition = body + attack;
            const size_t index = static_cast<size_t>(targetFrame) * kStereoChannels;
            stereo[index] = SaturatingAdd(stereo[index], addition);
            stereo[index + 1U] = SaturatingAdd(stereo[index + 1U], addition);
        }
    }
}

std::vector<uint64_t> ProcessGameTransients(const std::vector<int16_t>& stereo) {
    AhConfig config{};
    assert(ah_config_init(&config, kExpectedSampleRate, kStereoChannels) ==
           AH_STATUS_OK);
    config.requested_scene = AH_SCENE_GAME;
    AhEngine* engine = nullptr;
    assert(ah_create(&config, &engine) == AH_STATUS_OK);
    assert(engine != nullptr);

    std::vector<uint64_t> transientTimesUs;
    const uint32_t totalFrames = static_cast<uint32_t>(
        stereo.size() / kStereoChannels);
    for (uint32_t firstFrame = 0U; firstFrame < totalFrames;
         firstFrame += kChunkFrames) {
        const uint32_t frameCount = std::min(kChunkFrames,
                                             totalFrames - firstFrame);
        const uint32_t capacity = ah_get_max_output_frames(engine, frameCount);
        std::vector<AhHapticFrame> outputs(capacity);
        AhProcessInput input{};
        input.struct_size = AH_PROCESS_INPUT_V1_SIZE;
        input.interleaved_pcm = stereo.data() +
            static_cast<size_t>(firstFrame) * kStereoChannels;
        input.frame_count = frameCount;
        input.first_sample_time_us =
            static_cast<uint64_t>(firstFrame) * 1000000ULL /
            kExpectedSampleRate;
        uint32_t outputCount = 0U;
        const AhStatus status = ah_process_i16(
            engine, &input, outputs.data(), capacity, &outputCount);
        assert(status == AH_STATUS_OK || status == AH_STATUS_OUTPUT_AVAILABLE);
        for (uint32_t index = 0U; index < outputCount; ++index) {
            if ((outputs[index].flags & AH_FRAME_TRANSIENT) != 0U) {
                transientTimesUs.push_back(outputs[index].timestamp_us);
            }
        }
    }
    ah_destroy(engine);
    return transientTimesUs;
}

uint32_t CountAfterWarmup(const std::vector<uint64_t>& times) {
    return static_cast<uint32_t>(std::count_if(
        times.begin(), times.end(),
        [](uint64_t timeUs) { return timeUs >= kWarmupUs; }));
}

uint32_t CountImpactRecall(const std::vector<uint64_t>& transientTimesUs,
                           const std::array<uint64_t, 4U>& impactTimesUs) {
    uint32_t recalled = 0U;
    for (const uint64_t impactTimeUs : impactTimesUs) {
        const bool detected = std::any_of(
            transientTimesUs.begin(), transientTimesUs.end(),
            [impactTimeUs](uint64_t transientTimeUs) {
                return transientTimeUs >= impactTimeUs &&
                    transientTimeUs <= impactTimeUs + 250000ULL;
            });
        if (detected) ++recalled;
    }
    return recalled;
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const WavData wav = ReadMonoPcm16Wav(argv[1]);
    assert(wav.samples.size() >= kExpectedSampleRate * 5U);

    const std::vector<int16_t> dialogue = CenterStereo(wav);
    const std::vector<uint64_t> dialogueTransients =
        ProcessGameTransients(dialogue);

    constexpr std::array<uint64_t, 4U> impactTimesUs{
        900000ULL, 1800000ULL, 2750000ULL, 4300000ULL};
    std::vector<int16_t> dialogueWithImpacts = dialogue;
    for (int16_t& sample : dialogueWithImpacts) {
        sample = static_cast<int16_t>(static_cast<double>(sample) * 0.65);
    }
    MixPhysicalImpacts(dialogueWithImpacts, impactTimesUs);
    const std::vector<uint64_t> mixedTransients =
        ProcessGameTransients(dialogueWithImpacts);

    const uint32_t dialogueFalsePositives = CountAfterWarmup(dialogueTransients);
    const uint32_t impactRecall = CountImpactRecall(mixedTransients, impactTimesUs);
    std::cerr << "dialogue_false_positives=" << dialogueFalsePositives
              << " impact_recall=" << impactRecall << "/"
              << impactTimesUs.size() << std::endl;

    // These are behavioural guardrails, not a benchmark score. Tighten them
    // only after expanding the pinned corpus with representative game mixes.
    assert(dialogueFalsePositives <= 8U);
    assert(impactRecall == static_cast<uint32_t>(impactTimesUs.size()));
    return 0;
}
