// SPDX-License-Identifier: Apache-2.0

package com.moonlight.haptics.android

import android.annotation.TargetApi
import android.content.Context
import android.os.Build
import android.os.Vibrator

/** Validated Android 16 envelope limits captured without retaining framework objects. */
class AndroidHapticEnvelopeLimits private constructor(
    val maxControlPoints: Int,
    val minControlPointDurationMs: Long,
    val maxControlPointDurationMs: Long,
    val maxDurationMs: Long
) {
    companion object {
        @JvmSynthetic
        internal fun create(
            maxControlPoints: Int,
            minControlPointDurationMs: Long,
            maxControlPointDurationMs: Long,
            maxDurationMs: Long
        ) = AndroidHapticEnvelopeLimits(
            maxControlPoints,
            minControlPointDurationMs,
            maxControlPointDurationMs,
            maxDurationMs
        )
    }
}

/** Immutable sampled acceleration response reported by Android 16. */
class AndroidHapticFrequencyResponse private constructor(
    frequenciesHz: FloatArray,
    outputAccelerationGs: FloatArray
) {
    private val frequencies = frequenciesHz.copyOf()
    private val accelerations = outputAccelerationGs.copyOf()

    val sampleCount: Int
        get() = frequencies.size

    fun frequencyHzAt(index: Int): Float = frequencies[index]

    fun outputAccelerationGsAt(index: Int): Float = accelerations[index]

    companion object {
        @JvmSynthetic
        internal fun create(
            frequenciesHz: FloatArray,
            outputAccelerationGs: FloatArray
        ) = AndroidHapticFrequencyResponse(frequenciesHz, outputAccelerationGs)
    }
}

/**
 * Read-only actuator information for diagnostics and capability-driven renderers.
 *
 * Nullable values mean that the platform did not expose a usable measurement.
 * Model-specific compensation remains in the SDK device profile and must not be
 * inferred from a missing or malformed vendor response.
 */
