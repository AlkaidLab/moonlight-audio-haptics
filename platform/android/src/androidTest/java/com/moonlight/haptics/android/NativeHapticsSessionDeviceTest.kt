// SPDX-License-Identifier: Apache-2.0

package com.moonlight.haptics.android

import android.os.Build
import android.os.SystemClock
import android.util.Log
import com.moonlight.haptics.HapticFrame
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeHapticsSessionDeviceTest {
    @Test
    fun reportsAudioCoupledHapticsAvailability() {
        val available = AndroidAudioCoupledHaptics.isPlatformAvailable()
        Log.i("MoonlightHapticsTest", "audioCoupledAvailable=$available")
    }

    @Test
    fun nativeLibraryAndSessionLifecycle() {
        val renderer = AndroidHapticRenderer(
            InstrumentationRegistry.getInstrumentation().targetContext
        )
        val session = NativeHapticsSession(listener = {})
        assertTrue(session.nativeHandle != 0L)
        session.setScene(HapticFrame.SCENE_MUSIC)
        session.setSensitivity(1.5f)
        assertEquals(0L, session.droppedFrameCount)
        session.stop()
        session.close()
        assertEquals(0L, session.nativeHandle)
        renderer.stop()
        renderer.close()
    }

    @Test
    fun api36FrequencyEnvelopePathIsActuallySubmitted() {
        val renderer = AndroidHapticRenderer(
            InstrumentationRegistry.getInstrumentation().targetContext
        )
        try {
            assumeTrue(Build.VERSION.SDK_INT >= 36)
            assumeTrue(renderer.capabilities.level == AndroidHapticCapabilityLevel.ENVELOPE)
            assumeTrue(renderer.actuatorCapabilities.frequencyResponse != null)

            assertTrue(
                renderer.submit(
                    timestampUs = 1L,
                    flags = HapticFrame.FLAG_CONTINUOUS_CHANGED,
                    continuousAmplitude = 0.4f,
                    transientAmplitude = 0f,
                    transientDurationMs = 40f,
                    sharpness = 0.5f,
                    lowBandRatio = 1f,
                    stereoPan = 0f,
                    confidence = 1f,
                    activeScene = HapticFrame.SCENE_MUSIC
                )
            )

            val deadlineMs = SystemClock.elapsedRealtime() + 2_000L
            while (renderer.lastContinuousRenderPath == AndroidContinuousRenderPath.NONE &&
                SystemClock.elapsedRealtime() < deadlineMs
            ) {
                SystemClock.sleep(10L)
            }
            Log.i(
                "MoonlightHapticsTest",
                "continuousRenderPath=${renderer.lastContinuousRenderPath}"
            )
            assertEquals(
                AndroidContinuousRenderPath.FREQUENCY_ENVELOPE,
                renderer.lastContinuousRenderPath
            )
        } finally {
            renderer.close()
        }
    }

    @Test
    fun preApi36ContinuousPathKeepsLegacyCapabilityFallback() {
        assumeTrue(Build.VERSION.SDK_INT < 36)
        val renderer = AndroidHapticRenderer(
            InstrumentationRegistry.getInstrumentation().targetContext
        )
        try {
            assertTrue(renderer.capabilities.hasVibrator)
            assertTrue(
                renderer.submit(
                    timestampUs = 1L,
                    flags = HapticFrame.FLAG_CONTINUOUS_CHANGED,
                    continuousAmplitude = 0.4f,
                    transientAmplitude = 0f,
                    transientDurationMs = 40f,
                    sharpness = 0.5f,
                    lowBandRatio = 1f,
                    stereoPan = 0f,
                    confidence = 1f,
                    activeScene = HapticFrame.SCENE_MUSIC
                )
            )

            val deadlineMs = SystemClock.elapsedRealtime() + 2_000L
            while (renderer.lastContinuousRenderPath == AndroidContinuousRenderPath.NONE &&
                SystemClock.elapsedRealtime() < deadlineMs
            ) {
                SystemClock.sleep(10L)
            }
            val expectedPath = if (
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.O &&
                renderer.capabilities.hasAmplitudeControl
            ) {
                AndroidContinuousRenderPath.AMPLITUDE_WAVEFORM
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                AndroidContinuousRenderPath.ON_OFF_WAVEFORM
            } else {
                AndroidContinuousRenderPath.LEGACY_ON_OFF
            }
            Log.i(
                "MoonlightHapticsTest",
                "continuousRenderPath=${renderer.lastContinuousRenderPath}"
            )
            assertEquals(expectedPath, renderer.lastContinuousRenderPath)
        } finally {
            renderer.close()
        }
    }
}
