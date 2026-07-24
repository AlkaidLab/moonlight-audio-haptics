// SPDX-License-Identifier: Apache-2.0

package com.moonlight.haptics.android.internal

import com.moonlight.haptics.android.AndroidHapticActuatorCapabilities
import kotlin.math.roundToInt

internal data class FrequencyEnvelopeOnsetPlan(
    val frequencyHz: Float,
    val attackDurationMs: Long,
    val settleDurationMs: Long?
)

/** Builds only finite onset envelopes that can join the steady amplitude bed. */
internal object HapticFrequencyEnvelopePolicy {
    fun plan(
        capabilities: AndroidHapticActuatorCapabilities,
        sharpness: Float,
        transientDurationMs: Long,
        hasTransient: Boolean
    ): FrequencyEnvelopeOnsetPlan? {
        val limits = capabilities.envelopeLimits ?: return null
        val response = capabilities.frequencyResponse ?: return null
        val requiredPoints = if (hasTransient) 2 else 1
        if (limits.maxControlPoints < requiredPoints) return null

        val attackDurationMs = limits.minControlPointDurationMs
        val settleDurationMs = if (hasTransient) {
            (transientDurationMs - attackDurationMs).coerceIn(
                limits.minControlPointDurationMs,
                limits.maxControlPointDurationMs
            )
        } else {
            null
        }
        val totalDurationMs = attackDurationMs + (settleDurationMs ?: 0L)
        if (attackDurationMs > limits.maxControlPointDurationMs ||
            totalDurationMs > limits.maxDurationMs
        ) {
            return null
        }

        val minimumFrequencyHz = capabilities.minFrequencyHz ?: return null
        val maximumFrequencyHz = capabilities.maxFrequencyHz ?: return null
        var candidateCount = 0
        for (index in 0 until response.sampleCount) {
            val frequencyHz = response.frequencyHzAt(index)
            if (frequencyHz in minimumFrequencyHz..maximumFrequencyHz &&
                response.outputAccelerationGsAt(index) >= MINIMUM_OUTPUT_ACCELERATION_GS
            ) {
                candidateCount++
            }
        }
        if (candidateCount == 0) return null

        val normalizedSharpness = sharpness.takeIf(Float::isFinite)?.coerceIn(0f, 1f) ?: 0.5f
        val selectedCandidate = (normalizedSharpness * (candidateCount - 1)).roundToInt()
        var currentCandidate = 0
        var selectedFrequencyHz: Float? = null
        for (index in 0 until response.sampleCount) {
            val frequencyHz = response.frequencyHzAt(index)
            if (frequencyHz in minimumFrequencyHz..maximumFrequencyHz &&
                response.outputAccelerationGsAt(index) >= MINIMUM_OUTPUT_ACCELERATION_GS
            ) {
                if (currentCandidate == selectedCandidate) {
                    selectedFrequencyHz = frequencyHz
                    break
                }
                currentCandidate++
            }
        }
        return FrequencyEnvelopeOnsetPlan(
            frequencyHz = selectedFrequencyHz ?: return null,
            attackDurationMs = attackDurationMs,
            settleDurationMs = settleDurationMs
        )
    }

    // Android's frequency-sweep guidance uses 0.1 g as its default useful floor.
    private const val MINIMUM_OUTPUT_ACCELERATION_GS = 0.1f
}