class AndroidHapticActuatorCapabilities private constructor(
    val envelopeLimits: AndroidHapticEnvelopeLimits?,
    val minFrequencyHz: Float?,
    val maxFrequencyHz: Float?,
    val resonantFrequencyHz: Float?,
    val qFactor: Float?,
    val maxOutputAccelerationGs: Float?,
    val frequencyResponse: AndroidHapticFrequencyResponse?
) {
    val hasFrequencyControl: Boolean
        get() = minFrequencyHz != null && maxFrequencyHz != null

    companion object {
        @JvmStatic
        fun detect(context: Context): AndroidHapticActuatorCapabilities {
            val vibrator = AndroidHapticCapabilities.vibrator(context)
            val basic = AndroidHapticCapabilities.detect(vibrator)
            return detect(
                vibrator,
                basic.level == AndroidHapticCapabilityLevel.ENVELOPE
            )
        }

        @JvmSynthetic
        internal fun detect(
            vibrator: Vibrator?,
            envelopeSupported: Boolean
        ): AndroidHapticActuatorCapabilities {
            if (vibrator == null) return empty()

            var resonantFrequencyHz: Float? = null
            var qFactor: Float? = null
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                try {
                    resonantFrequencyHz = vibrator.resonantFrequency
                    qFactor = vibrator.qFactor
                } catch (_: RuntimeException) {
                    // Some vendor vibrator services reject optional queries.
                }
            }

            var envelopeMaxControlPoints: Int? = null
            var envelopeMinControlPointDurationMs: Long? = null
            var envelopeMaxControlPointDurationMs: Long? = null
            var envelopeMaxDurationMs: Long? = null
            var minFrequencyHz: Float? = null
            var maxFrequencyHz: Float? = null
            var maxOutputAccelerationGs: Float? = null
            var responseFrequenciesHz: FloatArray? = null
            var responseOutputAccelerationGs: FloatArray? = null
            if (Build.VERSION.SDK_INT >= 36) {
                if (envelopeSupported) {
                    try {
                        val info = vibrator.envelopeEffectInfo
                        envelopeMaxControlPoints = info?.maxSize
                        envelopeMinControlPointDurationMs =
                            info?.minControlPointDurationMillis
                        envelopeMaxControlPointDurationMs =
                            info?.maxControlPointDurationMillis
                        envelopeMaxDurationMs = info?.maxDurationMillis
                    } catch (_: RuntimeException) {
                        // Envelope support remains usable through BasicEnvelopeBuilder.
                    }
                }
                try {
                    val frequency = vibrator.frequencyProfile
                    minFrequencyHz = frequency?.minFrequencyHz
                    maxFrequencyHz = frequency?.maxFrequencyHz
                    maxOutputAccelerationGs = frequency?.maxOutputAccelerationGs
                    val response = frequency?.frequenciesOutputAcceleration
                    if (response != null && response.size() > 0) {
                        val frequencies = FloatArray(response.size())
                        val accelerations = FloatArray(response.size())
                        for (index in 0 until response.size()) {
                            frequencies[index] = response.keyAt(index).toFloat()
                            accelerations[index] =
                                response.valueAt(index) ?: Float.NaN
                        }
                        responseFrequenciesHz = frequencies
                        responseOutputAccelerationGs = accelerations
                    }
                } catch (_: RuntimeException) {
                    // Frequency control is optional even when envelopes exist.
                }
            }

            return fromRaw(
                envelopeMaxControlPoints = envelopeMaxControlPoints,
                envelopeMinControlPointDurationMs = envelopeMinControlPointDurationMs,
                envelopeMaxControlPointDurationMs = envelopeMaxControlPointDurationMs,
                envelopeMaxDurationMs = envelopeMaxDurationMs,
                minFrequencyHz = minFrequencyHz,
                maxFrequencyHz = maxFrequencyHz,
                resonantFrequencyHz = resonantFrequencyHz,
                qFactor = qFactor,
                maxOutputAccelerationGs = maxOutputAccelerationGs,
                responseFrequenciesHz = responseFrequenciesHz,
                responseOutputAccelerationGs = responseOutputAccelerationGs
            )
        }

        @TargetApi(36)
        @JvmSynthetic
        internal fun fromRaw(
            envelopeMaxControlPoints: Int?,
            envelopeMinControlPointDurationMs: Long?,
            envelopeMaxControlPointDurationMs: Long?,
            envelopeMaxDurationMs: Long?,
            minFrequencyHz: Float?,
            maxFrequencyHz: Float?,
            resonantFrequencyHz: Float?,
            qFactor: Float?,
            maxOutputAccelerationGs: Float?,
            responseFrequenciesHz: FloatArray? = null,
            responseOutputAccelerationGs: FloatArray? = null
        ): AndroidHapticActuatorCapabilities {
            val envelope = if (
                envelopeMaxControlPoints != null && envelopeMaxControlPoints > 0 &&
                envelopeMinControlPointDurationMs != null &&
                envelopeMinControlPointDurationMs > 0L &&
                envelopeMaxControlPointDurationMs != null &&
                envelopeMaxControlPointDurationMs >= envelopeMinControlPointDurationMs &&
                envelopeMaxDurationMs != null &&
                envelopeMaxDurationMs >= envelopeMaxControlPointDurationMs
            ) {
                AndroidHapticEnvelopeLimits.create(
                    maxControlPoints = envelopeMaxControlPoints,
                    minControlPointDurationMs = envelopeMinControlPointDurationMs,
                    maxControlPointDurationMs = envelopeMaxControlPointDurationMs,
                    maxDurationMs = envelopeMaxDurationMs
                )
            } else {
                null
            }

            val minimumFrequency = minFrequencyHz.positiveFiniteOrNull()
            val maximumFrequency = maxFrequencyHz.positiveFiniteOrNull()
            val validFrequencyRange = minimumFrequency != null &&
                maximumFrequency != null && maximumFrequency >= minimumFrequency
            val response = validatedFrequencyResponse(
                responseFrequenciesHz,
                responseOutputAccelerationGs
            )
            val effectiveMinimumFrequency = if (validFrequencyRange) {
                minimumFrequency
            } else {
                response?.frequencyHzAt(0)
            }
            val effectiveMaximumFrequency = if (validFrequencyRange) {
                maximumFrequency
            } else {
                response?.let { it.frequencyHzAt(it.sampleCount - 1) }
            }
            val reportedMaximumAcceleration = maxOutputAccelerationGs.positiveFiniteOrNull()
            val effectiveMaximumAcceleration = reportedMaximumAcceleration ?: response?.let {
                var maximum = 0f
                for (index in 0 until it.sampleCount) {
                    maximum = maxOf(maximum, it.outputAccelerationGsAt(index))
                }
                maximum
            }

            return AndroidHapticActuatorCapabilities(
                envelopeLimits = envelope,
                minFrequencyHz = effectiveMinimumFrequency,
                maxFrequencyHz = effectiveMaximumFrequency,
                resonantFrequencyHz = resonantFrequencyHz.positiveFiniteOrNull(),
                qFactor = qFactor.positiveFiniteOrNull(),
                maxOutputAccelerationGs = effectiveMaximumAcceleration,
                frequencyResponse = response
            )
        }

        private fun empty() = AndroidHapticActuatorCapabilities(
            envelopeLimits = null,
            minFrequencyHz = null,
            maxFrequencyHz = null,
            resonantFrequencyHz = null,
            qFactor = null,
            maxOutputAccelerationGs = null,
            frequencyResponse = null
        )

        private fun validatedFrequencyResponse(
            frequenciesHz: FloatArray?,
            outputAccelerationGs: FloatArray?
        ): AndroidHapticFrequencyResponse? {
            if (frequenciesHz == null || outputAccelerationGs == null ||
                frequenciesHz.isEmpty() || frequenciesHz.size != outputAccelerationGs.size
            ) {
                return null
            }
            var previousFrequency = 0f
            for (index in frequenciesHz.indices) {
                val frequency = frequenciesHz[index]
                val acceleration = outputAccelerationGs[index]
                if (!frequency.isFinite() || frequency <= previousFrequency ||
                    !acceleration.isFinite() || acceleration <= 0f
                ) {
                    return null
                }
                previousFrequency = frequency
            }
            return AndroidHapticFrequencyResponse.create(
                frequenciesHz,
                outputAccelerationGs
            )
        }

        private fun Float?.positiveFiniteOrNull(): Float? =
            this?.takeIf { it.isFinite() && it > 0f }
    }
}
