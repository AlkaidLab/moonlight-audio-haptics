# Changelog

All notable SDK changes are recorded here. Versions follow Semantic Versioning
while the public ABI has its own explicit version reported by
`ah_get_abi_version()`.

## Unreleased

- Add a dialogue-aware GAME soft mask using the bundled WebRTC GMM VAD,
  stereo-centre evidence, and voice-band energy while preserving physical
  impacts through continuous evidence-based bypass.
- Add causal fixed-capacity resampling for non-WebRTC sample rates without
  introducing process-time allocation or look-ahead.
- Fix the ndk-build source manifest and the Windows `<version>` header-name
  collision in standalone SDK source consumption.
- Apply two documented libfvad safety corrections, distribute the upstream
  WebRTC patent grant, and verify both legal files in packaged artifacts.

## 0.5.14 - 2026-07-17

- Establish the standalone Apache-2.0 SDK repository.
- Add the `action-rpg-p4g-v4` GAME profile with causal trailing-median HPSS
  approximation and SuperFlux-style vibrato suppression.
- Keep the validated MUSIC path and public C ABI v1 unchanged.
- Ship the Android AAR adapter with capability-aware rendering and device
  policy tests.
- Add relocatable CMake package metadata and standalone release automation.
