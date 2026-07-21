<!-- SPDX-License-Identifier: Apache-2.0 -->

# Dialogue corpus regression

The optional `moonlight_haptics_dialogue_corpus` test uses the real-speech
`audio_tiny16.wav` fixture from libfvad commit
`532ab666c20d3cfda38bca63abbb0f152706c369`:

https://github.com/dpirch/libfvad/blob/532ab666c20d3cfda38bca63abbb0f152706c369/tests/data/audio_tiny16.wav

The upstream repository and fixture are covered by the bundled libfvad
BSD-3-Clause license and WebRTC patent grant already recorded under
`third_party/libfvad/`. The fixture is downloaded only in CI and is not included
in SDK source or binary packages.

Expected SHA-256:

`551d3f0282c5d5e1e32e76da97bd3532c38e6f4bb02a32510caf562d7c06ae9c`

Configure a local build with:

```text
-DMOONLIGHT_HAPTICS_DIALOGUE_CORPUS_WAV=/path/to/audio_tiny16.wav
```

The regression measures two product behaviours: dialogue-only false haptic
transients after warm-up, and recall of deterministic physical impacts mixed
over the real speech. It deliberately avoids asserting individual VAD frames,
which are an implementation detail rather than the SDK's output contract.
