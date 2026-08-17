# Changelog

All notable SDK changes are recorded here. Versions follow Semantic Versioning
while the public ABI has its own explicit version reported by
`ah_get_abi_version()`.

## Unreleased

- Add ABI v2 authored-stereo analysis for Sunshine fallback transports while
  preserving the existing ABI v1 scene-authoring API.
- Preserve left/right source lanes in causal 5 ms IR frames with no processing
  allocation, locking, device calibration, or actuator-specific curves.
- Restrict every authored lane feature to a fourth-order Butterworth 50-400 Hz
  tactile band so sub-bass drift and high-frequency hiss no longer become
  persistent rumble; amplitude, attack, correlation, and the silence flag all
  now describe that band rather than full-range audio energy.- Derive the low-band ratio from a 160 Hz crossover and the zero-crossing
  texture from a trailing causal 40 ms window.
- Reset cleanly on explicit discontinuity or sequence gaps and flush a marked
  partial tail at end-of-stream.
- Add ABI-size, lane-isolation, chunk-invariance, discontinuity, timestamp, and
  end-of-stream regression coverage.

## 0.6.0 - 2026-07-24

- Add a dialogue-aware GAME soft mask using the bundled WebRTC GMM VAD,
  stereo-centre evidence, and voice-band energy while preserving physical
  impacts through continuous evidence-based bypass.
- Add causal fixed-capacity resampling for non-WebRTC sample rates without
  introducing process-time allocation or look-ahead.
- Fix the ndk-build source manifest and the Windows `<version>` header-name
  collision in standalone SDK source consumption.
- Apply three documented libfvad safety corrections, distribute the upstream
  WebRTC patent grant, and verify both legal files in packaged artifacts.
- Add a pinned real-speech dialogue regression with physical-impact recall
  guardrails, without distributing an additional corpus in SDK artifacts.
- Define ABI v1 AUTO as a GAME compatibility alias and use neutral centre
  evidence when multichannel input has no explicit channel layout.
- Reduce Android renderer public configuration to the stable device-profile
  opt-out; queue, timing, and throttling values remain internal SDK policy.

## 0.5.14 - 2026-07-17

- Establish the standalone Apache-2.0 SDK repository.
- Add the `action-rpg-p4g-v4` GAME profile with causal trailing-median HPSS
  approximation and SuperFlux-style vibrato suppression.
- Keep the validated MUSIC path and public C ABI v1 unchanged.
- Ship the Android AAR adapter with capability-aware rendering and device
  policy tests.
- Add relocatable CMake package metadata and standalone release automation.
