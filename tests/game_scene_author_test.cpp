// SPDX-License-Identifier: Apache-2.0

#include "core/game_scene_author.h"

#include <cassert>

namespace {

moonlight::haptics::core::FeatureFrame PhysicalImpactFrame() {
    moonlight::haptics::core::FeatureFrame frame;
    frame.lowBandRatio = 0.82F;
    frame.lowNovelty = 0.060F;
    frame.midNovelty = 0.008F;
    frame.highNovelty = 0.002F;
    frame.percussiveNovelty = 0.052F;
    frame.percussiveSalience = 0.92F;
    frame.harmonicSalience = 0.08F;
    frame.percussiveLowBandRatio = 0.76F;
    frame.tactilePeak = 0.090F;
    return frame;
}

moonlight::haptics::core::FeatureFrame SkillAttackFrame() {
    moonlight::haptics::core::FeatureFrame frame;
    frame.lowBandRatio = 0.08F;
    frame.lowNovelty = 0.004F;
    frame.midNovelty = 0.035F;
    frame.highNovelty = 0.050F;
    frame.percussiveNovelty = 0.034F;
    frame.percussiveSalience = 0.82F;
    frame.harmonicSalience = 0.18F;
    frame.percussiveLowBandRatio = 0.05F;
    frame.tactilePeak = 0.030F;
    return frame;
}

moonlight::haptics::core::FeatureFrame RumbleFrame() {
    auto frame = PhysicalImpactFrame();
    frame.percussiveNovelty = 0.002F;
    frame.percussiveSalience = 0.54F;
    frame.harmonicSalience = 0.46F;
    frame.percussiveLowBandRatio = 0.72F;
    frame.tactilePeak = 0.025F;
    return frame;
}

moonlight::haptics::core::FeatureFrame OrchestralFrame() {
    moonlight::haptics::core::FeatureFrame frame;
    frame.lowBandRatio = 0.22F;
    frame.lowNovelty = 0.025F;
    frame.midNovelty = 0.040F;
    frame.highNovelty = 0.022F;
    frame.percussiveNovelty = 0.0007F;
    frame.percussiveSalience = 0.14F;
    frame.harmonicSalience = 0.86F;
    frame.percussiveLowBandRatio = 0.10F;
    frame.tactilePeak = 0.012F;
    return frame;
}

moonlight::haptics::core::FeatureFrame DialogueFrame() {
    moonlight::haptics::core::FeatureFrame frame;
    frame.lowBandRatio = 0.05F;
    frame.lowNovelty = 0.002F;
    frame.midNovelty = 0.030F;
    frame.highNovelty = 0.040F;
    frame.percussiveNovelty = 0.015F;
    frame.percussiveSalience = 0.55F;
    frame.harmonicSalience = 0.45F;
    frame.percussiveLowBandRatio = 0.03F;
    frame.tactilePeak = 0.012F;
    frame.voiceBandRatio = 0.85F;
    frame.centerDominance = 0.95F;
    return frame;
}

moonlight::haptics::core::FeatureFrame DialogueBedFrame() {
    auto frame = DialogueFrame();
    frame.lowBandRatio = 0.30F;
    frame.percussiveSalience = 0.42F;
    frame.harmonicSalience = 0.58F;
    frame.percussiveLowBandRatio = 0.02F;
    return frame;
}

moonlight::haptics::core::OnsetResult Onset(float sharpness) {
    moonlight::haptics::core::OnsetResult onset;
    onset.detected = true;
    onset.amplitude = 0.70F;
    onset.sharpness = sharpness;
    onset.confidence = 0.78F;
    return onset;
}

void AssertContinuousNeedsPersistentNonTonalRumble() {
    moonlight::haptics::core::GameSceneAuthor author;
    const auto rumble = RumbleFrame();
    const moonlight::haptics::core::OnsetResult noOnset;

    for (uint32_t hop = 0U; hop < 11U; ++hop) {
        const auto intent = author.Process(
            0.60F, 0.90F, 0.0F, false, rumble, noOnset);
        assert(intent.continuousAmplitude == 0.0F);
    }
    const auto started = author.Process(
        0.60F, 0.90F, 0.0F, false, rumble, noOnset);
    assert(started.continuousAmplitude > 0.04F);
    assert(started.continuousAmplitude <= 0.24F);

    author.Reset();
    const auto orchestral = OrchestralFrame();
    for (uint32_t hop = 0U; hop < 400U; ++hop) {
        const auto intent = author.Process(
            0.80F, 0.70F, 0.0F, false, orchestral, noOnset);
        assert(intent.continuousAmplitude == 0.0F);
    }
}

void AssertContinuousReleasesAndFatigues() {
    moonlight::haptics::core::GameSceneAuthor author;
    const auto rumble = RumbleFrame();
    const moonlight::haptics::core::OnsetResult noOnset;
    float early = 0.0F;
    float late = 0.0F;
    for (uint32_t hop = 0U; hop < 2400U; ++hop) {
        const auto intent = author.Process(
            0.65F, 0.92F, 0.0F, false, rumble, noOnset);
        if (hop == 100U) early = intent.continuousAmplitude;
        if (hop == 2399U) late = intent.continuousAmplitude;
    }
    assert(early > 0.15F);
    assert(early <= 0.24F);
    assert(late > 0.04F);
    assert(late < early * 0.60F);

    const moonlight::haptics::core::FeatureFrame silence;
    for (uint32_t hop = 0U; hop < 100U; ++hop) {
        author.Process(0.0F, 0.0F, 0.0F, false, silence, noOnset);
    }
    const auto stopped = author.Process(
        0.0F, 0.0F, 0.0F, false, silence, noOnset);
    assert(stopped.continuousAmplitude == 0.0F);
}

void AssertOrchestrationAndVibratoAreRejected() {
    moonlight::haptics::core::GameSceneAuthor author;
    const auto onset = Onset(0.70F);
    const auto rejected = author.Process(
        0.40F, 0.55F, 0.80F, true, OrchestralFrame(), onset);
    assert(!rejected.hasTransient);
    assert(rejected.transientAmplitude == 0.0F);
}

void AssertImpactAndSkillAttackRemainDistinct() {
    moonlight::haptics::core::GameSceneAuthor impactAuthor;
    const auto impact = impactAuthor.Process(
        0.0F, 0.92F, 0.70F, true,
        PhysicalImpactFrame(), Onset(0.12F));
    assert(impact.hasTransient);

    moonlight::haptics::core::GameSceneAuthor skillAuthor;
    const auto skill = skillAuthor.Process(
        0.0F, 0.22F, 0.70F, true,
        SkillAttackFrame(), Onset(0.92F));
    assert(skill.hasTransient);

    assert(impact.transientAmplitude > skill.transientAmplitude);
    assert(impact.transientDurationMs > skill.transientDurationMs + 20.0F);
    assert(impact.sharpness < skill.sharpness);
}

void AssertStableBgmBeatIsSuppressedButImpactBypassesIt() {
    moonlight::haptics::core::GameSceneAuthor beatAuthor;
    const auto stableBeat = beatAuthor.Process(
        0.0F, 0.22F, 0.70F, true,
        SkillAttackFrame(), Onset(0.75F), true, 0.95F);
    assert(!stableBeat.hasTransient);

    moonlight::haptics::core::GameSceneAuthor impactAuthor;
    const auto physicalImpact = impactAuthor.Process(
        0.0F, 0.92F, 0.70F, true,
        PhysicalImpactFrame(), Onset(0.12F), true, 0.95F);
    assert(physicalImpact.hasTransient);
}

void AssertFatigueNeverReducesTransient() {
    moonlight::haptics::core::GameSceneAuthor fresh;
    moonlight::haptics::core::GameSceneAuthor fatigued;
    const auto rumble = RumbleFrame();
    const moonlight::haptics::core::OnsetResult noOnset;
    for (uint32_t hop = 0U; hop < 2400U; ++hop) {
        fatigued.Process(0.65F, 0.92F, 0.0F, false, rumble, noOnset);
    }

    const auto freshImpact = fresh.Process(
        0.0F, 0.92F, 0.75F, true,
        PhysicalImpactFrame(), Onset(0.15F));
    const auto fatiguedImpact = fatigued.Process(
        0.65F, 0.92F, 0.75F, true,
        PhysicalImpactFrame(), Onset(0.15F));
    assert(freshImpact.transientAmplitude ==
           fatiguedImpact.transientAmplitude);
}

void AssertDialogueSoftMaskRejectsAmbiguousMouthOnset() {
    const auto onset = Onset(0.80F);
    auto unmaskedFrame = DialogueFrame();
    moonlight::haptics::core::GameSceneAuthor unmaskedAuthor;
    const auto unmasked = unmaskedAuthor.Process(
        0.0F, 0.12F, 0.70F, true, unmaskedFrame, onset);
    assert(unmasked.hasTransient);

    auto dialogueFrame = DialogueFrame();
    dialogueFrame.speechProbability = 0.95F;
    moonlight::haptics::core::GameSceneAuthor dialogueAuthor;
    const auto filtered = dialogueAuthor.Process(
        0.0F, 0.12F, 0.70F, true, dialogueFrame, onset);
    assert(!filtered.hasTransient);
    assert(filtered.transientAmplitude == 0.0F);
}

void AssertDialogueSoftMaskDucksAcceptedTransient() {
    const auto onset = Onset(0.92F);
    auto baselineFrame = SkillAttackFrame();
    moonlight::haptics::core::GameSceneAuthor baselineAuthor;
    const auto baseline = baselineAuthor.Process(
        0.0F, 0.22F, 0.70F, true, baselineFrame, onset);
    assert(baseline.hasTransient);

    auto dialogueFrame = SkillAttackFrame();
    dialogueFrame.speechProbability = 0.90F;
    dialogueFrame.centerDominance = 0.95F;
    dialogueFrame.voiceBandRatio = 0.85F;
    moonlight::haptics::core::GameSceneAuthor dialogueAuthor;
    const auto ducked = dialogueAuthor.Process(
        0.0F, 0.22F, 0.70F, true, dialogueFrame, onset);

    assert(ducked.hasTransient);
    assert(ducked.transientAmplitude > 0.0F);
    assert(ducked.transientAmplitude < baseline.transientAmplitude);
}

void AssertPhysicalImpactBypassesDialogueMask() {
    auto impactFrame = PhysicalImpactFrame();
    moonlight::haptics::core::GameSceneAuthor baselineAuthor;
    const auto baseline = baselineAuthor.Process(
        0.0F, 0.92F, 0.75F, true, impactFrame, Onset(0.12F));

    impactFrame.speechProbability = 0.98F;
    impactFrame.centerDominance = 0.98F;
    impactFrame.voiceBandRatio = 0.90F;
    moonlight::haptics::core::GameSceneAuthor dialogueAuthor;
    const auto overDialogue = dialogueAuthor.Process(
        0.0F, 0.92F, 0.75F, true, impactFrame, Onset(0.12F));

    assert(overDialogue.hasTransient);
    assert(overDialogue.transientAmplitude == baseline.transientAmplitude);
    assert(overDialogue.transientDurationMs == baseline.transientDurationMs);
}

void AssertDialogueCannotStartAmbiguousContinuousBed() {
    const moonlight::haptics::core::OnsetResult noOnset;
    auto unmaskedFrame = DialogueBedFrame();
    moonlight::haptics::core::GameSceneAuthor unmaskedAuthor;
    float unmaskedAmplitude = 0.0F;
    for (uint32_t hop = 0U; hop < 20U; ++hop) {
        unmaskedAmplitude = unmaskedAuthor.Process(
            0.60F, 0.52F, 0.0F, false, unmaskedFrame, noOnset)
                                .continuousAmplitude;
    }
    assert(unmaskedAmplitude > 0.0F);

    auto dialogueFrame = DialogueBedFrame();
    dialogueFrame.speechProbability = 0.98F;
    moonlight::haptics::core::GameSceneAuthor dialogueAuthor;
    for (uint32_t hop = 0U; hop < 100U; ++hop) {
        const auto filtered = dialogueAuthor.Process(
            0.60F, 0.52F, 0.0F, false, dialogueFrame, noOnset);
        assert(filtered.continuousAmplitude == 0.0F);
    }
}

void AssertDialogueDucksActiveContinuousBed() {
    const moonlight::haptics::core::OnsetResult noOnset;
    const auto baselineFrame = DialogueBedFrame();
    moonlight::haptics::core::GameSceneAuthor baselineAuthor;
    moonlight::haptics::core::GameSceneAuthor dialogueAuthor;

    float baselineAmplitude = 0.0F;
    float dialogueAmplitude = 0.0F;
    for (uint32_t hop = 0U; hop < 20U; ++hop) {
        baselineAmplitude = baselineAuthor.Process(
            0.60F, 0.52F, 0.0F, false, baselineFrame, noOnset)
                                .continuousAmplitude;
        dialogueAmplitude = dialogueAuthor.Process(
            0.60F, 0.52F, 0.0F, false, baselineFrame, noOnset)
                                .continuousAmplitude;
    }
    assert(baselineAmplitude > 0.0F);
    assert(dialogueAmplitude == baselineAmplitude);

    auto dialogueFrame = DialogueBedFrame();
    dialogueFrame.speechProbability = 0.98F;
    for (uint32_t hop = 0U; hop < 20U; ++hop) {
        baselineAmplitude = baselineAuthor.Process(
            0.60F, 0.52F, 0.0F, false, baselineFrame, noOnset)
                                .continuousAmplitude;
        dialogueAmplitude = dialogueAuthor.Process(
            0.60F, 0.52F, 0.0F, false, dialogueFrame, noOnset)
                                .continuousAmplitude;
    }
    assert(dialogueAmplitude > 0.0F);
    assert(dialogueAmplitude < baselineAmplitude);
}

} // namespace

int main() {
    AssertContinuousNeedsPersistentNonTonalRumble();
    AssertContinuousReleasesAndFatigues();
    AssertOrchestrationAndVibratoAreRejected();
    AssertImpactAndSkillAttackRemainDistinct();
    AssertStableBgmBeatIsSuppressedButImpactBypassesIt();
    AssertFatigueNeverReducesTransient();
    AssertDialogueSoftMaskRejectsAmbiguousMouthOnset();
    AssertDialogueSoftMaskDucksAcceptedTransient();
    AssertPhysicalImpactBypassesDialogueMask();
    AssertDialogueCannotStartAmbiguousContinuousBed();
    AssertDialogueDucksActiveContinuousBed();
    return 0;
}
