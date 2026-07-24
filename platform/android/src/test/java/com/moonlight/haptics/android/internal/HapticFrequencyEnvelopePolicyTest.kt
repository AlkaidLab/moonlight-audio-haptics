// SPDX-License-Identifier: Apache-2.0

package com.moonlight.haptics.android.internal

import com.moonlight.haptics.android.AndroidHapticActuatorCapabilities
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class HapticFrequencyEnvelopePolicyTest {
    @Test
    fun selectsExactUsefulResponseSamplesBySharpness() {
        val capabilities = capabilities(
            frequenciesHz = floatArrayOf(40f, 80f, 140f, 220f, 300f),
            accelerationsGs = floatArrayOf(0.05f, 1.0f, 2.0f, 1.1f, 0.05f)
        )

        val soft = HapticFrequencyEnvelopePolicy.plan(capabilities, 0f, 50L, false)
        val neutral = HapticFrequencyEnvelopePolicy.plan(capabilities, 0.5f, 50L, false)
        val sharp = HapticFrequencyEnvelopePolicy.plan(capabilities, 1f, 50L, false)

        assertEquals(80f, soft?.frequencyHz)
        assertEquals(140f, neutral?.frequencyHz)
        assertEquals(220f, sharp?.frequencyHz)
        assertEquals(20L, neutral?.attackDurationMs)
        assertNull(neutral?.settleDurationMs)
    }

    @Test
    fun transientPlanUsesFastAttackAndBoundedSettle() {
        val capabilities = capabilities(maxControlPointDurationMs = 30L)

        val plan = HapticFrequencyEnvelopePolicy.plan(
            capabilities,
            sharpness = Float.NaN,
            transientDurationMs = 100L,
            hasTransient = true
        )

        assertEquals(140f, plan?.frequencyHz)
        assertEquals(20L, plan?.attackDurationMs)
        assertEquals(30L, plan?.settleDurationMs)
    }

    @Test
    fun rejectsEnvelopeThatCannotFitOrHasNoFrequencyResponse() {
        val onePoint = capabilities(maxControlPoints = 1)
        val tooShort = capabilities(
            maxControlPointDurationMs = 20L,
            maxDurationMs = 30L
        )
        val noResponse = AndroidHapticActuatorCapabilities.fromRaw(
            envelopeMaxControlPoints = 16,
            envelopeMinControlPointDurationMs = 20L,
            envelopeMaxControlPointDurationMs = 100L,
            envelopeMaxDurationMs = 1_000L,
            minFrequencyHz = 40f,
            maxFrequencyHz = 300f,
            resonantFrequencyHz = 140f,
            qFactor = 4f,
            maxOutputAccelerationGs = 2f
        )
        val responseBelowUsefulFloor = capabilities(
            frequenciesHz = floatArrayOf(80f, 140f),
            accelerationsGs = floatArrayOf(0.05f, 0.09f)
        )

        assertNull(HapticFrequencyEnvelopePolicy.plan(onePoint, 0.5f, 50L, true))
        assertNull(HapticFrequencyEnvelopePolicy.plan(tooShort, 0.5f, 50L, true))
        assertNull(HapticFrequencyEnvelopePolicy.plan(noResponse, 0.5f, 50L, false))
        assertNull(
            HapticFrequencyEnvelopePolicy.plan(
                responseBelowUsefulFloor,
                0.5f,
                50L,
                false
            )
        )
    }

    private fun capabilities(
        maxControlPoints: Int = 16,
        maxControlPointDurationMs: Long = 100L,
        maxDurationMs: Long = 1_000L,
        frequenciesHz: FloatArray = floatArrayOf(80f, 140f, 220f),
        accelerationsGs: FloatArray = floatArrayOf(1f, 2f, 1f)
    ) = AndroidHapticActuatorCapabilities.fromRaw(
        envelopeMaxControlPoints = maxControlPoints,
        envelopeMinControlPointDurationMs = 20L,
        envelopeMaxControlPointDurationMs = maxControlPointDurationMs,
        envelopeMaxDurationMs = maxDurationMs,
        minFrequencyHz = 40f,
        maxFrequencyHz = 300f,
        resonantFrequencyHz = 140f,
        qFactor = 4f,
        maxOutputAccelerationGs = 2f,
        responseFrequenciesHz = frequenciesHz,
        responseOutputAccelerationGs = accelerationsGs
    )
}
