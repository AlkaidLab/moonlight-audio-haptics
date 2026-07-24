// SPDX-License-Identifier: Apache-2.0

package com.moonlight.haptics.android

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AndroidHapticActuatorCapabilitiesTest {
    @Test
    fun retainsValidatedEnvelopeAndFrequencyMeasurements() {
        val responseFrequencies = floatArrayOf(50f, 150f, 300f)
        val responseAccelerations = floatArrayOf(0.5f, 2.5f, 0.8f)
        val capabilities = AndroidHapticActuatorCapabilities.fromRaw(
            envelopeMaxControlPoints = 16,
            envelopeMinControlPointDurationMs = 5L,
            envelopeMaxControlPointDurationMs = 100L,
            envelopeMaxDurationMs = 1_000L,
            minFrequencyHz = 50f,
            maxFrequencyHz = 300f,
            resonantFrequencyHz = 150f,
            qFactor = 4f,
            maxOutputAccelerationGs = 2.5f,
            responseFrequenciesHz = responseFrequencies,
            responseOutputAccelerationGs = responseAccelerations
        )
        responseFrequencies[0] = 999f
        responseAccelerations[1] = 999f

        val envelope = capabilities.envelopeLimits
        assertEquals(16, envelope?.maxControlPoints)
        assertEquals(5L, envelope?.minControlPointDurationMs)
        assertEquals(100L, envelope?.maxControlPointDurationMs)
        assertEquals(1_000L, envelope?.maxDurationMs)
        assertTrue(capabilities.hasFrequencyControl)
        assertEquals(50f, capabilities.minFrequencyHz)
        assertEquals(300f, capabilities.maxFrequencyHz)
        assertEquals(150f, capabilities.resonantFrequencyHz)
        assertEquals(4f, capabilities.qFactor)
        assertEquals(2.5f, capabilities.maxOutputAccelerationGs)
        val response = capabilities.frequencyResponse
        assertEquals(3, response?.sampleCount)
        assertEquals(50f, response?.frequencyHzAt(0))
        assertEquals(2.5f, response?.outputAccelerationGsAt(1))
    }

    @Test
    fun rejectsMalformedVendorMeasurementsWithoutDiscardingIndependentValues() {
        val capabilities = AndroidHapticActuatorCapabilities.fromRaw(
            envelopeMaxControlPoints = 0,
            envelopeMinControlPointDurationMs = -1L,
            envelopeMaxControlPointDurationMs = 4L,
            envelopeMaxDurationMs = 2L,
            minFrequencyHz = 300f,
            maxFrequencyHz = 50f,
            resonantFrequencyHz = Float.NaN,
            qFactor = 3f,
            maxOutputAccelerationGs = -2f,
            responseFrequenciesHz = floatArrayOf(100f, 80f),
            responseOutputAccelerationGs = floatArrayOf(1f, Float.NaN)
        )

        assertNull(capabilities.envelopeLimits)
        assertFalse(capabilities.hasFrequencyControl)
        assertNull(capabilities.minFrequencyHz)
        assertNull(capabilities.maxFrequencyHz)
        assertNull(capabilities.resonantFrequencyHz)
        assertEquals(3f, capabilities.qFactor)
        assertNull(capabilities.maxOutputAccelerationGs)
        assertNull(capabilities.frequencyResponse)
    }
}
