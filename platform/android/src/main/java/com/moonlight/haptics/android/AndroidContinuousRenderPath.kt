// SPDX-License-Identifier: Apache-2.0

package com.moonlight.haptics.android

/** Last successfully submitted continuous-effect path, intended for diagnostics. */
enum class AndroidContinuousRenderPath {
    NONE,
    FREQUENCY_ENVELOPE,
    AMPLITUDE_WAVEFORM,
    ON_OFF_WAVEFORM,
    LEGACY_ON_OFF,
    ONE_SHOT_FALLBACK
}
